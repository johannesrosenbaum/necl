#include "nec_lite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t n;
} mem_sink_t;

static size_t mem_sink_fn(const uint8_t *p, size_t n, void *ctx) {
    mem_sink_t *s = (mem_sink_t *)ctx;
    if (s->n + n > s->cap)
        return 0;
    if (n)
        memcpy(s->buf + s->n, p, n);
    s->n += n;
    return n;
}

static int roundtrip(const uint8_t *src, size_t n, int fe) {
    size_t cap = nec_lite_compress_bound(n);
    uint8_t *dst = (uint8_t *)malloc(cap ? cap : 1);
    uint8_t *back = (uint8_t *)malloc(n ? n : 1);
    size_t clen = cap;
    size_t dlen = n;
    NecLiteHeader h;
    int rc;
    int compact;
    if (!dst || !back)
        return fail("malloc");
    rc = nec_lite_compress(src, n, dst, &clen, NULL, 0, fe);
    if (rc != NEC_OK)
        return fail("compress");
    if (clen > nec_lite_compress_bound(n))
        return fail("bound violated");
    compact = (clen > 0 && (dst[0] == NEC_LITE_COMPACT_DELTA || dst[0] == NEC_LITE_COMPACT_FIRE ||
                            dst[0] == NEC_LITE_COMPACT_LZ));
    if (!compact) {
        if (nec_lite_parse_header(dst, clen, &h) != NEC_OK)
            return fail("parse");
        if (n > 0 && !(h.flags & NEC_LITE_FLAG_STREAM))
            return fail("expected FLAG_STREAM");
    }
    dlen = n;
    rc = nec_lite_decompress(dst, clen, back, &dlen, NULL, 0);
    if (rc != NEC_OK)
        return fail(nec_lite_strerror(rc));
    if (dlen != n || memcmp(back, src, n) != 0)
        return fail("roundtrip mismatch");
    free(dst);
    free(back);
    return 0;
}

static int stream_slices(const uint8_t *src, size_t n, int fe, size_t slice) {
    nec_lite_enc_t enc;
    nec_lite_dec_t dec;
    uint8_t *coded;
    uint8_t *back;
    mem_sink_t csink;
    mem_sink_t osink;
    size_t cap = nec_lite_compress_bound(n) + 32;
    size_t off;
    int rc;
    if (slice == 0)
        slice = 1;
    coded = (uint8_t *)malloc(cap);
    back = (uint8_t *)malloc(n ? n : 1);
    if (!coded || !back)
        return fail("stream malloc");
    csink.buf = coded;
    csink.cap = cap;
    csink.n = 0;
    rc = nec_lite_enc_init(&enc, fe, mem_sink_fn, &csink);
    if (rc != NEC_OK)
        return fail("enc_init");
    off = 0;
    while (off < n) {
        size_t take = n - off;
        if (take > slice)
            take = slice;
        if (nec_lite_enc_push(&enc, src + off, take) != NEC_OK)
            return fail("enc_push");
        off += take;
    }
    if (nec_lite_enc_finish(&enc) != NEC_OK)
        return fail("enc_finish");

    osink.buf = back;
    osink.cap = n ? n : 1;
    osink.n = 0;
    if (nec_lite_dec_init(&dec, mem_sink_fn, &osink) != NEC_OK)
        return fail("dec_init");
    off = 0;
    while (off < csink.n) {
        size_t take = csink.n - off;
        if (take > slice)
            take = slice;
        if (nec_lite_dec_push(&dec, coded + off, take) != NEC_OK)
            return fail(nec_lite_strerror(dec.err));
        off += take;
    }
    if (nec_lite_dec_finish(&dec) != NEC_OK)
        return fail("dec_finish");
    if (osink.n != n || memcmp(back, src, n) != 0)
        return fail("stream mismatch");
    free(coded);
    free(back);
    return 0;
}

int main(void) {
    uint8_t zeros[4096];
    uint8_t i16[256];
    uint8_t ticks[800];
    uint8_t mixed[300];
    uint8_t long_i16[600];
    size_t i;
    memset(zeros, 0, sizeof(zeros));
    if (nec_lite_scratch_bound(100) != 0)
        return fail("scratch_bound must be 0");
    if (nec_lite_enc_sizeof() != sizeof(nec_lite_enc_t))
        return fail("enc sizeof");
    if (nec_lite_dec_sizeof() != sizeof(nec_lite_dec_t))
        return fail("dec sizeof");
    if (roundtrip(zeros, sizeof(zeros), NEC_LITE_FE_NONE))
        return 1;

    for (i = 0; i < sizeof(i16); i += 2) {
        int16_t v = (int16_t)(i * 3);
        i16[i] = (uint8_t)(v & 0xff);
        i16[i + 1] = (uint8_t)((v >> 8) & 0xff);
    }
    if (roundtrip(i16, sizeof(i16), NEC_LITE_FE_I16))
        return fail("i16");
    printf("  i16 n=%zu block OK  enc=%zu dec=%zu\n",
           sizeof(i16),
           nec_lite_enc_sizeof(),
           nec_lite_dec_sizeof());

    {
        uint32_t ts = 1700000000u;
        int32_t px = 10000;
        for (i = 0; i < sizeof(ticks); i += 8) {
            ts += 1 + (uint32_t)(i % 5);
            px += (int32_t)((i / 8) % 3) - 1;
            ticks[i] = (uint8_t)(ts & 0xff);
            ticks[i + 1] = (uint8_t)((ts >> 8) & 0xff);
            ticks[i + 2] = (uint8_t)((ts >> 16) & 0xff);
            ticks[i + 3] = (uint8_t)((ts >> 24) & 0xff);
            ticks[i + 4] = (uint8_t)(px & 0xff);
            ticks[i + 5] = (uint8_t)((px >> 8) & 0xff);
            ticks[i + 6] = (uint8_t)((px >> 16) & 0xff);
            ticks[i + 7] = (uint8_t)((px >> 24) & 0xff);
        }
        if (roundtrip(ticks, sizeof(ticks), NEC_LITE_FE_TICK8))
            return fail("tick8");
        printf("  tick8 n=%zu OK\n", sizeof(ticks));
    }

    for (i = 0; i < sizeof(mixed); i++)
        mixed[i] = (uint8_t)((i * 73u + 19u) & 255u);
    if (roundtrip(mixed, sizeof(mixed), NEC_LITE_FE_NONE))
        return fail("mixed");

    for (i = 0; i < sizeof(long_i16); i += 2) {
        int16_t v = (int16_t)(1000 + (int)i);
        long_i16[i] = (uint8_t)(v & 0xff);
        long_i16[i + 1] = (uint8_t)((v >> 8) & 0xff);
    }
    if (roundtrip(long_i16, sizeof(long_i16), NEC_LITE_FE_I16))
        return fail("i16-600");
    if (stream_slices(long_i16, sizeof(long_i16), NEC_LITE_FE_I16, 1))
        return fail("stream 1-byte");
    if (stream_slices(long_i16, sizeof(long_i16), NEC_LITE_FE_I16, 17))
        return fail("stream 17-byte");
    if (stream_slices(ticks, sizeof(ticks), NEC_LITE_FE_TICK8, 7))
        return fail("stream tick 7-byte");
    if (stream_slices(zeros, 0, NEC_LITE_FE_NONE, 3))
        return fail("stream empty");
    printf("  stream slices OK (delta persists across 256 B)\n");

    {
        uint8_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        size_t cap = nec_lite_compress_bound(8);
        uint8_t dst[96];
        uint8_t back[8];
        size_t clen = cap;
        size_t dlen = 8;
        nec_lite_compress(src, 8, dst, &clen, NULL, 0, NEC_LITE_FE_NONE);
        dst[28] ^= 0xff;
        if (nec_lite_decompress(dst, clen, back, &dlen, NULL, 0) != NEC_ERR_CRC)
            return fail("CRC must fail");
        printf("  CRC reject OK\n");
    }

    if (roundtrip(zeros, 0, NEC_LITE_FE_NONE))
        return fail("empty");

    printf("nec_lite: ALL PASS  bound(100)=%zu scratch(100)=%zu enc=%zu dec=%zu\n",
           nec_lite_compress_bound(100),
           nec_lite_scratch_bound(100),
           nec_lite_enc_sizeof(),
           nec_lite_dec_sizeof());
    return 0;
}

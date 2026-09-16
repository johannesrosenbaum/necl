#include "nec_lite.h"
#include "nec_frontend.h"

#include <string.h>

#define NEC_RC_TOP     (1u << 24)
#define NEC_RC_INC     32u
#define NEC_RC_RESCALE 16384u

typedef nec_lite_model_t nec_nib_model_t;

static void nec_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void nec_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void nec_put_u64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++)
        p[i] = (uint8_t)((v >> (i * 8)) & 0xffu);
}

static uint16_t nec_get_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t nec_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t nec_get_u64(const uint8_t *p) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static uint32_t nec_crc32_update(uint32_t c, const uint8_t *data, size_t n) {
    size_t i;
    int b;
    for (i = 0; i < n; i++) {
        c ^= (uint32_t)data[i];
        for (b = 0; b < 8; b++) {
            if (c & 1u)
                c = (c >> 1) ^ 0xEDB88320u;
            else
                c >>= 1;
        }
    }
    return c;
}

uint32_t nec_lite_crc32(const uint8_t *data, size_t n) {
    return nec_crc32_update(0xFFFFFFFFu, data, n) ^ 0xFFFFFFFFu;
}

static size_t nec_nchunks(size_t n) {
    if (n == 0)
        return 0;
    return (n + (size_t)NEC_LITE_CHUNK - 1u) / (size_t)NEC_LITE_CHUNK;
}

static size_t nec_flag_bytes(size_t n) {
    size_t chunks = nec_nchunks(n);
    if (chunks == 0)
        return 0;
    return (chunks + 7u) / 8u;
}

size_t nec_lite_compress_bound(size_t n) {
    size_t extra = 2u * nec_nchunks(n) + 16u;
    if (n > (SIZE_MAX - (size_t)NEC_HEADER_SIZE - extra))
        return SIZE_MAX;
    return n + (size_t)NEC_HEADER_SIZE + extra;
}

size_t nec_lite_scratch_bound(size_t n) {
    (void)n;
    return 0;
}

size_t nec_lite_enc_sizeof(void) {
    return sizeof(nec_lite_enc_t);
}

size_t nec_lite_dec_sizeof(void) {
    return sizeof(nec_lite_dec_t);
}

const char *nec_lite_strerror(int err) {
    switch (err) {
    case NEC_OK:
        return "ok";
    case NEC_ERR_DST:
        return "destination too small";
    case NEC_ERR_SRC:
        return "invalid or truncated source";
    case NEC_ERR_CRC:
        return "crc mismatch";
    case NEC_ERR_WEIGHT:
        return "weight_id mismatch";
    case NEC_ERR_ARG:
        return "null argument";
    case NEC_ERR_INTERNAL:
        return "internal error";
    default:
        return "unknown error";
    }
}

static uint16_t nec_fe_flag(int frontend) {
    if (frontend == NEC_LITE_FE_I16)
        return (uint16_t)NEC_LITE_FLAG_DELTA_I16;
    if (frontend == NEC_LITE_FE_TICK8)
        return (uint16_t)NEC_LITE_FLAG_DELTA_TICK8;
    return 0;
}

static int nec_fe_from_flags(uint16_t flags) {
    if (flags & NEC_LITE_FLAG_DELTA_I16)
        return NEC_LITE_FE_I16;
    if (flags & NEC_LITE_FLAG_DELTA_TICK8)
        return NEC_LITE_FE_TICK8;
    return NEC_LITE_FE_NONE;
}

static void nec_delta_from_enc(nec_lite_enc_t *e, nec_delta_st *s) {
    s->prev_i16 = e->prev_i16;
    s->prev_ts = e->prev_ts;
    s->prev_px = e->prev_px;
}

static void nec_delta_to_enc(nec_lite_enc_t *e, const nec_delta_st *s) {
    e->prev_i16 = s->prev_i16;
    e->prev_ts = s->prev_ts;
    e->prev_px = s->prev_px;
}

static void nec_delta_from_dec(nec_lite_dec_t *d, nec_delta_st *s) {
    s->prev_i16 = d->prev_i16;
    s->prev_ts = d->prev_ts;
    s->prev_px = d->prev_px;
}

static void nec_delta_to_dec(nec_lite_dec_t *d, const nec_delta_st *s) {
    d->prev_i16 = s->prev_i16;
    d->prev_ts = s->prev_ts;
    d->prev_px = s->prev_px;
}

static void nec_nib_init(nec_nib_model_t *m) {
    int i, j;
    for (i = 0; i < 16; i++) {
        m->hi[i] = 1;
        for (j = 0; j < 16; j++)
            m->lo[i][j] = 1;
    }
}

static void nec_nib_project(const uint16_t *count, uint16_t *freq) {
    uint32_t s = 0;
    int i, shift = 0, best = 0;
    for (;;) {
        s = 0;
        for (i = 0; i < 16; i++) {
            uint16_t v = (uint16_t)(count[i] >> shift);
            if (v == 0)
                v = 1;
            freq[i] = v;
            s += v;
        }
        if (s <= 256u)
            break;
        shift++;
        if (shift > 16)
            break;
    }
    for (i = 1; i < 16; i++)
        if (freq[i] > freq[best])
            best = i;
    freq[best] = (uint16_t)(freq[best] + (256u - s));
}

static void nec_nib_update(uint16_t *count, int sym) {
    uint32_t sum = 0;
    int i;
    count[sym] = (uint16_t)(count[sym] + NEC_RC_INC);
    for (i = 0; i < 16; i++)
        sum += count[i];
    if (sum > NEC_RC_RESCALE) {
        for (i = 0; i < 16; i++) {
            count[i] >>= 1;
            if (count[i] == 0)
                count[i] = 1;
        }
    }
}

static void nec_nib_see(nec_nib_model_t *m, uint8_t b) {
    int h = (int)(b >> 4);
    int l = (int)(b & 15u);
    nec_nib_update(m->hi, h);
    nec_nib_update(m->lo[h], l);
}

typedef struct {
    uint32_t low, range, cache_size;
    uint8_t cache;
    uint8_t hi;
    uint8_t *p;
    size_t i, cap;
    int err;
} nec_rc_enc;

typedef struct {
    uint32_t code, range;
    const uint8_t *p;
    size_t i, n;
} nec_rc_dec;

static void nec_rc_put(nec_rc_enc *e, uint8_t b) {
    if (e->i >= e->cap) {
        e->err = 1;
        return;
    }
    e->p[e->i++] = b;
}

static void nec_rc_shift(nec_rc_enc *e, uint32_t hi) {
    if (e->low < 0xFF000000u || hi) {
        uint8_t temp = e->cache;
        do {
            nec_rc_put(e, (uint8_t)(temp + (uint8_t)hi));
            temp = 0xFFu;
        } while (--e->cache_size);
        e->cache = (uint8_t)(e->low >> 24);
    }
    e->cache_size++;
    e->low <<= 8;
}

static void nec_rc_enc_sym(nec_rc_enc *e, uint32_t cum, uint32_t freq) {
    uint32_t r = e->range >> 8;
    uint32_t old = e->low;
    e->low += r * cum;
    if (e->low < old)
        e->hi = 1;
    e->range = r * freq;
    while (e->range < NEC_RC_TOP) {
        nec_rc_shift(e, e->hi);
        e->hi = 0;
        e->range <<= 8;
    }
}

static void nec_rc_flush(nec_rc_enc *e) {
    int k;
    for (k = 0; k < 5; k++) {
        nec_rc_shift(e, e->hi);
        e->hi = 0;
    }
}

static uint8_t nec_rc_get(nec_rc_dec *d) {
    if (d->i >= d->n)
        return 0;
    return d->p[d->i++];
}

static void nec_rc_dec_init(nec_rc_dec *d, const uint8_t *p, size_t n) {
    int k;
    d->p = p;
    d->n = n;
    d->i = 0;
    d->range = 0xFFFFFFFFu;
    d->code = 0;
    for (k = 0; k < 5; k++)
        d->code = (d->code << 8) | nec_rc_get(d);
}

static void nec_rc_dec_renorm(nec_rc_dec *d) {
    while (d->range < NEC_RC_TOP) {
        d->code = (d->code << 8) | nec_rc_get(d);
        d->range <<= 8;
    }
}

static int nec_rc_dec_sym(nec_rc_dec *d, const uint16_t *freq) {
    uint32_t r = d->range >> 8;
    uint32_t acc = 0;
    int s;
    for (s = 0; s < 16; s++) {
        uint32_t nacc = acc + freq[s];
        if (d->code < r * nacc) {
            d->code -= r * acc;
            d->range = r * freq[s];
            nec_rc_dec_renorm(d);
            return s;
        }
        acc = nacc;
    }
    return -1;
}

static uint16_t nec_freq_cum(const uint16_t *freq, int sym) {
    uint16_t c = 0;
    int k;
    for (k = 0; k < sym; k++)
        c = (uint16_t)(c + freq[k]);
    return c;
}

static void nec_rc_enc_nibble(nec_rc_enc *e, uint16_t *count, int sym) {
    uint16_t freq[16];
    nec_nib_project(count, freq);
    nec_rc_enc_sym(e, nec_freq_cum(freq, sym), freq[sym]);
    nec_nib_update(count, sym);
}

static int nec_rc_dec_nibble(nec_rc_dec *d, uint16_t *count) {
    uint16_t freq[16];
    int s;
    nec_nib_project(count, freq);
    s = nec_rc_dec_sym(d, freq);
    if (s < 0)
        return -1;
    nec_nib_update(count, s);
    return s;
}

static void nec_rc_enc_byte(nec_rc_enc *e, nec_nib_model_t *m, uint8_t b) {
    int h = (int)(b >> 4);
    int l = (int)(b & 15u);
    nec_rc_enc_nibble(e, m->hi, h);
    nec_rc_enc_nibble(e, m->lo[h], l);
}

static int nec_rc_dec_byte(nec_rc_dec *d, nec_nib_model_t *m) {
    int h = nec_rc_dec_nibble(d, m->hi);
    int l;
    if (h < 0)
        return -1;
    l = nec_rc_dec_nibble(d, m->lo[h]);
    if (l < 0)
        return -1;
    return (h << 4) | l;
}

static void nec_write_header(
    uint8_t *dst,
    uint16_t flags,
    uint64_t orig,
    uint64_t coded,
    uint32_t crc
) {
    dst[0] = 78;
    dst[1] = 69;
    dst[2] = 67;
    dst[3] = 49;
    nec_put_u16(dst + 4, (uint16_t)NEC_LITE_VERSION);
    nec_put_u16(dst + 6, flags);
    nec_put_u64(dst + 8, orig);
    nec_put_u64(dst + 16, coded);
    nec_put_u32(dst + 24, NEC_LITE_WEIGHT_ID);
    nec_put_u32(dst + 28, crc);
}

static int nec_header_fields(const uint8_t *src, NecLiteHeader *out) {
    NecLiteHeader h;
    if (src[0] != 78 || src[1] != 69 || src[2] != 67 || src[3] != 49)
        return NEC_ERR_SRC;
    h.version = nec_get_u16(src + 4);
    if (h.version != NEC_LITE_VERSION)
        return NEC_ERR_SRC;
    h.flags = nec_get_u16(src + 6);
    h.orig_len = nec_get_u64(src + 8);
    h.coded_len = nec_get_u64(src + 16);
    h.weight_id = nec_get_u32(src + 24);
    h.crc32 = nec_get_u32(src + 28);
    if (h.weight_id != NEC_LITE_WEIGHT_ID)
        return NEC_ERR_WEIGHT;
    if ((h.flags & NEC_LITE_FLAG_DELTA_I16) && (h.flags & NEC_LITE_FLAG_DELTA_TICK8))
        return NEC_ERR_SRC;
    *out = h;
    return NEC_OK;
}

int nec_lite_parse_header(
    const uint8_t *src,
    size_t src_n,
    NecLiteHeader *out
) {
    NecLiteHeader h;
    uint64_t need;
    int rc;
    if (!src || !out)
        return NEC_ERR_ARG;
    if (src_n < NEC_HEADER_SIZE)
        return NEC_ERR_SRC;
    rc = nec_header_fields(src, &h);
    if (rc != NEC_OK)
        return rc;
    if ((h.flags & NEC_LITE_FLAG_STREAM) && h.orig_len == 0 && h.coded_len == 0) {
        *out = h;
        return NEC_OK;
    }
    if (h.coded_len > (uint64_t)(SIZE_MAX - NEC_HEADER_SIZE))
        return NEC_ERR_SRC;
    need = (uint64_t)NEC_HEADER_SIZE + h.coded_len;
    if ((uint64_t)src_n < need)
        return NEC_ERR_SRC;
    *out = h;
    return NEC_OK;
}

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
} nec_mem_sink;

static size_t nec_mem_sink_fn(const uint8_t *p, size_t n, void *ctx) {
    nec_mem_sink *s = (nec_mem_sink *)ctx;
    if (!s || (n > 0 && !p))
        return 0;
    if (s->i + n > s->cap)
        return 0;
    if (n)
        memcpy(s->p + s->i, p, n);
    s->i += n;
    return n;
}

static int nec_sink_all(nec_lite_enc_t *e, const uint8_t *p, size_t n) {
    if (e->err != NEC_OK)
        return e->err;
    if (n == 0)
        return NEC_OK;
    if (!e->sink || e->sink(p, n, e->sink_ctx) != n) {
        e->err = NEC_ERR_DST;
        return e->err;
    }
    e->coded += n;
    return NEC_OK;
}

static size_t nec_nsym_enc(size_t clen) {
    if (clen == (size_t)NEC_LITE_CHUNK)
        return 0;
    return clen;
}

static size_t nec_nsym_dec(uint8_t b) {
    return b == 0 ? (size_t)NEC_LITE_CHUNK : (size_t)b;
}

static int nec_enc_emit_chunk(nec_lite_enc_t *e, const uint8_t *raw, size_t clen) {
    uint8_t delta[NEC_LITE_CHUNK];
    uint8_t rec[2];
    nec_rc_enc enc;
    nec_delta_st st;
    size_t bi;
    uint8_t nsym;

    if (clen == 0 || clen > (size_t)NEC_LITE_CHUNK)
        return NEC_ERR_INTERNAL;
    nec_delta_from_enc(e, &st);
    if (nec_delta_fwd_chunk(&st, e->frontend, raw, delta, clen) != 0) {
        e->err = NEC_ERR_ARG;
        return e->err;
    }
    nec_delta_to_enc(e, &st);

    /* RC-Versuch aktualisiert das Modell. STORE darf das nicht
     * nochmal sehen — sonst divergiert der Decoder nach dem ersten
     * nicht-schrumpfenden Chunk. */
    {
        nec_lite_model_t saved = e->model;
        memset(&enc, 0, sizeof enc);
        enc.range = 0xFFFFFFFFu;
        enc.cache_size = 1;
        enc.p = e->rc_tmp;
        enc.cap = sizeof e->rc_tmp;
        for (bi = 0; bi < clen; bi++)
            nec_rc_enc_byte(&enc, &e->model, delta[bi]);
        nec_rc_flush(&enc);

        nsym = (uint8_t)nec_nsym_enc(clen);
        if (!enc.err && enc.i > 0 && enc.i < clen && enc.i <= 254u) {
            rec[0] = (uint8_t)enc.i;
            rec[1] = nsym;
            if (nec_sink_all(e, rec, 2) != NEC_OK)
                return e->err;
            return nec_sink_all(e, e->rc_tmp, enc.i);
        }
        e->model = saved;
    }
    rec[0] = 0xFFu;
    rec[1] = nsym;
    if (nec_sink_all(e, rec, 2) != NEC_OK)
        return e->err;
    if (nec_sink_all(e, delta, clen) != NEC_OK)
        return e->err;
    for (bi = 0; bi < clen; bi++)
        nec_nib_see(&e->model, delta[bi]);
    return NEC_OK;
}

static void nec_enc_reset_state(nec_lite_enc_t *e, int frontend, nec_lite_sink_fn sink, void *ctx) {
    memset(e, 0, sizeof *e);
    nec_nib_init(&e->model);
    e->crc_state = 0xFFFFFFFFu;
    e->sink = sink;
    e->sink_ctx = ctx;
    e->frontend = frontend;
    e->err = NEC_OK;
}

static uint16_t nec_enc_flags(const nec_lite_enc_t *e) {
    return (uint16_t)(nec_fe_flag(e->frontend) | NEC_LITE_FLAG_RC | NEC_LITE_FLAG_STREAM);
}

int nec_lite_enc_init(nec_lite_enc_t *e, int frontend, nec_lite_sink_fn sink, void *ctx) {
    uint8_t hdr[NEC_HEADER_SIZE];
    if (!e || !sink)
        return NEC_ERR_ARG;
    if (frontend != NEC_LITE_FE_NONE && frontend != NEC_LITE_FE_I16 &&
        frontend != NEC_LITE_FE_TICK8)
        return NEC_ERR_ARG;
    nec_enc_reset_state(e, frontend, sink, ctx);
    e->live = 1;
    nec_write_header(hdr, nec_enc_flags(e), 0, 0, 0);
    return nec_sink_all(e, hdr, NEC_HEADER_SIZE);
}

int nec_lite_enc_push(nec_lite_enc_t *e, const uint8_t *src, size_t n) {
    if (!e)
        return NEC_ERR_ARG;
    if (e->err != NEC_OK)
        return e->err;
    if (n > 0 && !src)
        return NEC_ERR_ARG;
    e->crc_state = nec_crc32_update(e->crc_state, src, n);
    e->orig += n;
    while (n) {
        size_t room = (size_t)NEC_LITE_CHUNK - e->fill;
        size_t take = n < room ? n : room;
        memcpy(e->chunk + e->fill, src, take);
        e->fill += take;
        src += take;
        n -= take;
        if (e->fill == (size_t)NEC_LITE_CHUNK) {
            if (nec_enc_emit_chunk(e, e->chunk, (size_t)NEC_LITE_CHUNK) != NEC_OK)
                return e->err;
            e->fill = 0;
        }
    }
    return NEC_OK;
}

int nec_lite_enc_finish(nec_lite_enc_t *e) {
    uint8_t trail[13];
    if (!e)
        return NEC_ERR_ARG;
    if (e->err != NEC_OK)
        return e->err;
    if (e->fill) {
        if (nec_enc_emit_chunk(e, e->chunk, e->fill) != NEC_OK)
            return e->err;
        e->fill = 0;
    }
    if (!e->live)
        return NEC_OK;
    trail[0] = 0x00;
    nec_put_u64(trail + 1, e->orig);
    nec_put_u32(trail + 9, e->crc_state ^ 0xFFFFFFFFu);
    return nec_sink_all(e, trail, 13);
}

int nec_lite_compress(
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t *dst_len,
    uint8_t *scratch,
    size_t scratch_n,
    int frontend
) {
    nec_lite_enc_t e;
    nec_mem_sink sink;
    size_t need;
    (void)scratch;
    (void)scratch_n;
    if (!dst_len)
        return NEC_ERR_ARG;
    if (n > 0 && !src)
        return NEC_ERR_ARG;
    if (!dst)
        return NEC_ERR_ARG;
    if (frontend != NEC_LITE_FE_NONE && frontend != NEC_LITE_FE_I16 &&
        frontend != NEC_LITE_FE_TICK8)
        return NEC_ERR_ARG;
    need = nec_lite_compress_bound(n);
    if (*dst_len < need) {
        *dst_len = need;
        return NEC_ERR_DST;
    }
    if (n == 0) {
        nec_write_header(
            dst,
            (uint16_t)(nec_fe_flag(frontend) | NEC_LITE_FLAG_RC),
            0,
            0,
            nec_lite_crc32(src, 0)
        );
        *dst_len = (size_t)NEC_HEADER_SIZE;
        return NEC_OK;
    }
    sink.p = dst + NEC_HEADER_SIZE;
    sink.i = 0;
    sink.cap = *dst_len - NEC_HEADER_SIZE;
    nec_enc_reset_state(&e, frontend, nec_mem_sink_fn, &sink);
    e.live = 0;
    if (n && nec_lite_enc_push(&e, src, n) != NEC_OK)
        return e.err;
    if (nec_lite_enc_finish(&e) != NEC_OK)
        return e.err;
    nec_write_header(
        dst,
        nec_enc_flags(&e),
        (uint64_t)n,
        (uint64_t)sink.i,
        e.crc_state ^ 0xFFFFFFFFu
    );
    *dst_len = (size_t)NEC_HEADER_SIZE + sink.i;
    return NEC_OK;
}

static int nec_dec_out(nec_lite_dec_t *d, uint8_t *delta, size_t n) {
    nec_delta_st st;
    int fe;
    if (n == 0)
        return NEC_OK;
    fe = nec_fe_from_flags(d->flags);
    nec_delta_from_dec(d, &st);
    if (nec_delta_inv_chunk(&st, fe, delta, n) != 0) {
        d->err = NEC_ERR_SRC;
        return d->err;
    }
    nec_delta_to_dec(d, &st);
    d->crc_state = nec_crc32_update(d->crc_state, delta, n);
    d->decoded += n;
    if (d->orig_len > 0 && d->decoded > d->orig_len) {
        d->err = NEC_ERR_SRC;
        return d->err;
    }
    if (d->sink && d->sink(delta, n, d->sink_ctx) != n) {
        d->err = NEC_ERR_DST;
        return d->err;
    }
    return NEC_OK;
}

static int nec_dec_apply_payload(nec_lite_dec_t *d, const uint8_t *pay, size_t pay_n) {
    size_t nsym = nec_nsym_dec(d->nsym);
    size_t bi;
    if (d->rec_kind == 0xFFu) {
        if (pay_n != nsym)
            return NEC_ERR_SRC;
        memcpy(d->chunk, pay, nsym);
        for (bi = 0; bi < nsym; bi++)
            nec_nib_see(&d->model, d->chunk[bi]);
        return nec_dec_out(d, d->chunk, nsym);
    }
    if (d->rec_kind < 1u || d->rec_kind > 254u)
        return NEC_ERR_SRC;
    if (pay_n != (size_t)d->rec_kind)
        return NEC_ERR_SRC;
    {
        nec_rc_dec dec;
        nec_rc_dec_init(&dec, pay, pay_n);
        for (bi = 0; bi < nsym; bi++) {
            int b = nec_rc_dec_byte(&dec, &d->model);
            if (b < 0)
                return NEC_ERR_SRC;
            d->chunk[bi] = (uint8_t)b;
        }
    }
    return nec_dec_out(d, d->chunk, nsym);
}

int nec_lite_dec_init(nec_lite_dec_t *d, nec_lite_sink_fn sink, void *ctx) {
    if (!d || !sink)
        return NEC_ERR_ARG;
    memset(d, 0, sizeof *d);
    nec_nib_init(&d->model);
    d->crc_state = 0xFFFFFFFFu;
    d->sink = sink;
    d->sink_ctx = ctx;
    d->err = NEC_OK;
    return NEC_OK;
}

static void nec_dec_consume(nec_lite_dec_t *d, size_t n) {
    if (n >= d->in_n) {
        d->in_n = 0;
        return;
    }
    memmove(d->in, d->in + n, d->in_n - n);
    d->in_n -= n;
}

static int nec_dec_pump(nec_lite_dec_t *d) {
    while (d->err == NEC_OK && !d->done) {
        if (!d->have_header) {
            NecLiteHeader h;
            int rc;
            if (d->in_n < NEC_HEADER_SIZE)
                return NEC_OK;
            rc = nec_header_fields(d->in, &h);
            if (rc != NEC_OK) {
                d->err = rc;
                return rc;
            }
            d->flags = h.flags;
            d->orig_len = h.orig_len;
            d->coded_expect = h.coded_len;
            d->hdr_crc = h.crc32;
            d->live = (uint8_t)((h.flags & NEC_LITE_FLAG_STREAM) && h.orig_len == 0 &&
                                h.coded_len == 0);
            if (!(h.flags & NEC_LITE_FLAG_STREAM)) {
                d->err = NEC_ERR_SRC;
                return d->err;
            }
            nec_dec_consume(d, NEC_HEADER_SIZE);
            d->have_header = 1;
            if (d->orig_len == 0 && !d->live) {
                d->done = 1;
                if ((d->crc_state ^ 0xFFFFFFFFu) != d->hdr_crc) {
                    d->err = NEC_ERR_CRC;
                    return d->err;
                }
                return NEC_OK;
            }
            continue;
        }

        if (!d->want_nsym && !d->want_pay && d->rec_kind == 0) {
            if (d->in_n < 1)
                return NEC_OK;
            d->rec_kind = d->in[0];
            nec_dec_consume(d, 1);
            d->payload_seen += 1;
            if (d->rec_kind == 0x00) {
                d->rec_need = 12;
                d->want_pay = 1;
                continue;
            }
            d->want_nsym = 1;
            continue;
        }

        if (d->want_nsym) {
            if (d->in_n < 1)
                return NEC_OK;
            d->nsym = d->in[0];
            nec_dec_consume(d, 1);
            d->payload_seen += 1;
            d->want_nsym = 0;
            if (d->rec_kind == 0xFFu)
                d->rec_need = nec_nsym_dec(d->nsym);
            else
                d->rec_need = (size_t)d->rec_kind;
            d->want_pay = 1;
            continue;
        }

        if (d->want_pay) {
            if (d->in_n < d->rec_need)
                return NEC_OK;
            if (d->rec_kind == 0x00) {
                uint64_t orig = nec_get_u64(d->in);
                uint32_t crc = nec_get_u32(d->in + 8);
                nec_dec_consume(d, 12);
                d->payload_seen += 12;
                if (!d->live) {
                    d->err = NEC_ERR_SRC;
                    return d->err;
                }
                d->orig_len = orig;
                d->hdr_crc = crc;
                d->want_pay = 0;
                d->rec_kind = 0;
                d->done = 1;
                if (d->decoded != d->orig_len) {
                    d->err = NEC_ERR_SRC;
                    return d->err;
                }
                if ((d->crc_state ^ 0xFFFFFFFFu) != d->hdr_crc) {
                    d->err = NEC_ERR_CRC;
                    return d->err;
                }
                return NEC_OK;
            }
            {
                int rc = nec_dec_apply_payload(d, d->in, d->rec_need);
                nec_dec_consume(d, d->rec_need);
                d->payload_seen += d->rec_need;
                d->want_pay = 0;
                d->rec_kind = 0;
                if (rc != NEC_OK) {
                    d->err = rc;
                    return rc;
                }
            }
            if (!d->live && d->orig_len > 0 && d->decoded == d->orig_len) {
                d->done = 1;
                if (d->coded_expect > 0 && d->payload_seen != d->coded_expect) {
                    d->err = NEC_ERR_SRC;
                    return d->err;
                }
                if ((d->crc_state ^ 0xFFFFFFFFu) != d->hdr_crc) {
                    d->err = NEC_ERR_CRC;
                    return d->err;
                }
                return NEC_OK;
            }
            continue;
        }
        return NEC_OK;
    }
    return d->err;
}

int nec_lite_dec_push(nec_lite_dec_t *d, const uint8_t *src, size_t n) {
    if (!d)
        return NEC_ERR_ARG;
    if (d->err != NEC_OK)
        return d->err;
    if (n > 0 && !src)
        return NEC_ERR_ARG;
    while (n) {
        size_t room = sizeof d->in - d->in_n;
        size_t take;
        if (room == 0) {
            if (nec_dec_pump(d) != NEC_OK)
                return d->err;
            room = sizeof d->in - d->in_n;
            if (room == 0) {
                d->err = NEC_ERR_SRC;
                return d->err;
            }
        }
        take = n < room ? n : room;
        memcpy(d->in + d->in_n, src, take);
        d->in_n += take;
        src += take;
        n -= take;
        if (nec_dec_pump(d) != NEC_OK)
            return d->err;
        if (d->done)
            break;
    }
    return d->err;
}

int nec_lite_dec_finish(nec_lite_dec_t *d) {
    if (!d)
        return NEC_ERR_ARG;
    if (d->err != NEC_OK)
        return d->err;
    if (nec_dec_pump(d) != NEC_OK)
        return d->err;
    if (!d->done) {
        d->err = NEC_ERR_SRC;
        return d->err;
    }
    if (d->in_n != 0) {
        d->err = NEC_ERR_SRC;
        return d->err;
    }
    return NEC_OK;
}

static int nec_decompress_stream(
    const uint8_t *src,
    size_t src_n,
    uint8_t *dst,
    size_t *dst_len,
    const NecLiteHeader *h
) {
    nec_lite_dec_t d;
    nec_mem_sink sink;
    int rc;
    sink.p = dst;
    sink.i = 0;
    sink.cap = *dst_len;
    rc = nec_lite_dec_init(&d, nec_mem_sink_fn, &sink);
    if (rc != NEC_OK)
        return rc;
    rc = nec_lite_dec_push(&d, src, src_n);
    if (rc != NEC_OK) {
        if (rc == NEC_ERR_DST) {
            if (h->orig_len > 0)
                *dst_len = (size_t)h->orig_len;
            else if (d.orig_len > 0)
                *dst_len = (size_t)d.orig_len;
        }
        return rc;
    }
    rc = nec_lite_dec_finish(&d);
    if (rc != NEC_OK)
        return rc;
    *dst_len = sink.i;
    return NEC_OK;
}

static int nec_apply_delta_inv_buf(uint16_t flags, uint8_t *buf, size_t n) {
    nec_delta_st st;
    nec_delta_st_init(&st);
    return nec_delta_inv_chunk(&st, nec_fe_from_flags(flags), buf, n);
}

int nec_lite_decompress(
    const uint8_t *src,
    size_t src_n,
    uint8_t *dst,
    size_t *dst_len,
    uint8_t *scratch,
    size_t scratch_n
) {
    NecLiteHeader h;
    size_t orig;
    size_t coded;
    const uint8_t *payload;
    size_t flag_n;
    size_t nchunks;
    size_t ci;
    size_t remain;
    const uint8_t *in;
    const uint8_t *end;
    nec_nib_model_t model;
    int rc;
    (void)scratch;
    (void)scratch_n;
    if (!dst_len)
        return NEC_ERR_ARG;
    rc = nec_lite_parse_header(src, src_n, &h);
    if (rc != NEC_OK)
        return rc;
    if (h.orig_len > (uint64_t)SIZE_MAX)
        return NEC_ERR_SRC;
    orig = (size_t)h.orig_len;
    coded = (size_t)h.coded_len;
    if (h.flags & NEC_LITE_FLAG_STREAM) {
        size_t use = src_n;
        if (h.orig_len > (uint64_t)SIZE_MAX)
            return NEC_ERR_SRC;
        if (h.orig_len > 0) {
            if (!dst && h.orig_len > 0)
                return NEC_ERR_ARG;
            if (h.orig_len > (uint64_t)*dst_len) {
                *dst_len = (size_t)h.orig_len;
                return NEC_ERR_DST;
            }
        }
        if (h.coded_len > 0) {
            uint64_t need = (uint64_t)NEC_HEADER_SIZE + h.coded_len;
            if ((uint64_t)src_n < need)
                return NEC_ERR_SRC;
            use = (size_t)need;
        }
        return nec_decompress_stream(src, use, dst, dst_len, &h);
    }
    if (orig > *dst_len) {
        *dst_len = orig;
        return NEC_ERR_DST;
    }
    if (orig > 0 && !dst)
        return NEC_ERR_ARG;
    payload = src + NEC_HEADER_SIZE;
    end = payload + coded;

    if (!(h.flags & NEC_LITE_FLAG_RC)) {
        if (!(h.flags & NEC_FLAG_STORE))
            return NEC_ERR_SRC;
        if (coded != orig)
            return NEC_ERR_SRC;
        if (orig)
            memcpy(dst, payload, orig);
        rc = nec_apply_delta_inv_buf(h.flags, dst, orig);
        if (rc != NEC_OK)
            return rc;
        if (nec_lite_crc32(dst, orig) != h.crc32)
            return NEC_ERR_CRC;
        *dst_len = orig;
        return NEC_OK;
    }

    if (orig == 0) {
        *dst_len = 0;
        if (nec_lite_crc32(dst, 0) != h.crc32)
            return NEC_ERR_CRC;
        return NEC_OK;
    }

    nec_nib_init(&model);
    flag_n = nec_flag_bytes(orig);
    if ((size_t)(end - payload) < flag_n)
        return NEC_ERR_SRC;
    nchunks = nec_nchunks(orig);
    in = payload + flag_n;
    remain = orig;
    for (ci = 0; ci < nchunks; ci++) {
        size_t off = ci * (size_t)NEC_LITE_CHUNK;
        size_t clen = remain > (size_t)NEC_LITE_CHUNK ? (size_t)NEC_LITE_CHUNK : remain;
        int compressed = (payload[ci / 8u] >> (ci % 8u)) & 1;
        size_t bi;
        if (compressed) {
            unsigned len;
            nec_rc_dec dec;
            if (in >= end)
                return NEC_ERR_SRC;
            len = *in++;
            if ((size_t)(end - in) < len)
                return NEC_ERR_SRC;
            nec_rc_dec_init(&dec, in, len);
            for (bi = 0; bi < clen; bi++) {
                int b = nec_rc_dec_byte(&dec, &model);
                if (b < 0)
                    return NEC_ERR_SRC;
                dst[off + bi] = (uint8_t)b;
            }
            in += len;
        } else {
            if ((size_t)(end - in) < clen)
                return NEC_ERR_SRC;
            memcpy(dst + off, in, clen);
            in += clen;
            for (bi = 0; bi < clen; bi++)
                nec_nib_see(&model, dst[off + bi]);
        }
        remain -= clen;
    }
    if (in != end)
        return NEC_ERR_SRC;
    rc = nec_apply_delta_inv_buf(h.flags, dst, orig);
    if (rc != NEC_OK)
        return rc;
    if (nec_lite_crc32(dst, orig) != h.crc32)
        return NEC_ERR_CRC;
    *dst_len = orig;
    return NEC_OK;
}

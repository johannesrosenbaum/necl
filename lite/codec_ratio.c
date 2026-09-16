#define _POSIX_C_SOURCE 200809L
/*
 * Host-Ratio: nec_lite vs heatshrink vs lz4 vs miniz vs DRH vs Sprintz-Delta.
 * Roundtrip-pflichtig. DRH/Sprintz nur auf geraden int16-Corpora (numeric=1).
 * Kein MCU-Flash — das steht in mcu-size.json.
 */
#include "drh.h"
#include "hs_stream.h"
#include "lz4.h"
#include "miniz.h"
#include "nec_lite.h"
#include "sprintz_delta.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static uint32_t rng_state = 0x4E4543u;

static uint32_t rndu(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static double rnd01(void) {
    return (rndu() >> 8) / 16777216.0;
}

static double gauss(double mu, double sigma) {
    double u1, u2;
    do {
        u1 = rnd01();
    } while (u1 <= 1e-12);
    u2 = rnd01();
    return mu + sigma * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static uint8_t *gen_i16(size_t samples, size_t *n_out) {
    size_t n = samples * 2;
    uint8_t *buf = (uint8_t *)malloc(n);
    double x = 0.0;
    size_t i;
    rng_state = 7;
    for (i = 0; i < samples; i++) {
        int v;
        x = 0.97 * x + gauss(0.0, 90.0);
        v = (int)lround(x);
        if (v > 32767)
            v = 32767;
        if (v < -32767)
            v = -32767;
        buf[i * 2] = (uint8_t)(v & 0xff);
        buf[i * 2 + 1] = (uint8_t)((v >> 8) & 0xff);
    }
    *n_out = n;
    return buf;
}

static uint8_t *gen_ticks(size_t count, size_t *n_out) {
    size_t n = count * 8;
    uint8_t *buf = (uint8_t *)malloc(n);
    uint32_t ts = 1700000000u;
    int32_t px = 10000;
    size_t i;
    rng_state = 13;
    for (i = 0; i < count; i++) {
        int32_t dpx;
        ts += 1u + (rndu() % 5u);
        dpx = (int32_t)lround(gauss(0.0, 4.0));
        px += dpx;
        buf[i * 8 + 0] = (uint8_t)(ts & 0xff);
        buf[i * 8 + 1] = (uint8_t)((ts >> 8) & 0xff);
        buf[i * 8 + 2] = (uint8_t)((ts >> 16) & 0xff);
        buf[i * 8 + 3] = (uint8_t)((ts >> 24) & 0xff);
        buf[i * 8 + 4] = (uint8_t)(px & 0xff);
        buf[i * 8 + 5] = (uint8_t)((px >> 8) & 0xff);
        buf[i * 8 + 6] = (uint8_t)((px >> 16) & 0xff);
        buf[i * 8 + 7] = (uint8_t)((px >> 24) & 0xff);
    }
    *n_out = n;
    return buf;
}

static uint8_t *gen_harness128(size_t *n_out) {
    uint8_t *buf = (uint8_t *)malloc(128);
    size_t i;
    for (i = 0; i < 128; i++)
        buf[i] = (uint8_t)(i * 3u);
    *n_out = 128;
    return buf;
}

static uint8_t *gen_logline(size_t n, size_t *n_out) {
    static const char pat[] = "sensor;id=12;temp=21.50;ok\n";
    size_t plen = sizeof(pat) - 1;
    uint8_t *buf = (uint8_t *)malloc(n);
    size_t i;
    for (i = 0; i < n; i++)
        buf[i] = (uint8_t)pat[i % plen];
    *n_out = n;
    return buf;
}

static uint8_t *gen_random(size_t n, size_t *n_out) {
    uint8_t *buf = (uint8_t *)malloc(n);
    size_t i;
    rng_state = 99;
    for (i = 0; i < n; i++)
        buf[i] = (uint8_t)(rndu() >> 16);
    *n_out = n;
    return buf;
}

static int roundtrip_nec(const uint8_t *src, size_t n, int fe, size_t *coded) {
    size_t bound = nec_lite_compress_bound(n);
    uint8_t *dst = (uint8_t *)malloc(bound);
    uint8_t *plain = (uint8_t *)malloc(n ? n : 1);
    size_t clen = bound;
    size_t dlen = n;
    int rc;
    if (!dst || !plain)
        return 1;
    rc = nec_lite_compress(src, n, dst, &clen, NULL, 0, fe);
    if (rc != NEC_OK)
        goto fail;
    rc = nec_lite_decompress(dst, clen, plain, &dlen, NULL, 0);
    if (rc != NEC_OK || dlen != n || memcmp(plain, src, n) != 0)
        goto fail;
    *coded = clen;
    free(dst);
    free(plain);
    return 0;
fail:
    free(dst);
    free(plain);
    return 1;
}

static int roundtrip_lz4(const uint8_t *src, size_t n, size_t *coded) {
    int bound = LZ4_compressBound((int)n);
    char *dst = (char *)malloc((size_t)bound);
    char *plain = (char *)malloc(n ? n : 1);
    int cn;
    int dn;
    if (!dst || !plain)
        return 1;
    cn = LZ4_compress_default((const char *)src, dst, (int)n, bound);
    if (cn <= 0)
        goto fail;
    dn = LZ4_decompress_safe(dst, plain, cn, (int)n);
    if (dn != (int)n || memcmp(plain, src, n) != 0)
        goto fail;
    *coded = (size_t)cn;
    free(dst);
    free(plain);
    return 0;
fail:
    free(dst);
    free(plain);
    return 1;
}

static int roundtrip_miniz(const uint8_t *src, size_t n, size_t *coded) {
    size_t cap = n * 2 + 64;
    uint8_t *dst = (uint8_t *)malloc(cap);
    uint8_t *plain = (uint8_t *)malloc(n ? n : 1);
    size_t cn;
    size_t dn;
    int flags = TDEFL_WRITE_ZLIB_HEADER | TDEFL_DEFAULT_MAX_PROBES;
    if (!dst || !plain)
        return 1;
    cn = tdefl_compress_mem_to_mem(dst, cap, src, n, flags);
    if (cn == 0)
        goto fail;
    dn = tinfl_decompress_mem_to_mem(plain, n, dst, cn, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (dn != n || memcmp(plain, src, n) != 0)
        goto fail;
    *coded = cn;
    free(dst);
    free(plain);
    return 0;
fail:
    free(dst);
    free(plain);
    return 1;
}

static int roundtrip_hs(const uint8_t *src, size_t n, size_t *coded) {
    static heatshrink_encoder enc;
    static heatshrink_decoder dec;
    size_t cap = n * 2 + 64;
    uint8_t *dst = (uint8_t *)malloc(cap);
    uint8_t *plain = (uint8_t *)malloc(n ? n : 1);
    size_t cn = 0;
    size_t dn = 0;
    if (!dst || !plain)
        return 1;
    if (hs_compress_mem(&enc, src, n, dst, cap, &cn) != 0 || cn == 0)
        goto fail;
    if (hs_decompress_mem(&dec, dst, cn, plain, n, &dn) != 0 || dn != n)
        goto fail;
    if (memcmp(plain, src, n) != 0)
        goto fail;
    *coded = cn;
    free(dst);
    free(plain);
    return 0;
fail:
    free(dst);
    free(plain);
    return 1;
}

static void emit(
    const char *corpus,
    const char *codec,
    size_t orig,
    size_t coded
) {
    double pct = orig ? (100.0 * (double)coded / (double)orig) : 0.0;
    printf("%-16s %-12s %8zu %8zu %6.1f%%\n", corpus, codec, orig, coded, pct);
}

static int roundtrip_drh(const uint8_t *src, size_t n, size_t *coded) {
    size_t ns = n / 2;
    const int16_t *s = (const int16_t *)(const void *)src;
    size_t cap = n * 2 + 64;
    uint8_t *dst = (uint8_t *)malloc(cap);
    int16_t *plain = (int16_t *)malloc(n ? n : 2);
    size_t cn = 0;
    size_t dn = 0;
    if (!dst || !plain)
        return 1;
    if (drh_compress_i16(s, ns, dst, cap, &cn) != 0 || cn == 0)
        goto fail;
    if (drh_decompress_i16(dst, cn, plain, ns, &dn) != 0 || dn != ns)
        goto fail;
    if (memcmp(plain, src, n) != 0)
        goto fail;
    *coded = cn;
    free(dst);
    free(plain);
    return 0;
fail:
    free(dst);
    free(plain);
    return 1;
}

static int roundtrip_sprintz(const uint8_t *src, size_t n, size_t *coded) {
    size_t ns = n / 2;
    const int16_t *s = (const int16_t *)(const void *)src;
    size_t cap = n * 2 + 64;
    uint8_t *dst = (uint8_t *)malloc(cap);
    int16_t *plain = (int16_t *)malloc(n ? n : 2);
    size_t cn = 0;
    size_t dn = 0;
    if (!dst || !plain)
        return 1;
    if (sprintz_delta_compress_i16(s, ns, 1, dst, cap, &cn) != 0 || cn == 0)
        goto fail;
    if (sprintz_delta_decompress_i16(dst, cn, 1, plain, ns, &dn) != 0 || dn != ns)
        goto fail;
    if (memcmp(plain, src, n) != 0)
        goto fail;
    *coded = cn;
    free(dst);
    free(plain);
    return 0;
fail:
    free(dst);
    free(plain);
    return 1;
}

static int one(const char *corpus, const uint8_t *src, size_t n, int fe, int numeric) {
    size_t coded;
    if (roundtrip_nec(src, n, fe, &coded) != 0) {
        fprintf(stderr, "FAIL nec_lite %s\n", corpus);
        return 1;
    }
    emit(corpus, "nec_lite", n, coded);
    if (roundtrip_hs(src, n, &coded) != 0) {
        fprintf(stderr, "FAIL heatshrink %s\n", corpus);
        return 1;
    }
    emit(corpus, "heatshrink", n, coded);
    if (roundtrip_lz4(src, n, &coded) != 0) {
        fprintf(stderr, "FAIL lz4 %s\n", corpus);
        return 1;
    }
    emit(corpus, "lz4", n, coded);
    if (roundtrip_miniz(src, n, &coded) != 0) {
        fprintf(stderr, "FAIL miniz %s\n", corpus);
        return 1;
    }
    emit(corpus, "miniz", n, coded);
    if (numeric && (n % 2u) == 0) {
        if (roundtrip_drh(src, n, &coded) != 0) {
            fprintf(stderr, "FAIL drh %s\n", corpus);
            return 1;
        }
        emit(corpus, "drh", n, coded);
        if (roundtrip_sprintz(src, n, &coded) != 0) {
            fprintf(stderr, "FAIL sprintz_d %s\n", corpus);
            return 1;
        }
        emit(corpus, "sprintz_d", n, coded);
    }
    return 0;
}

int main(void) {
    size_t n;
    uint8_t *buf;
    printf(
        "%-16s %-12s %8s %8s %7s\n",
        "corpus",
        "codec",
        "orig",
        "coded",
        "ratio"
    );

    buf = gen_harness128(&n);
    if (one("harness128", buf, n, NEC_LITE_FE_I16, 1) != 0)
        return 1;
    free(buf);

    buf = gen_i16(80000, &n);
    if (one("timeseries_i16", buf, n, NEC_LITE_FE_I16, 1) != 0)
        return 1;
    if (one("timeseries_16k", buf, 16384, NEC_LITE_FE_I16, 1) != 0)
        return 1;
    free(buf);

    buf = gen_ticks(18000, &n);
    if (one("ticks", buf, n, NEC_LITE_FE_TICK8, 0) != 0)
        return 1;
    if (one("ticks_16k", buf, 16384, NEC_LITE_FE_TICK8, 0) != 0)
        return 1;
    free(buf);

    buf = gen_logline(16384, &n);
    if (one("logline_16k", buf, n, NEC_LITE_FE_NONE, 0) != 0)
        return 1;
    free(buf);

    buf = gen_random(16384, &n);
    if (one("random_16k", buf, n, NEC_LITE_FE_NONE, 0) != 0)
        return 1;
    free(buf);

    {
        const char *data_dir = getenv("NEC_DATA_DIR");
        DIR *dir;
        struct dirent *ent;
        char path[512];
        if (!data_dir || !data_dir[0])
            data_dir = "data";
        dir = opendir(data_dir);
        if (dir) {
            while ((ent = readdir(dir)) != NULL) {
                size_t len = strlen(ent->d_name);
                FILE *f;
                long sz;
                if (len < 7)
                    continue;
                if (strcmp(ent->d_name + len - 6, ".i16le") != 0)
                    continue;
                snprintf(path, sizeof path, "%s/%s", data_dir, ent->d_name);
                f = fopen(path, "rb");
                if (!f)
                    continue;
                if (fseek(f, 0, SEEK_END) != 0) {
                    fclose(f);
                    continue;
                }
                sz = ftell(f);
                rewind(f);
                if (sz <= 0) {
                    fclose(f);
                    continue;
                }
                buf = (uint8_t *)malloc((size_t)sz);
                if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
                    free(buf);
                    fclose(f);
                    closedir(dir);
                    return 1;
                }
                fclose(f);
                if (one(ent->d_name, buf, (size_t)sz, NEC_LITE_FE_I16, 1) != 0) {
                    free(buf);
                    closedir(dir);
                    return 1;
                }
                free(buf);
            }
            closedir(dir);
        }
    }

    return 0;
}

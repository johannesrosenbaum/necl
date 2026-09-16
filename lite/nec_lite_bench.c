#define _POSIX_C_SOURCE 200809L
/* Gateway-Bench: nec_lite vs lz4 vs zstd-1. Kein Mojo.
 * Misst Ratio, Decode-ns (Median), deklariertes Working-Set.
 */
#include "nec_lite.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zstd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int LZ4_compress_default(const char *src, char *dst, int srcSize, int dstCapacity);
int LZ4_decompress_safe(const char *src, char *dst, int compressedSize, int dstCapacity);
int LZ4_compressBound(int inputSize);

#define LOOPS 80
#define WARMUP 8

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

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t ns_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t median_ns(uint64_t *v, int n) {
    qsort(v, (size_t)n, sizeof(uint64_t), cmp_u64);
    return v[n / 2];
}

static void report(
    const char *corpus,
    const char *codec,
    size_t orig,
    size_t coded,
    uint64_t decode_ns,
    size_t workset
) {
    double pct = orig ? (100.0 * (double)coded / (double)orig) : 0.0;
    double mb_s = decode_ns ? ((double)orig / (1024.0 * 1024.0)) / ((double)decode_ns / 1e9) : 0.0;
    printf(
        "%-16s %-10s %8zu %8zu %6.1f%%  %8.1f us  %7.1f MB/s  ws=%zu\n",
        corpus,
        codec,
        orig,
        coded,
        pct,
        (double)decode_ns / 1000.0,
        mb_s,
        workset
    );
}

static int bench_one(const char *corpus, uint8_t *src, size_t n, int fe) {
    uint8_t *nec_c = malloc(nec_lite_compress_bound(n));
    uint8_t *plain = malloc(n ? n : 1);
    size_t nec_n = nec_lite_compress_bound(n);
    int lz_bound = LZ4_compressBound((int)n);
    uint8_t *lz_c = malloc((size_t)lz_bound);
    uint8_t *z_c = malloc(ZSTD_compressBound(n));
    size_t z_n;
    int lz_n;
    uint64_t samples[LOOPS];
    int i;
    int rc;
    uint64_t t0, t1;

    if (!nec_c || !plain || !lz_c || !z_c)
        return 1;
    rc = nec_lite_compress(src, n, nec_c, &nec_n, NULL, 0, fe);
    if (rc != NEC_OK) {
        fprintf(stderr, "nec_lite compress: %s\n", nec_lite_strerror(rc));
        return 1;
    }
    {
        size_t dlen = n;
        rc = nec_lite_decompress(nec_c, nec_n, plain, &dlen, NULL, 0);
        if (rc != NEC_OK || dlen != n || memcmp(plain, src, n) != 0) {
            fprintf(stderr, "nec_lite roundtrip fail rc=%d\n", rc);
            return 1;
        }
    }
    for (i = 0; i < WARMUP; i++) {
        size_t dlen = n;
        nec_lite_decompress(nec_c, nec_n, plain, &dlen, NULL, 0);
    }
    for (i = 0; i < LOOPS; i++) {
        size_t dlen = n;
        t0 = ns_now();
        nec_lite_decompress(nec_c, nec_n, plain, &dlen, NULL, 0);
        t1 = ns_now();
        samples[i] = t1 - t0;
    }
    report(
        corpus,
        "nec_lite",
        n,
        nec_n,
        median_ns(samples, LOOPS),
        n + nec_lite_enc_sizeof() + (size_t)NEC_HEADER_SIZE
    );

    lz_n = LZ4_compress_default((const char *)src, (char *)lz_c, (int)n, lz_bound);
    if (lz_n <= 0)
        return 1;
    if (LZ4_decompress_safe((const char *)lz_c, (char *)plain, lz_n, (int)n) != (int)n)
        return 1;
    if (memcmp(plain, src, n) != 0)
        return 1;
    for (i = 0; i < WARMUP; i++)
        LZ4_decompress_safe((const char *)lz_c, (char *)plain, lz_n, (int)n);
    for (i = 0; i < LOOPS; i++) {
        t0 = ns_now();
        LZ4_decompress_safe((const char *)lz_c, (char *)plain, lz_n, (int)n);
        t1 = ns_now();
        samples[i] = t1 - t0;
    }
    report(corpus, "lz4", n, (size_t)lz_n, median_ns(samples, LOOPS), n + (size_t)lz_n);

    z_n = ZSTD_compress(z_c, ZSTD_compressBound(n), src, n, 1);
    if (ZSTD_isError(z_n))
        return 1;
    if (ZSTD_decompress(plain, n, z_c, z_n) != n)
        return 1;
    if (memcmp(plain, src, n) != 0)
        return 1;
    for (i = 0; i < WARMUP; i++)
        ZSTD_decompress(plain, n, z_c, z_n);
    for (i = 0; i < LOOPS; i++) {
        t0 = ns_now();
        ZSTD_decompress(plain, n, z_c, z_n);
        t1 = ns_now();
        samples[i] = t1 - t0;
    }
    report(corpus, "zstd-1", n, z_n, median_ns(samples, LOOPS), n + z_n);

    free(nec_c);
    free(plain);
    free(lz_c);
    free(z_c);
    return 0;
}

int main(void) {
    size_t n;
    uint8_t *i16;
    uint8_t *ticks;
    printf(
        "%-16s %-10s %8s %8s %7s  %10s  %11s  %s\n",
        "corpus",
        "codec",
        "orig",
        "coded",
        "ratio",
        "decode",
        "throughput",
        "workset"
    );
    i16 = gen_i16(80000, &n);
    if (bench_one("timeseries_i16", i16, n, NEC_LITE_FE_I16) != 0)
        return 1;
    free(i16);
    ticks = gen_ticks(18000, &n);
    if (bench_one("ticks", ticks, n, NEC_LITE_FE_TICK8) != 0)
        return 1;
    free(ticks);
    return 0;
}

/*
 * Misst nec_lite-Ratio unter Compile-Flags (typisch -DNEC_LITE_NO_MALLOC).
 * Synth-Corpora identisch zu codec_ratio.c (Suite-Vergleichbarkeit).
 *
 *   corpus  frontend  orig  coded  ratio_pct  mark_hex
 */
#include "nec_lite.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    const char *id;
    const char *path;
    int frontend;
    int synth; /* 0=file, 1=ticks, 2=timeseries */
    size_t synth_n;
} Case;

static uint32_t rng_state = 7;

static uint32_t rndu(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static double rnd01(void) {
    return (rndu() >> 8) * (1.0 / 16777216.0);
}

static double gauss(double mu, double sigma) {
    double u1, u2;
    do {
        u1 = rnd01();
    } while (u1 <= 1e-12);
    u2 = rnd01();
    return mu + sigma * sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

static int load_file(const char *path, uint8_t **out, size_t *n) {
    FILE *f;
    long sz;
    uint8_t *buf;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *n = (size_t)sz;
    return 0;
}

static uint8_t *gen_ticks(size_t count, size_t *n_out) {
    size_t n = count * 8;
    uint8_t *buf = (uint8_t *)malloc(n);
    uint32_t ts = 1700000000u;
    int32_t px = 10000;
    size_t i;
    rng_state = 13;
    if (!buf)
        return NULL;
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

static uint8_t *gen_i16(size_t samples, size_t *n_out) {
    size_t n = samples * 2;
    uint8_t *buf = (uint8_t *)malloc(n);
    double x = 0.0;
    size_t i;
    rng_state = 7;
    if (!buf)
        return NULL;
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

static int run_one(const char *id, const uint8_t *src, size_t n, int fe) {
    size_t cap = nec_lite_compress_bound(n);
    uint8_t *dst = (uint8_t *)malloc(cap);
    uint8_t *back = (uint8_t *)malloc(n ? n : 1);
    size_t clen = cap, dlen = n;
    int rc;
    if (!dst || !back) {
        free(dst);
        free(back);
        return -1;
    }
    rc = nec_lite_compress(src, n, dst, &clen, NULL, 0, fe);
    if (rc != NEC_OK) {
        fprintf(stderr, "%s compress: %s\n", id, nec_lite_strerror(rc));
        free(dst);
        free(back);
        return -1;
    }
    rc = nec_lite_decompress(dst, clen, back, &dlen, NULL, 0);
    if (rc != NEC_OK || dlen != n || (n && memcmp(src, back, n) != 0)) {
        fprintf(stderr, "%s roundtrip fail rc=%d\n", id, rc);
        free(dst);
        free(back);
        return -1;
    }
    printf(
        "%s\t%d\t%zu\t%zu\t%.2f\t0x%02x\n",
        id,
        fe,
        n,
        clen,
        n ? (100.0 * (double)clen / (double)n) : 0.0,
        n ? dst[0] : 0
    );
    free(dst);
    free(back);
    return 0;
}

int main(int argc, char **argv) {
    const char *data = getenv("NEC_DATA_DIR");
    char path[512];
    int i, fails = 0;
    Case cases[] = {
        {"ticks", NULL, NEC_LITE_FE_TICK8, 1, 18000},
        {"timeseries_i16", NULL, NEC_LITE_FE_I16, 2, 80000},
        {"nab_machine_temp.i16le", "nab_machine_temp.i16le", NEC_LITE_FE_I16, 0, 0},
        {"nab_ambient_temp.i16le", "nab_ambient_temp.i16le", NEC_LITE_FE_I16, 0, 0},
        {"nab_ec2_cpu.i16le", "nab_ec2_cpu.i16le", NEC_LITE_FE_I16, 0, 0},
        {"melbourne_daily_min_temp.i16le", "melbourne_daily_min_temp.i16le", NEC_LITE_FE_I16, 0,
         0},
        {"intel_lab_temp.i16le", "intel_lab_temp.i16le", NEC_LITE_FE_I16, 0, 0},
        {"joint_7axis_1khz_10s.i16le", "joint_7axis_1khz_10s.i16le", NEC_LITE_FE_I16, 0, 0},
    };
    (void)argc;
    (void)argv;
    if (!data)
        data = "data";

    printf("# nomalloc=%d\n",
#ifdef NEC_LITE_NO_MALLOC
           1
#else
           0
#endif
    );
    printf("# corpus\tfe\torig\tcoded\tratio_pct\tmark\n");

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        uint8_t *src = NULL;
        size_t n = 0;
        Case *c = &cases[i];
        if (c->synth == 1) {
            src = gen_ticks(c->synth_n, &n);
            if (!src) {
                fails++;
                continue;
            }
        } else if (c->synth == 2) {
            src = gen_i16(c->synth_n, &n);
            if (!src) {
                fails++;
                continue;
            }
        } else {
            snprintf(path, sizeof path, "%s/%s", data, c->path);
            if (load_file(path, &src, &n) != 0) {
                fprintf(stderr, "skip missing %s\n", path);
                continue;
            }
        }
        if (run_one(c->id, src, n, c->frontend) != 0)
            fails++;
        free(src);
    }
    return fails ? 1 : 0;
}

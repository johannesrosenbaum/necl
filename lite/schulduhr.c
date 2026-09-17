#include "schulduhr.h"

#include <string.h>

#define SDU_WIN   32
#define SDU_TAG_SYNC 0
#define SDU_TAG_TILG 1
#define SDU_TAG_RAW  2
#define SDU_TAG_VEL  3

static size_t sdu_tilg_count;
static size_t sdu_tick_samples;

void schulduhr_stats_reset(void) {
    sdu_tilg_count = 0;
    sdu_tick_samples = 0;
}

void schulduhr_stats_get(size_t *tilg_count, size_t *tick_samples) {
    if (tilg_count)
        *tilg_count = sdu_tilg_count;
    if (tick_samples)
        *tick_samples = sdu_tick_samples;
}

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
    uint32_t acc;
    int bits;
    int err;
} sdu_bw;

typedef struct {
    const uint8_t *p;
    size_t i;
    size_t n;
    uint32_t acc;
    int bits;
} sdu_br;

static void sdu_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint32_t sdu_get_u32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint16_t sdu_zz(int16_t n) {
    return (uint16_t)(((uint16_t)n << 1) ^ (uint16_t)(n >> 15));
}

static int16_t sdu_unzz(uint16_t u) {
    return (int16_t)((u >> 1) ^ (uint16_t)-(int16_t)(u & 1u));
}

static void sdu_put_bits(sdu_bw *b, uint32_t v, int n) {
    if (n <= 0 || b->err)
        return;
    b->acc |= (v & ((n >= 32) ? 0xffffffffu : ((1u << n) - 1u))) << b->bits;
    b->bits += n;
    while (b->bits >= 8) {
        if (b->i >= b->cap) {
            b->err = 1;
            return;
        }
        b->p[b->i++] = (uint8_t)(b->acc & 0xffu);
        b->acc >>= 8;
        b->bits -= 8;
    }
}

static void sdu_flush(sdu_bw *b) {
    if (b->bits > 0)
        sdu_put_bits(b, 0, 8 - b->bits);
}

static int sdu_get_bits(sdu_br *r, int n, uint32_t *out) {
    uint32_t v = 0;
    int got = 0;
    if (n <= 0) {
        *out = 0;
        return 0;
    }
    while (got < n) {
        int take;
        if (r->bits == 0) {
            if (r->i >= r->n)
                return -1;
            r->acc = r->p[r->i++];
            r->bits = 8;
        }
        take = n - got;
        if (take > r->bits)
            take = r->bits;
        v |= (r->acc & ((1u << take) - 1u)) << got;
        r->acc >>= take;
        r->bits -= take;
        got += take;
    }
    *out = v;
    return 0;
}

static void sdu_put_zz16(sdu_bw *b, int16_t x) {
    sdu_put_bits(b, sdu_zz(x), 16);
}

static int sdu_get_zz16(sdu_br *r, int16_t *out) {
    uint32_t z;
    if (sdu_get_bits(r, 16, &z) != 0)
        return -1;
    *out = sdu_unzz((uint16_t)z);
    return 0;
}

static int16_t sdu_clamp_i16(int32_t x) {
    if (x > 32767)
        return 32767;
    if (x < -32768)
        return -32768;
    return (int16_t)x;
}

static int16_t sdu_tick(int16_t p, int16_t vel) {
    return sdu_clamp_i16((int32_t)p + (int32_t)vel);
}

static int16_t sdu_pred(int16_t p, int16_t vel, size_t steps) {
    return sdu_clamp_i16((int32_t)p + (int32_t)vel * (int32_t)steps);
}

static size_t sdu_max_tilg_run(
    const int16_t *src,
    size_t off,
    size_t n,
    int16_t p,
    int16_t vel,
    size_t max_run,
    int16_t *dp_out
) {
    size_t best = 0;
    size_t run;
    if (max_run > SDU_WIN)
        max_run = SDU_WIN;
    if (max_run > n - off)
        max_run = n - off;
    for (run = 1; run <= max_run; run++) {
        size_t k;
        int ok = 1;
        for (k = 0; k < run; k++) {
            if (sdu_pred(p, vel, k + 1) != src[off + k]) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            break;
        best = run;
        *dp_out = (int16_t)(src[off + run - 1] - sdu_pred(p, vel, run));
    }
    return best;
}

static void sdu_emit_sync(sdu_bw *bw, int16_t p, int16_t vel) {
    sdu_put_bits(bw, SDU_TAG_SYNC, 2);
    sdu_put_zz16(bw, p);
    sdu_put_zz16(bw, vel);
}

static void sdu_emit_vel(sdu_bw *bw, int16_t vel) {
    sdu_put_bits(bw, SDU_TAG_VEL, 2);
    sdu_put_zz16(bw, vel);
}

static void sdu_emit_tilg(sdu_bw *bw, size_t run, int16_t dp) {
    sdu_put_bits(bw, SDU_TAG_TILG, 2);
    sdu_put_bits(bw, (uint32_t)(run - 1), 5);
    sdu_put_zz16(bw, dp);
    sdu_tilg_count++;
    sdu_tick_samples += run;
}

static void sdu_emit_raw(sdu_bw *bw, const int16_t *src, size_t run) {
    size_t i;
    sdu_put_bits(bw, SDU_TAG_RAW, 2);
    sdu_put_bits(bw, (uint32_t)(run - 1), 5);
    for (i = 0; i < run; i++)
        sdu_put_zz16(bw, src[i]);
}

static int sdu_dec_ticks(int16_t *p, int16_t vel, int16_t *dst, size_t out, size_t run) {
    size_t k;
    for (k = 0; k < run; k++) {
        *p = sdu_tick(*p, vel);
        dst[out + k] = *p;
    }
    return 0;
}

int schulduhr_compress_i16(
    const int16_t *src,
    size_t n,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
) {
    sdu_bw bw;
    size_t off = 0;
    int16_t p = 0;
    int16_t vel = 0;
    if (!out_n || !dst)
        return -1;
    if (n && !src)
        return -1;
    if (cap < 4)
        return -1;

    schulduhr_stats_reset();
    sdu_put_u32(dst, (uint32_t)n);
    memset(&bw, 0, sizeof bw);
    bw.p = dst;
    bw.i = 4;
    bw.cap = cap;

    if (n == 0) {
        *out_n = 4;
        return 0;
    }

    p = src[0];
    vel = (n > 1) ? (int16_t)(src[1] - src[0]) : 0;
    sdu_emit_sync(&bw, p, vel);
    off = 1;

    while (off < n) {
        size_t remain = n - off;
        size_t max_run = remain > SDU_WIN ? SDU_WIN : remain;
        int16_t dp = 0;
        size_t trun = sdu_max_tilg_run(src, off, n, p, vel, max_run, &dp);
        size_t raw_cost;
        size_t tilg_cost;

        if (trun == 0) {
            size_t rr = remain > 4 ? 4 : remain;
            sdu_emit_raw(&bw, src + off, rr);
            off += rr;
            if (off < n) {
                p = src[off - 1];
                vel = (int16_t)(src[off] - src[off - 1]);
                sdu_emit_vel(&bw, vel);
            }
            if (bw.err)
                return -1;
            continue;
        }

        tilg_cost = 2 + 5 + 16;
        raw_cost = 2 + 5 + trun * 16;
        if (raw_cost < tilg_cost && trun <= 2) {
            sdu_emit_raw(&bw, src + off, trun);
            off += trun;
            if (off < n) {
                p = src[off - 1];
                vel = (int16_t)(src[off] - src[off - 1]);
                sdu_emit_vel(&bw, vel);
            }
        } else {
            size_t k;
            sdu_emit_tilg(&bw, trun, dp);
            for (k = 0; k < trun; k++)
                p = sdu_tick(p, vel);
            p = sdu_clamp_i16((int32_t)p + (int32_t)dp);
            off += trun;
            if (off < n) {
                int16_t need_vel = (int16_t)(src[off] - src[off - 1]);
                if (need_vel != vel) {
                    vel = need_vel;
                    sdu_emit_vel(&bw, vel);
                }
            }
        }
        if (bw.err)
            return -1;
    }

    sdu_flush(&bw);
    if (bw.err)
        return -1;
    *out_n = bw.i;
    return 0;
}

int schulduhr_decompress_i16(
    const uint8_t *src,
    size_t n,
    int16_t *dst,
    size_t cap,
    size_t *out_n
) {
    sdu_br br;
    size_t nsamp;
    size_t out = 0;
    int16_t p = 0;
    int16_t vel = 0;
    int have_sync = 0;
    if (!out_n || !src || n < 4)
        return -1;
    nsamp = (size_t)sdu_get_u32(src);
    if (nsamp > cap || (nsamp > 0 && !dst))
        return -1;
    memset(&br, 0, sizeof br);
    br.p = src;
    br.i = 4;
    br.n = n;

    if (nsamp == 0) {
        *out_n = 0;
        return 0;
    }

    while (out < nsamp) {
        uint32_t tag32, run32;
        int tag;
        size_t run;
        if (sdu_get_bits(&br, 2, &tag32) != 0)
            return -1;
        tag = (int)tag32;

        if (tag == SDU_TAG_SYNC) {
            if (sdu_get_zz16(&br, &p) != 0 || sdu_get_zz16(&br, &vel) != 0)
                return -1;
            have_sync = 1;
            if (out < nsamp)
                dst[out++] = p;
            continue;
        }
        if (tag == SDU_TAG_VEL) {
            if (!have_sync || sdu_get_zz16(&br, &vel) != 0)
                return -1;
            continue;
        }
        if (!have_sync)
            return -1;

        if (sdu_get_bits(&br, 5, &run32) != 0)
            return -1;
        run = (size_t)run32 + 1;
        if (out + run > nsamp)
            return -1;

        if (tag == SDU_TAG_TILG) {
            int16_t dp;
            if (sdu_get_zz16(&br, &dp) != 0)
                return -1;
            if (sdu_dec_ticks(&p, vel, dst, out, run) != 0)
                return -1;
            out += run;
            p = sdu_clamp_i16((int32_t)p + (int32_t)dp);
        } else if (tag == SDU_TAG_RAW) {
            size_t k;
            for (k = 0; k < run; k++) {
                if (sdu_get_zz16(&br, &p) != 0)
                    return -1;
                dst[out + k] = p;
            }
            out += run;
        } else {
            return -1;
        }
    }

    *out_n = nsamp;
    return 0;
}

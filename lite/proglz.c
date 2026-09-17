#include "proglz.h"

#include <string.h>

#define PROGLZ_WIN     32
#define PROGLZ_CACHE   16
#define PROGLZ_TAG_HOLD   0
#define PROGLZ_TAG_RAMP   1
#define PROGLZ_TAG_RECALL 2
#define PROGLZ_TAG_RAW    3

static size_t proglz_tag_counts[4];

void proglz_stats_reset(void) {
    memset(proglz_tag_counts, 0, sizeof proglz_tag_counts);
}

void proglz_stats_get(size_t counts[4]) {
    size_t i;
    for (i = 0; i < 4; i++)
        counts[i] = proglz_tag_counts[i];
}

typedef struct {
    uint8_t kind; /* HOLD or RAMP */
    int16_t a;
    int16_t b;
} proglz_prog;

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
    uint32_t acc;
    int bits;
    int err;
} proglz_bw;

typedef struct {
    const uint8_t *p;
    size_t i;
    size_t n;
    uint32_t acc;
    int bits;
} proglz_br;

static void proglz_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint32_t proglz_get_u32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint16_t proglz_zz(int16_t n) {
    return (uint16_t)(((uint16_t)n << 1) ^ (uint16_t)(n >> 15));
}

static int16_t proglz_unzz(uint16_t u) {
    return (int16_t)((u >> 1) ^ (uint16_t)-(int16_t)(u & 1u));
}

static void proglz_put_bits(proglz_bw *b, uint32_t v, int n) {
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

static void proglz_flush(proglz_bw *b) {
    if (b->bits > 0)
        proglz_put_bits(b, 0, 8 - b->bits);
}

static int proglz_get_bits(proglz_br *r, int n, uint32_t *out) {
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

static void proglz_put_zz16(proglz_bw *b, int16_t x) {
    proglz_put_bits(b, proglz_zz(x), 16);
}

static int proglz_get_zz16(proglz_br *r, int16_t *out) {
    uint32_t z;
    if (proglz_get_bits(r, 16, &z) != 0)
        return -1;
    *out = proglz_unzz((uint16_t)z);
    return 0;
}

static int proglz_nbits_res(const int16_t *pred, const int16_t *src, size_t n) {
    int nb = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        uint16_t z = proglz_zz((int16_t)(src[i] - pred[i]));
        int b = 0;
        uint16_t v = z;
        while (v) {
            b++;
            v >>= 1;
        }
        if (b > nb)
            nb = b;
    }
    if (nb > 16)
        nb = 16;
    return nb;
}

static void proglz_gen_hold(int16_t *pred, size_t n, int16_t v) {
    size_t i;
    for (i = 0; i < n; i++)
        pred[i] = v;
}

static void proglz_gen_ramp(int16_t *pred, size_t n, int16_t v0, int16_t step) {
    size_t i;
    int32_t x = v0;
    for (i = 0; i < n; i++) {
        pred[i] = (int16_t)x;
        x += step;
    }
}

static void proglz_fit_hold(const int16_t *src, size_t n, int16_t *v_out) {
    int16_t a = src[0];
    int16_t b = src[n / 2];
    int16_t c = src[n - 1];
    if (a > b) {
        int16_t t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        int16_t t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        int16_t t = a;
        a = b;
        b = t;
    }
    *v_out = b;
}

static void proglz_fit_ramp(const int16_t *src, size_t n, int16_t *v0, int16_t *step) {
    int32_t s0 = src[0];
    int32_t s1 = src[n - 1];
    int32_t st;
    if (n <= 1) {
        *v0 = src[0];
        *step = 0;
        return;
    }
    st = (s1 - s0) / (int32_t)(n - 1);
    if (st > 32767)
        st = 32767;
    if (st < -32768)
        st = -32768;
    *v0 = (int16_t)s0;
    *step = (int16_t)st;
}

static void proglz_emit_resid(
    proglz_bw *bw,
    const int16_t *pred,
    const int16_t *src,
    size_t n,
    int nb
) {
    size_t i;
    proglz_put_bits(bw, (uint32_t)nb, 5);
    if (nb == 0)
        return;
    for (i = 0; i < n; i++)
        proglz_put_bits(bw, proglz_zz((int16_t)(src[i] - pred[i])), nb);
}

static int proglz_read_resid(proglz_br *br, int16_t *pred, size_t n) {
    uint32_t nb32;
    int nb;
    size_t i;
    if (proglz_get_bits(br, 5, &nb32) != 0)
        return -1;
    nb = (int)nb32;
    if (nb > 16)
        return -1;
    for (i = 0; i < n; i++) {
        uint32_t z = 0;
        if (nb > 0 && proglz_get_bits(br, nb, &z) != 0)
            return -1;
        if (nb)
            pred[i] = (int16_t)(pred[i] + proglz_unzz((uint16_t)z));
    }
    return 0;
}

static void proglz_cache_push(proglz_prog *cache, int *cache_n, proglz_prog chosen) {
    int j;
    for (j = PROGLZ_CACHE - 1; j > 0; j--)
        cache[j] = cache[j - 1];
    cache[0] = chosen;
    if (*cache_n < PROGLZ_CACHE)
        (*cache_n)++;
}

int proglz_compress_i16(
    const int16_t *src,
    size_t n,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
) {
    proglz_bw bw;
    proglz_prog cache[PROGLZ_CACHE];
    int cache_n = 0;
    size_t off = 0;
    if (!out_n || !dst)
        return -1;
    if (n && !src)
        return -1;
    if (cap < 4)
        return -1;
    memset(cache, 0, sizeof cache);
    proglz_put_u32(dst, (uint32_t)n);
    memset(&bw, 0, sizeof bw);
    bw.p = dst;
    bw.i = 4;
    bw.cap = cap;

    while (off < n) {
        size_t w = n - off;
        int16_t pred[PROGLZ_WIN];
        int16_t v_hold = 0, v0 = 0, step = 0;
        int nb_hold, nb_ramp;
        size_t cost_hold, cost_ramp, cost_raw, best_cost;
        int best_tag;
        int rec_k = -1;
        int nb_best = 0;
        int k;
        proglz_prog chosen;
        if (w > PROGLZ_WIN)
            w = PROGLZ_WIN;

        proglz_fit_hold(src + off, w, &v_hold);
        proglz_gen_hold(pred, w, v_hold);
        nb_hold = proglz_nbits_res(pred, src + off, w);
        /* tag2 + len5 + zz16 + nbits5 + resid */
        cost_hold = 2 + 5 + 16 + 5 + w * (size_t)nb_hold;

        proglz_fit_ramp(src + off, w, &v0, &step);
        proglz_gen_ramp(pred, w, v0, step);
        nb_ramp = proglz_nbits_res(pred, src + off, w);
        cost_ramp = 2 + 5 + 32 + 5 + w * (size_t)nb_ramp;

        cost_raw = 2 + 5 + w * 16;
        best_cost = cost_raw;
        best_tag = PROGLZ_TAG_RAW;
        nb_best = 16;

        if (cost_hold < best_cost) {
            best_cost = cost_hold;
            best_tag = PROGLZ_TAG_HOLD;
            nb_best = nb_hold;
        }
        if (cost_ramp < best_cost) {
            best_cost = cost_ramp;
            best_tag = PROGLZ_TAG_RAMP;
            nb_best = nb_ramp;
        }

        /* Exact RECALL: nur Cache-Index + Residuum — kein erneutes Param-Payload.
         * Das ist der eigentliche Novelty-Test gegen NeaTS-ohne-Seasonality. */
        for (k = 0; k < cache_n; k++) {
            int nb;
            size_t cost;
            if (cache[k].kind == PROGLZ_TAG_HOLD)
                proglz_gen_hold(pred, w, cache[k].a);
            else
                proglz_gen_ramp(pred, w, cache[k].a, cache[k].b);
            nb = proglz_nbits_res(pred, src + off, w);
            cost = 2 + 5 + 4 + 5 + w * (size_t)nb; /* tag+len+k+nbits+resid */
            if (cost < best_cost) {
                best_cost = cost;
                best_tag = PROGLZ_TAG_RECALL;
                rec_k = k;
                nb_best = nb;
            }
        }

        proglz_put_bits(&bw, (uint32_t)best_tag, 2);
        proglz_put_bits(&bw, (uint32_t)(w - 1), 5);
        if (best_tag >= 0 && best_tag < 4)
            proglz_tag_counts[best_tag]++;

        if (best_tag == PROGLZ_TAG_RAW) {
            size_t i;
            for (i = 0; i < w; i++)
                proglz_put_zz16(&bw, src[off + i]);
            chosen.kind = PROGLZ_TAG_HOLD;
            chosen.a = src[off];
            chosen.b = 0;
        } else if (best_tag == PROGLZ_TAG_HOLD) {
            proglz_put_zz16(&bw, v_hold);
            proglz_gen_hold(pred, w, v_hold);
            proglz_emit_resid(&bw, pred, src + off, w, nb_best);
            chosen.kind = PROGLZ_TAG_HOLD;
            chosen.a = v_hold;
            chosen.b = 0;
        } else if (best_tag == PROGLZ_TAG_RAMP) {
            proglz_put_zz16(&bw, v0);
            proglz_put_zz16(&bw, step);
            proglz_gen_ramp(pred, w, v0, step);
            proglz_emit_resid(&bw, pred, src + off, w, nb_best);
            chosen.kind = PROGLZ_TAG_RAMP;
            chosen.a = v0;
            chosen.b = step;
        } else {
            proglz_put_bits(&bw, (uint32_t)rec_k, 4);
            if (cache[rec_k].kind == PROGLZ_TAG_HOLD)
                proglz_gen_hold(pred, w, cache[rec_k].a);
            else
                proglz_gen_ramp(pred, w, cache[rec_k].a, cache[rec_k].b);
            proglz_emit_resid(&bw, pred, src + off, w, nb_best);
            chosen = cache[rec_k];
        }

        proglz_cache_push(cache, &cache_n, chosen);
        if (bw.err)
            return -1;
        off += w;
    }

    proglz_flush(&bw);
    if (bw.err)
        return -1;
    *out_n = bw.i;
    return 0;
}

int proglz_decompress_i16(
    const uint8_t *src,
    size_t n,
    int16_t *dst,
    size_t cap,
    size_t *out_n
) {
    proglz_br br;
    proglz_prog cache[PROGLZ_CACHE];
    int cache_n = 0;
    size_t nsamp;
    size_t out = 0;
    if (!out_n || !src || n < 4)
        return -1;
    nsamp = (size_t)proglz_get_u32(src);
    if (nsamp > cap || (nsamp > 0 && !dst))
        return -1;
    memset(cache, 0, sizeof cache);
    memset(&br, 0, sizeof br);
    br.p = src;
    br.i = 4;
    br.n = n;

    while (out < nsamp) {
        uint32_t tag32, w32;
        int tag;
        size_t w;
        int16_t pred[PROGLZ_WIN];
        proglz_prog chosen;
        if (proglz_get_bits(&br, 2, &tag32) != 0)
            return -1;
        if (proglz_get_bits(&br, 5, &w32) != 0)
            return -1;
        tag = (int)tag32;
        w = (size_t)w32 + 1;
        if (w > PROGLZ_WIN || out + w > nsamp)
            return -1;

        if (tag == PROGLZ_TAG_RAW) {
            size_t i;
            for (i = 0; i < w; i++) {
                int16_t v;
                if (proglz_get_zz16(&br, &v) != 0)
                    return -1;
                dst[out + i] = v;
            }
            chosen.kind = PROGLZ_TAG_HOLD;
            chosen.a = dst[out];
            chosen.b = 0;
        } else if (tag == PROGLZ_TAG_HOLD) {
            int16_t v;
            if (proglz_get_zz16(&br, &v) != 0)
                return -1;
            proglz_gen_hold(pred, w, v);
            if (proglz_read_resid(&br, pred, w) != 0)
                return -1;
            memcpy(dst + out, pred, w * sizeof(int16_t));
            chosen.kind = PROGLZ_TAG_HOLD;
            chosen.a = v;
            chosen.b = 0;
        } else if (tag == PROGLZ_TAG_RAMP) {
            int16_t a, b;
            if (proglz_get_zz16(&br, &a) != 0 || proglz_get_zz16(&br, &b) != 0)
                return -1;
            proglz_gen_ramp(pred, w, a, b);
            if (proglz_read_resid(&br, pred, w) != 0)
                return -1;
            memcpy(dst + out, pred, w * sizeof(int16_t));
            chosen.kind = PROGLZ_TAG_RAMP;
            chosen.a = a;
            chosen.b = b;
        } else if (tag == PROGLZ_TAG_RECALL) {
            uint32_t k32;
            int k;
            if (proglz_get_bits(&br, 4, &k32) != 0)
                return -1;
            k = (int)k32;
            if (k < 0 || k >= cache_n)
                return -1;
            if (cache[k].kind == PROGLZ_TAG_HOLD)
                proglz_gen_hold(pred, w, cache[k].a);
            else
                proglz_gen_ramp(pred, w, cache[k].a, cache[k].b);
            if (proglz_read_resid(&br, pred, w) != 0)
                return -1;
            memcpy(dst + out, pred, w * sizeof(int16_t));
            chosen = cache[k];
        } else {
            return -1;
        }

        proglz_cache_push(cache, &cache_n, chosen);
        out += w;
    }

    *out_n = nsamp;
    return 0;
}

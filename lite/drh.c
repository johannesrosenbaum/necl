#include "drh.h"

#include <stdlib.h>
#include <string.h>

static uint16_t drh_zz(int16_t n) {
    return (uint16_t)(((uint16_t)n << 1) ^ (uint16_t)(n >> 15));
}

static int16_t drh_unzz(uint16_t u) {
    return (int16_t)((u >> 1) ^ (uint16_t)-(int16_t)(u & 1u));
}

static void drh_freqs(uint32_t freq[256]) {
    int i;
    for (i = 0; i < 256; i++)
        freq[i] = (uint32_t)(1024 / (i + 1) + 1);
}

typedef struct {
    int16_t left;
    int16_t right;
    uint32_t f;
} drh_node;

static uint32_t drh_code[256];
static uint8_t drh_len[256];
static int16_t drh_left[512];
static int16_t drh_right[512];
static int drh_root;
static uint8_t drh_ready;

static void drh_assign(int node, uint32_t code, int len) {
    if (node < 256) {
        drh_code[node] = code;
        drh_len[node] = (uint8_t)(len > 255 ? 255 : len);
        return;
    }
    drh_assign(drh_left[node], code, len + 1);
    drh_assign(drh_right[node], code | (1u << len), len + 1);
}

static void drh_build(void) {
    uint32_t freq[256];
    drh_node nd[512];
    int parent[512];
    int i, nnodes, a, b;
    if (drh_ready)
        return;
    drh_freqs(freq);
    for (i = 0; i < 256; i++) {
        nd[i].left = nd[i].right = -1;
        nd[i].f = freq[i];
        parent[i] = -1;
        drh_left[i] = drh_right[i] = -1;
    }
    nnodes = 256;
    for (;;) {
        a = -1;
        b = -1;
        for (i = 0; i < nnodes; i++) {
            if (parent[i] != -1)
                continue;
            if (a < 0 || nd[i].f < nd[a].f)
                a = i;
        }
        for (i = 0; i < nnodes; i++) {
            if (parent[i] != -1 || i == a)
                continue;
            if (b < 0 || nd[i].f < nd[b].f)
                b = i;
        }
        if (b < 0)
            break;
        nd[nnodes].left = (int16_t)a;
        nd[nnodes].right = (int16_t)b;
        nd[nnodes].f = nd[a].f + nd[b].f;
        drh_left[nnodes] = (int16_t)a;
        drh_right[nnodes] = (int16_t)b;
        parent[a] = parent[b] = nnodes;
        parent[nnodes] = -1;
        nnodes++;
    }
    drh_root = nnodes - 1;
    drh_assign(drh_root, 0, 0);
    drh_ready = 1;
}

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
    uint32_t acc;
    int bits;
    int err;
} drh_bw;

static void drh_put_bit(drh_bw *b, int bit) {
    if (b->err)
        return;
    b->acc |= ((uint32_t)(bit & 1)) << b->bits;
    b->bits++;
    if (b->bits == 8) {
        if (b->i >= b->cap) {
            b->err = 1;
            return;
        }
        b->p[b->i++] = (uint8_t)b->acc;
        b->acc = 0;
        b->bits = 0;
    }
}

static void drh_put_code(drh_bw *b, uint32_t code, int n) {
    int k;
    for (k = 0; k < n; k++)
        drh_put_bit(b, (int)((code >> k) & 1u));
}

static void drh_flush(drh_bw *b) {
    if (b->bits > 0 && !b->err) {
        if (b->i >= b->cap) {
            b->err = 1;
            return;
        }
        b->p[b->i++] = (uint8_t)b->acc;
        b->acc = 0;
        b->bits = 0;
    }
}

typedef struct {
    const uint8_t *p;
    size_t i;
    size_t n;
    uint32_t acc;
    int bits;
} drh_br;

static int drh_get_bit(drh_br *r) {
    if (r->bits == 0) {
        if (r->i >= r->n)
            return -1;
        r->acc = r->p[r->i++];
        r->bits = 8;
    }
    {
        int b = (int)(r->acc & 1u);
        r->acc >>= 1;
        r->bits--;
        return b;
    }
}

static int drh_decode_sym(drh_br *r) {
    int node = drh_root;
    if (drh_root < 256)
        return drh_root;
    for (;;) {
        int b = drh_get_bit(r);
        if (b < 0)
            return -1;
        node = (b == 0) ? drh_left[node] : drh_right[node];
        if (node < 0)
            return -1;
        if (node < 256)
            return node;
    }
}

static int drh_packbits(const uint8_t *src, size_t n, uint8_t *dst, size_t cap, size_t *out) {
    size_t i = 0;
    size_t o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && src[i + run] == src[i] && run < 128)
            run++;
        if (run >= 3) {
            if (o + 2 > cap)
                return -1;
            dst[o++] = (uint8_t)(257u - run);
            dst[o++] = src[i];
            i += run;
        } else {
            size_t lit = 0;
            size_t start = i;
            while (i < n && lit < 128) {
                run = 1;
                while (i + run < n && src[i + run] == src[i] && run < 128)
                    run++;
                if (run >= 3)
                    break;
                i++;
                lit++;
            }
            if (lit == 0)
                continue;
            if (o + 1 + lit > cap)
                return -1;
            dst[o++] = (uint8_t)(lit - 1);
            memcpy(dst + o, src + start, lit);
            o += lit;
        }
    }
    *out = o;
    return 0;
}

static int drh_unpackbits(const uint8_t *src, size_t n, uint8_t *dst, size_t cap, size_t *out) {
    size_t i = 0;
    size_t o = 0;
    while (i < n) {
        uint8_t ctrl = src[i++];
        if (ctrl <= 127) {
            size_t lit = (size_t)ctrl + 1;
            if (i + lit > n || o + lit > cap)
                return -1;
            memcpy(dst + o, src + i, lit);
            o += lit;
            i += lit;
        } else if (ctrl == 128) {
            continue;
        } else {
            size_t run = 257u - (size_t)ctrl;
            if (i >= n || o + run > cap)
                return -1;
            memset(dst + o, src[i], run);
            o += run;
            i++;
        }
    }
    *out = o;
    return 0;
}

static void drh_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint32_t drh_get_u32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

int drh_compress_i16(
    const int16_t *src,
    size_t n,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
) {
    uint8_t *zz;
    uint8_t *rle;
    size_t i;
    size_t rle_n = 0;
    drh_bw bw;
    int16_t prev = 0;
    size_t rle_cap;
    if (!out_n || !dst)
        return -1;
    if (n && !src)
        return -1;
    drh_build();
    zz = (uint8_t *)malloc(n * 2u + 8u);
    rle_cap = n * 2u + n / 8u + 16u;
    rle = (uint8_t *)malloc(rle_cap);
    if (!zz || !rle) {
        free(zz);
        free(rle);
        return -1;
    }
    for (i = 0; i < n; i++) {
        int16_t d = (int16_t)(src[i] - prev);
        uint16_t z = drh_zz(d);
        zz[i * 2u] = (uint8_t)(z & 0xffu);
        zz[i * 2u + 1] = (uint8_t)(z >> 8);
        prev = src[i];
    }
    if (drh_packbits(zz, n * 2u, rle, rle_cap, &rle_n) != 0) {
        free(zz);
        free(rle);
        return -1;
    }
    if (cap < 8) {
        free(zz);
        free(rle);
        return -1;
    }
    drh_put_u32(dst, (uint32_t)n);
    drh_put_u32(dst + 4, (uint32_t)rle_n);
    memset(&bw, 0, sizeof bw);
    bw.p = dst;
    bw.i = 8;
    bw.cap = cap;
    for (i = 0; i < rle_n; i++)
        drh_put_code(&bw, drh_code[rle[i]], (int)drh_len[rle[i]]);
    drh_flush(&bw);
    free(zz);
    free(rle);
    if (bw.err)
        return -1;
    *out_n = bw.i;
    return 0;
}

int drh_decompress_i16(
    const uint8_t *src,
    size_t n,
    int16_t *dst,
    size_t cap,
    size_t *out_n
) {
    size_t orig;
    size_t rle_n;
    size_t got = 0;
    uint8_t *rle;
    uint8_t *zz;
    size_t zz_n = 0;
    drh_br br;
    int16_t prev = 0;
    size_t i;
    if (!out_n || !src || n < 8)
        return -1;
    drh_build();
    orig = (size_t)drh_get_u32(src);
    rle_n = (size_t)drh_get_u32(src + 4);
    if (orig > cap || (orig > 0 && !dst))
        return -1;
    rle = (uint8_t *)malloc(rle_n ? rle_n : 1);
    zz = (uint8_t *)malloc(orig * 2u + 8u);
    if (!rle || !zz) {
        free(rle);
        free(zz);
        return -1;
    }
    memset(&br, 0, sizeof br);
    br.p = src;
    br.i = 8;
    br.n = n;
    while (got < rle_n) {
        int s = drh_decode_sym(&br);
        if (s < 0) {
            free(rle);
            free(zz);
            return -1;
        }
        rle[got++] = (uint8_t)s;
    }
    if (drh_unpackbits(rle, rle_n, zz, orig * 2u, &zz_n) != 0 || zz_n != orig * 2u) {
        free(rle);
        free(zz);
        return -1;
    }
    for (i = 0; i < orig; i++) {
        uint16_t z = (uint16_t)zz[i * 2u] | ((uint16_t)zz[i * 2u + 1] << 8);
        int16_t x = (int16_t)(prev + drh_unzz(z));
        dst[i] = x;
        prev = x;
    }
    free(rle);
    free(zz);
    *out_n = orig;
    return 0;
}

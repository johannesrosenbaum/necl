#include "sprintz_delta.h"

#include <string.h>

static uint16_t sp_zz(int16_t n) {
    return (uint16_t)(((uint16_t)n << 1) ^ (uint16_t)(n >> 15));
}

static int16_t sp_unzz(uint16_t u) {
    return (int16_t)((u >> 1) ^ (uint16_t)-(int16_t)(u & 1u));
}

static int sp_nbits(const uint16_t err[8]) {
    int nb = 0;
    int i;
    for (i = 0; i < 8; i++) {
        uint16_t v = err[i];
        int b = 0;
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

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
    uint32_t acc;
    int bits;
    int err;
} sp_bw;

static void sp_put_bits(sp_bw *b, uint32_t v, int n) {
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

static void sp_flush(sp_bw *b) {
    if (b->bits > 0)
        sp_put_bits(b, 0, 8 - b->bits);
}

typedef struct {
    const uint8_t *p;
    size_t i;
    size_t n;
    uint32_t acc;
    int bits;
} sp_br;

static int sp_get_bits(sp_br *r, int n, uint32_t *out) {
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

static void sp_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static uint32_t sp_get_u32(const uint8_t *p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

int sprintz_delta_compress_i16(
    const int16_t *src,
    size_t n,
    int ndims,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
) {
    size_t nsteps;
    int d;
    sp_bw bw;
    if (!out_n || !dst)
        return -1;
    if (ndims < 1)
        return -1;
    if (n % (size_t)ndims != 0)
        return -1;
    if (n && !src)
        return -1;
    if (cap < 6)
        return -1;
    nsteps = n / (size_t)ndims;
    sp_put_u32(dst, (uint32_t)n);
    dst[4] = (uint8_t)ndims;
    memset(&bw, 0, sizeof bw);
    bw.p = dst;
    bw.i = 5;
    bw.cap = cap;
    for (d = 0; d < ndims; d++) {
        int16_t prev = 0;
        size_t t = 0;
        while (t < nsteps) {
            uint16_t err[8];
            size_t n_in;
            int nb;
            int i;
            size_t run = 0;
            n_in = nsteps - t;
            if (n_in > 8)
                n_in = 8;
            for (i = 0; i < 8; i++) {
                if ((size_t)i < n_in) {
                    int16_t x = src[(t + (size_t)i) * (size_t)ndims + (size_t)d];
                    int16_t delta = (int16_t)(x - prev);
                    err[i] = sp_zz(delta);
                    prev = x;
                } else {
                    err[i] = 0;
                }
            }
            nb = sp_nbits(err);
            if (nb == 0) {
                int16_t prev_save = prev;
                size_t t2 = t + n_in;
                run = 0;
                while (run < 255 && t2 < nsteps) {
                    uint16_t err2[8];
                    size_t n_in2 = nsteps - t2;
                    int16_t p2 = prev_save;
                    int j;
                    int zok = 1;
                    if (n_in2 > 8)
                        n_in2 = 8;
                    for (j = 0; j < 8; j++) {
                        if ((size_t)j < n_in2) {
                            int16_t x = src[(t2 + (size_t)j) * (size_t)ndims + (size_t)d];
                            int16_t delta = (int16_t)(x - p2);
                            err2[j] = sp_zz(delta);
                            p2 = x;
                            if (err2[j] != 0)
                                zok = 0;
                        } else {
                            err2[j] = 0;
                        }
                    }
                    if (!zok)
                        break;
                    prev_save = p2;
                    t2 += n_in2;
                    run++;
                }
                sp_put_bits(&bw, 0, 5);
                sp_put_bits(&bw, (uint32_t)run, 8);
                prev = prev_save;
                t = t2;
            } else {
                sp_put_bits(&bw, (uint32_t)nb, 5);
                for (i = 0; i < 8; i++)
                    sp_put_bits(&bw, err[i], nb);
                t += n_in;
            }
            if (bw.err)
                return -1;
        }
    }
    sp_flush(&bw);
    if (bw.err)
        return -1;
    *out_n = bw.i;
    return 0;
}

int sprintz_delta_decompress_i16(
    const uint8_t *src,
    size_t n,
    int ndims,
    int16_t *dst,
    size_t cap,
    size_t *out_n
) {
    size_t nsamp;
    size_t nsteps;
    int hdr_dims;
    int d;
    sp_br br;
    if (!out_n || !src || n < 5)
        return -1;
    if (ndims < 1)
        return -1;
    nsamp = (size_t)sp_get_u32(src);
    hdr_dims = (int)src[4];
    if (hdr_dims != ndims)
        return -1;
    if (nsamp % (size_t)ndims != 0)
        return -1;
    if (nsamp > cap || (nsamp > 0 && !dst))
        return -1;
    nsteps = nsamp / (size_t)ndims;
    memset(&br, 0, sizeof br);
    br.p = src;
    br.i = 5;
    br.n = n;
    for (d = 0; d < ndims; d++) {
        int16_t prev = 0;
        size_t t = 0;
        while (t < nsteps) {
            uint32_t nb32;
            int nb;
            size_t n_in = nsteps - t;
            if (n_in > 8)
                n_in = 8;
            if (sp_get_bits(&br, 5, &nb32) != 0)
                return -1;
            nb = (int)nb32;
            if (nb == 0) {
                uint32_t run32;
                size_t extra;
                size_t k;
                if (sp_get_bits(&br, 8, &run32) != 0)
                    return -1;
                extra = (size_t)run32;
                for (k = 0; k <= extra; k++) {
                    size_t remain = nsteps - t;
                    size_t take = remain > 8 ? 8 : remain;
                    size_t i;
                    if (take == 0)
                        return -1;
                    for (i = 0; i < take; i++) {
                        dst[(t + i) * (size_t)ndims + (size_t)d] = prev;
                    }
                    t += take;
                }
            } else if (nb > 16) {
                return -1;
            } else {
                int i;
                for (i = 0; i < 8; i++) {
                    uint32_t v;
                    if (sp_get_bits(&br, nb, &v) != 0)
                        return -1;
                    if ((size_t)i < n_in) {
                        int16_t x = (int16_t)(prev + sp_unzz((uint16_t)v));
                        dst[(t + (size_t)i) * (size_t)ndims + (size_t)d] = x;
                        prev = x;
                    }
                }
                t += n_in;
            }
        }
    }
    *out_n = nsamp;
    return 0;
}

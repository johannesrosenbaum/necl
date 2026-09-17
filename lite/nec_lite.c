#include "nec_lite.h"
#include "nec_frontend.h"

#include <string.h>
#ifndef NEC_LITE_NO_MALLOC
#include <stdlib.h>
#endif

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
    if (frontend == NEC_LITE_FE_I16 || frontend == NEC_LITE_FE_I16_DELTA)
        return (uint16_t)NEC_LITE_FLAG_DELTA_I16;
    if (frontend == NEC_LITE_FE_I16_FIRE)
        return (uint16_t)NEC_LITE_FLAG_FIRE_I16;
    if (frontend == NEC_LITE_FE_TICK8)
        return (uint16_t)NEC_LITE_FLAG_DELTA_TICK8;
    return 0;
}

static int nec_fe_from_flags(uint16_t flags) {
    if (flags & NEC_LITE_FLAG_FIRE_I16)
        return NEC_LITE_FE_I16_FIRE;
    if (flags & NEC_LITE_FLAG_DELTA_I16)
        return NEC_LITE_FE_I16_DELTA;
    if (flags & NEC_LITE_FLAG_DELTA_TICK8)
        return NEC_LITE_FE_TICK8;
    return NEC_LITE_FE_NONE;
}

static int nec_fe_to_delta(int frontend) {
    if (frontend == NEC_LITE_FE_I16 || frontend == NEC_LITE_FE_I16_DELTA)
        return NEC_DELTA_I16;
    if (frontend == NEC_LITE_FE_I16_FIRE)
        return NEC_DELTA_I16_FIRE;
    if (frontend == NEC_LITE_FE_TICK8)
        return NEC_DELTA_TICK8;
    return NEC_DELTA_NONE;
}

static void nec_delta_from_enc(nec_lite_enc_t *e, nec_delta_st *s) {
    s->prev_i16 = e->prev_i16;
    s->prev2_i16 = e->prev2_i16;
    s->fire_l = e->fire_l;
    s->prev_ts = e->prev_ts;
    s->prev_px = e->prev_px;
}

static void nec_delta_to_enc(nec_lite_enc_t *e, const nec_delta_st *s) {
    e->prev_i16 = s->prev_i16;
    e->prev2_i16 = s->prev2_i16;
    e->fire_l = s->fire_l;
    e->prev_ts = s->prev_ts;
    e->prev_px = s->prev_px;
}

static void nec_delta_from_dec(nec_lite_dec_t *d, nec_delta_st *s) {
    s->prev_i16 = d->prev_i16;
    s->prev2_i16 = d->prev2_i16;
    s->fire_l = d->fire_l;
    s->prev_ts = d->prev_ts;
    s->prev_px = d->prev_px;
}

static void nec_delta_to_dec(nec_lite_dec_t *d, const nec_delta_st *s) {
    d->prev_i16 = s->prev_i16;
    d->prev2_i16 = s->prev2_i16;
    d->fire_l = s->fire_l;
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

#ifndef NEC_LITE_NO_MALLOC
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
#endif /* !NEC_LITE_NO_MALLOC */

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

#ifndef NEC_LITE_NO_MALLOC
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
#endif

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

#ifndef NEC_LITE_NO_MALLOC
static void nec_rc_enc_byte(nec_rc_enc *e, nec_nib_model_t *m, uint8_t b) {
    int h = (int)(b >> 4);
    int l = (int)(b & 15u);
    nec_rc_enc_nibble(e, m->hi, h);
    nec_rc_enc_nibble(e, m->lo[h], l);
}
#endif

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
    if ((h.flags & NEC_LITE_FLAG_FIRE_I16) && (h.flags & NEC_LITE_FLAG_DELTA_TICK8))
        return NEC_ERR_SRC;
    if ((h.flags & NEC_LITE_FLAG_FIRE_I16) && (h.flags & NEC_LITE_FLAG_DELTA_I16))
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

/* --- int16 ZigZag-Residual Bitpack + Zero-RLE (Sprintz-Stil, 8er-Blöcke) --- */

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
    uint32_t acc;
    int bits;
    int err;
} nec_bw;

static void nec_bw_put(nec_bw *b, uint32_t v, int n) {
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

static void nec_bw_flush(nec_bw *b) {
    if (b->bits > 0)
        nec_bw_put(b, 0, 8 - b->bits);
}

typedef struct {
    const uint8_t *p;
    size_t i;
    size_t n;
    uint32_t acc;
    int bits;
} nec_br;

static int nec_br_get(nec_br *r, int n, uint32_t *out) {
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

static int nec_bp_nbits(const uint16_t err[8]) {
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

static uint16_t nec_load_zz(const uint8_t *delta, size_t samp) {
    size_t o = samp * 2u;
    return (uint16_t)delta[o] | ((uint16_t)delta[o + 1] << 8);
}

static void nec_store_zz(uint8_t *delta, size_t samp, uint16_t v) {
    size_t o = samp * 2u;
    delta[o] = (uint8_t)(v & 0xffu);
    delta[o + 1] = (uint8_t)((v >> 8) & 0xffu);
}

/* Packt ZigZag-u16 Residuals. 0 = Fehler/zu groß. */
static size_t nec_bitpack_enc(const uint8_t *delta, size_t clen, uint8_t *dst, size_t cap) {
    size_t nsamp;
    size_t t;
    nec_bw bw;
    if (clen < 2 || (clen & 1u) != 0)
        return 0;
    nsamp = clen / 2u;
    memset(&bw, 0, sizeof bw);
    bw.p = dst;
    bw.cap = cap;
    t = 0;
    while (t < nsamp) {
        uint16_t err[8];
        size_t n_in = nsamp - t;
        int i;
        int nb;
        if (n_in > 8)
            n_in = 8;
        for (i = 0; i < 8; i++) {
            if ((size_t)i < n_in)
                err[i] = nec_load_zz(delta, t + (size_t)i);
            else
                err[i] = 0;
        }
        nb = nec_bp_nbits(err);
        if (nb == 0) {
            size_t run = 0;
            size_t t2 = t + n_in;
            while (run < 255 && t2 < nsamp) {
                size_t n_in2 = nsamp - t2;
                int zok = 1;
                int j;
                if (n_in2 > 8)
                    n_in2 = 8;
                for (j = 0; j < 8; j++) {
                    uint16_t e = 0;
                    if ((size_t)j < n_in2)
                        e = nec_load_zz(delta, t2 + (size_t)j);
                    if (e != 0)
                        zok = 0;
                }
                if (!zok)
                    break;
                t2 += n_in2;
                run++;
            }
            nec_bw_put(&bw, 0, 5);
            nec_bw_put(&bw, (uint32_t)run, 8);
            t = t2;
        } else {
            nec_bw_put(&bw, (uint32_t)nb, 5);
            for (i = 0; i < 8; i++)
                nec_bw_put(&bw, err[i], nb);
            t += n_in;
        }
        if (bw.err)
            return 0;
    }
    nec_bw_flush(&bw);
    if (bw.err || bw.i == 0)
        return 0;
    return bw.i;
}

static int nec_bitpack_dec(const uint8_t *src, size_t src_n, uint8_t *delta, size_t clen) {
    size_t nsamp;
    size_t t;
    nec_br br;
    if (clen < 2 || (clen & 1u) != 0)
        return -1;
    nsamp = clen / 2u;
    memset(&br, 0, sizeof br);
    br.p = src;
    br.n = src_n;
    t = 0;
    while (t < nsamp) {
        uint32_t nb_u;
        int nb;
        size_t n_in = nsamp - t;
        int i;
        if (n_in > 8)
            n_in = 8;
        if (nec_br_get(&br, 5, &nb_u) != 0)
            return -1;
        nb = (int)nb_u;
        if (nb == 0) {
            uint32_t run_u;
            size_t run;
            size_t k;
            if (nec_br_get(&br, 8, &run_u) != 0)
                return -1;
            run = (size_t)run_u;
            /* dieser Block + run weitere Null-Blöcke */
            for (k = 0; k < n_in; k++)
                nec_store_zz(delta, t + k, 0);
            t += n_in;
            while (run > 0 && t < nsamp) {
                size_t n_in2 = nsamp - t;
                if (n_in2 > 8)
                    n_in2 = 8;
                for (k = 0; k < n_in2; k++)
                    nec_store_zz(delta, t + k, 0);
                t += n_in2;
                run--;
            }
        } else if (nb > 16) {
            return -1;
        } else {
            for (i = 0; i < 8; i++) {
                uint32_t e;
                if (nec_br_get(&br, nb, &e) != 0)
                    return -1;
                if ((size_t)i < n_in)
                    nec_store_zz(delta, t + (size_t)i, (uint16_t)e);
            }
            t += n_in;
        }
    }
    return 0;
}

static int nec_fe_is_i16(int frontend) {
    return frontend == NEC_LITE_FE_I16 || frontend == NEC_LITE_FE_I16_FIRE ||
           frontend == NEC_LITE_FE_I16_DELTA;
}

static int nec_enc_emit_chunk(nec_lite_enc_t *e, const uint8_t *raw, size_t clen) {
    uint8_t delta[NEC_LITE_CHUNK];
    uint8_t rec[4];
#ifndef NEC_LITE_NO_MALLOC
    uint8_t rc_buf[NEC_LITE_RC_TMP];
    nec_rc_enc enc;
    nec_lite_model_t saved;
    size_t rc_len = 0;
    int have_rc = 0;
#endif
    nec_delta_st st;
    size_t bi;
    uint8_t nsym;
    size_t bp_len = 0;
    int have_bp = 0;

    if (clen == 0 || clen > (size_t)NEC_LITE_CHUNK)
        return NEC_ERR_INTERNAL;
    nec_delta_from_enc(e, &st);
    if (nec_delta_fwd_chunk(&st, nec_fe_to_delta(e->frontend), raw, delta, clen) != 0) {
        e->err = NEC_ERR_ARG;
        return e->err;
    }
    nec_delta_to_enc(e, &st);

    nsym = (uint8_t)nec_nsym_enc(clen);
    {
        int all_z = 1;
        for (bi = 0; bi < clen; bi++) {
            if (delta[bi] != 0) {
                all_z = 0;
                break;
            }
        }
        if (all_z) {
            rec[0] = 0xFEu;
            rec[1] = nsym;
            if (nec_sink_all(e, rec, 2) != NEC_OK)
                return e->err;
            for (bi = 0; bi < clen; bi++)
                nec_nib_see(&e->model, 0);
            return NEC_OK;
        }
    }

    if (nec_fe_is_i16(e->frontend) && (clen & 1u) == 0) {
        bp_len = nec_bitpack_enc(delta, clen, e->rc_tmp, sizeof e->rc_tmp);
        if (bp_len > 0 && bp_len < clen && bp_len <= 255u)
            have_bp = 1;
    }

#ifdef NEC_LITE_NO_MALLOC
    /* MCU: Bitpack / Zero / STORE — kein RC (Flash). */
    if (have_bp) {
        for (bi = 0; bi < clen; bi++)
            nec_nib_see(&e->model, delta[bi]);
        if (clen == (size_t)NEC_LITE_CHUNK) {
            rec[0] = 0xFBu;
            rec[1] = (uint8_t)bp_len;
            if (nec_sink_all(e, rec, 2) != NEC_OK)
                return e->err;
        } else {
            rec[0] = 0xFDu;
            rec[1] = nsym;
            rec[2] = (uint8_t)bp_len;
            if (nec_sink_all(e, rec, 3) != NEC_OK)
                return e->err;
        }
        return nec_sink_all(e, e->rc_tmp, bp_len);
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
#else
    /* Host: Bitpack sofort wenn klar besser; sonst RC-Wettkampf (intel/UCR). */
    {
        size_t bp_oh = (clen == (size_t)NEC_LITE_CHUNK) ? 2u : 3u;
        int bp_strong = have_bp && (bp_len + bp_oh) * 5u <= clen * 3u; /* ≤60 % */
        if (have_bp && bp_strong) {
            for (bi = 0; bi < clen; bi++)
                nec_nib_see(&e->model, delta[bi]);
            if (clen == (size_t)NEC_LITE_CHUNK) {
                rec[0] = 0xFBu;
                rec[1] = (uint8_t)bp_len;
                if (nec_sink_all(e, rec, 2) != NEC_OK)
                    return e->err;
            } else {
                rec[0] = 0xFDu;
                rec[1] = nsym;
                rec[2] = (uint8_t)bp_len;
                if (nec_sink_all(e, rec, 3) != NEC_OK)
                    return e->err;
            }
            return nec_sink_all(e, e->rc_tmp, bp_len);
        }

        saved = e->model;
        memset(&enc, 0, sizeof enc);
        enc.range = 0xFFFFFFFFu;
        enc.cache_size = 1;
        enc.p = rc_buf;
        enc.cap = sizeof rc_buf;
        for (bi = 0; bi < clen; bi++)
            nec_rc_enc_byte(&enc, &e->model, delta[bi]);
        nec_rc_flush(&enc);
        if (!enc.err && enc.i > 0 && enc.i < clen && enc.i <= 252u) {
            have_rc = 1;
            rc_len = enc.i;
        } else {
            e->model = saved;
        }

        if (have_bp && (!have_rc || bp_len + bp_oh <= rc_len + 2u) &&
            bp_len + bp_oh < clen + 2u) {
            e->model = saved;
            for (bi = 0; bi < clen; bi++)
                nec_nib_see(&e->model, delta[bi]);
            if (clen == (size_t)NEC_LITE_CHUNK) {
                rec[0] = 0xFBu;
                rec[1] = (uint8_t)bp_len;
                if (nec_sink_all(e, rec, 2) != NEC_OK)
                    return e->err;
            } else {
                rec[0] = 0xFDu;
                rec[1] = nsym;
                rec[2] = (uint8_t)bp_len;
                if (nec_sink_all(e, rec, 3) != NEC_OK)
                    return e->err;
            }
            return nec_sink_all(e, e->rc_tmp, bp_len);
        }
        if (have_rc) {
            rec[0] = (uint8_t)rc_len;
            rec[1] = nsym;
            if (nec_sink_all(e, rec, 2) != NEC_OK)
                return e->err;
            return nec_sink_all(e, rc_buf, rc_len);
        }

        e->model = saved;
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
#endif
}

static void nec_enc_reset_state(nec_lite_enc_t *e, int frontend, nec_lite_sink_fn sink, void *ctx) {
    memset(e, 0, sizeof *e);
    nec_nib_init(&e->model);
    e->crc_state = 0xFFFFFFFFu;
    e->fire_l = 256;
    e->sink = sink;
    e->sink_ctx = ctx;
    e->frontend = frontend;
    e->err = NEC_OK;
}

static int nec_fe_ok(int frontend) {
    return frontend == NEC_LITE_FE_NONE || frontend == NEC_LITE_FE_I16 ||
           frontend == NEC_LITE_FE_TICK8 || frontend == NEC_LITE_FE_I16_FIRE ||
           frontend == NEC_LITE_FE_I16_DELTA;
}

static uint16_t nec_enc_flags(const nec_lite_enc_t *e) {
    return (uint16_t)(nec_fe_flag(e->frontend) | NEC_LITE_FLAG_RC | NEC_LITE_FLAG_STREAM);
}

int nec_lite_enc_init(nec_lite_enc_t *e, int frontend, nec_lite_sink_fn sink, void *ctx) {
    uint8_t hdr[NEC_HEADER_SIZE];
    if (!e || !sink)
        return NEC_ERR_ARG;
    if (!nec_fe_ok(frontend))
        return NEC_ERR_ARG;
    /* Stream: FE_I16 = Delta (Sensor-Default). FIRE nur explizit / Block-Choose. */
    if (frontend == NEC_LITE_FE_I16)
        frontend = NEC_LITE_FE_I16_DELTA;
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

#ifndef NEC_LITE_NO_MALLOC
/* Stack-Probe: geschätzte Kompakt-Größe (kein malloc). */
static size_t nec_probe_compact_cost(const uint8_t *src, size_t n, int frontend) {
    uint8_t res[512];
    uint8_t pk[640];
    nec_delta_st st;
    size_t bp_len;
    int fe;
    if (n < 64 || n > sizeof res || (n & 1u) != 0)
        return (size_t)-1;
    fe = nec_fe_to_delta(frontend);
    nec_delta_st_init(&st);
    st.fire_l = 256;
    if (nec_delta_fwd_chunk(&st, fe, src, res, n) != 0)
        return (size_t)-1;
    bp_len = nec_bitpack_enc(res, n, pk, sizeof pk);
    if (bp_len == 0 || bp_len >= n)
        return (size_t)-1;
    return (size_t)NEC_LITE_COMPACT_HEAD + bp_len;
}

/* Whole-buffer Bitpack → Kompakt; Bitpack direkt hinter Header. */
static int nec_compress_whole_bp(
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t *dst_len,
    int frontend
) {
    uint8_t *res = NULL;
    nec_delta_st st;
    size_t bp_cap;
    size_t bp_len;
    int fe = nec_fe_to_delta(frontend);
    uint8_t mark;

    if (n < 64 || (n & 1u) != 0)
        return NEC_ERR_ARG;
    if (!nec_fe_is_i16(frontend))
        return NEC_ERR_ARG;
    if (n > 0xffffffffu)
        return NEC_ERR_ARG;
    if (*dst_len < (size_t)NEC_LITE_COMPACT_HEAD + 8u)
        return NEC_ERR_DST;
    bp_cap = *dst_len - (size_t)NEC_LITE_COMPACT_HEAD;
    /* Kleine Blöcke: Residual auf dem Stack — weniger malloc-Latenz. */
    {
        uint8_t stack_res[8192];
        int on_stack = (n <= sizeof stack_res);
        res = on_stack ? stack_res : (uint8_t *)malloc(n);
        if (!res)
            return NEC_ERR_INTERNAL;
        nec_delta_st_init(&st);
        st.fire_l = 256;
        if (nec_delta_fwd_chunk(&st, fe, src, res, n) != 0) {
            if (!on_stack)
                free(res);
            return NEC_ERR_ARG;
        }
        bp_len = nec_bitpack_enc(res, n, dst + NEC_LITE_COMPACT_HEAD, bp_cap);
        if (!on_stack)
            free(res);
    }
    if (bp_len == 0 || bp_len >= n)
        return NEC_ERR_INTERNAL;
    mark = (uint8_t)NEC_LITE_COMPACT_DELTA;
    if (frontend == NEC_LITE_FE_I16_FIRE)
        mark = (uint8_t)NEC_LITE_COMPACT_FIRE;
    dst[0] = mark;
    nec_put_u32(dst + 1, (uint32_t)n);
    *dst_len = (size_t)NEC_LITE_COMPACT_HEAD + bp_len;
    return NEC_OK;
}

static int nec_decompress_compact(
    const uint8_t *src,
    size_t src_n,
    uint8_t *dst,
    size_t *dst_len
) {
    uint32_t orig_u;
    size_t orig;
    size_t bp_len;
    nec_delta_st st;
    int fe;
    if (!dst_len || src_n < (size_t)NEC_LITE_COMPACT_HEAD)
        return NEC_ERR_ARG;
    if (src[0] != NEC_LITE_COMPACT_DELTA && src[0] != NEC_LITE_COMPACT_FIRE)
        return NEC_ERR_SRC;
    orig_u = nec_get_u32(src + 1);
    orig = (size_t)orig_u;
    if (orig == 0 || (orig & 1u) != 0)
        return NEC_ERR_SRC;
    if (orig > *dst_len) {
        *dst_len = orig;
        return NEC_ERR_DST;
    }
    if (!dst)
        return NEC_ERR_ARG;
    bp_len = src_n - (size_t)NEC_LITE_COMPACT_HEAD;
    if (nec_bitpack_dec(src + NEC_LITE_COMPACT_HEAD, bp_len, dst, orig) != 0)
        return NEC_ERR_SRC;
    fe = nec_fe_to_delta(
        (src[0] == NEC_LITE_COMPACT_FIRE) ? NEC_LITE_FE_I16_FIRE : NEC_LITE_FE_I16_DELTA
    );
    nec_delta_st_init(&st);
    st.fire_l = 256;
    if (nec_delta_inv_chunk(&st, fe, dst, orig) != 0)
        return NEC_ERR_SRC;
    *dst_len = orig;
    return NEC_OK;
}

/* Host Mini-LZ: Plateaus/Stufen (nab_ec2). Fenster 2 KiB, Hash-Kette. */
enum { NEC_LZ_WIN = 2048, NEC_LZ_MIN = 3, NEC_LZ_HASH = 4096, NEC_LZ_MAX_N = 65536 };

static unsigned nec_lz_hash3(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    return (unsigned)((v * 2654435761u) >> 20) & (NEC_LZ_HASH - 1u);
}

static int nec_looks_lz_friendly(const uint8_t *src, size_t n) {
    size_t same = 0, checks = 0, i;
    if (n < 64u)
        return 0;
    for (i = 2; i + 1u < n && i < 512u; i += 2u) {
        checks++;
        if (src[i] == src[i - 2u] && src[i + 1u] == src[i - 1u])
            same++;
    }
    return checks > 0 && same * 5u >= checks * 2u;
}

static int nec_compress_compact_lz(
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t *dst_len
) {
    int32_t head[NEC_LZ_HASH];
    int32_t *prev = NULL;
    size_t i, lit0, o;
    int k;
    if (!dst_len || !src || !dst)
        return NEC_ERR_ARG;
    if (n < 16u || n > (size_t)NEC_LZ_MAX_N)
        return NEC_ERR_ARG;
    if (*dst_len < (size_t)NEC_LITE_COMPACT_HEAD + n)
        return NEC_ERR_DST;
    prev = (int32_t *)malloc(n * sizeof(int32_t));
    if (!prev)
        return NEC_ERR_INTERNAL;
    for (k = 0; k < NEC_LZ_HASH; k++)
        head[k] = -1;
    for (i = 0; i < n; i++)
        prev[i] = -1;
    dst[0] = (uint8_t)NEC_LITE_COMPACT_LZ;
    nec_put_u32(dst + 1, (uint32_t)n);
    o = (size_t)NEC_LITE_COMPACT_HEAD;
    i = 0;
    lit0 = 0;
    while (i < n) {
        size_t best_l = 0, best_d = 0;
        if (i + (size_t)NEC_LZ_MIN <= n) {
            unsigned h = nec_lz_hash3(src + i);
            int32_t p = head[h];
            int steps = 0;
            while (p >= 0 && steps < 128) {
                size_t dist = i - (size_t)p;
                size_t L = 0;
                if (dist == 0 || dist > (size_t)NEC_LZ_WIN)
                    break;
                while (L < 258u && i + L < n && src[(size_t)p + L] == src[i + L])
                    L++;
                if (L >= (size_t)NEC_LZ_MIN && L > best_l) {
                    best_l = L;
                    best_d = dist;
                    if (best_l >= 64u)
                        break;
                }
                p = prev[p];
                steps++;
            }
        }
        if (best_l >= (size_t)NEC_LZ_MIN) {
            size_t lit_len = i - lit0;
            size_t t;
            while (lit_len > 0) {
                size_t c = lit_len > 127u ? 127u : lit_len;
                if (o + 1u + c > *dst_len) {
                    free(prev);
                    return NEC_ERR_DST;
                }
                dst[o++] = (uint8_t)c;
                memcpy(dst + o, src + lit0, c);
                o += c;
                lit0 += c;
                lit_len -= c;
            }
            {
                size_t rem = best_l;
                while (rem > 0) {
                    size_t c = rem > 127u ? 127u : rem;
                    if (o + 3u > *dst_len) {
                        free(prev);
                        return NEC_ERR_DST;
                    }
                    dst[o++] = (uint8_t)(0x80u | (uint8_t)c);
                    nec_put_u16(dst + o, (uint16_t)best_d);
                    o += 2;
                    rem -= c;
                }
            }
            for (t = 0; t < best_l; t++) {
                if (i + t + 2u < n) {
                    unsigned hh = nec_lz_hash3(src + i + t);
                    prev[i + t] = head[hh];
                    head[hh] = (int32_t)(i + t);
                }
            }
            i += best_l;
            lit0 = i;
        } else {
            if (i + 2u < n) {
                unsigned hh = nec_lz_hash3(src + i);
                prev[i] = head[hh];
                head[hh] = (int32_t)i;
            }
            i++;
        }
    }
    if (lit0 < n) {
        size_t lit_len = n - lit0;
        while (lit_len > 0) {
            size_t c = lit_len > 127u ? 127u : lit_len;
            if (o + 1u + c > *dst_len) {
                free(prev);
                return NEC_ERR_DST;
            }
            dst[o++] = (uint8_t)c;
            memcpy(dst + o, src + lit0, c);
            o += c;
            lit0 += c;
            lit_len -= c;
        }
    }
    free(prev);
    *dst_len = o;
    return NEC_OK;
}

static int nec_decompress_compact_lz(
    const uint8_t *src,
    size_t src_n,
    uint8_t *dst,
    size_t *dst_len
) {
    size_t orig, o = 0, i;
    if (!dst_len || src_n < (size_t)NEC_LITE_COMPACT_HEAD)
        return NEC_ERR_ARG;
    if (src[0] != NEC_LITE_COMPACT_LZ)
        return NEC_ERR_SRC;
    orig = (size_t)nec_get_u32(src + 1);
    if (orig == 0 || orig > (size_t)NEC_LZ_MAX_N)
        return NEC_ERR_SRC;
    if (orig > *dst_len) {
        *dst_len = orig;
        return NEC_ERR_DST;
    }
    if (!dst)
        return NEC_ERR_ARG;
    i = (size_t)NEC_LITE_COMPACT_HEAD;
    while (o < orig) {
        uint8_t b;
        size_t ln, k;
        if (i >= src_n)
            return NEC_ERR_SRC;
        b = src[i++];
        if (b & 0x80u) {
            uint16_t dist;
            ln = (size_t)(b & 0x7fu);
            if (ln == 0 || i + 2u > src_n)
                return NEC_ERR_SRC;
            dist = nec_get_u16(src + i);
            i += 2;
            if (dist == 0 || (size_t)dist > o || o + ln > orig)
                return NEC_ERR_SRC;
            for (k = 0; k < ln; k++)
                dst[o + k] = dst[o - (size_t)dist + k];
            o += ln;
        } else {
            ln = (size_t)b;
            if (ln == 0 || i + ln > src_n || o + ln > orig)
                return NEC_ERR_SRC;
            memcpy(dst + o, src + i, ln);
            o += ln;
            i += ln;
        }
    }
    if (o != orig)
        return NEC_ERR_SRC;
    *dst_len = orig;
    return NEC_OK;
}

static int nec_compress_stream_block(
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t *dst_len,
    int frontend
) {
    nec_lite_enc_t e;
    nec_mem_sink sink;
    size_t need = nec_lite_compress_bound(n);
    if (*dst_len < need) {
        *dst_len = need;
        return NEC_ERR_DST;
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
#endif

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
    if (!nec_fe_ok(frontend))
        return NEC_ERR_ARG;
    need = nec_lite_compress_bound(n);
    if (*dst_len < need) {
        *dst_len = need;
        return NEC_ERR_DST;
    }
    if (frontend == NEC_LITE_FE_I16 && n > 0) {
#ifndef NEC_LITE_NO_MALLOC
        int best_fe = NEC_LITE_FE_I16_DELTA;
        size_t probe_n = n < 512u ? n : 512u;
        size_t cd = (size_t)-1, cf = (size_t)-1;
        size_t len = *dst_len;
        int rc;
        /* ≤16 KiB: nur DELTA — Timing-Pfad ohne FIRE-Probe. */
        if (n > 16384u && probe_n >= 64u && (probe_n & 1u) == 0) {
            cd = nec_probe_compact_cost(src, probe_n, NEC_LITE_FE_I16_DELTA);
            cf = nec_probe_compact_cost(src, probe_n, NEC_LITE_FE_I16_FIRE);
            if (cd != (size_t)-1 && cf != (size_t)-1)
                best_fe = (cf < cd) ? NEC_LITE_FE_I16_FIRE : NEC_LITE_FE_I16_DELTA;
            else if (cf != (size_t)-1)
                best_fe = NEC_LITE_FE_I16_FIRE;
        }
        rc = nec_compress_whole_bp(src, n, dst, &len, best_fe);
        if (rc == NEC_OK) {
            if (cd != (size_t)-1 && cf != (size_t)-1) {
                size_t a = cd < cf ? cd : cf;
                size_t b = cd < cf ? cf : cd;
                if (b <= a + (a / 20u)) {
                    int other = (best_fe == NEC_LITE_FE_I16_DELTA) ? NEC_LITE_FE_I16_FIRE
                                                                    : NEC_LITE_FE_I16_DELTA;
                    size_t len2 = need;
                    uint8_t *tmp = (uint8_t *)malloc(need);
                    if (tmp &&
                        nec_compress_whole_bp(src, n, tmp, &len2, other) == NEC_OK &&
                        len2 < len) {
                        memcpy(dst, tmp, len2);
                        len = len2;
                    }
                    free(tmp);
                }
            }
            /* Plateau-LZ (Host): nur wenn i16-Wiederholungen dominant (EC2). */
            if (n <= (size_t)NEC_LZ_MAX_N && nec_looks_lz_friendly(src, n)) {
                uint8_t *tmp = (uint8_t *)malloc(need);
                size_t lz = need;
                if (tmp &&
                    nec_compress_compact_lz(src, n, tmp, &lz) == NEC_OK &&
                    lz < len) {
                    memcpy(dst, tmp, lz);
                    len = lz;
                }
                free(tmp);
            }
            /* Stream+RC nur bei großen, BP-schwachen Serien (z.B. intel).
             * Kleine Sprintz-Parität-Fälle (16 KiB Timing) nicht anfassen. */
            if (n >= 32768u && len * 50u > n * 21u) {
                uint8_t *tmp = (uint8_t *)malloc(need);
                size_t ls = need;
                int other = (best_fe == NEC_LITE_FE_I16_DELTA) ? NEC_LITE_FE_I16_FIRE
                                                                : NEC_LITE_FE_I16_DELTA;
                if (tmp &&
                    nec_compress_stream_block(src, n, tmp, &ls, best_fe) == NEC_OK &&
                    ls < len) {
                    memcpy(dst, tmp, ls);
                    len = ls;
                }
                if (tmp && len * 50u > n * 21u) {
                    ls = need;
                    if (nec_compress_stream_block(src, n, tmp, &ls, other) == NEC_OK &&
                        ls < len) {
                        memcpy(dst, tmp, ls);
                        len = ls;
                    }
                }
                free(tmp);
            }
            *dst_len = len;
            return NEC_OK;
        }
#endif
        frontend = NEC_LITE_FE_I16_DELTA;
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
    fe = nec_fe_to_delta(nec_fe_from_flags(d->flags));
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
    if (d->rec_kind == 0xFEu) {
        size_t zs = nec_nsym_dec(d->nsym);
        memset(d->chunk, 0, zs);
        for (bi = 0; bi < zs; bi++)
            nec_nib_see(&d->model, 0);
        return nec_dec_out(d, d->chunk, zs);
    }
    if (d->rec_kind == 0xFAu) {
        /* Whole-BP nur über nec_lite_decompress-Fastpath (Puffer > 288 B). */
        return NEC_ERR_SRC;
    }
    if (d->rec_kind == 0xFDu || d->rec_kind == 0xFBu) {
        if (nec_bitpack_dec(pay, pay_n, d->chunk, nsym) != 0)
            return NEC_ERR_SRC;
        for (bi = 0; bi < nsym; bi++)
            nec_nib_see(&d->model, d->chunk[bi]);
        return nec_dec_out(d, d->chunk, nsym);
    }
    if (d->rec_kind == 0xFFu) {
        if (pay_n != nsym)
            return NEC_ERR_SRC;
        memcpy(d->chunk, pay, nsym);
        for (bi = 0; bi < nsym; bi++)
            nec_nib_see(&d->model, d->chunk[bi]);
        return nec_dec_out(d, d->chunk, nsym);
    }
#ifdef NEC_LITE_NO_MALLOC
    /* MCU-Stream: nur BP/Zero/STORE — RC-Records ablehnen (Encoder emittiert keine). */
    return NEC_ERR_SRC;
#else
    if (d->rec_kind < 1u || d->rec_kind > 252u)
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
#endif
}

int nec_lite_dec_init(nec_lite_dec_t *d, nec_lite_sink_fn sink, void *ctx) {
    if (!d || !sink)
        return NEC_ERR_ARG;
    memset(d, 0, sizeof *d);
    nec_nib_init(&d->model);
    d->crc_state = 0xFFFFFFFFu;
    d->fire_l = 256;
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

        if (!d->want_nsym && !d->want_pay && !d->want_bp_hdr && d->rec_kind == 0) {
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
            if (d->rec_kind == 0xFBu) {
                d->nsym = 0; /* full chunk */
                d->want_bp_hdr = 1;
                continue;
            }
            if (d->rec_kind == 0xFAu) {
                d->err = NEC_ERR_SRC; /* Fastpath in nec_lite_decompress */
                return d->err;
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
            if (d->rec_kind == 0xFDu) {
                d->want_bp_hdr = 1;
                continue;
            }
            if (d->rec_kind == 0xFFu)
                d->rec_need = nec_nsym_dec(d->nsym);
            else if (d->rec_kind == 0xFEu)
                d->rec_need = 0;
            else
                d->rec_need = (size_t)d->rec_kind;
            d->want_pay = 1;
            continue;
        }

        if (d->want_bp_hdr) {
            if (d->want_bp_hdr == 2) {
                if (d->in_n < 4)
                    return NEC_OK;
                d->rec_need = (size_t)nec_get_u32(d->in);
                nec_dec_consume(d, 4);
                d->payload_seen += 4;
            } else {
                if (d->in_n < 1)
                    return NEC_OK;
                d->rec_need = (size_t)d->in[0];
                nec_dec_consume(d, 1);
                d->payload_seen += 1;
            }
            d->want_bp_hdr = 0;
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

static int nec_decompress_fa(
    const uint8_t *src,
    size_t src_n,
    uint8_t *dst,
    size_t *dst_len,
    const NecLiteHeader *h
) {
    const uint8_t *pay;
    uint32_t plen;
    size_t orig;
    nec_delta_st st;
    int fe;
    if (!h || !dst_len)
        return NEC_ERR_ARG;
    orig = (size_t)h->orig_len;
    if (orig == 0 || (orig & 1u) != 0)
        return NEC_ERR_SRC;
    if (orig > *dst_len) {
        *dst_len = orig;
        return NEC_ERR_DST;
    }
    if (!dst)
        return NEC_ERR_ARG;
    if (src_n < (size_t)NEC_HEADER_SIZE + 5u)
        return NEC_ERR_SRC;
    pay = src + NEC_HEADER_SIZE;
    if (pay[0] != 0xFAu)
        return NEC_ERR_SRC;
    plen = nec_get_u32(pay + 1);
    if ((uint64_t)NEC_HEADER_SIZE + 5u + (uint64_t)plen != (uint64_t)src_n &&
        (size_t)h->coded_len != 5u + (size_t)plen)
        return NEC_ERR_SRC;
    if ((size_t)NEC_HEADER_SIZE + 5u + (size_t)plen > src_n)
        return NEC_ERR_SRC;
    if (nec_bitpack_dec(pay + 5, (size_t)plen, dst, orig) != 0)
        return NEC_ERR_SRC;
    fe = nec_fe_to_delta(nec_fe_from_flags(h->flags));
    nec_delta_st_init(&st);
    st.fire_l = 256;
    if (nec_delta_inv_chunk(&st, fe, dst, orig) != 0)
        return NEC_ERR_SRC;
    if (nec_lite_crc32(dst, orig) != h->crc32)
        return NEC_ERR_CRC;
    *dst_len = orig;
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
    return nec_delta_inv_chunk(&st, nec_fe_to_delta(nec_fe_from_flags(flags)), buf, n);
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
#ifndef NEC_LITE_NO_MALLOC
    if (src_n > 0 && src &&
        (src[0] == NEC_LITE_COMPACT_DELTA || src[0] == NEC_LITE_COMPACT_FIRE))
        return nec_decompress_compact(src, src_n, dst, dst_len);
    if (src_n > 0 && src && src[0] == NEC_LITE_COMPACT_LZ)
        return nec_decompress_compact_lz(src, src_n, dst, dst_len);
#endif
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
        if (h.orig_len > 0 && use > (size_t)NEC_HEADER_SIZE &&
            src[NEC_HEADER_SIZE] == 0xFAu) {
            return nec_decompress_fa(src, use, dst, dst_len, &h);
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

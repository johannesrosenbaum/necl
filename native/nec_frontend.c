/* Domain frontend: first-order delta + ZigZag. Integer-only, bit-exact. */
#include "nec_frontend.h"

#include <stdint.h>
#include <string.h>

static uint16_t nec_zz16(int16_t n) {
    return (uint16_t)(((uint16_t)n << 1) ^ (uint16_t)(n >> 15));
}

static int16_t nec_unzz16(uint16_t u) {
    return (int16_t)((u >> 1) ^ (uint16_t)-(int16_t)(u & 1u));
}

static uint32_t nec_zz32(int32_t n) {
    return ((uint32_t)n << 1) ^ (uint32_t)(n >> 31);
}

static int32_t nec_unzz32(uint32_t u) {
    return (int32_t)((u >> 1) ^ (uint32_t)-(int32_t)(u & 1u));
}

static uint16_t nec_load_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void nec_store_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static uint32_t nec_load_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void nec_store_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

void nec_delta_st_init(nec_delta_st *s) {
    if (!s)
        return;
    s->prev_i16 = 0;
    s->prev_ts = 0;
    s->prev_px = 0;
}

int nec_delta_fwd_chunk(nec_delta_st *s, int fe, const uint8_t *src, uint8_t *dst, size_t n) {
    size_t i;
    if (!s || (n > 0 && (!src || !dst)))
        return -5;
    if (fe == NEC_DELTA_NONE) {
        if (n)
            memcpy(dst, src, n);
        return 0;
    }
    if (fe == NEC_DELTA_I16) {
        for (i = 0; i + 1 < n; i += 2) {
            int16_t x = (int16_t)nec_load_u16(src + i);
            int16_t d = (int16_t)(x - s->prev_i16);
            nec_store_u16(dst + i, nec_zz16(d));
            s->prev_i16 = x;
        }
        if (i < n)
            dst[i] = src[i];
        return 0;
    }
    if (fe == NEC_DELTA_TICK8) {
        for (i = 0; i + 7 < n; i += 8) {
            int32_t ts = (int32_t)nec_load_u32(src + i);
            int32_t px = (int32_t)nec_load_u32(src + i + 4);
            nec_store_u32(dst + i, nec_zz32(ts - s->prev_ts));
            nec_store_u32(dst + i + 4, nec_zz32(px - s->prev_px));
            s->prev_ts = ts;
            s->prev_px = px;
        }
        if (i < n)
            memcpy(dst + i, src + i, n - i);
        return 0;
    }
    return -5;
}

int nec_delta_inv_chunk(nec_delta_st *s, int fe, uint8_t *buf, size_t n) {
    size_t i;
    if (!s || (n > 0 && !buf))
        return -5;
    if (fe == NEC_DELTA_NONE)
        return 0;
    if (fe == NEC_DELTA_I16) {
        for (i = 0; i + 1 < n; i += 2) {
            int16_t d = nec_unzz16(nec_load_u16(buf + i));
            int16_t x = (int16_t)(s->prev_i16 + d);
            nec_store_u16(buf + i, (uint16_t)x);
            s->prev_i16 = x;
        }
        return 0;
    }
    if (fe == NEC_DELTA_TICK8) {
        for (i = 0; i + 7 < n; i += 8) {
            int32_t ts = s->prev_ts + nec_unzz32(nec_load_u32(buf + i));
            int32_t px = s->prev_px + nec_unzz32(nec_load_u32(buf + i + 4));
            nec_store_u32(buf + i, (uint32_t)ts);
            nec_store_u32(buf + i + 4, (uint32_t)px);
            s->prev_ts = ts;
            s->prev_px = px;
        }
        return 0;
    }
    return -5;
}

int nec_delta_i16_forward(const uint8_t *src, uint8_t *dst, size_t n) {
    nec_delta_st st;
    nec_delta_st_init(&st);
    return nec_delta_fwd_chunk(&st, NEC_DELTA_I16, src, dst, n);
}

int nec_delta_i16_inverse(const uint8_t *src, uint8_t *dst, size_t n) {
    nec_delta_st st;
    nec_delta_st_init(&st);
    if (!src || !dst)
        return -5;
    if (src != dst)
        memcpy(dst, src, n);
    return nec_delta_inv_chunk(&st, NEC_DELTA_I16, dst, n);
}

int nec_delta_tick8_forward(const uint8_t *src, uint8_t *dst, size_t n) {
    nec_delta_st st;
    nec_delta_st_init(&st);
    return nec_delta_fwd_chunk(&st, NEC_DELTA_TICK8, src, dst, n);
}

int nec_delta_tick8_inverse(const uint8_t *src, uint8_t *dst, size_t n) {
    nec_delta_st st;
    nec_delta_st_init(&st);
    if (!src || !dst)
        return -5;
    if (src != dst)
        memcpy(dst, src, n);
    return nec_delta_inv_chunk(&st, NEC_DELTA_TICK8, dst, n);
}

#ifndef NEC_LITE_CORE_ONLY
/* A=00 C=01 G=10 T=11, LSB-first, 4 Basen/Byte. Nur Großbuchstaben, bitexakt. */
static int nec_dna_code(uint8_t c) {
    if (c == 'A')
        return 0;
    if (c == 'C')
        return 1;
    if (c == 'G')
        return 2;
    if (c == 'T')
        return 3;
    return -1;
}

static const uint8_t nec_dna_base[4] = {'A', 'C', 'G', 'T'};

static size_t nec_dna_pack_bytes(size_t n) {
    return (n + 3u) / 4u;
}

static int nec_pack_acgt(const uint8_t *src, size_t n, uint8_t *dst) {
    size_t i;
    size_t pn = nec_dna_pack_bytes(n);
    memset(dst, 0, pn);
    for (i = 0; i < n; i++) {
        int c = nec_dna_code(src[i]);
        if (c < 0)
            return -5;
        dst[i / 4] |= (uint8_t)((unsigned)c << ((i % 4) * 2));
    }
    return 0;
}

static void nec_unpack_acgt(const uint8_t *src, size_t n, uint8_t *dst) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v = (src[i / 4] >> ((i % 4) * 2)) & 3u;
        dst[i] = nec_dna_base[v];
    }
}

size_t nec_dna2_bound(size_t n) {
    return 4u + nec_dna_pack_bytes(n);
}

int nec_dna2_forward(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
) {
    size_t i;
    size_t need;
    if (!dst_len)
        return -5;
    if (src_len > 0xFFFFFFFFu)
        return -5;
    if (src_len > 0 && !src)
        return -5;
    for (i = 0; i < src_len; i++) {
        if (nec_dna_code(src[i]) < 0)
            return -5;
    }
    need = nec_dna2_bound(src_len);
    if (*dst_len < need) {
        *dst_len = need;
        return -1;
    }
    if (!dst)
        return -5;
    nec_store_u32(dst, (uint32_t)src_len);
    if (nec_pack_acgt(src, src_len, dst + 4) != 0)
        return -5;
    *dst_len = need;
    return 0;
}

int nec_dna2_inverse(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
) {
    uint32_t orig;
    size_t pack;
    if (!dst_len)
        return -5;
    if (!src || src_len < 4)
        return -2;
    orig = nec_load_u32(src);
    pack = nec_dna_pack_bytes((size_t)orig);
    if (src_len != 4u + pack)
        return -2;
    if (*dst_len < (size_t)orig) {
        *dst_len = (size_t)orig;
        return -1;
    }
    if ((size_t)orig > 0 && !dst)
        return -5;
    nec_unpack_acgt(src + 4, (size_t)orig, dst);
    *dst_len = (size_t)orig;
    return 0;
}

static int nec_find_nl(const uint8_t *s, size_t n, size_t i, size_t *nl) {
    while (i < n && s[i] != '\n') {
        if (s[i] == '\r')
            return -2;
        i++;
    }
    if (i >= n)
        return -2;
    *nl = i;
    return 0;
}

size_t nec_fastq4_bound(size_t n) {
    /* Tiny records: binary framing can exceed ASCII. 150 bp Illumina shrinks. */
    if (n > (SIZE_MAX / 2u) - 64u)
        return SIZE_MAX;
    return (n * 2u) + 64u;
}

int nec_fastq4_forward(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
) {
    size_t i;
    size_t nrec;
    size_t need;
    size_t o;
    uint32_t recs;
    if (!dst_len)
        return -5;
    if (src_len > 0 && !src)
        return -5;
    if (src_len > 0xFFFFFFFFu)
        return -5;

    i = 0;
    nrec = 0;
    need = 8;
    while (i < src_len) {
        size_t nl_h, nl_s, nl_p, nl_q;
        size_t hlen, slen, plen, qlen;
        if (src[i] != '@')
            return -2;
        if (nec_find_nl(src, src_len, i, &nl_h) != 0)
            return -2;
        hlen = nl_h - i;
        if (hlen == 0 || hlen > 0xFFFFu)
            return -2;
        if (nec_find_nl(src, src_len, nl_h + 1, &nl_s) != 0)
            return -2;
        slen = nl_s - (nl_h + 1);
        {
            size_t b;
            for (b = 0; b < slen; b++) {
                if (nec_dna_code(src[nl_h + 1 + b]) < 0)
                    return -5;
            }
        }
        if (nl_s + 1 >= src_len || src[nl_s + 1] != '+')
            return -2;
        if (nec_find_nl(src, src_len, nl_s + 1, &nl_p) != 0)
            return -2;
        plen = nl_p - (nl_s + 1);
        if (plen == 0 || plen > 0xFFFFu)
            return -2;
        if (nec_find_nl(src, src_len, nl_p + 1, &nl_q) != 0)
            return -2;
        qlen = nl_q - (nl_p + 1);
        if (qlen != slen)
            return -2;
        need += 2u + hlen + 4u + nec_dna_pack_bytes(slen) + 2u + plen + slen;
        nrec++;
        i = nl_q + 1;
    }
    if (*dst_len < need) {
        *dst_len = need;
        return -1;
    }
    if (!dst)
        return -5;

    nec_store_u32(dst, (uint32_t)src_len);
    recs = (uint32_t)nrec;
    nec_store_u32(dst + 4, recs);
    o = 8;
    i = 0;
    while (i < src_len) {
        size_t nl_h = 0, nl_s = 0, nl_p = 0, nl_q = 0;
        size_t hlen, slen, plen;
        nec_find_nl(src, src_len, i, &nl_h);
        hlen = nl_h - i;
        nec_find_nl(src, src_len, nl_h + 1, &nl_s);
        slen = nl_s - (nl_h + 1);
        nec_find_nl(src, src_len, nl_s + 1, &nl_p);
        plen = nl_p - (nl_s + 1);
        nec_find_nl(src, src_len, nl_p + 1, &nl_q);
        dst[o] = (uint8_t)(hlen & 0xffu);
        dst[o + 1] = (uint8_t)((hlen >> 8) & 0xffu);
        o += 2;
        memcpy(dst + o, src + i, hlen);
        o += hlen;
        nec_store_u32(dst + o, (uint32_t)slen);
        o += 4;
        if (nec_pack_acgt(src + nl_h + 1, slen, dst + o) != 0)
            return -5;
        o += nec_dna_pack_bytes(slen);
        dst[o] = (uint8_t)(plen & 0xffu);
        dst[o + 1] = (uint8_t)((plen >> 8) & 0xffu);
        o += 2;
        memcpy(dst + o, src + nl_s + 1, plen);
        o += plen;
        memcpy(dst + o, src + nl_p + 1, slen);
        o += slen;
        i = nl_q + 1;
    }
    *dst_len = o;
    return 0;
}

int nec_fastq4_inverse(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
) {
    uint32_t orig;
    uint32_t nrec;
    size_t i;
    size_t o;
    uint32_t r;
    if (!dst_len)
        return -5;
    if (!src || src_len < 8)
        return -2;
    orig = nec_load_u32(src);
    nrec = nec_load_u32(src + 4);
    if (*dst_len < (size_t)orig) {
        *dst_len = (size_t)orig;
        return -1;
    }
    if ((size_t)orig > 0 && !dst)
        return -5;
    i = 8;
    o = 0;
    for (r = 0; r < nrec; r++) {
        size_t hlen, plen, pack;
        uint32_t slen;
        if (i + 2 > src_len)
            return -2;
        hlen = (size_t)src[i] | ((size_t)src[i + 1] << 8);
        i += 2;
        if (i + hlen + 4 > src_len)
            return -2;
        if (o + hlen + 1 > (size_t)orig)
            return -2;
        memcpy(dst + o, src + i, hlen);
        o += hlen;
        dst[o++] = '\n';
        i += hlen;
        slen = nec_load_u32(src + i);
        i += 4;
        pack = nec_dna_pack_bytes((size_t)slen);
        if (i + pack + 2 > src_len)
            return -2;
        if (o + (size_t)slen + 1 > (size_t)orig)
            return -2;
        nec_unpack_acgt(src + i, (size_t)slen, dst + o);
        o += (size_t)slen;
        dst[o++] = '\n';
        i += pack;
        plen = (size_t)src[i] | ((size_t)src[i + 1] << 8);
        i += 2;
        if (i + plen + (size_t)slen > src_len)
            return -2;
        if (o + plen + 1 + (size_t)slen + 1 > (size_t)orig)
            return -2;
        memcpy(dst + o, src + i, plen);
        o += plen;
        dst[o++] = '\n';
        i += plen;
        memcpy(dst + o, src + i, (size_t)slen);
        o += (size_t)slen;
        dst[o++] = '\n';
        i += (size_t)slen;
    }
    if (i != src_len || o != (size_t)orig)
        return -2;
    *dst_len = (size_t)orig;
    return 0;
}
#endif /* NEC_LITE_CORE_ONLY */

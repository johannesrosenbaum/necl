#ifndef NEC_FRONTEND_H
#define NEC_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches NEC_LITE_FE_*. */
#define NEC_DELTA_NONE  0
#define NEC_DELTA_I16   1
#define NEC_DELTA_TICK8 2

typedef struct {
    int16_t prev_i16;
    int32_t prev_ts;
    int32_t prev_px;
} nec_delta_st;

void nec_delta_st_init(nec_delta_st *s);
int nec_delta_fwd_chunk(nec_delta_st *s, int fe, const uint8_t *src, uint8_t *dst, size_t n);
int nec_delta_inv_chunk(nec_delta_st *s, int fe, uint8_t *buf, size_t n);

/* First-order delta + ZigZag. Same length as input. Odd trailing byte copied.
 * int16 little-endian samples (AR(1) / ADC). prev starts at 0. */
int nec_delta_i16_forward(const uint8_t *src, uint8_t *dst, size_t n);
int nec_delta_i16_inverse(const uint8_t *src, uint8_t *dst, size_t n);

/* Packed ticks: repeating <u32 ts><i32 price> LE. Delta+ZigZag per field. */
int nec_delta_tick8_forward(const uint8_t *src, uint8_t *dst, size_t n);
int nec_delta_tick8_inverse(const uint8_t *src, uint8_t *dst, size_t n);

/* 2-bit ACGT pack. Payload: u32 orig_len LE + packed bases (LSB-first).
 * Inverse restores uppercase A,C,G,T. Other bytes → NEC_ERR_ARG. */
size_t nec_dna2_bound(size_t n);
int nec_dna2_forward(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
);
int nec_dna2_inverse(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
);

/* 4-line FASTQ (LF). Sequence lines packed 2-bit; header/plus/quality copied.
 * dst_len in/out like compress. Only uppercase ACGT in sequences. */
size_t nec_fastq4_bound(size_t n);
int nec_fastq4_forward(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
);
int nec_fastq4_inverse(
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t *dst_len
);

#ifdef __cplusplus
}
#endif

#endif

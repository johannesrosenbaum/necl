#ifndef HS_STREAM_H
#define HS_STREAM_H

#include "heatshrink_decoder.h"
#include "heatshrink_encoder.h"

#include <stddef.h>
#include <stdint.h>

/* Block-Wrapper um die inkrementelle heatshrink-API. Encoder/Decoder
 * kommen vom Caller (static BSS oder Stack) — kein malloc. */
int hs_compress_mem(
    heatshrink_encoder *enc,
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t dst_cap,
    size_t *out_n
);

int hs_decompress_mem(
    heatshrink_decoder *dec,
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t dst_cap,
    size_t *out_n
);

#endif

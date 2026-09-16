#ifndef SPRINTZ_DELTA_H
#define SPRINTZ_DELTA_H

#include <stddef.h>
#include <stdint.h>

/* Sprintz-Delta (Paper 2018, Algorithmus 1 ohne Huffman/FIRE):
 * univariate oder multivariate int16, 8-Sample-Blöcke, Bitpack, RLE auf
 * Nullfehler-Blöcke. Skalar, kein AVX — Ratio-Vergleich, nicht deren
 * Host-Durchsatz. ndims >= 1, n % ndims == 0 (n = Anzahl int16). */
int sprintz_delta_compress_i16(
    const int16_t *src,
    size_t n,
    int ndims,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
);
int sprintz_delta_decompress_i16(
    const uint8_t *src,
    size_t n,
    int ndims,
    int16_t *dst,
    size_t cap,
    size_t *out_n
);

#endif

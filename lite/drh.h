#ifndef DRH_H
#define DRH_H

#include <stddef.h>
#include <stdint.h>

/* Delta + ZigZag + PackBits-RLE + statisches Huffman (feste Tabelle, JPEG-artig
 * gewichtet auf kleine Residuen). Univariate int16 LE. Roundtrip-pflichtig. */
int drh_compress_i16(
    const int16_t *src,
    size_t n,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
);
int drh_decompress_i16(
    const uint8_t *src,
    size_t n,
    int16_t *dst,
    size_t cap,
    size_t *out_n
);

#endif

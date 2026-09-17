#ifndef PROGLZ_H
#define PROGLZ_H

#include <stddef.h>
#include <stdint.h>

/*
 * ProgLZ — Experiment: lossless int16 als generative Mikroprogramme +
 * Program-LZ (RECALL), nicht als Byte-Entropie.
 *
 * Literatur-Abgrenzung (wichtig):
 * - NeaTS (ICDE 2025): Funktionsfragmente + Residuen, offline Partition,
 *   Random Access. Seasonality/ähnliche Fragmente = deren Future Work.
 * - LeCo: Model+Delta für Spalten.
 * - FOE: Opcodes für beliebige Binär-Chunks (keine Zeit-Semantik).
 * - Generalized Dedup: Base+Deviation für Storage.
 *
 * Neu an diesem Versuch: Streaming, feste ISA, Ringpuffer vergangener
 * Programme, RECALL(k) mit Parameter-Patch — LZ über Programme, nicht
 * über Samples. Wenn RECALL nichts bringt, ist die Idee tot.
 *
 * Window=32, univariate int16. Roundtrip-pflichtig.
 */
void proglz_stats_reset(void);
/* counts[0..3] = HOLD, RAMP, RECALL, RAW */
void proglz_stats_get(size_t counts[4]);

int proglz_compress_i16(
    const int16_t *src,
    size_t n,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
);
int proglz_decompress_i16(
    const uint8_t *src,
    size_t n,
    int16_t *dst,
    size_t cap,
    size_t *out_n
);

#endif

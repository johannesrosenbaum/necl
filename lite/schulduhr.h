#ifndef SCHULDUHR_H
#define SCHULDUHR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Schuld-Uhr (Experiment): Zwischenobjekt = Tilgungsplan, nicht Residuenstrom.
 *
 * Decoder-State: Uhr (position p, velocity vel). Zwischen Tilgungen tickt die
 * Uhr: p += vel pro Sample. Tilgung = (Lauf, dp, dv): L Schritte mit aktuellem
 * vel, dann p += dp und vel += dv. Kein adaptives Modell, kein Bitpack.
 *
 * Lossless int16, Fenster bis 32. Roundtrip-pflichtig.
 */
void schulduhr_stats_reset(void);
void schulduhr_stats_get(size_t *tilg_count, size_t *tick_samples);

int schulduhr_compress_i16(
    const int16_t *src,
    size_t n,
    uint8_t *dst,
    size_t cap,
    size_t *out_n
);
int schulduhr_decompress_i16(
    const uint8_t *src,
    size_t n,
    int16_t *dst,
    size_t cap,
    size_t *out_n
);

#endif

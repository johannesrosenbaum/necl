#ifndef HEATSHRINK_CONFIG_H
#define HEATSHRINK_CONFIG_H

/*
 * Embedded-Konfiguration für den MCU-Vergleich.
 * Upstream-Default ist DYNAMIC_ALLOC=1; hier bewusst statisch,
 * analog zu MINIZ_NO_MALLOC / nec_lite caller-buffers.
 *
 * Fenster 2^8 = 256 Byte, Lookahead 2^4 = 16 Byte:
 * das ist die kanonische Small-MCU-Config aus heatshrink_config.h
 * (static-Zweig) und die Größenordnung der „~1 KB Decoder / kleines RAM“-Nische.
 */
#define HEATSHRINK_DYNAMIC_ALLOC 0
#define HEATSHRINK_STATIC_INPUT_BUFFER_SIZE 32
#define HEATSHRINK_STATIC_WINDOW_BITS 8
#define HEATSHRINK_STATIC_LOOKAHEAD_BITS 4
#define HEATSHRINK_DEBUGGING_LOGS 0
#define HEATSHRINK_USE_INDEX 1

#endif

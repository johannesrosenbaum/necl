#ifndef NEC_LITE_H
#define NEC_LITE_H

/*
 * nec_lite — Mojo-freier C-Pfad fürs Gateway/MCU.
 *
 * Kein malloc, kein N-Gramm, kein Arithmetic-Coder.
 * Encoder: optional Delta+ZigZag (Zustand über Chunks), dann Nibble-Range-
 * Coder in 256-Byte-Chunks mit per-Chunk-STORE. Framing: FLAG_STREAM
 * self-describing records (kein Bitpack, n muss nicht vorab bekannt sein).
 *
 * RAM: Library-Heap = 0. Block-API: scratch_bound = 0 (kein O(n)-Scratch).
 * Arbeitsset = nec_lite_enc_t / nec_lite_dec_t (Caller, ~1.4 KiB), plus
 * dst bzw. Sink. Bound: n + 32 + 2*ceil(n/256) + 16.
 *
 * weight_id = NEC_LITE_WEIGHT_ID. Hybrid-Mojo-Decoder lehnt diese
 * Dateien ab (bewusst: Frontend sitzt hier im Container).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEC_OK            0
#define NEC_ERR_DST      -1
#define NEC_ERR_SRC      -2
#define NEC_ERR_CRC      -3
#define NEC_ERR_WEIGHT   -4
#define NEC_ERR_ARG      -5
#define NEC_ERR_INTERNAL -6

#define NEC_HEADER_SIZE         32u
#define NEC_LITE_VERSION        1u
#define NEC_LITE_CHUNK          256u
#define NEC_FLAG_STORE          1u
#define NEC_LITE_FLAG_DELTA_I16 2u
#define NEC_LITE_FLAG_DELTA_TICK8 4u
#define NEC_LITE_FLAG_RLE       8u /* legacy decoder unused; encoder writes FLAG_RC */
#define NEC_LITE_FLAG_RC        16u
#define NEC_LITE_FLAG_STREAM    32u
#define NEC_LITE_WEIGHT_ID      0x4E45434Cu /* "NECL" */

#define NEC_LITE_FE_NONE  0
#define NEC_LITE_FE_I16   1
#define NEC_LITE_FE_TICK8 2

#define NEC_LITE_RC_TMP (NEC_LITE_CHUNK * 2u + 16u)

size_t nec_lite_compress_bound(size_t n);
size_t nec_lite_scratch_bound(size_t n);
const char *nec_lite_strerror(int err);
uint32_t nec_lite_crc32(const uint8_t *data, size_t n);

typedef struct {
    uint16_t version;
    uint16_t flags;
    uint64_t orig_len;
    uint64_t coded_len;
    uint32_t weight_id;
    uint32_t crc32;
} NecLiteHeader;

int nec_lite_parse_header(
    const uint8_t *src,
    size_t src_n,
    NecLiteHeader *out
);

/* frontend: NEC_LITE_FE_*. scratch darf NULL sein (scratch_bound = 0). */
int nec_lite_compress(
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t *dst_len,
    uint8_t *scratch,
    size_t scratch_n,
    int frontend
);

int nec_lite_decompress(
    const uint8_t *src,
    size_t src_n,
    uint8_t *dst,
    size_t *dst_len,
    uint8_t *scratch,
    size_t scratch_n
);

/* Sink: muss n zurückgeben, sonst Fehler. */
typedef size_t (*nec_lite_sink_fn)(const uint8_t *p, size_t n, void *ctx);

typedef struct {
    uint16_t hi[16];
    uint16_t lo[16][16];
} nec_lite_model_t;

typedef struct nec_lite_enc {
    nec_lite_model_t model;
    uint8_t chunk[NEC_LITE_CHUNK];
    uint8_t rc_tmp[NEC_LITE_RC_TMP];
    int16_t prev_i16;
    int32_t prev_ts;
    int32_t prev_px;
    uint32_t crc_state;
    uint64_t orig;
    uint64_t coded;
    size_t fill;
    nec_lite_sink_fn sink;
    void *sink_ctx;
    int frontend;
    int live;
    int err;
} nec_lite_enc_t;

typedef struct nec_lite_dec {
    nec_lite_model_t model;
    uint8_t in[288];
    uint8_t chunk[NEC_LITE_CHUNK];
    int16_t prev_i16;
    int32_t prev_ts;
    int32_t prev_px;
    uint32_t crc_state;
    uint32_t hdr_crc;
    uint64_t decoded;
    uint64_t orig_len;
    uint64_t coded_expect;
    uint64_t payload_seen;
    size_t in_n;
    size_t rec_need;
    nec_lite_sink_fn sink;
    void *sink_ctx;
    uint16_t flags;
    uint8_t have_header;
    uint8_t rec_kind;
    uint8_t nsym;
    uint8_t live;
    uint8_t done;
    uint8_t want_nsym;
    uint8_t want_pay;
    int err;
} nec_lite_dec_t;

size_t nec_lite_enc_sizeof(void);
size_t nec_lite_dec_sizeof(void);

/* Live: Header orig=0/coded=0, Chunks, END+orig+crc. */
int nec_lite_enc_init(nec_lite_enc_t *e, int frontend, nec_lite_sink_fn sink, void *ctx);
int nec_lite_enc_push(nec_lite_enc_t *e, const uint8_t *src, size_t n);
int nec_lite_enc_finish(nec_lite_enc_t *e);

int nec_lite_dec_init(nec_lite_dec_t *d, nec_lite_sink_fn sink, void *ctx);
int nec_lite_dec_push(nec_lite_dec_t *d, const uint8_t *src, size_t n);
int nec_lite_dec_finish(nec_lite_dec_t *d);

#ifdef __cplusplus
}
#endif

#endif

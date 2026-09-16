#include "hs_stream.h"

enum { N = 128 };

static uint8_t src[N];
static uint8_t dst[N * 2 + 32];
static uint8_t back[N];
static heatshrink_encoder enc;
static heatshrink_decoder dec;
volatile int g_sink;

int main(void) {
    size_t i;
    size_t clen = 0;
    size_t dlen = 0;
    int rc;
    for (i = 0; i < N; i++)
        src[i] = (uint8_t)(i * 3u);
    rc = hs_compress_mem(&enc, src, N, dst, sizeof(dst), &clen);
    if (rc != 0 || clen == 0)
        return 1;
    rc = hs_decompress_mem(&dec, dst, clen, back, sizeof(back), &dlen);
    g_sink = rc + (int)dlen + (int)clen;
    return g_sink;
}

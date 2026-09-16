#include "lz4.h"

enum { N = 128 };

static uint8_t src[N];
static uint8_t dst[LZ4_COMPRESSBOUND(N)];
static uint8_t back[N];
volatile int g_sink;

int main(void) {
    int i;
    int cn;
    int dn;
    for (i = 0; i < N; i++)
        src[i] = (uint8_t)(i * 3u);
    cn = LZ4_compress_default((const char *)src, (char *)dst, N, (int)sizeof(dst));
    if (cn <= 0)
        return 1;
    dn = LZ4_decompress_safe((const char *)dst, (char *)back, cn, N);
    g_sink = dn + cn;
    return g_sink;
}

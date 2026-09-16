#include "miniz.h"

enum { N = 128 };

static uint8_t src[N];
static uint8_t dst[256];
static uint8_t back[N];
volatile int g_sink;

int main(void) {
    int i;
    size_t cn;
    size_t dn;
    for (i = 0; i < N; i++)
        src[i] = (uint8_t)(i * 3u);
    cn = tdefl_compress_mem_to_mem(dst, sizeof(dst), src, N, TDEFL_DEFAULT_MAX_PROBES);
    if (cn == 0)
        return 1;
    dn = tinfl_decompress_mem_to_mem(back, N, dst, cn, TINFL_FLAG_PARSE_ZLIB_HEADER);
    g_sink = (int)dn + (int)cn;
    return g_sink;
}

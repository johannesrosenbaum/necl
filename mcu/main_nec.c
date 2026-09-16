#include "nec_lite.h"

/* Streaming-Harness: Sensor-FIFO-Simulation in src[], Arbeitsset = enc+dec.
 * DWT-Zyklen auf Cortex-M3/M4; M0 lässt g_cy_* = 0. */

enum { N = 2048 };

static uint8_t src[N];
static uint8_t coded[N + 96];
static uint8_t plain[N];
static nec_lite_enc_t enc;
static nec_lite_dec_t dec;

volatile uint32_t g_cy_enc;
volatile uint32_t g_cy_dec;
volatile uint32_t g_coded;
volatile uint32_t g_ok;
volatile int g_sink;

#ifdef NEC_SEMIHOST
static void semi_write0(const char *s) {
    register int r0 asm("r0") = 4;
    register const char *r1 asm("r1") = s;
    asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
}

static void semi_exit(int code) {
    uint32_t args[2];
    register int r0 asm("r0") = 0x20;
    register uint32_t *r1 asm("r1") = args;
    args[0] = 0x20026u;
    args[1] = (uint32_t)code;
    asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
}

static void semi_report_u32(const char *label, uint32_t v) {
    char buf[48];
    size_t i = 0;
    char tmp[10];
    unsigned n = 0;
    while (label[i] && i < 20) {
        buf[i] = label[i];
        i++;
    }
    buf[i++] = '=';
    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v && n < 10) {
            tmp[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (n)
            buf[i++] = tmp[--n];
    }
    buf[i++] = '\n';
    buf[i] = 0;
    semi_write0(buf);
}
#endif

static uint32_t cyccnt_read(void) {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
    return *(volatile uint32_t *)0xE0001004u;
#else
    return 0;
#endif
}

static void cyccnt_start(void) {
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
    *(volatile uint32_t *)0xE000EDFCu |= (1u << 24);
    *(volatile uint32_t *)0xE0001000u |= 1u;
    *(volatile uint32_t *)0xE0001004u = 0;
#endif
}

typedef struct {
    uint8_t *p;
    size_t i;
    size_t cap;
} sink_t;

static size_t sink_fn(const uint8_t *p, size_t n, void *ctx) {
    sink_t *s = (sink_t *)ctx;
    size_t k;
    if (s->i + n > s->cap)
        return 0;
    for (k = 0; k < n; k++)
        s->p[s->i + k] = p[k];
    s->i += n;
    return n;
}

static void fill_src(void) {
    size_t i;
    int16_t x = 0;
    for (i = 0; i + 1 < N; i += 2) {
        x = (int16_t)((x * 3) / 4 + (int16_t)((i / 2) % 9) - 4);
        src[i] = (uint8_t)(x & 0xff);
        src[i + 1] = (uint8_t)((x >> 8) & 0xff);
    }
}

int main(void) {
    sink_t cs;
    sink_t os;
    uint32_t t0, t1;
    int rc;
    size_t i;
    uint32_t match;

    fill_src();
    cyccnt_start();

    cs.p = coded;
    cs.i = 0;
    cs.cap = sizeof coded;
    rc = nec_lite_enc_init(&enc, NEC_LITE_FE_I16, sink_fn, &cs);
    if (rc != NEC_OK)
        return rc;
    t0 = cyccnt_read();
    rc = nec_lite_enc_push(&enc, src, N);
    if (rc != NEC_OK)
        return rc;
    rc = nec_lite_enc_finish(&enc);
    t1 = cyccnt_read();
    if (rc != NEC_OK)
        return rc;
    g_cy_enc = t1 - t0;
    g_coded = (uint32_t)cs.i;

    os.p = plain;
    os.i = 0;
    os.cap = sizeof plain;
    rc = nec_lite_dec_init(&dec, sink_fn, &os);
    if (rc != NEC_OK)
        return rc;
    t0 = cyccnt_read();
    rc = nec_lite_dec_push(&dec, coded, cs.i);
    if (rc != NEC_OK)
        return rc;
    rc = nec_lite_dec_finish(&dec);
    t1 = cyccnt_read();
    if (rc != NEC_OK)
        return rc;
    g_cy_dec = t1 - t0;
    match = 1;
    if (os.i != (size_t)N)
        match = 0;
    for (i = 0; i < N; i++) {
        if (plain[i] != src[i])
            match = 0;
    }
    g_ok = match;
    g_sink = (int)g_ok + (int)g_coded;
#ifdef NEC_SEMIHOST
    semi_report_u32("ok", g_ok);
    semi_report_u32("n", (uint32_t)N);
    semi_report_u32("coded", g_coded);
    semi_report_u32("cy_enc", g_cy_enc);
    semi_report_u32("cy_dec", g_cy_dec);
    semi_exit(g_ok ? 0 : 1);
#endif
    return g_ok ? 0 : 1;
}

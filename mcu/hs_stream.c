#include "hs_stream.h"

int hs_compress_mem(
    heatshrink_encoder *enc,
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t dst_cap,
    size_t *out_n
) {
    size_t in_i = 0;
    size_t out_i = 0;
    if (!enc || (!src && n) || !dst || !out_n)
        return -1;
    heatshrink_encoder_reset(enc);
    while (in_i < n) {
        size_t sink_sz = 0;
        if (heatshrink_encoder_sink(enc, (uint8_t *)(src + in_i), n - in_i, &sink_sz) < 0)
            return -1;
        in_i += sink_sz;
        for (;;) {
            size_t poll_sz = 0;
            HSE_poll_res pres;
            if (out_i >= dst_cap)
                break;
            pres = heatshrink_encoder_poll(enc, dst + out_i, dst_cap - out_i, &poll_sz);
            if (pres < 0)
                return -1;
            out_i += poll_sz;
            if (pres != HSER_POLL_MORE)
                break;
        }
        if (sink_sz == 0)
            return -1;
    }
    for (;;) {
        size_t poll_sz = 0;
        HSE_poll_res pres;
        HSE_finish_res fres;
        if (out_i >= dst_cap)
            return -1;
        pres = heatshrink_encoder_poll(enc, dst + out_i, dst_cap - out_i, &poll_sz);
        if (pres < 0)
            return -1;
        out_i += poll_sz;
        if (pres == HSER_POLL_MORE)
            continue;
        fres = heatshrink_encoder_finish(enc);
        if (fres < 0)
            return -1;
        if (fres == HSER_FINISH_DONE)
            break;
    }
    *out_n = out_i;
    return 0;
}

int hs_decompress_mem(
    heatshrink_decoder *dec,
    const uint8_t *src,
    size_t n,
    uint8_t *dst,
    size_t dst_cap,
    size_t *out_n
) {
    size_t in_i = 0;
    size_t out_i = 0;
    if (!dec || (!src && n) || !dst || !out_n)
        return -1;
    heatshrink_decoder_reset(dec);
    while (in_i < n) {
        size_t sink_sz = 0;
        if (heatshrink_decoder_sink(dec, (uint8_t *)(src + in_i), n - in_i, &sink_sz) < 0)
            return -1;
        in_i += sink_sz;
        for (;;) {
            size_t poll_sz = 0;
            HSD_poll_res pres;
            if (out_i >= dst_cap)
                break;
            pres = heatshrink_decoder_poll(dec, dst + out_i, dst_cap - out_i, &poll_sz);
            if (pres < 0)
                return -1;
            out_i += poll_sz;
            if (pres != HSDR_POLL_MORE)
                break;
        }
        if (sink_sz == 0)
            return -1;
    }
    for (;;) {
        size_t poll_sz = 0;
        HSD_poll_res pres;
        HSD_finish_res fres;
        size_t room = dst_cap - out_i;
        uint8_t dummy;
        uint8_t *p = room ? (dst + out_i) : &dummy;
        size_t cap = room ? room : 1;
        /* finish/poll even if output is already full — DONE must be reachable */
        pres = heatshrink_decoder_poll(dec, p, cap, &poll_sz);
        if (pres < 0)
            return -1;
        if (room == 0 && poll_sz > 0)
            return -1;
        if (room)
            out_i += poll_sz;
        if (pres == HSDR_POLL_MORE)
            continue;
        fres = heatshrink_decoder_finish(dec);
        if (fres < 0)
            return -1;
        if (fres == HSDR_FINISH_DONE)
            break;
    }
    *out_n = out_i;
    return 0;
}

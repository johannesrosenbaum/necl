/* Linux-Gateway-Encoder/Decoder. malloc nur im Host, nicht in nec_lite. */
#include "nec_lite.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fprintf(
        stderr,
        "Usage:\n"
        "  nec-gateway compress --frontend i16|tick8|none --input <in> --output <out.nec>\n"
        "  nec-gateway decompress --input <in.nec> --output <out>\n"
    );
}

static int eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static int read_all(const char *path, uint8_t **buf, size_t *n) {
    FILE *f = fopen(path, "rb");
    long sz;
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return -1;
    }
    rewind(f);
    *n = (size_t)sz;
    *buf = (uint8_t *)malloc(*n ? *n : 1);
    if (!*buf) {
        fclose(f);
        return -1;
    }
    if (*n && fread(*buf, 1, *n, f) != *n) {
        free(*buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int write_all(const char *path, const uint8_t *buf, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    if (n && fwrite(buf, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int parse_fe(const char *s) {
    if (eq(s, "none") || eq(s, "raw"))
        return NEC_LITE_FE_NONE;
    if (eq(s, "i16") || eq(s, "int16"))
        return NEC_LITE_FE_I16;
    if (eq(s, "tick8") || eq(s, "ticks"))
        return NEC_LITE_FE_TICK8;
    return -1;
}

int main(int argc, char **argv) {
    const char *cmd = NULL;
    const char *inp = NULL;
    const char *outp = NULL;
    const char *fe_s = "none";
    int i;
    int fe;
    uint8_t *src = NULL;
    uint8_t *dst = NULL;
    size_t n = 0;
    size_t cap = 0;
    int rc;

    if (argc < 2) {
        usage();
        return 2;
    }
    cmd = argv[1];
    for (i = 2; i < argc; i++) {
        if ((eq(argv[i], "--input") || eq(argv[i], "-i")) && i + 1 < argc)
            inp = argv[++i];
        else if ((eq(argv[i], "--output") || eq(argv[i], "-o")) && i + 1 < argc)
            outp = argv[++i];
        else if ((eq(argv[i], "--frontend") || eq(argv[i], "-f")) && i + 1 < argc)
            fe_s = argv[++i];
        else if (eq(argv[i], "--help") || eq(argv[i], "-h")) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            usage();
            return 2;
        }
    }
    if (!inp || !outp) {
        usage();
        return 2;
    }

    if (read_all(inp, &src, &n) != 0) {
        fprintf(stderr, "read %s: %s\n", inp, strerror(errno));
        return 1;
    }

    if (eq(cmd, "compress") || eq(cmd, "c")) {
        fe = parse_fe(fe_s);
        if (fe < 0) {
            fprintf(stderr, "unknown frontend: %s\n", fe_s);
            free(src);
            return 2;
        }
        cap = nec_lite_compress_bound(n);
        dst = (uint8_t *)malloc(cap ? cap : 1);
        if (!dst) {
            free(src);
            return 1;
        }
        rc = nec_lite_compress(src, n, dst, &cap, NULL, 0, fe);
        if (rc != NEC_OK) {
            fprintf(stderr, "compress: %s\n", nec_lite_strerror(rc));
            free(src);
            free(dst);
            return 1;
        }
        if (write_all(outp, dst, cap) != 0) {
            fprintf(stderr, "write %s: %s\n", outp, strerror(errno));
            rc = 1;
        } else {
            unsigned pct = n ? (unsigned)((cap * 100u) / n) : 0;
            printf(
                "nec-gateway compress %zu -> %zu bytes (%u%%) frontend=%s\n",
                n,
                cap,
                pct,
                fe_s
            );
            rc = 0;
        }
        free(src);
        free(dst);
        return rc;
    }

    if (eq(cmd, "decompress") || eq(cmd, "d")) {
        NecLiteHeader h;
        rc = nec_lite_parse_header(src, n, &h);
        if (rc != NEC_OK) {
            fprintf(stderr, "header: %s\n", nec_lite_strerror(rc));
            free(src);
            return 1;
        }
        cap = (size_t)h.orig_len;
        if (cap == 0 && (h.flags & NEC_LITE_FLAG_STREAM) && n >= (size_t)NEC_HEADER_SIZE + 13u)
            cap = (size_t)(
                (uint64_t)src[n - 12] | ((uint64_t)src[n - 11] << 8) | ((uint64_t)src[n - 10] << 16) |
                ((uint64_t)src[n - 9] << 24) | ((uint64_t)src[n - 8] << 32) |
                ((uint64_t)src[n - 7] << 40) | ((uint64_t)src[n - 6] << 48) |
                ((uint64_t)src[n - 5] << 56)
            );
        dst = (uint8_t *)malloc(cap ? cap : 1);
        if (!dst) {
            free(src);
            return 1;
        }
        rc = nec_lite_decompress(src, n, dst, &cap, NULL, 0);
        if (rc != NEC_OK) {
            fprintf(stderr, "decompress: %s\n", nec_lite_strerror(rc));
            free(src);
            free(dst);
            return 1;
        }
        if (write_all(outp, dst, cap) != 0) {
            fprintf(stderr, "write %s: %s\n", outp, strerror(errno));
            rc = 1;
        } else {
            printf("nec-gateway decompress %zu -> %zu bytes\n", n, cap);
            rc = 0;
        }
        free(src);
        free(dst);
        return rc;
    }

    usage();
    free(src);
    return 2;
}

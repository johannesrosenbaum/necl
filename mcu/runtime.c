void *memcpy(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)c;
    while (n--)
        *d++ = v;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned long n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *__aeabi_memcpy(void *d, const void *s, unsigned int n) {
    return memcpy(d, s, n);
}

void *__aeabi_memcpy4(void *d, const void *s, unsigned int n) {
    return memcpy(d, s, n);
}

void *__aeabi_memset(void *d, char c, unsigned int n) {
    return memset(d, (int)(unsigned char)c, n);
}

void *__aeabi_memclr(void *d, unsigned int n) {
    return memset(d, 0, n);
}

void *__aeabi_memclr4(void *d, unsigned int n) {
    return memset(d, 0, n);
}

void __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)file;
    (void)line;
    (void)func;
    (void)expr;
    for (;;)
        ;
}

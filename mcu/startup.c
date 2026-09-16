#include <stdint.h>

extern int main(void);
extern uint32_t _estack;
extern uint32_t _sdata, _edata, _sidata, _sbss, _ebss;

void Reset_Handler(void);
void Default_Handler(void);

void Reset_Handler(void) {
    uint32_t *s;
    uint32_t *d;
    s = &_sidata;
    for (d = &_sdata; d < &_edata; )
        *d++ = *s++;
    for (d = &_sbss; d < &_ebss; )
        *d++ = 0;
    (void)main();
    for (;;)
        ;
}

void Default_Handler(void) {
    for (;;)
        ;
}

__attribute__((section(".isr_vector"), used))
void (*const g_vectors[])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler,
    Default_Handler,
};

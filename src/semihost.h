#ifndef SEMIHOST_H
#define SEMIHOST_H
#include <stdint.h>

/* ARM semihosting SYS_EXIT: lets QEMU terminate with an exit code so tests
 * can run unattended. Only works under an emulator/debugger, not on bare metal. */
static inline void semihost_exit(int code)
{
    register uint32_t op  __asm__("r0") = 0x18;                       /* SYS_EXIT */
    register uint32_t arg __asm__("r1") = code ? 0x20023u : 0x20026u; /* error / ok */
    __asm volatile("bkpt 0xAB" : : "r"(op), "r"(arg) : "memory");
    for (;;) { }
}
#endif

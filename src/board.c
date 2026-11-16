#include <stdint.h>
#include "board.h"
#include "uart.h"

#define SYSCTL_RCC (*(volatile uint32_t *)0x400FE060u)

void board_init(void)
{
    /* QEMU only computes the system clock after RCC is written, so program it
     * (reset value: main oscillator, bypass PLL). Without this SysTick has a
     * zero period and the scheduler tick never fires. */
    SYSCTL_RCC = 0x078E3AC0u;
    uart_init();
}

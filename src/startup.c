#include <stdint.h>
#include "uart.h"
#include "semihost.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);
/* Implemented in kernel.c */
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void Default_Handler(void) { for (;;) { } }

void HardFault_Handler(void)
{
    uart_puts("\n*** HARD FAULT ***\n");
    semihost_exit(1);
}

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    main();
    for (;;) { }
}

__attribute__((section(".isr_vector"), used))
void (* const vector_table[])(void) = {
    (void (*)(void))&_estack, /*  0 initial MSP    */
    Reset_Handler,            /*  1 reset          */
    Default_Handler,          /*  2 NMI            */
    HardFault_Handler,        /*  3 hard fault     */
    Default_Handler,          /*  4 mem manage     */
    Default_Handler,          /*  5 bus fault      */
    Default_Handler,          /*  6 usage fault    */
    0, 0, 0, 0,               /*  7-10 reserved    */
    SVC_Handler,              /* 11 SVCall         */
    Default_Handler,          /* 12 debug monitor  */
    0,                        /* 13 reserved       */
    PendSV_Handler,           /* 14 PendSV         */
    SysTick_Handler,          /* 15 SysTick        */
};

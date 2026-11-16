#include <stdarg.h>
#include <stdint.h>
#include "uart.h"

/* Stellaris/PL011 UART0 registers (QEMU lm3s6965evb) */
#define UART0_BASE 0x4000C000u
#define UART_DR   (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_CTL  (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define FR_TXFF   (1u << 5)

void uart_init(void)
{
    UART_CTL = (1u << 0) | (1u << 8) | (1u << 9); /* UARTEN | TXE | RXE */
}

void uart_putc(char c)
{
    while (UART_FR & FR_TXFF) { }
    UART_DR = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void put_uint(uint32_t v, uint32_t base)
{
    char buf[12];
    int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v) {
        uint32_t d = v % base;
        buf[n++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v /= base;
    }
    while (n) uart_putc(buf[--n]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (*fmt == '\n') uart_putc('\r');
            uart_putc(*fmt);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': uart_puts(va_arg(ap, const char *)); break;
        case 'c': uart_putc((char)va_arg(ap, int)); break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) { uart_putc('-'); put_uint((uint32_t)(-v), 10); }
            else put_uint((uint32_t)v, 10);
            break;
        }
        case 'u': put_uint(va_arg(ap, uint32_t), 10); break;
        case 'x': put_uint(va_arg(ap, uint32_t), 16); break;
        case '%': uart_putc('%'); break;
        default:  uart_putc('?'); break;
        }
    }
    va_end(ap);
}

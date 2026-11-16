#ifndef UART_H
#define UART_H
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
/* Minimal printf: %s %c %d %u %x %% (no floats, no width) */
void kprintf(const char *fmt, ...);
#endif

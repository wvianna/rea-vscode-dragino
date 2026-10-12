/*
 * console.h - saida textual do console AT (USART2) e LED de status.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>
#include <stdint.h>

#include "payload.h"

void console_init(void);
void console_puts(const char *s);
void console_putc(char c);
/* Subconjunto proprio de printf: %d %i %u %x %X %s %c %% %f (com precisao
 * opcional 0..3) e largura fixa com 0 (ex.: %02X). Sem newlib, sem float. */
void console_printf(const char *fmt, ...);

void console_hex(const uint8_t *data, size_t len, char sep);

/* Bloco de diagnostico no formato do firmware original ("PB14_status:%d",
 * "Bat:%.3f V", "DS18B20_temp1:%.1f", ...). */
void console_dump_sample(const sample_t *s);

#endif /* CONSOLE_H */

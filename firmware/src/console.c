#include "console.h"

#include <stdarg.h>

#include "hw.h"

void console_init(void)
{
    hw_console_init();
}

void console_putc(char c)
{
    hw_console_tx(c);
}

void console_puts(const char *s)
{
    hw_console_write(s);
}

static void out_pad(const char *digits, int len, int width, char pad)
{
    for (int i = len; i < width; i++) {
        console_putc(pad);
    }
    for (int i = 0; i < len; i++) {
        console_putc(digits[i]);
    }
}

static int fmt_unsigned(char *buf, uint32_t v, uint32_t base, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[12];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v) {
        tmp[n++] = digits[v % base];
        v /= base;
    }
    for (int i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    return n;
}

static int fmt_signed(char *buf, int32_t v)
{
    uint32_t mag = (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    int n = fmt_unsigned(buf + 1, mag, 10, false);
    if (v < 0) {
        buf[0] = '-';
        return n + 1;
    }
    for (int i = 0; i < n; i++) {
        buf[i] = buf[i + 1];
    }
    return n;
}

void console_printf(const char *fmt, ...)
{
    va_list ap;
    char buf[24];
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            console_putc(*p);
            continue;
        }
        p++;
        int width = 0;
        char pad = ' ';
        if (*p == '0') {
            pad = '0';
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            while (*p >= '0' && *p <= '9') {
                prec = prec * 10 + (*p - '0');
                p++;
            }
        }

        switch (*p) {
        case 'd':
        case 'i': {
            int n = fmt_signed(buf, va_arg(ap, int));
            out_pad(buf, n, width, pad);
            break;
        }
        case 'u': {
            int n = fmt_unsigned(buf, va_arg(ap, unsigned), 10, false);
            out_pad(buf, n, width, pad);
            break;
        }
        case 'x':
        case 'X': {
            int n = fmt_unsigned(buf, va_arg(ap, unsigned), 16, *p == 'X');
            out_pad(buf, n, width, pad);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            for (const char *q = s; *q; q++) {
                console_putc(*q);
            }
            break;
        }
        case 'c': {
            console_putc((char)va_arg(ap, int));
            break;
        }
        case 'f': {
            /* double convertido com precisao fixa, sem newlib */
            double d = va_arg(ap, double);
            int digits = (prec < 0) ? 1 : prec;
            uint32_t scale = 1;
            for (int i = 0; i < digits; i++) {
                scale *= 10u;
            }
            bool neg = (d < 0);
            if (neg) {
                d = -d;
            }
            uint32_t scaled = (uint32_t)(d * (double)scale + 0.5);
            int n = fmt_signed(buf, neg ? -(int32_t)scaled : (int32_t)scaled);
            /* imprime inteiro e insere o separador decimal */
            for (int i = 0; i < n; i++) {
                if (digits > 0 && i == n - digits) {
                    console_putc('.');
                    if (buf[i] == '-') {
                        console_putc('0');
                    }
                }
                console_putc(buf[i]);
            }
            if (digits == 0) {
                console_putc('.');
            }
            break;
        }
        case '%':
            console_putc('%');
            break;
        default:
            if (*p) {
                console_putc(*p);
            }
            break;
        }
    }
    va_end(ap);
}

void console_hex(const uint8_t *data, size_t len, char sep)
{
    const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        if (i && sep) {
            console_putc(sep);
        }
        console_putc(digits[(data[i] >> 4) & 0x0F]);
        console_putc(digits[data[i] & 0x0F]);
    }
}

void console_dump_sample(const sample_t *s)
{
    /* Rotulos equivalentes aos do firmware original (strings em 0x08001f0b..) */
    console_printf("Bat:%.3f V\r\n", (double)s->battery_mv / 1000.0);
    console_printf("PB14_status:%d\r\n", (s->din & DIN_INT1) ? 1 : 0);
    console_printf("PB14_count1:%u\r\n", (unsigned)s->count);
    console_printf("PB15_status:%d\r\n", (s->din & DIN_INT2) ? 1 : 0);
    console_printf("PA4_status:%d\r\n", (s->din & DIN_INT3) ? 1 : 0);
    console_printf("PA12_status:%d\r\n", (s->din & DIN_PA12) ? 1 : 0);

    if (s->temp_valid[0]) {
        console_printf("DS18B20_temp1:%.1f\r\n",
                       (double)s->temp_x10[0] / 10.0);
    } else {
        console_puts("DS18B20_temp1:null\r\n");
    }
    if (s->temp_valid[1]) {
        console_printf("DS18B20_temp2:%.1f\r\n",
                       (double)s->temp_x10[1] / 10.0);
    }
    if (s->temp_valid[2]) {
        console_printf("DS18B20_temp3:%.1f\r\n",
                       (double)s->temp_x10[2] / 10.0);
    }
    if (s->sht_valid) {
        console_printf(" SHT2x_temp:%.1f,SHT2x_hum:%.1f\r\n",
                       (double)s->sht_temp_x10 / 10.0,
                       (double)s->sht_hum_x10 / 10.0);
    }
    if (s->distance_valid) {
        console_printf("ULT_distance:%.1f cm\r\n",
                       (double)s->distance_mm / 10.0);
    }
    console_printf("ADC_PA0:%.3f V\r\n", (double)s->adc_mv[0] / 1000.0);
    console_printf("ADC_PA1:%.3f V\r\n", (double)s->adc_mv[1] / 1000.0);
    console_printf("ADC_PA4:%.3f V\r\n", (double)s->adc_mv[2] / 1000.0);
    console_printf("HX711_Weight:%d g\r\n", (int)s->weight_g);
}

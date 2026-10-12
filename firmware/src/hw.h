/*
 * hw.h - camada de hardware do STM32L072 (bare-metal, apenas CMSIS).
 *
 * Tudo o que toca registradores fica aqui; o restante do firmware (core/, e os
 * modulos at_cmds/sensors/lorawan) usa esta interface.
 */
#ifndef HW_H
#define HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"

/* ------------------------------------------------------------------------ */
/* Sistema                                                                   */
/* ------------------------------------------------------------------------ */
void hw_init(void); /* habilita clocks e o timebase de microssegundos */
uint32_t hw_millis(void);
uint32_t hw_micros(void);
void hw_delay_ms(uint32_t ms);
void hw_delay_us(uint32_t us);
void hw_system_reset(void);

/* ------------------------------------------------------------------------ */
/* GPIO                                                                      */
/* ------------------------------------------------------------------------ */
enum { HW_PUPD_NONE = 0, HW_PUPD_UP = 1, HW_PUPD_DOWN = 2 };

void hw_gpio_cfg_output(GPIO_TypeDef *port, uint8_t pin, bool level);
void hw_gpio_cfg_input(GPIO_TypeDef *port, uint8_t pin, uint8_t pupd);
void hw_gpio_cfg_af(GPIO_TypeDef *port, uint8_t pin, uint8_t af, uint8_t pupd);
void hw_gpio_cfg_analog(GPIO_TypeDef *port, uint8_t pin);
void hw_gpio_set(GPIO_TypeDef *port, uint8_t pin);
void hw_gpio_clr(GPIO_TypeDef *port, uint8_t pin);
void hw_gpio_write(GPIO_TypeDef *port, uint8_t pin, bool level);
void hw_gpio_toggle(GPIO_TypeDef *port, uint8_t pin);
bool hw_gpio_read(GPIO_TypeDef *port, uint8_t pin);

void hw_led(bool on);
void hw_led_blink(unsigned times);
void hw_5v(bool on);

/* ------------------------------------------------------------------------ */
/* Console UART (AT)                                                         */
/* ------------------------------------------------------------------------ */
void hw_console_init(void);
bool hw_console_rx_pop(char *c);
void hw_console_tx(char c);
void hw_console_write(const char *s);

/* USART1 - usada pelo TF-Luna/TF-Mini (MOD2) */
void hw_tf_uart_init(uint32_t baud);
bool hw_tf_uart_rx_pop(uint8_t *b);
void hw_tf_uart_deinit(void);

/* ------------------------------------------------------------------------ */
/* SPI (radio SX1276)                                                        */
/* ------------------------------------------------------------------------ */
void hw_spi_init(void);
uint8_t hw_spi_xfer(uint8_t out);

/* ------------------------------------------------------------------------ */
/* I2C (sensores)                                                            */
/* ------------------------------------------------------------------------ */
void hw_i2c_init(void);
bool hw_i2c_write(uint8_t addr7, const uint8_t *data, size_t len);
bool hw_i2c_read(uint8_t addr7, uint8_t *data, size_t len);
bool hw_i2c_probe(uint8_t addr7);

/* ------------------------------------------------------------------------ */
/* ADC                                                                       */
/* ------------------------------------------------------------------------ */
void hw_adc_init(void);
uint16_t hw_adc_read_raw(uint8_t channel);
uint32_t hw_adc_vdda_mv(void);
uint32_t hw_adc_pin_mv(uint8_t channel);
int16_t hw_adc_temp_x10(void);

/* ------------------------------------------------------------------------ */
/* 1-Wire / DS18B20                                                          */
/* ------------------------------------------------------------------------ */
typedef struct {
    GPIO_TypeDef *port;
    uint8_t pin;
} ow_pin_t;

void ow_init(const ow_pin_t *p);
bool ow_reset(const ow_pin_t *p);
void ow_write_byte(const ow_pin_t *p, uint8_t v);
uint8_t ow_read_byte(const ow_pin_t *p);
/* Le a temperatura: pede conversao, aguarda 750 ms e le o scratchpad. */
bool ow_read_temp_x10(const ow_pin_t *p, int16_t *x10);

/* ------------------------------------------------------------------------ */
/* EEPROM de dados                                                           */
/* ------------------------------------------------------------------------ */
bool hw_eeprom_read(uint16_t offset, void *dst, size_t len);
bool hw_eeprom_write(uint16_t offset, const void *src, size_t len);

/* ------------------------------------------------------------------------ */
/* RTC + STOP mode                                                           */
/* ------------------------------------------------------------------------ */
void hw_rtc_init(void);
/* Entra em STOP e acorda por RTC (granularidade de 1 s) ou por EXTI. */
void hw_sleep_seconds(uint32_t seconds);
bool hw_woke_from_stop(void);
void hw_woke_from_stop_clear(void);

/* ------------------------------------------------------------------------ */
/* EXTI - contagem de interrupcoes (MOD6)                                    */
/* ------------------------------------------------------------------------ */
enum { HW_INT1 = 0, HW_INT2 = 1, HW_INT3 = 2 };
void hw_exti_init(void);
void hw_exti_config(uint8_t idx, uint8_t mode); /* 0 off, 1 ambos, 2 descida, 3 subida */
uint32_t hw_exti_count(uint8_t idx);
uint32_t hw_exti_take_count(uint8_t idx);
bool hw_exti_seen_level(uint8_t idx);

/* ------------------------------------------------------------------------ */
/* Watchdog (nao e' habilitado por default - ver limpeza no README)          */
/* ------------------------------------------------------------------------ */
void hw_iwdg_init(uint32_t timeout_ms);
void hw_iwdg_feed(void);

#endif /* HW_H */

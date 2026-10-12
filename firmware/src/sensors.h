/*
 * sensors.h - aquisicao dos sensores dos modos MOD1..MOD6.
 *
 * Mapa de sensores x modo (manual 2.3):
 *   MOD1 I2C      : bateria, DS18B20 (PB3), ADC (PA0), digital in, SHT2x/SHT3x/BH1750
 *   MOD2 distancia: idem + LIDAR-Lite v3HP / ultrassom / TF-Luna
 *   MOD3 3ADC     : PA0, PA1, PA4 + I2C + bateria
 *   MOD4 3DS18B20 : PB3, PA9, PA10
 *   MOD5 peso     : HX711
 *   MOD6 contagem : contador de interrupcoes
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "payload.h"

void sensors_init(void);
/* Liga/desliga o +5V dos sensores externos (PB5) e o dreno comutado (PB4). */
void sensors_power(bool on);
/* Preenche a amostra conforme cfg->mode. Retorna false se nada foi lido. */
bool sensors_read(const config_t *cfg, sample_t *s);

uint16_t sensors_battery_mv(void);
bool sensors_sht_read(int16_t *t_x10, uint16_t *rh_x10);
bool sensors_bh1750_read(uint16_t *lux);
bool sensors_distance_read(uint16_t *mm, uint16_t *strength, const char **kind);
int32_t sensors_hx711_read(const config_t *cfg);
/* Valor bruto de 24 bits do HX711 (usado pelo AT+WEIGRE). */
int32_t sensors_hx711_raw(void);
void sensors_int_apply(const config_t *cfg);

#endif /* SENSORS_H */

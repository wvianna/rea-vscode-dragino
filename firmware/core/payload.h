/*
 * payload.h - montagem do payload de uplink (MOD1..MOD6).
 *
 * Layouts copiados do User Manual v1.7.4, secao 2.3 ("Working Mode & Uplink
 * Payload") e 2.4 ("Payload Explanation"):
 *
 *   MOD1 (11 B): BAT(2) Temp(2) ADC(2) DigIn&Int(1) SHT_temp(2) SHT_hum(2)
 *   MOD2 (11 B): BAT(2) Temp(2) ADC(2) DigIn&Int(1) Dist(2) Reserved(2)
 *   MOD2 TF-Luna (11 B): BAT(2) Temp(2) DigIn&Int(1) ADC(2) Dist(2) Strength(2)
 *   MOD3 (12 B): ADC1(2) ADC2(2) ADC3(2) DigIn&Int(1) SHT_temp(2) SHT_hum(2) BAT(1)
 *   MOD4 (11 B): BAT(2) T1(2) ADC(2) DigIn&Int(1) T2(2) T3(2)
 *   MOD5 (11 B): BAT(2) Temp(2) ADC(2) DigIn&Int(1) Weight(2) Reserved(2)
 *   MOD6 (11 B): BAT(2) Temp(2) ADC(2) DigIn&Int(1) Count(4)
 *
 * Regras de escala (manual 2.4):
 *   - bateria e ADC em mV (ex.: 0x0B45 = 2885 mV);
 *   - temperatura em decimos de grau com sinal (0x0105 -> 26.1 C;
 *     0xFF3F -> -19.3 C);
 *   - digital in: bit 1 do byte = estado de PA12.
 *
 * Portatil (sem headers de MCU).
 */
#ifndef CORE_PAYLOAD_H
#define CORE_PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tamanho maximo de payload do firmware original (MOD3 = 12 bytes). */
#define PAYLOAD_MAX 12

/* Variante do sensor de distancia no MOD2 */
enum {
    MOD2_SENSOR_ULTRASONIC_LIDAR = 0, /* distancia(2) + reservado(2) */
    MOD2_SENSOR_TF_SERIES = 1         /* distancia(2) + intensidade(2) */
};

/* Estado do byte de "Digital in & Digital Interrupt" (1 byte). */
#define DIN_PA12 0x01u   /* bit 1 (valor 1) = PA12 alto   */
#define DIN_INT1 0x02u   /* bit 2 = interrupcao em PB14   */
#define DIN_INT2 0x04u   /* bit 3 = interrupcao em PB15   */
#define DIN_INT3 0x08u   /* bit 4 = interrupcao em PA4    */
#define DIN_COUNT 0x10u  /* bit 5 = houve contagem no periodo */

typedef struct {
    uint16_t battery_mv;
    bool temp_valid[3];    /* T1 (PB3/PB4), T2 (PA9), T3 (PA10) */
    int16_t temp_x10[3];
    uint16_t adc_mv[3];    /* PA0, PA1, PA4 */
    uint8_t din;           /* mascara DIN_* */
    bool sht_valid;
    int16_t sht_temp_x10;
    uint16_t sht_hum_x10;
    bool distance_valid;
    uint16_t distance_mm;  /* mm; convertido para cm no payload */
    uint16_t distance_strength;
    int32_t weight_g;
    uint32_t count;        /* MOD6 */
} sample_t;

/* Monta o payload no modo pedido. Retorna o numero de bytes escritos ou 0 se
 * o modo for invalido / o buffer pequeno. `mod2_tf` seleciona o layout do MOD2. */
size_t payload_build(uint8_t mode, const sample_t *s, bool mod2_tf, uint8_t *out,
                     size_t max);

/* Le o payload de volta (usado nos testes de host e no decoder). */
bool payload_parse(uint8_t mode, bool mod2_tf, const uint8_t *in, size_t len,
                   sample_t *s);

/* Formatadores usados pelo console e pelos testes. */
int16_t payload_c_to_x10(float celsius);
float payload_x10_to_c(int16_t x10);

#endif /* CORE_PAYLOAD_H */

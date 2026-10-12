#include "payload.h"

#include <string.h>

int16_t payload_c_to_x10(float celsius)
{
    /* arredonda para o decimo mais proximo; evita depender de libm */
    float v = (celsius >= 0.0f) ? (celsius * 10.0f + 0.5f)
                                : (celsius * 10.0f - 0.5f);
    return (int16_t)v;
}

float payload_x10_to_c(int16_t x10)
{
    return (float)x10 / 10.0f;
}

static void put16(uint8_t *out, size_t *pos, uint16_t v)
{
    out[(*pos)++] = (uint8_t)(v & 0xFFu);
    out[(*pos)++] = (uint8_t)(v >> 8);
}

static uint16_t get16(const uint8_t *in, size_t *pos)
{
    uint16_t v = (uint16_t)(in[*pos] | (in[*pos + 1] << 8));
    *pos += 2;
    return v;
}

static void put32(uint8_t *out, size_t *pos, uint32_t v)
{
    put16(out, pos, (uint16_t)(v & 0xFFFFu));
    put16(out, pos, (uint16_t)(v >> 16));
}

static uint32_t get32(const uint8_t *in, size_t *pos)
{
    uint32_t lo = get16(in, pos);
    uint32_t hi = get16(in, pos);
    return lo | (hi << 16);
}

/* ------------------------------------------------------------------------- */

size_t payload_build(uint8_t mode, const sample_t *s, bool mod2_tf, uint8_t *out,
                     size_t max)
{
    size_t pos = 0;

    if (mode == 1 || mode == 4 || mode == 5 || mode == 6) {
        if (max < 11) {
            return 0;
        }
    } else if (mode == 2) {
        if (max < 11) {
            return 0;
        }
    } else if (mode == 3) {
        if (max < 12) {
            return 0;
        }
    } else {
        return 0;
    }

    switch (mode) {
    case 1: /* MOD1: I2C - BAT, T1, ADC, DIN/INT, SHT temp, SHT hum */
        put16(out, &pos, s->battery_mv);
        put16(out, &pos, (uint16_t)s->temp_x10[0]);
        put16(out, &pos, s->adc_mv[0]);
        out[pos++] = s->din;
        put16(out, &pos, (uint16_t)s->sht_temp_x10);
        put16(out, &pos, s->sht_hum_x10);
        break;

    case 2: /* MOD2: distancia (duas variantes de sensor) */
        if (!mod2_tf) {
            put16(out, &pos, s->battery_mv);
            put16(out, &pos, (uint16_t)s->temp_x10[0]);
            put16(out, &pos, s->adc_mv[0]);
            out[pos++] = s->din;
            put16(out, &pos, (uint16_t)(s->distance_mm / 10u)); /* cm */
            put16(out, &pos, 0);                               /* reservado */
        } else {
            put16(out, &pos, s->battery_mv);
            put16(out, &pos, (uint16_t)s->temp_x10[0]);
            out[pos++] = s->din;
            put16(out, &pos, s->adc_mv[0]);
            put16(out, &pos, (uint16_t)(s->distance_mm / 10u));
            put16(out, &pos, s->distance_strength);
        }
        break;

    case 3: /* MOD3: 3x ADC + I2C, BAT no ultimo byte (1 byte). O manual nao
             * define a escala deste byte; adotamos 100 mV/LSB (0,1 V), que
             * cobre 0..25,5 V sem estourar. Ver firmware/README.md. */
        put16(out, &pos, s->adc_mv[0]);
        put16(out, &pos, s->adc_mv[1]);
        put16(out, &pos, s->adc_mv[2]);
        out[pos++] = s->din;
        put16(out, &pos, (uint16_t)s->sht_temp_x10);
        put16(out, &pos, s->sht_hum_x10);
        out[pos++] = (uint8_t)(s->battery_mv / 100u);
        break;

    case 4: /* MOD4: 3x DS18B20 */
        put16(out, &pos, s->battery_mv);
        put16(out, &pos, (uint16_t)s->temp_x10[0]);
        put16(out, &pos, s->adc_mv[0]);
        out[pos++] = s->din;
        put16(out, &pos, (uint16_t)s->temp_x10[1]);
        put16(out, &pos, (uint16_t)s->temp_x10[2]);
        break;

    case 5: /* MOD5: HX711 */
        put16(out, &pos, s->battery_mv);
        put16(out, &pos, (uint16_t)s->temp_x10[0]);
        put16(out, &pos, s->adc_mv[0]);
        out[pos++] = s->din;
        put16(out, &pos, (uint16_t)(s->weight_g < 0 ? 0 : s->weight_g));
        put16(out, &pos, 0);
        break;

    case 6: /* MOD6: contagem */
        put16(out, &pos, s->battery_mv);
        put16(out, &pos, (uint16_t)s->temp_x10[0]);
        put16(out, &pos, s->adc_mv[0]);
        out[pos++] = s->din;
        put32(out, &pos, s->count);
        break;

    default:
        return 0;
    }

    return pos;
}

bool payload_parse(uint8_t mode, bool mod2_tf, const uint8_t *in, size_t len,
                   sample_t *s)
{
    size_t pos = 0;
    size_t need = (mode == 3) ? 12u : 11u;

    if (mode < 1 || mode > 6 || len < need) {
        return false;
    }
    memset(s, 0, sizeof(*s));

    switch (mode) {
    case 1:
        s->battery_mv = get16(in, &pos);
        s->temp_x10[0] = (int16_t)get16(in, &pos);
        s->temp_valid[0] = true;
        s->adc_mv[0] = get16(in, &pos);
        s->din = in[pos++];
        s->sht_temp_x10 = (int16_t)get16(in, &pos);
        s->sht_hum_x10 = get16(in, &pos);
        s->sht_valid = true;
        break;
    case 2:
        s->battery_mv = get16(in, &pos);
        s->temp_x10[0] = (int16_t)get16(in, &pos);
        s->temp_valid[0] = true;
        if (!mod2_tf) {
            s->adc_mv[0] = get16(in, &pos);
            s->din = in[pos++];
        } else {
            s->din = in[pos++];
            s->adc_mv[0] = get16(in, &pos);
        }
        s->distance_mm = (uint16_t)(get16(in, &pos) * 10u);
        s->distance_strength = get16(in, &pos);
        s->distance_valid = true;
        break;
    case 3:
        s->adc_mv[0] = get16(in, &pos);
        s->adc_mv[1] = get16(in, &pos);
        s->adc_mv[2] = get16(in, &pos);
        s->din = in[pos++];
        s->sht_temp_x10 = (int16_t)get16(in, &pos);
        s->sht_hum_x10 = get16(in, &pos);
        s->sht_valid = true;
        s->battery_mv = (uint16_t)(in[pos++] * 100u);
        break;
    case 4:
        s->battery_mv = get16(in, &pos);
        s->temp_x10[0] = (int16_t)get16(in, &pos);
        s->adc_mv[0] = get16(in, &pos);
        s->din = in[pos++];
        s->temp_x10[1] = (int16_t)get16(in, &pos);
        s->temp_x10[2] = (int16_t)get16(in, &pos);
        s->temp_valid[0] = s->temp_valid[1] = s->temp_valid[2] = true;
        break;
    case 5:
        s->battery_mv = get16(in, &pos);
        s->temp_x10[0] = (int16_t)get16(in, &pos);
        s->adc_mv[0] = get16(in, &pos);
        s->din = in[pos++];
        s->weight_g = (int32_t)get16(in, &pos);
        (void)get16(in, &pos);
        break;
    case 6:
        s->battery_mv = get16(in, &pos);
        s->temp_x10[0] = (int16_t)get16(in, &pos);
        s->adc_mv[0] = get16(in, &pos);
        s->din = in[pos++];
        s->count = get32(in, &pos);
        break;
    default:
        return false;
    }
    return pos == need;
}

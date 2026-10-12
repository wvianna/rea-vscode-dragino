#include "sensors.h"

#include <string.h>

#include "board.h"
#include "console.h"
#include "hw.h"

/* ------------------------------------------------------------------------ */
/* I2C                                                                       */
/* ------------------------------------------------------------------------ */

/* ACK do sensor detectado; as strings de log sao as do firmware original
 * (0x080019d2 "Use Sensor is STH2x", 0x08001a22 "No I2C device detected"...). */
static enum { SHT_NONE = 0, SHT_2X, SHT_3X, SHT_BH1750 } g_sht_kind;

static void i2c_detect(void)
{
    if (hw_i2c_probe(BOARD_SHT2X_I2C_ADDR)) {
        g_sht_kind = SHT_2X;
        console_puts("Use Sensor is STH2x\r\n");
    } else if (hw_i2c_probe(BOARD_SHT3X_I2C_ADDR)) {
        g_sht_kind = SHT_3X;
        console_puts("Use Sensor is STH3x\r\n");
    } else if (hw_i2c_probe(BOARD_BH1750_I2C_ADDR)) {
        g_sht_kind = SHT_BH1750;
        console_puts("Use Sensor is BH1750\r\n");
    } else {
        g_sht_kind = SHT_NONE;
        console_puts("No I2C device detected\r\n");
    }
}

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool sensors_sht_read(int16_t *t_x10, uint16_t *rh_x10)
{
    uint8_t cmd[2];
    uint8_t buf[6];

    if (g_sht_kind == SHT_2X) {
        /* SHT2x: comando sem clock stretching; le 3 bytes por grandeza */
        cmd[0] = 0xF3u; /* medida de temperatura */
        if (!hw_i2c_write(BOARD_SHT2X_I2C_ADDR, cmd, 1)) {
            return false;
        }
        hw_delay_ms(85);
        if (!hw_i2c_read(BOARD_SHT2X_I2C_ADDR, buf, 3)) {
            return false;
        }
        uint16_t raw_t = (uint16_t)((buf[0] << 8) | buf[1]);
        int32_t t = -4685 + (17572 * (int32_t)raw_t) / 65536; /* x100 */

        cmd[0] = 0xF5u; /* medida de umidade relativa */
        if (!hw_i2c_write(BOARD_SHT2X_I2C_ADDR, cmd, 1)) {
            return false;
        }
        hw_delay_ms(30);
        if (!hw_i2c_read(BOARD_SHT2X_I2C_ADDR, buf, 3)) {
            return false;
        }
        uint16_t raw_h = (uint16_t)((buf[0] << 8) | buf[1]);
        int32_t rh = -600 + (12500 * (int32_t)raw_h) / 65536; /* x100 */

        *t_x10 = (int16_t)(t / 10);
        *rh_x10 = (uint16_t)((rh < 0 ? 0 : rh) / 10);
        return true;
    }

    if (g_sht_kind == SHT_3X) {
        cmd[0] = 0x24u; /* single shot, high repeatability, sem clock stretch */
        cmd[1] = 0x00u;
        if (!hw_i2c_write(BOARD_SHT3X_I2C_ADDR, cmd, 2)) {
            return false;
        }
        hw_delay_ms(20);
        if (!hw_i2c_read(BOARD_SHT3X_I2C_ADDR, buf, 6)) {
            return false;
        }
        if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) {
            return false;
        }
        uint16_t raw_t = (uint16_t)((buf[0] << 8) | buf[1]);
        uint16_t raw_h = (uint16_t)((buf[3] << 8) | buf[4]);
        int32_t t = -4500 + (17500 * (int32_t)raw_t) / 65535; /* x100 */
        int32_t rh = (10000 * (int32_t)raw_h) / 65535;        /* x100 */
        *t_x10 = (int16_t)(t / 10);
        *rh_x10 = (uint16_t)(rh / 10);
        return true;
    }
    return false;
}

bool sensors_bh1750_read(uint16_t *lux)
{
    uint8_t buf[2];
    uint8_t cmd = 0x20u; /* one-time H-res mode */

    if (g_sht_kind != SHT_BH1750) {
        return false;
    }
    if (!hw_i2c_write(BOARD_BH1750_I2C_ADDR, &cmd, 1)) {
        return false;
    }
    hw_delay_ms(180);
    if (!hw_i2c_read(BOARD_BH1750_I2C_ADDR, buf, 2)) {
        return false;
    }
    /* lux = raw / 1,2 */
    *lux = (uint16_t)((((uint32_t)buf[0] << 8) | buf[1]) * 10u / 12u);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Distancia (MOD2)                                                          */
/* ------------------------------------------------------------------------ */

static bool lidar_read(uint16_t *mm)
{
    uint8_t cmd[2] = {0x00u, 0x04u}; /* ACQ_COMMAND: single shot */
    uint8_t dist[2];
    uint8_t status;

    if (!hw_i2c_probe(BOARD_LIDAR_I2C_ADDR)) {
        return false;
    }
    if (!hw_i2c_write(BOARD_LIDAR_I2C_ADDR, cmd, 2)) {
        return false;
    }
    for (int i = 0; i < 20; i++) {
        hw_delay_ms(3);
        uint8_t reg = 0x01u; /* STATUS */
        if (!hw_i2c_write(BOARD_LIDAR_I2C_ADDR, &reg, 1)) {
            return false;
        }
        if (!hw_i2c_read(BOARD_LIDAR_I2C_ADDR, &status, 1)) {
            return false;
        }
        if (status & 0x01u) { /* bus busy=0 -> pronto */
            break;
        }
    }
    uint8_t reg = 0x8Fu; /* DISTANCE (big-endian) */
    if (!hw_i2c_write(BOARD_LIDAR_I2C_ADDR, &reg, 1) ||
        !hw_i2c_read(BOARD_LIDAR_I2C_ADDR, dist, 2)) {
        return false;
    }
    *mm = (uint16_t)((dist[0] << 8) | dist[1]) * 10u; /* cm -> mm */
    return true;
}

static bool tf_luna_read(uint16_t *mm, uint16_t *strength)
{
    uint8_t frame[9];
    uint8_t checksum = 0;
    uint32_t start = hw_millis();

    hw_tf_uart_init(BOARD_TF_BAUD);
    /* procura a sequencia de sincronismo 0x59 0x59 (protocolo TF-Luna) */
    while ((uint32_t)(hw_millis() - start) < 120u) {
        uint8_t b;
        if (!hw_tf_uart_rx_pop(&b)) {
            continue;
        }
        if (b != 0x59u) {
            continue;
        }
        frame[0] = b;
        bool ok = true;
        for (int i = 1; i < 9; i++) {
            uint32_t t0 = hw_millis();
            while (!hw_tf_uart_rx_pop(&frame[i])) {
                if ((uint32_t)(hw_millis() - t0) > 20u) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                break;
            }
        }
        if (!ok || frame[1] != 0x59u) {
            continue;
        }
        for (int i = 0; i < 8; i++) {
            checksum = (uint8_t)(checksum + frame[i]);
        }
        if (checksum != frame[8]) {
            continue;
        }
        uint16_t raw = (uint16_t)(frame[2] | (frame[3] << 8));
        *mm = raw * 10u; /* cm -> mm */
        *strength = (uint16_t)(frame[4] | (frame[5] << 8));
        hw_tf_uart_deinit();
        return true;
    }
    hw_tf_uart_deinit();
    return false;
}

static bool ultrasonic_read(uint16_t *mm)
{
    /* Pulsos de 10 us no TRIG e largura do eco em us / 58 = cm */
    hw_gpio_cfg_output(BOARD_ULT_TRIG_PORT, BOARD_ULT_TRIG_PIN, false);
    hw_gpio_cfg_input(BOARD_ULT_ECHO_PORT, BOARD_ULT_ECHO_PIN, HW_PUPD_NONE);

    hw_gpio_set(BOARD_ULT_TRIG_PORT, BOARD_ULT_TRIG_PIN);
    hw_delay_us(10);
    hw_gpio_clr(BOARD_ULT_TRIG_PORT, BOARD_ULT_TRIG_PIN);

    uint32_t timeout = 30000u;
    while (!hw_gpio_read(BOARD_ULT_ECHO_PORT, BOARD_ULT_ECHO_PIN)) {
        if (--timeout == 0) {
            return false;
        }
        hw_delay_us(1);
    }
    uint32_t t0 = hw_micros();
    timeout = 30000u;
    while (hw_gpio_read(BOARD_ULT_ECHO_PORT, BOARD_ULT_ECHO_PIN)) {
        if (--timeout == 0) {
            return false;
        }
        hw_delay_us(1);
    }
    uint32_t width_us = (uint32_t)((uint16_t)hw_micros() - (uint16_t)t0);
    if (width_us < 100u) {
        return false;
    }
    *mm = (uint16_t)((width_us * 10u) / 58u); /* cm -> mm */
    return true;
}

bool sensors_distance_read(uint16_t *mm, uint16_t *strength, const char **kind)
{
    if (lidar_read(mm)) {
        *kind = "LIDAR_Lite_v3HP";
        console_puts("Use Sensor is LIDAR_Lite_v3HP\r\n");
        return true;
    }
    if (tf_luna_read(mm, strength)) {
        *kind = "TF-series sensor";
        console_puts("Use Sensor is TF-series sensor\r\n");
        return true;
    }
    if (ultrasonic_read(mm)) {
        *kind = "ultrasonic distance measurement";
        console_puts("Use Sensor is ultrasonic distance measurement\r\n");
        return true;
    }
    *kind = "none";
    console_puts("No distance measurement device detected\r\n");
    return false;
}

/* ------------------------------------------------------------------------ */
/* HX711 (MOD5)                                                              */
/* ------------------------------------------------------------------------ */

int32_t sensors_hx711_raw(void)
{
    GPIO_TypeDef *sck = BOARD_HX711_SCK_PORT;
    GPIO_TypeDef *dout = BOARD_HX711_DOUT_PORT;
    int32_t raw = 0;

    hw_gpio_cfg_output(sck, BOARD_HX711_SCK_PIN, false);
    hw_gpio_cfg_input(dout, BOARD_HX711_DOUT_PIN, HW_PUPD_NONE);

    uint32_t guard = 200000u;
    while (hw_gpio_read(dout, BOARD_HX711_DOUT_PIN)) {
        if (--guard == 0) {
            return 0;
        }
    }
    for (int i = 0; i < 24; i++) {
        hw_gpio_set(sck, BOARD_HX711_SCK_PIN);
        hw_delay_us(2);
        raw = (raw << 1) | (hw_gpio_read(dout, BOARD_HX711_DOUT_PIN) ? 1 : 0);
        hw_gpio_clr(sck, BOARD_HX711_SCK_PIN);
        hw_delay_us(2);
    }
    hw_gpio_set(sck, BOARD_HX711_SCK_PIN);
    hw_delay_us(2);
    hw_gpio_clr(sck, BOARD_HX711_SCK_PIN);
    hw_delay_us(2);

    if (raw & 0x800000) {
        raw -= 0x1000000;
    }
    return raw;
}

int32_t sensors_hx711_read(const config_t *cfg)
{
    int32_t raw = sensors_hx711_raw();

    /* Peso = (bruto - zero) * 100 / weigap_x10 (weigap_x10 = fator * 10) */
    int32_t delta = raw - cfg->weigre_zero;
    if (cfg->weigap_x10 == 0) {
        return 0;
    }
    return (int32_t)(((int64_t)delta * 100) / (int32_t)cfg->weigap_x10);
}

/* ------------------------------------------------------------------------ */
/* Montagem da amostra                                                       */
/* ------------------------------------------------------------------------ */

static uint8_t read_din(const config_t *cfg)
{
    uint8_t din = 0;
    if (hw_gpio_read(BOARD_DIN_PORT, BOARD_DIN_PIN)) {
        din |= DIN_PA12;
    }
    if (cfg->intmod[0] != INTMOD_OFF && hw_exti_seen_level(HW_INT1)) {
        din |= DIN_INT1;
    }
    if (cfg->intmod[1] != INTMOD_OFF && hw_exti_seen_level(HW_INT2)) {
        din |= DIN_INT2;
    }
    if (cfg->intmod[2] != INTMOD_OFF && hw_exti_seen_level(HW_INT3)) {
        din |= DIN_INT3;
    }
    if (hw_exti_count(HW_INT1) || hw_exti_count(HW_INT2) || hw_exti_count(HW_INT3)) {
        din |= DIN_COUNT;
    }
    return din;
}

void sensors_int_apply(const config_t *cfg)
{
    hw_exti_config(HW_INT1, cfg->intmod[0]);
    hw_exti_config(HW_INT2, cfg->intmod[1]);
    hw_exti_config(HW_INT3, cfg->intmod[2]);
}

void sensors_init(void)
{
    /* +5V desligado por default (PB5 alto) e LED apagado */
    hw_gpio_cfg_output(BOARD_5V_PORT, BOARD_5V_PIN, true);
    hw_gpio_cfg_output(BOARD_LED_PORT, BOARD_LED_PIN, false);
    /* dreno comutado dos sensores resistivos: entra como entrada (alta Z) */
    hw_gpio_cfg_input(BOARD_SENSGND_PORT, BOARD_SENSGND_PIN, HW_PUPD_NONE);

    const ow_pin_t ow1 = {BOARD_OW1_PORT, BOARD_OW1_PIN};
    const ow_pin_t ow2 = {BOARD_OW2_PORT, BOARD_OW2_PIN};
    const ow_pin_t ow3 = {BOARD_OW3_PORT, BOARD_OW3_PIN};
    ow_init(&ow1);
    ow_init(&ow2);
    ow_init(&ow3);

    hw_gpio_cfg_input(BOARD_DIN_PORT, BOARD_DIN_PIN, HW_PUPD_NONE);
    hw_exti_init();
    hw_adc_init();
    hw_i2c_init();
    i2c_detect();
}

void sensors_power(bool on)
{
    hw_5v(on);
    if (on) {
        /* o dreno do sensor resistivo (PB4) vai a 0 V durante a amostragem
         * (manual 2.4.4) e volta a alta impedancia depois */
        hw_gpio_cfg_output(BOARD_SENSGND_PORT, BOARD_SENSGND_PIN, false);
    } else {
        hw_gpio_cfg_input(BOARD_SENSGND_PORT, BOARD_SENSGND_PIN, HW_PUPD_NONE);
    }
}

uint16_t sensors_battery_mv(void)
{
    /* A tensao de alimentacao (VDDA) e' a propria bateria atras do diodo D1;
     * ela e' obtida com a referencia interna VREFINT (canal 17). */
    return (uint16_t)hw_adc_vdda_mv();
}

bool sensors_read(const config_t *cfg, sample_t *s)
{
    const ow_pin_t ow1 = {BOARD_OW1_PORT, BOARD_OW1_PIN};
    const ow_pin_t ow2 = {BOARD_OW2_PORT, BOARD_OW2_PIN};
    const ow_pin_t ow3 = {BOARD_OW3_PORT, BOARD_OW3_PIN};

    memset(s, 0, sizeof(*s));
    s->battery_mv = sensors_battery_mv();
    s->din = read_din(cfg);
    s->count = hw_exti_count(HW_INT1) + hw_exti_count(HW_INT2) +
               hw_exti_count(HW_INT3);

    switch (cfg->mode) {
    case MOD_IIC:
        s->temp_valid[0] = ow_read_temp_x10(&ow1, &s->temp_x10[0]);
        s->adc_mv[0] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH0);
        if (g_sht_kind == SHT_BH1750) {
            uint16_t lux = 0;
            if (sensors_bh1750_read(&lux)) {
                s->distance_strength = lux; /* reaproveitado em MOD1 */
            }
        } else {
            s->sht_valid = sensors_sht_read(&s->sht_temp_x10, &s->sht_hum_x10);
        }
        return true;

    case MOD_DISTANCE: {
        const char *kind = NULL;
        s->temp_valid[0] = ow_read_temp_x10(&ow1, &s->temp_x10[0]);
        s->adc_mv[0] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH0);
        s->distance_valid = sensors_distance_read(&s->distance_mm,
                                                  &s->distance_strength, &kind);
        return true;
    }

    case MOD_3ADC:
        s->adc_mv[0] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH0);
        s->adc_mv[1] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH1);
        s->adc_mv[2] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH2);
        s->sht_valid = sensors_sht_read(&s->sht_temp_x10, &s->sht_hum_x10);
        return true;

    case MOD_3DS18B20:
        s->temp_valid[0] = ow_read_temp_x10(&ow1, &s->temp_x10[0]);
        s->adc_mv[0] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH0);
        s->temp_valid[1] = ow_read_temp_x10(&ow2, &s->temp_x10[1]);
        s->temp_valid[2] = ow_read_temp_x10(&ow3, &s->temp_x10[2]);
        return true;

    case MOD_WEIGHT:
        s->temp_valid[0] = ow_read_temp_x10(&ow1, &s->temp_x10[0]);
        s->adc_mv[0] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH0);
        s->weight_g = sensors_hx711_read(cfg);
        return true;

    case MOD_COUNT:
        s->temp_valid[0] = ow_read_temp_x10(&ow1, &s->temp_x10[0]);
        s->adc_mv[0] = (uint16_t)hw_adc_pin_mv(BOARD_ADC_CH0);
        s->count = hw_exti_count(HW_INT1) + hw_exti_count(HW_INT2) +
                   hw_exti_count(HW_INT3);
        return true;

    default:
        return false;
    }
}

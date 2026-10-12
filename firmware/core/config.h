/*
 * config.h - configuracao persistente do no' (equivalente ao conjunto de
 * parametros guardado na EEPROM do LSN50 original).
 *
 * Este arquivo e' PORTATIL: nao inclui nenhum header de MCU, para poder ser
 * compilado no PC pelos testes de host.
 *
 * Base factual: o firmware LSN50_AU915_v1.8.1 referencia as constantes
 * 0x08080000 / 0x08080004 / 0x08080058 / 0x08080060 (data EEPROM do STM32L0),
 * ou seja, os parametros dos AT commands sao persistidos na EEPROM interna.
 */
#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CFG_MAGIC 0x4C53u /* "LS" */
#define CFG_VERSION 1u

/* Tamanhos definidos pelo LoRaWAN 1.0.x */
#define CFG_EUI_LEN 8
#define CFG_KEY_LEN 16
#define CFG_ADDR_LEN 4

/* Identificacao que aparece no banner (substitui o DR-LWS-007 do original) */
#define CFG_IMAGE_VERSION "v0.1.0"
#define CFG_FREQ_BAND "AU915"

/* Modos de trabalho (AT+MOD), iguais aos do firmware original */
enum {
    MOD_IIC = 1,      /* I2C: bateria, DS18B20, ADC, digital in, SHT2x/SHT3x/BH1750 */
    MOD_DISTANCE = 2, /* + distancia (LiDAR/ultrassom) */
    MOD_3ADC = 3,     /* 3x ADC + I2C, payload de 12 bytes */
    MOD_3DS18B20 = 4, /* 3x DS18B20 (PB3, PA9, PA10) */
    MOD_WEIGHT = 5,   /* HX711 */
    MOD_COUNT = 6     /* contagem de interrupcoes */
};

/* Modos de disparo da entrada de interrupcao (AT+INTMOD) */
enum { INTMOD_OFF = 0, INTMOD_BOTH = 1, INTMOD_FALLING = 2, INTMOD_RISING = 3 };

typedef struct {
    uint8_t mode;       /* AT+MOD */
    uint8_t nj_mode;    /* AT+NJM: 0 = ABP, 1 = OTAA */
    uint8_t adr;        /* AT+ADR */
    uint8_t dr;         /* AT+DR */
    uint8_t txp;        /* AT+TXP: 0..5 ou 40..50 (dBm fora da spec) */
    uint8_t dev_class;  /* AT+CLASS: 'A' */
    uint8_t pnm;        /* AT+PNM: 0 privado, 1 publico */
    uint8_t dcs;        /* AT+DCS */
    uint8_t dwellt;     /* AT+DWELLT */
    uint8_t rpl;        /* AT+RPL */
    uint8_t decrypt;    /* AT+DECRYPT */
    uint8_t dismacans;  /* AT+DISMACANS */
    uint8_t disfcntcheck; /* AT+DISFCNTCHECK */
    uint8_t maxnbtrans; /* AT+SETMAXNBTRANS */
    uint8_t ddetect;    /* AT+DDETECT */
    uint8_t rxdatest;   /* AT+RXDATEST */
    uint8_t debug;      /* AT+DEBUG */
    uint8_t port;       /* AT+PORT */
    uint8_t cfm;        /* AT+CFM */
    uint8_t che;        /* AT+CHE */
    uint8_t rx2dr;      /* AT+RX2DR */
    uint8_t rx1wto;     /* AT+RX1WTO */
    uint8_t rx2wto;     /* AT+RX2WTO */
    uint8_t intmod[3];  /* AT+INTMOD1 (PB14), INTMOD2 (PB15), INTMOD3 (PA4) */
    uint8_t conf_status; /* AT+CFS: status do ultimo uplink confirmado */

    uint32_t tdc_ms;      /* AT+TDC - intervalo de uplink */
    uint32_t rjtdc_min;   /* AT+RJTDC - intervalo de rejoin */
    uint32_t fcu;         /* AT+FCU - frame counter uplink */
    uint32_t fcd;         /* AT+FCD - frame counter downlink */
    uint32_t chs_freq_hz; /* AT+CHS - canal unico */
    uint32_t rx2freq_hz;  /* AT+RX2FQ */
    uint32_t setcnt;      /* AT+SETCNT - contador (MODO 6) */

    uint16_t five_vt_ms;  /* AT+5VT - tempo do 5V ligado */
    uint16_t rx1dl_ms;    /* AT+RX1DL */
    uint16_t rx2dl_ms;    /* AT+RX2DL */
    uint16_t jn1dl_ms;    /* AT+JN1DL */
    uint16_t jn2dl_ms;    /* AT+JN2DL */
    uint16_t weigap_x10;  /* AT+WEIGAP: 403.0 -> 4030 */
    int32_t weigre_zero;  /* AT+WEIGRE: offset de zero em unidades do ADC do HX711 */

    uint8_t deveui[CFG_EUI_LEN];
    uint8_t appeui[CFG_EUI_LEN];
    uint8_t appkey[CFG_KEY_LEN];
    uint8_t nwkskey[CFG_KEY_LEN];
    uint8_t appskey[CFG_KEY_LEN];
    uint8_t devaddr[CFG_ADDR_LEN];
    uint8_t nwkid[CFG_ADDR_LEN];
} config_t;

/* Preenche com os defaults de fabrica (equivalente a AT+FDR, mas com as
 * chaves preservadas - ver cfg_factory_keys()). */
void cfg_defaults(config_t *c);

/* Restaura os defaults sem tocar em DEUI/APPEUI/APPKEY (AT+FDR do original
 * diz "Keys Reserve"). */
void cfg_factory_reset(config_t *c);

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) - mesmo tipo de protecao
 * usada para validar o blob de configuracao. */
uint16_t cfg_crc16(const uint8_t *data, size_t len);

/* Codifica/decodifica em little-endian. Retorna o numero de bytes usados
 * (0 em caso de falta de espaco ou buffer invalido). */
size_t cfg_encode(const config_t *c, uint8_t *out, size_t max);
bool cfg_decode(const uint8_t *in, size_t len, config_t *c);

/* Utilitarios de hex usados pelos comandos AT ("AT+DEUI=AA BB ..."). */
bool cfg_hex_to_bytes(const char *text, uint8_t *out, size_t len);
size_t cfg_bytes_to_hex(const uint8_t *in, size_t len, char *out, size_t max,
                        bool upper, char sep);

/* Chave de sessao/endereco derivados do ABP (usado pelo AT+CFG). */
void cfg_set_devaddr(config_t *c, uint32_t addr);
uint32_t cfg_get_devaddr(const config_t *c);

#endif /* CORE_CONFIG_H */

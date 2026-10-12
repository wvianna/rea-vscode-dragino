/*
 * test_core.c - testes de host do nucleo portatil (core/).
 *
 * Rodar:  pio run -e native -t exec
 *
 * Cobre: AES-128/CMAC contra os vetores oficiais (FIPS-197 / RFC 4493),
 * serializacao da configuracao, layouts de payload do manual e o parser AT.
 */
#include <stdio.h>
#include <string.h>

#include "at.h"
#include "config.h"
#include "crypto.h"
#include "payload.h"

static int failures;
static int checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("FALHA %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

static bool bytes_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

static void hex_to_bytes(const char *hex, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void print_hex(const char *label, const uint8_t *b, size_t n)
{
    printf("      %s = ", label);
    for (size_t i = 0; i < n; i++) {
        printf("%02x", b[i]);
    }
    printf("\n");
}

/* ------------------------------------------------------------------------- */

static void test_aes_fips197(void)
{
    uint8_t key[16], pt[16], ct[16], expect[16], back[16];
    hex_to_bytes("000102030405060708090a0b0c0d0e0f", key, 16);
    hex_to_bytes("00112233445566778899aabbccddeeff", pt, 16);
    hex_to_bytes("69c4e0d86a7b0430d8cdb78070b4c55a", expect, 16);

    aes128_ecb_encrypt(key, pt, ct);
    if (!bytes_eq(ct, expect, 16)) {
        print_hex("obtido  ", ct, 16);
        print_hex("esperado", expect, 16);
    }
    CHECK(bytes_eq(ct, expect, 16));

    /* a cifra inversa (usada no Join Accept OTAA e no AT+DECRYPT) precisa
     * devolver o texto claro original */
    aes128_ecb_decrypt(key, expect, back);
    CHECK(bytes_eq(back, pt, 16));
}

static void test_cmac_rfc4493(void)
{
    uint8_t key[16], msg[64], mac[16], expect[16];
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);

    /* Exemplo 1: mensagem vazia */
    hex_to_bytes("bb1d6929e95937287fa37d129b756746", expect, 16);
    aes128_cmac(key, NULL, 0, mac);
    CHECK(bytes_eq(mac, expect, 16));

    /* Exemplo 2: 16 bytes */
    hex_to_bytes("6bc1bee22e409f96e93d7e117393172a", msg, 16);
    hex_to_bytes("070a16b46b4d4144f79bdd9dd04a287c", expect, 16);
    aes128_cmac(key, msg, 16, mac);
    CHECK(bytes_eq(mac, expect, 16));

    /* Exemplo 3: 40 bytes */
    hex_to_bytes("6bc1bee22e409f96e93d7e117393172a"
                 "ae2d8a571e03ac9c9eb76fac45af8e51"
                 "30c81c46a35ce411",
                 msg, 40);
    hex_to_bytes("dfa66747de9ae63030ca32611497c827", expect, 16);
    aes128_cmac(key, msg, 40, mac);
    CHECK(bytes_eq(mac, expect, 16));

    /* Exemplo 4: 64 bytes */
    hex_to_bytes("6bc1bee22e409f96e93d7e117393172a"
                 "ae2d8a571e03ac9c9eb76fac45af8e51"
                 "30c81c46a35ce411e5fbc1191a0a52ef"
                 "f69f2445df4f9b17ad2b417be66c3710",
                 msg, 64);
    hex_to_bytes("51f0bebf7e3b9d92fc49741779363cfe", expect, 16);
    aes128_cmac(key, msg, 64, mac);
    CHECK(bytes_eq(mac, expect, 16));
}

/* ------------------------------------------------------------------------- */

static void test_config_roundtrip(void)
{
    config_t a, b;
    uint8_t buf[256];

    cfg_defaults(&a);
    memcpy(a.deveui, "12345678", 8);
    a.tdc_ms = 60000;
    a.mode = MOD_3DS18B20;
    a.weigap_x10 = 4030;
    a.weigre_zero = -1234;
    a.fcu = 42;
    cfg_set_devaddr(&a, 0xA84041FFu);

    size_t n = cfg_encode(&a, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK(cfg_decode(buf, n, &b));
    CHECK(b.tdc_ms == a.tdc_ms);
    CHECK(b.mode == a.mode);
    CHECK(b.weigap_x10 == a.weigap_x10);
    CHECK(b.weigre_zero == a.weigre_zero);
    CHECK(b.fcu == a.fcu);
    CHECK(cfg_get_devaddr(&b) == 0xA84041FFu);
    CHECK(memcmp(b.deveui, a.deveui, 8) == 0);

    /* CRC invalido precisa ser rejeitado */
    buf[n - 1] ^= 0xFFu;
    CHECK(!cfg_decode(buf, n, &b));

    /* AT+FDR preserva as chaves (doc: "Keys Reserve") */
    config_t f = a;
    cfg_factory_reset(&f);
    CHECK(memcmp(f.deveui, a.deveui, 8) == 0);
    CHECK(memcmp(f.appkey, a.appkey, 16) == 0);
    CHECK(f.tdc_ms == 30000);
    CHECK(f.mode == MOD_IIC);
}

static void test_config_hex(void)
{
    uint8_t out[16];
    char text[64];

    CHECK(cfg_hex_to_bytes("00 44 34 22 33 45 55 55", out, 8));
    CHECK(out[0] == 0x00 && out[1] == 0x44 && out[7] == 0x55);
    CHECK(cfg_hex_to_bytes("0035555 52223555343242342353535", out, 16) ==
          false); /* com espaco extra o tamanho deixa de casar */
    CHECK(!cfg_hex_to_bytes("00 44 34 22 33 45 55", out, 8)); /* curto */
    CHECK(!cfg_hex_to_bytes("00 44 34 22 33 45 55 zz", out, 8)); /* invalido */

    cfg_bytes_to_hex(out, 4, text, sizeof(text), false, 0);
    CHECK(strcmp(text, "00443422") == 0);
}

/* ------------------------------------------------------------------------- */

static void test_payload_modes(void)
{
    sample_t s, back;
    uint8_t buf[PAYLOAD_MAX];
    size_t n;

    memset(&s, 0, sizeof(s));
    s.battery_mv = 2885;      /* manual: 0x0B45 = 2885 mV */
    s.temp_x10[0] = 261;      /* manual: 0x0105 -> 26.1 C */
    s.temp_valid[0] = true;
    s.adc_mv[0] = 543;        /* manual: 0x021F = 543 mV */
    s.din = DIN_PA12 | DIN_INT1;
    s.sht_valid = true;
    s.sht_temp_x10 = 235;
    s.sht_hum_x10 = 541;
    s.distance_mm = 1234;
    s.distance_valid = true;
    s.weight_g = 401;
    s.count = 7;

    /* MOD1: 11 bytes, ordem BAT/Temp/ADC/DIN/SHTtemp/SHThum */
    n = payload_build(1, &s, false, buf, sizeof(buf));
    CHECK(n == 11);
    CHECK(buf[0] == 0x45 && buf[1] == 0x0B);         /* 2885 -> 0x0B45 LE */
    CHECK(buf[2] == 0x05 && buf[3] == 0x01);         /* 26.1 -> 0x0105 */
    CHECK(buf[4] == 0x1F && buf[5] == 0x02);         /* 543  -> 0x021F */
    CHECK(buf[6] == (DIN_PA12 | DIN_INT1));
    CHECK(payload_parse(1, false, buf, n, &back));
    CHECK(back.battery_mv == 2885);
    CHECK(back.temp_x10[0] == 261);
    CHECK(back.sht_hum_x10 == 541);

    /* MOD2 (ultrassom/LiDAR): 11 bytes com distancia em cm */
    n = payload_build(2, &s, false, buf, sizeof(buf));
    CHECK(n == 11);
    CHECK(buf[6] == (DIN_PA12 | DIN_INT1));
    CHECK(buf[7] == (1234 / 10) % 256 && buf[8] == (1234 / 10) / 256);
    CHECK(payload_parse(2, false, buf, n, &back));
    CHECK(back.distance_mm == 1230); /* cm arredondado para baixo */

    /* MOD2 variante TF-Luna: o manual troca a ordem - BAT, Temp, DIN/INT,
     * ADC, distancia, intensidade (bytes 4, 5-6, 7-8, 9-10) */
    n = payload_build(2, &s, true, buf, sizeof(buf));
    CHECK(n == 11);
    CHECK(buf[4] == (DIN_PA12 | DIN_INT1));
    CHECK(buf[5] == 0x1F && buf[6] == 0x02); /* ADC em 5..6 */
    CHECK(payload_parse(2, true, buf, n, &back));
    CHECK(back.adc_mv[0] == 543);

    /* MOD3: 12 bytes, 3x ADC, BAT em 1 byte (100 mV/LSB - inferencia) */
    s.adc_mv[1] = 1000;
    s.adc_mv[2] = 2000;
    n = payload_build(3, &s, false, buf, sizeof(buf));
    CHECK(n == 12);
    CHECK(buf[0] == 0x1F && buf[1] == 0x02);
    CHECK(buf[6] == (DIN_PA12 | DIN_INT1));
    CHECK(buf[11] == (uint8_t)(2885 / 100));
    CHECK(payload_parse(3, false, buf, n, &back));
    CHECK(back.adc_mv[2] == 2000 && back.battery_mv == 2800);

    /* MOD4: 3x DS18B20 */
    s.temp_x10[1] = -193; /* -19.3 C (manual: 0xFF3F) */
    s.temp_x10[2] = 500;
    n = payload_build(4, &s, false, buf, sizeof(buf));
    CHECK(n == 11);
    CHECK(buf[7] == 0x3F && buf[8] == 0xFF);
    CHECK(payload_parse(4, false, buf, n, &back));
    CHECK(back.temp_x10[1] == -193);
    CHECK(back.temp_x10[2] == 500);

    /* MOD5: peso */
    n = payload_build(5, &s, false, buf, sizeof(buf));
    CHECK(n == 11);
    CHECK(payload_parse(5, false, buf, n, &back));
    CHECK(back.weight_g == 401);

    /* MOD6: contador de 4 bytes */
    s.count = 0x12345678u;
    n = payload_build(6, &s, false, buf, sizeof(buf));
    CHECK(n == 11);
    CHECK(payload_parse(6, false, buf, n, &back));
    CHECK(back.count == 0x12345678u);

    /* modo invalido e buffer pequeno */
    CHECK(payload_build(0, &s, false, buf, sizeof(buf)) == 0);
    CHECK(payload_build(1, &s, false, buf, 5) == 0);
}

/* ------------------------------------------------------------------------- */

/* Tabela AT de teste: imita o layout do firmware original (nome, id, help,
 * get, set) com apenas dois comandos. */
static uint16_t g_value;
static bool g_reset_called;

static at_status_t get_value(char *out, size_t max, void *user)
{
    (void)user;
    snprintf(out, max, "%u", (unsigned)g_value);
    return AT_OK;
}

static at_status_t set_value(const char *arg, char *out, size_t max, void *user)
{
    (void)out;
    (void)max;
    (void)user;
    if (arg == NULL) {
        return AT_PARAM_ERROR;
    }
    unsigned v;
    if (sscanf(arg, "%u", &v) != 1 || v > 60000u) {
        return AT_PARAM_ERROR;
    }
    g_value = (uint16_t)v;
    return AT_OK;
}

static at_status_t set_join(const char *arg, char *out, size_t max, void *user)
{
    (void)arg;
    (void)user;
    snprintf(out, max, "AT+JOIN: Join network");
    return AT_OK;
}

static at_status_t get_always_error(char *out, size_t max, void *user)
{
    (void)out;
    (void)max;
    (void)user;
    return AT_NO_NETWORK_JOINED;
}

static const at_cmd_t TEST_TABLE[] = {
    {"Z", 1, "ATZ: Trig a reset of the MCU", NULL, NULL},
    {"+TDC", 4, "AT+TDC: Get or set the application data transmission interval in ms",
     get_value, set_value},
    {"+JOIN", 5, "AT+JOIN: Join network", NULL, set_join},
    {"+NJS", 4, "AT+NJS: Get the join status", get_always_error, NULL},
};

static char g_out[4096];
static size_t g_out_len;

static void capture(const char *text, void *ctx)
{
    (void)ctx;
    size_t n = strlen(text);
    if (g_out_len + n < sizeof(g_out)) {
        memcpy(g_out + g_out_len, text, n);
        g_out_len += n;
        g_out[g_out_len] = '\0';
    }
}

static void reset_cb(void *user)
{
    (void)user;
    g_reset_called = true;
}

static void test_at_engine(void)
{
    at_ctx_t at;
    g_out[0] = '\0';
    g_out_len = 0;
    g_value = 100;
    g_reset_called = false;

    at_init(&at, TEST_TABLE, sizeof(TEST_TABLE) / sizeof(TEST_TABLE[0]), capture,
            NULL);
    at.on_reset = reset_cb;

    /* "AT" puro -> OK */
    CHECK(at_exec(&at, "AT") == AT_OK);
    CHECK(strstr(g_out, "\r\nOK\r\n") != NULL);

    /* leitura: valor + OK (formato do manual 1.1) */
    g_out[0] = '\0';
    g_out_len = 0;
    CHECK(at_exec(&at, "AT+TDC=?") == AT_OK);
    CHECK(strcmp(g_out, "100\r\n\r\nOK\r\n") == 0);

    /* escrita e validacao de faixa */
    g_out[0] = '\0';
    g_out_len = 0;
    g_value = 0;
    CHECK(at_exec(&at, "AT+TDC=60000") == AT_OK);
    CHECK(g_value == 60000);
    CHECK(at_exec(&at, "AT+TDC=70000") == AT_PARAM_ERROR);
    CHECK(g_value == 60000);
    CHECK(at_exec(&at, "AT+TDC=abc") == AT_PARAM_ERROR);

    /* comando desconhecido */
    g_out[0] = '\0';
    g_out_len = 0;
    CHECK(at_exec(&at, "AT+NOPE") == AT_ERROR);
    CHECK(strcmp(g_out, "\r\nAT_ERROR\r\n") == 0);
    CHECK(at_exec(&at, "FOO") == AT_ERROR);

    /* ajuda do comando */
    g_out[0] = '\0';
    g_out_len = 0;
    CHECK(at_exec(&at, "AT+TDC?") == AT_OK);
    CHECK(strstr(g_out, "AT+TDC: Get or set") != NULL);

    /* AT+JOIN sem '=' executa o set com arg NULL e devolve valor */
    g_out[0] = '\0';
    g_out_len = 0;
    CHECK(at_exec(&at, "AT+JOIN") == AT_OK);
    CHECK(strstr(g_out, "AT+JOIN: Join network") != NULL);

    /* status de erro do handler e' propagado (AT+NJS) */
    g_out[0] = '\0';
    g_out_len = 0;
    CHECK(at_exec(&at, "AT+NJS=?") == AT_NO_NETWORK_JOINED);
    CHECK(strstr(g_out, "AT_NO_NETWORK_JOINED") != NULL);

    /* ATZ reinicia e responde OK */
    g_out[0] = '\0';
    g_out_len = 0;
    CHECK(at_exec(&at, "ATZ") == AT_OK);
    CHECK(g_reset_called);

    /* alimentacao caractere a caractere, com CR */
    g_out[0] = '\0';
    g_out_len = 0;
    g_value = 0;
    const char *line = "AT+TDC=1500\r";
    for (const char *p = line; *p; p++) {
        at_feed(&at, *p);
    }
    CHECK(g_value == 1500);
    CHECK(strcmp(g_out, "\r\nOK\r\n") == 0);

    /* linha maior que o buffer -> AT_TEST_PARAM_OVERFLOW */
    g_out[0] = '\0';
    g_out_len = 0;
    char big[AT_LINE_MAX + 32];
    memset(big, 'x', sizeof(big) - 1);
    big[0] = 'A';
    big[1] = 'T';
    big[2] = '+';
    big[3] = 'T';
    big[4] = 'D';
    big[5] = 'C';
    big[6] = '=';
    big[sizeof(big) - 1] = '\0';
    for (const char *p = big; *p; p++) {
        at_feed(&at, *p);
    }
    at_feed(&at, '\r');
    CHECK(strstr(g_out, "AT_TEST_PARAM_OVERFLOW") != NULL);

    /* CR/LF redundantes nao geram resposta */
    g_out[0] = '\0';
    g_out_len = 0;
    at_feed(&at, '\r');
    at_feed(&at, '\n');
    CHECK(g_out_len == 0);

    /* busca na tabela aceita com e sem '+' */
    CHECK(at_find(&at, "+TDC") == at_find(&at, "TDC"));
}

/* ------------------------------------------------------------------------- */

int main(void)
{
    test_aes_fips197();
    test_cmac_rfc4493();
    test_config_roundtrip();
    test_config_hex();
    test_payload_modes();
    test_at_engine();

    printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

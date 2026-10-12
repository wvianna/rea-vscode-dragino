/*
 * at_cmds.c - comandos AT do no'.
 *
 * A tabela segue o mesmo modelo do firmware original (nome curto, id, texto
 * de ajuda e dois handlers) reconstruida do binario v1.8.1: a tabela de
 * descritores fica em 0x08013e34 com passo de 24 bytes e os textos de ajuda
 * estao em 0x080148e0..0x080159d4. Os nomes e as ajudas abaixo sao os mesmos
 * (ver docs/firmware/LSN50_AU915_v1.8.1-at-table.txt).
 */
#include "at_cmds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "console.h"
#include "hw.h"
#include "lorawan.h"
#include "sensors.h"

app_t g_app;
static at_ctx_t g_at;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static void mark_dirty(void)
{
    g_app.cfg_dirty = true;
}

static void save_now(void)
{
    uint8_t buf[256];
    size_t n = cfg_encode(&g_app.cfg, buf, sizeof(buf));
    if (n && hw_eeprom_write(BOARD_CFG_OFFSET, buf, n)) {
        g_app.cfg_dirty = false;
    }
}

typedef enum { KIND_U8, KIND_U16, KIND_U32, KIND_I32, KIND_CLASS } kind_t;

typedef struct {
    size_t offset;
    kind_t kind;
    uint32_t min;
    uint32_t max;
} num_field_t;

static uint32_t field_get(const config_t *c, const num_field_t *f)
{
    const uint8_t *p = (const uint8_t *)c + f->offset;
    switch (f->kind) {
    case KIND_U8:
    case KIND_CLASS:
        return *p;
    case KIND_U16: {
        uint16_t v;
        memcpy(&v, p, sizeof(v));
        return v;
    }
    default: {
        uint32_t v;
        memcpy(&v, p, sizeof(v));
        return v;
    }
    }
}

static void field_set(config_t *c, const num_field_t *f, uint32_t v)
{
    uint8_t *p = (uint8_t *)c + f->offset;
    switch (f->kind) {
    case KIND_U8:
    case KIND_CLASS:
        *p = (uint8_t)v;
        break;
    case KIND_U16: {
        uint16_t t = (uint16_t)v;
        memcpy(p, &t, sizeof(t));
        break;
    }
    default:
        memcpy(p, &v, sizeof(v));
        break;
    }
}

static at_status_t num_get(char *out, size_t max, void *user, const num_field_t *f)
{
    app_t *app = (app_t *)user;
    uint32_t v = field_get(&app->cfg, f);
    if (f->kind == KIND_CLASS) {
        snprintf(out, max, "%c", (char)v);
    } else if (f->kind == KIND_I32) {
        snprintf(out, max, "%d", (int)(int32_t)v);
    } else {
        snprintf(out, max, "%u", (unsigned)v);
    }
    return AT_OK;
}

static at_status_t num_set(const char *arg, char *out, size_t max, void *user,
                           const num_field_t *f)
{
    app_t *app = (app_t *)user;
    (void)out;
    (void)max;

    if (arg == NULL || *arg == '\0') {
        return AT_PARAM_ERROR;
    }
    if (f->kind == KIND_CLASS) {
        char c = arg[0];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != 'A' && c != 'C') {
            return AT_PARAM_ERROR;
        }
        field_set(&app->cfg, f, (uint32_t)c);
        mark_dirty();
        return AT_OK;
    }

    char *end = NULL;
    long v = strtol(arg, &end, 10);
    if (end == arg || (end && *end != '\0') || v < 0) {
        return AT_PARAM_ERROR;
    }
    if ((uint32_t)v < f->min || (uint32_t)v > f->max) {
        return AT_PARAM_ERROR;
    }
    field_set(&app->cfg, f, (uint32_t)v);
    mark_dirty();
    return AT_OK;
}

#define NUM_CMD(tag, member, kind, lo, hi)                                    \
    static const num_field_t tag##_field = {offsetof(config_t, member), kind,  \
                                            (lo), (hi)};                       \
    static at_status_t tag##_get(char *o, size_t m, void *u)                   \
    {                                                                          \
        return num_get(o, m, u, &tag##_field);                                 \
    }                                                                          \
    static at_status_t tag##_set(const char *a, char *o, size_t m, void *u)    \
    {                                                                          \
        return num_set(a, o, m, u, &tag##_field);                              \
    }

NUM_CMD(adr, adr, KIND_U8, 0, 1)
NUM_CMD(txp, txp, KIND_U8, 0, 50)
NUM_CMD(dr, dr, KIND_U8, 0, 13)
NUM_CMD(dcs, dcs, KIND_U8, 0, 1)
NUM_CMD(pnm, pnm, KIND_U8, 0, 1)
NUM_CMD(njm, nj_mode, KIND_U8, 0, 1)
NUM_CMD(dwellt, dwellt, KIND_U8, 0, 1)
NUM_CMD(rpl, rpl, KIND_U8, 0, 7)
NUM_CMD(decrypt, decrypt, KIND_U8, 0, 1)
NUM_CMD(dismacans, dismacans, KIND_U8, 0, 1)
NUM_CMD(disfcntcheck, disfcntcheck, KIND_U8, 0, 1)
NUM_CMD(ddetect, ddetect, KIND_U8, 0, 1)
NUM_CMD(rxdatest, rxdatest, KIND_U8, 0, 1)
NUM_CMD(debug, debug, KIND_U8, 0, 1)
NUM_CMD(setmaxnbtrans, maxnbtrans, KIND_U8, 1, 15)
NUM_CMD(che, che, KIND_U8, 0, 8)
NUM_CMD(port, port, KIND_U8, 0, 223)
NUM_CMD(cfm, cfm, KIND_U8, 0, 1)
NUM_CMD(rx2dr, rx2dr, KIND_U8, 0, 13)
NUM_CMD(rx1wto, rx1wto, KIND_U8, 0, 255)
NUM_CMD(rx2wto, rx2wto, KIND_U8, 0, 255)
NUM_CMD(devclass, dev_class, KIND_CLASS, 0, 0)
NUM_CMD(tdc, tdc_ms, KIND_U32, 1000, 86400000)
NUM_CMD(rjtdc, rjtdc_min, KIND_U32, 0, 1440)
NUM_CMD(fcu, fcu, KIND_U32, 0, 0xFFFFFFFFu)
NUM_CMD(fcd, fcd, KIND_U32, 0, 0xFFFFFFFFu)
NUM_CMD(chs, chs_freq_hz, KIND_U32, 0, 1020000000u)
NUM_CMD(rx2fq, rx2freq_hz, KIND_U32, 0, 1020000000u)
NUM_CMD(fivevt, five_vt_ms, KIND_U16, 0, 60000)
NUM_CMD(rx1dl, rx1dl_ms, KIND_U16, 0, 60000)
NUM_CMD(rx2dl, rx2dl_ms, KIND_U16, 0, 60000)
NUM_CMD(jn1dl, jn1dl_ms, KIND_U16, 0, 60000)
NUM_CMD(jn2dl, jn2dl_ms, KIND_U16, 0, 60000)

/* ------------------------------------------------------------------------ */
/* Campos hexadecimais (EUI/chaves/endereco)                                 */
/* ------------------------------------------------------------------------ */

#define HEX_CMD(tag, member, len)                                             \
    static at_status_t tag##_get(char *o, size_t m, void *u)                  \
    {                                                                          \
        app_t *app = (app_t *)u;                                              \
        cfg_bytes_to_hex(app->cfg.member, (len), o, m, false, ' ');           \
        return AT_OK;                                                          \
    }                                                                          \
    static at_status_t tag##_set(const char *a, char *o, size_t m, void *u)   \
    {                                                                          \
        app_t *app = (app_t *)u;                                              \
        (void)o;                                                               \
        (void)m;                                                               \
        if (a == NULL || !cfg_hex_to_bytes(a, app->cfg.member, (len))) {       \
            return AT_PARAM_ERROR;                                             \
        }                                                                      \
        mark_dirty();                                                          \
        return AT_OK;                                                          \
    }

HEX_CMD(deui, deveui, 8)
HEX_CMD(appeui, appeui, 8)
HEX_CMD(appkey, appkey, 16)
HEX_CMD(nwkskey, nwkskey, 16)
HEX_CMD(appskey, appskey, 16)
HEX_CMD(daddr, devaddr, 4)
HEX_CMD(nwkid, nwkid, 4)

/* ------------------------------------------------------------------------ */
/* Comandos especiais                                                        */
/* ------------------------------------------------------------------------ */

static at_status_t cmd_ver_get(char *out, size_t max, void *user)
{
    (void)user;
    snprintf(out, max, "%s %s", CFG_IMAGE_VERSION, CFG_FREQ_BAND);
    return AT_OK;
}

static at_status_t cmd_cfg_get(char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    (void)max;
    out[0] = '\0';

    console_printf("AT+DEUI=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   app->cfg.deveui[0], app->cfg.deveui[1], app->cfg.deveui[2],
                   app->cfg.deveui[3], app->cfg.deveui[4], app->cfg.deveui[5],
                   app->cfg.deveui[6], app->cfg.deveui[7]);
    console_printf("AT+DADDR=%02X %02X %02X %02X\r\n", app->cfg.devaddr[0],
                   app->cfg.devaddr[1], app->cfg.devaddr[2], app->cfg.devaddr[3]);
    console_printf("AT+APPEUI=%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   app->cfg.appeui[0], app->cfg.appeui[1], app->cfg.appeui[2],
                   app->cfg.appeui[3], app->cfg.appeui[4], app->cfg.appeui[5],
                   app->cfg.appeui[6], app->cfg.appeui[7]);
    console_printf("AT+APPKEY=");
    console_hex(app->cfg.appkey, 16, ' ');
    console_puts("\r\nAT+NWKSKEY=");
    console_hex(app->cfg.nwkskey, 16, ' ');
    console_puts("\r\nAT+APPSKEY=");
    console_hex(app->cfg.appskey, 16, ' ');
    console_puts("\r\n");
    console_printf("AT+NJM=%u\r\n", (unsigned)app->cfg.nj_mode);
    console_printf("AT+ADR=%u\r\n", (unsigned)app->cfg.adr);
    console_printf("AT+DR=%u\r\n", (unsigned)app->cfg.dr);
    console_printf("AT+TXP=%u\r\n", (unsigned)app->cfg.txp);
    console_printf("AT+CLASS=%c\r\n", (char)app->cfg.dev_class);
    console_printf("AT+PNM=%u\r\n", (unsigned)app->cfg.pnm);
    console_printf("AT+DCS=%u\r\n", (unsigned)app->cfg.dcs);
    console_printf("AT+DWELLT=%u\r\n", (unsigned)app->cfg.dwellt);
    console_printf("AT+RX2DR=%u\r\n", (unsigned)app->cfg.rx2dr);
    console_printf("AT+RX2FQ=%lu\r\n", (unsigned long)app->cfg.rx2freq_hz);
    console_printf("AT+RX1DL=%u\r\n", (unsigned)app->cfg.rx1dl_ms);
    console_printf("AT+RX2DL=%u\r\n", (unsigned)app->cfg.rx2dl_ms);
    console_printf("AT+JN1DL=%u\r\n", (unsigned)app->cfg.jn1dl_ms);
    console_printf("AT+JN2DL=%u\r\n", (unsigned)app->cfg.jn2dl_ms);
    console_printf("AT+CHS=%lu\r\n", (unsigned long)app->cfg.chs_freq_hz);
    console_printf("AT+CHE=%u\r\n", (unsigned)app->cfg.che);
    console_printf("AT+TDC=%lu\r\n", (unsigned long)app->cfg.tdc_ms);
    console_printf("AT+RJTDC=%lu\r\n", (unsigned long)app->cfg.rjtdc_min);
    console_printf("AT+MOD=%u\r\n", (unsigned)app->cfg.mode);
    console_printf("AT+PORT=%u\r\n", (unsigned)app->cfg.port);
    console_printf("AT+CFM=%u\r\n", (unsigned)app->cfg.cfm);
    console_printf("AT+5VT=%u\r\n", (unsigned)app->cfg.five_vt_ms);
    console_printf("AT+INTMOD1=%u\r\n", (unsigned)app->cfg.intmod[0]);
    console_printf("AT+INTMOD2=%u\r\n", (unsigned)app->cfg.intmod[1]);
    console_printf("AT+INTMOD3=%u\r\n", (unsigned)app->cfg.intmod[2]);
    console_printf("AT+WEIGAP=%u.%u\r\n", (unsigned)(app->cfg.weigap_x10 / 10u),
                   (unsigned)(app->cfg.weigap_x10 % 10u));
    console_printf("AT+FCU=%lu\r\n", (unsigned long)app->cfg.fcu);
    console_printf("AT+FCD=%lu\r\n", (unsigned long)app->cfg.fcd);
    return AT_OK;
}

static at_status_t cmd_fdr_set(const char *arg, char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    (void)arg;
    (void)out;
    (void)max;
    cfg_factory_reset(&app->cfg);
    mark_dirty();
    sensors_int_apply(&app->cfg);
    console_puts("AT+FDR: Reset Parameters to Factory Default, Keys Reserve\r\n");
    console_puts("Please set the parameters or reset Device to apply change\r\n");
    return AT_OK;
}

static at_status_t cmd_join_set(const char *arg, char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    (void)arg;
    (void)out;
    (void)max;
    app->join_request = true;
    return AT_OK;
}

static at_status_t cmd_njs_get(char *out, size_t max, void *user)
{
    (void)user;
    snprintf(out, max, "%u", lw_joined() ? 1u : 0u);
    return AT_OK;
}

static at_status_t cmd_cfs_get(char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    snprintf(out, max, "%u", (unsigned)app->cfg.conf_status);
    return AT_OK;
}

static at_status_t cmd_rssi_get(char *out, size_t max, void *user)
{
    (void)user;
    snprintf(out, max, "%d", (int)lw_last_rssi());
    return AT_OK;
}

static at_status_t cmd_snr_get(char *out, size_t max, void *user)
{
    (void)user;
    /* o original responde em dB inteiro */
    snprintf(out, max, "%d", (int)(lw_last_snr_x4() / 4));
    return AT_OK;
}

static at_status_t cmd_recv_get(char *out, size_t max, void *user)
{
    const uint8_t *data;
    uint8_t port;
    (void)user;
    size_t len = lw_last_downlink(&data, &port);
    if (len == 0) {
        snprintf(out, max, "0:");
        return AT_OK;
    }
    snprintf(out, max, "%u:", (unsigned)port);
    size_t used = strlen(out);
    for (size_t i = 0; i < len && used + 1 < max; i++) {
        out[used++] = (char)data[i];
    }
    out[used] = '\0';
    return AT_OK;
}

static at_status_t cmd_recvb_get(char *out, size_t max, void *user)
{
    const uint8_t *data;
    uint8_t port;
    (void)user;
    size_t len = lw_last_downlink(&data, &port);
    int n = snprintf(out, max, "%u: ", (unsigned)port);
    if (n < 0) {
        return AT_ERROR;
    }
    size_t used = (size_t)n;
    for (size_t i = 0; i < len && used + 3 < max; i++) {
        snprintf(out + used, max - used, "%02x", data[i]);
        used += 2;
    }
    return AT_OK;
}

static at_status_t cmd_send_set(const char *arg, char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    (void)out;
    (void)max;
    if (arg == NULL) {
        return AT_PARAM_ERROR;
    }
    /* formato do manual: <port>:<data> */
    char *colon = strchr(arg, ':');
    long port = app->cfg.port;
    const char *data = arg;
    if (colon) {
        char *end = NULL;
        port = strtol(arg, &end, 10);
        if (end != colon || port < 0 || port > 223) {
            return AT_PARAM_ERROR;
        }
        data = colon + 1;
    }
    app->tx_port = (uint8_t)port;

    if (app->send_hex) {
        size_t n = 0;
        int hi = -1;
        for (const char *p = data; *p && n < sizeof(app->tx_data); p++) {
            int v;
            if (*p >= '0' && *p <= '9') {
                v = *p - '0';
            } else if (*p >= 'a' && *p <= 'f') {
                v = *p - 'a' + 10;
            } else if (*p >= 'A' && *p <= 'F') {
                v = *p - 'A' + 10;
            } else {
                continue;
            }
            if (hi < 0) {
                hi = v;
            } else {
                app->tx_data[n++] = (uint8_t)((hi << 4) | v);
                hi = -1;
            }
        }
        if (hi >= 0 || n == 0) {
            return AT_PARAM_ERROR;
        }
        app->tx_len = n;
    } else {
        size_t n = strlen(data);
        if (n == 0 || n > sizeof(app->tx_data)) {
            return AT_PARAM_ERROR;
        }
        memcpy(app->tx_data, data, n);
        app->tx_len = n;
    }
    app->send_request = true;
    return AT_OK;
}

static at_status_t cmd_sendb_set(const char *arg, char *out, size_t max,
                                 void *user)
{
    app_t *app = (app_t *)user;
    app->send_hex = true;
    at_status_t st = cmd_send_set(arg, out, max, user);
    app->send_hex = false;
    return st;
}

static at_status_t cmd_sendset_plain(const char *arg, char *out, size_t max,
                                     void *user)
{
    app_t *app = (app_t *)user;
    app->send_hex = false;
    return cmd_send_set(arg, out, max, user);
}

static at_status_t cmd_mod_set(const char *arg, char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    (void)out;
    (void)max;
    if (arg == NULL) {
        return AT_PARAM_ERROR;
    }
    long v = strtol(arg, NULL, 10);
    if (v < 1 || v > 6) {
        return AT_PARAM_ERROR;
    }
    app->cfg.mode = (uint8_t)v;
    mark_dirty();
    return AT_OK;
}

static at_status_t cmd_mod_get(char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    snprintf(out, max, "%u", (unsigned)app->cfg.mode);
    return AT_OK;
}

static at_status_t intmod_set(const char *arg, char *out, size_t max, void *user,
                              uint8_t idx)
{
    app_t *app = (app_t *)user;
    (void)out;
    (void)max;
    if (arg == NULL) {
        return AT_PARAM_ERROR;
    }
    long v = strtol(arg, NULL, 10);
    if (v < 0 || v > 3) {
        return AT_PARAM_ERROR;
    }
    app->cfg.intmod[idx] = (uint8_t)v;
    sensors_int_apply(&app->cfg);
    mark_dirty();
    return AT_OK;
}

#define INTMOD_CMD(tag, idx)                                                  \
    static at_status_t tag##_get(char *o, size_t m, void *u)                  \
    {                                                                          \
        app_t *app = (app_t *)u;                                              \
        snprintf(o, m, "%u", (unsigned)app->cfg.intmod[idx]);                 \
        return AT_OK;                                                          \
    }                                                                          \
    static at_status_t tag##_set(const char *a, char *o, size_t m, void *u)   \
    {                                                                          \
        return intmod_set(a, o, m, u, idx);                                    \
    }

INTMOD_CMD(intmod1, 0)
INTMOD_CMD(intmod2, 1)
INTMOD_CMD(intmod3, 2)

static at_status_t cmd_weigap_get(char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    snprintf(out, max, "%u.%u", (unsigned)(app->cfg.weigap_x10 / 10u),
             (unsigned)(app->cfg.weigap_x10 % 10u));
    return AT_OK;
}

static at_status_t cmd_weigap_set(const char *arg, char *out, size_t max,
                                  void *user)
{
    app_t *app = (app_t *)user;
    (void)out;
    (void)max;
    if (arg == NULL) {
        return AT_PARAM_ERROR;
    }
    long inteiro = strtol(arg, NULL, 10);
    long decimo = 0;
    const char *dot = strchr(arg, '.');
    if (dot && dot[1] >= '0' && dot[1] <= '9') {
        decimo = dot[1] - '0';
    }
    if (inteiro < 0 || inteiro > 6553) {
        return AT_PARAM_ERROR;
    }
    app->cfg.weigap_x10 = (uint16_t)(inteiro * 10 + decimo);
    mark_dirty();

    /* o original responde com o peso lido com o novo fator */
    int32_t g = sensors_hx711_read(&app->cfg);
    snprintf(out, max, "Weight:%d g", (int)g);
    return AT_OK;
}

static at_status_t cmd_weigre_set(const char *arg, char *out, size_t max,
                                  void *user)
{
    app_t *app = (app_t *)user;
    (void)arg;
    (void)out;
    (void)max;
    /* zero de calibracao: guarda o valor bruto do HX711 com a celula vazia
     * (manual 2.3.5a - "Don't put anything on load cell and run AT+WEIGRE") */
    app->cfg.weigre_zero = sensors_hx711_raw();
    mark_dirty();
    return AT_OK;
}

static at_status_t cmd_setcnt_set(const char *arg, char *out, size_t max,
                                  void *user)
{
    app_t *app = (app_t *)user;
    (void)out;
    (void)max;
    if (arg == NULL) {
        return AT_PARAM_ERROR;
    }
    long v = strtol(arg, NULL, 10);
    if (v < 0) {
        return AT_PARAM_ERROR;
    }
    app->cfg.setcnt = (uint32_t)v;
    hw_exti_take_count(HW_INT1);
    hw_exti_take_count(HW_INT2);
    hw_exti_take_count(HW_INT3);
    mark_dirty();
    return AT_OK;
}

static at_status_t cmd_setcnt_get(char *out, size_t max, void *user)
{
    app_t *app = (app_t *)user;
    uint32_t total = app->cfg.setcnt + hw_exti_count(HW_INT1) +
                     hw_exti_count(HW_INT2) + hw_exti_count(HW_INT3);
    snprintf(out, max, "%lu", (unsigned long)total);
    return AT_OK;
}

static at_status_t cmd_getsensorvalue_set(const char *arg, char *out, size_t max,
                                          void *user)
{
    app_t *app = (app_t *)user;
    (void)arg;
    (void)out;
    (void)max;
    app->sample_request = true;
    return AT_OK;
}

/* ------------------------------------------------------------------------ */
/* Tabela de comandos                                                        */
/* ------------------------------------------------------------------------ */
/* Os ids e os textos de ajuda sao os do firmware v1.8.1 (tabela em
 * 0x08013e34 / ajudas em 0x080148e0..0x080159d4). */

static const at_cmd_t AT_TABLE[] = {
    {"Z", 1, "ATZ: Trig a reset of the MCU", NULL, NULL},
    {"+FDR", 4, "AT+FDR: Reset Parameters to Factory Default, Keys Reserve", NULL,
     cmd_fdr_set},
    {"+DEUI", 5, "AT+DEUI: Get or Set the Device EUI", deui_get, deui_set},
    {"+APPEUI", 7, "AT+APPEUI: Get or Set the Application EUI", appeui_get,
     appeui_set},
    {"+APPKEY", 7, "AT+APPKEY: Get or Set the Application Key", appkey_get,
     appkey_set},
    {"+DADDR", 6, "AT+DADDR: Get or Set the Device Address", daddr_get,
     daddr_set},
    {"+NWKSKEY", 8, "AT+NWKSKEY: Get or Set the Network Session Key",
     nwkskey_get, nwkskey_set},
    {"+APPSKEY", 8, "AT+APPSKEY: Get or Set the Application Session Key",
     appskey_get, appskey_set},
    {"+NWKID", 6, "AT+NWKID: Get or Set the Network ID", nwkid_get, nwkid_set},
    {"+ADR", 4, "AT+ADR: Get or Set the Adaptive Data Rate setting. (0: off, 1: on)",
     adr_get, adr_set},
    {"+TXP", 4,
     "AT+TXP: Get or Set the Transmit Power (0-5, MAX:0, MIN:5, according to LoRaWAN Spec)",
     txp_get, txp_set},
    {"+DR", 3, "AT+DR: Get or Set the Data Rate. (0-7 corresponding to DR_X)",
     dr_get, dr_set},
    {"+DCS", 4,
     "AT+DCS: Get or Set the ETSI Duty Cycle setting - 0=disable, 1=enable - Only for testing",
     dcs_get, dcs_set},
    {"+PNM", 4, "AT+PNM: Get or Set the public network mode. (0: off, 1: on)",
     pnm_get, pnm_set},
    {"+RX2FQ", 6, "AT+RX2FQ: Get or Set the Rx2 window frequency", rx2fq_get,
     rx2fq_set},
    {"+RX2DR", 6, "AT+RX2DR: Get or Set the Rx2 window data rate (0-7 corresponding to DR_X)",
     rx2dr_get, rx2dr_set},
    {"+RX1DL", 6,
     "AT+RX1DL: Get or Set the delay between the end of the Tx and the Rx Window 1 in ms",
     rx1dl_get, rx1dl_set},
    {"+RX2DL", 6,
     "AT+RX2DL: Get or Set the delay between the end of the Tx and the Rx Window 2 in ms",
     rx2dl_get, rx2dl_set},
    {"+JN1DL", 6,
     "AT+JN1DL: Get or Set the Join Accept Delay between the end of the Tx and the Join Rx Window 1 in ms",
     jn1dl_get, jn1dl_set},
    {"+JN2DL", 6,
     "AT+JN2DL: Get or Set the Join Accept Delay between the end of the Tx and the Join Rx Window 2 in ms",
     jn2dl_get, jn2dl_set},
    {"+NJM", 4, "AT+NJM: Get or Set the Network Join Mode. (0: ABP, 1: OTAA)",
     njm_get, njm_set},
    {"+FCU", 4, "AT+FCU: Get or Set the Frame Counter Uplink", fcu_get, fcu_set},
    {"+FCD", 4, "AT+FCD: Get or Set the Frame Counter Downlink", fcd_get,
     fcd_set},
    {"+CLASS", 6, "AT+CLASS: Get or Set the Device Class", devclass_get,
     devclass_set},
    {"+JOIN", 5, "AT+JOIN: Join network", NULL, cmd_join_set},
    {"+NJS", 4, "AT+NJS: Get the join status", cmd_njs_get, NULL},
    {"+SENDB", 6, "AT+SENDB: Send hexadecimal data along with the application port",
     NULL, cmd_sendb_set},
    {"+SEND", 5, "AT+SEND: Send text data along with the application port", NULL,
     cmd_sendset_plain},
    {"+RECVB", 6,
     "AT+RECVB: Print last received data in binary format (with hexadecimal values)",
     cmd_recvb_get, NULL},
    {"+RECV", 5, "AT+RECV: Print last received data in raw format", cmd_recv_get,
     NULL},
    {"+DWELLT", 7, "AT+DWELLT: Get or set UplinkDwellTime", dwellt_get,
     dwellt_set},
    {"+RJTDC", 6, "AT+RJTDC: Get or set the ReJoin data transmission interval in min",
     rjtdc_get, rjtdc_set},
    {"+RPL", 4, "AT+RPL: Get or set response level", rpl_get, rpl_set},
    {"+VER", 4, "AT+VER: Get current image version and Frequency Band",
     cmd_ver_get, NULL},
    {"+CFM", 4, "AT+CFM: Get or Set the confirmation mode (0-1)", cfm_get,
     cfm_set},
    {"+CFS", 4, "AT+CFS: Get confirmation status of the last AT+SEND (0-1)",
     cmd_cfs_get, NULL},
    {"+TDC", 4, "AT+TDC: Get or set the application data transmission interval in ms",
     tdc_get, tdc_set},
    {"+MOD", 4, "AT+MOD:Get or Set the work mode", cmd_mod_get, cmd_mod_set},
    {"+INTMOD1", 8,
     "AT+INTMOD1:Get or Set the trigger interrupt mode for PB14(0:Disable,1:falling or rising,2:falling,3:rising)",
     intmod1_get, intmod1_set},
    {"+INTMOD3", 7,
     "AT+INTMOD3:Get or Set the trigger interrupt mode for PA4(0:Disable,1:falling or rising,2:falling,3:rising)",
     intmod3_get, intmod3_set},
    {"+INTMOD2", 7,
     "AT+INTMOD2:Get or Set the trigger interrupt mode for PB15(0:Disable,1:falling or rising,2:falling,3:rising)",
     intmod2_get, intmod2_set},
    {"+WEIGRE", 7, "AT+WEIGRE:Set the weight to 0g", NULL, cmd_weigre_set},
    {"+WEIGAP", 7, "AT+WEIGAP:Get or Set the GapValue of weight", cmd_weigap_get,
     cmd_weigap_set},
    {"+5VT", 4, "AT+5VT:Get or Set extend the time of 5V power", fivevt_get,
     fivevt_set},
    {"+SETCNT", 7, "AT+SETCNT: Get or set the count at present", cmd_setcnt_get,
     cmd_setcnt_set},
    {"+CHS", 4, "AT+CHS: Get or Set Frequency (Unit: Hz) for Single Channel Mode",
     chs_get, chs_set},
    {"+CHE", 4, "AT+CHE: Get or Set eight channels mode,Only for US915,AU915,CN470",
     che_get, che_set},
    {"+PORT", 4, "AT+PORT: Get or set the application port", port_get, port_set},
    {"+GETSENSORVALUE", 15,
     "AT+GETSENSORVALUE: Returns the current sensor value", NULL,
     cmd_getsensorvalue_set},
    {"+DDETECT", 8, "AT+DDETECT: Get or set the downlink detection", ddetect_get,
     ddetect_set},
    {"+SETMAXNBTRANS", 14, "AT+SETMAXNBTRANS: Get or set the max nbtrans in LinkADR",
     setmaxnbtrans_get, setmaxnbtrans_set},
    {"+RXDATEST", 9, "AT+RXDATEST: Set the downlink data detection",
     rxdatest_get, rxdatest_set},
    {"+CFG", 4, "AT+CFG: Print all configurations", cmd_cfg_get, NULL},
    {"+DECRYPT", 9, "AT+DECRYPT: Get or Set the Decrypt the uplink payload(0:Disable,1:Enable)",
     decrypt_get, decrypt_set},
    {"+DISMACANS", 10, "AT+DISMACANS: Get or set the MAC ANS switch(0:Enable,1:Disable)",
     dismacans_get, dismacans_set},
    {"+DISFCNTCHECK", 14, "AT+DISFCNTCHECK: Get or set the Downlink Frame Check(0:Enable,1:Disable)",
     disfcntcheck_get, disfcntcheck_set},
    {"+DEBUG", 6, "AT+DEBUG:Set more info input", debug_get, debug_set},
    {"+RSSI", 5, "AT+RSSI: Get the RSSI of the last received packet",
     cmd_rssi_get, NULL},
    {"+SNR", 4, "AT+SNR: Get the SNR of the last received packet", cmd_snr_get,
     NULL},
    {"+RX1WTO", 7,
     "AT+RX1WTO: Get or Set the number of symbols to detect and timeout from RXwindow1(0 to 255)",
     rx1wto_get, rx1wto_set},
    {"+RX2WTO", 7,
     "AT+RX2WTO: Get or Set the number of symbols to detect and timeout from RXwindow2(0 to 255)",
     rx2wto_get, rx2wto_set},
};

#define AT_TABLE_COUNT (sizeof(AT_TABLE) / sizeof(AT_TABLE[0]))

static void at_write(const char *text, void *ctx)
{
    (void)ctx;
    console_puts(text);
}

at_ctx_t *at_cmds_context(void)
{
    return &g_at;
}

void at_cmds_init(app_t *app)
{
    static uint8_t blob[256];

    cfg_defaults(&app->cfg);
    app->send_hex = false;

    /* O blob codificado tem tamanho fixo: medimos codificando os defaults. */
    size_t blob_len = cfg_encode(&app->cfg, blob, sizeof(blob));
    if (blob_len > 0 && blob_len <= sizeof(blob)) {
        if (hw_eeprom_read(BOARD_CFG_OFFSET, blob, blob_len)) {
            config_t loaded;
            if (cfg_decode(blob, blob_len, &loaded)) {
                app->cfg = loaded;
            } else {
                console_puts("EEPROM com CRC invalido: usando defaults\r\n");
            }
        }
    }

    at_init(&g_at, AT_TABLE, AT_TABLE_COUNT, at_write, app);
    at_cmds_set_sample(NULL);
}

const at_cmd_t *at_cmd_table(size_t *count)
{
    *count = AT_TABLE_COUNT;
    return AT_TABLE;
}

bool at_cmd_apply_text(const char *line)
{
    bool before = g_app.cfg_dirty;
    at_exec(&g_at, line);
    return g_app.cfg_dirty != before;
}

void at_cmds_save_if_dirty(void)
{
    if (g_app.cfg_dirty) {
        save_now();
    }
}

void at_cmds_set_sample(const sample_t *s)
{
    if (s) {
        g_app.sample = *s;
        g_app.have_sample = true;
    } else {
        memset(&g_app.sample, 0, sizeof(g_app.sample));
        g_app.have_sample = false;
    }
}

size_t at_cmds_build_payload(uint8_t *out, size_t max)
{
    bool mod2_tf = false;
    return payload_build(g_app.cfg.mode, &g_app.sample, mod2_tf, out, max);
}

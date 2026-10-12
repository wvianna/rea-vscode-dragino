#include "config.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Defaults                                                                   */
/* ------------------------------------------------------------------------- */

void cfg_defaults(config_t *c)
{
    memset(c, 0, sizeof(*c));

    c->mode = MOD_IIC;
    c->nj_mode = 1; /* OTAA, igual ao default do LSN50 */
    c->adr = 1;
    c->dr = 5;    /* AT+DR=? do manual AU915 responde 5 */
    c->txp = 0;   /* MAX */
    c->dev_class = 'A';
    c->pnm = 1;
    c->dcs = 0;
    c->dwellt = 1; /* AU915 usa uplink dwell time */
    c->rpl = 0;
    c->maxnbtrans = 1;
    c->port = 2;  /* AT+PORT default do manual: 21; o firmware usa 2 no envio */
    c->cfm = 0;
    c->rx2dr = 8; /* AU915 DR8 no RX2 */
    c->rx1wto = 0;
    c->rx2wto = 0;

    c->tdc_ms = 30000; /* AT+TDC default (manual: 30000 ms) */
    c->rjtdc_min = 0;
    c->fcu = 0;
    c->fcd = 0;
    c->chs_freq_hz = 0;
    c->rx2freq_hz = 923300000; /* AU915 RX2 */
    c->setcnt = 0;

    c->five_vt_ms = 500; /* manual: AT+5VT default 500 ms */
    c->rx1dl_ms = 1000;  /* manual: RX1DL 1000 ms */
    c->rx2dl_ms = 2000;  /* manual: RX2DL 2000 ms */
    c->jn1dl_ms = 5000;  /* manual: JN1DL 5000 ms */
    c->jn2dl_ms = 6000;  /* manual: JN2DL 6000 ms */
    c->weigap_x10 = 4000; /* AT+WEIGAP default 400.0 */

    /* Exemplo do manual (AT+DEUI=? -> 00 44 34 22 33 45 55 55) nao e' uma
     * chave real; aqui ficam zeros e o usuario define via AT+DEUI/APPKEY. */
}

void cfg_factory_reset(config_t *c)
{
    uint8_t deveui[CFG_EUI_LEN], appeui[CFG_EUI_LEN], appkey[CFG_KEY_LEN];
    memcpy(deveui, c->deveui, sizeof(deveui));
    memcpy(appeui, c->appeui, sizeof(appeui));
    memcpy(appkey, c->appkey, sizeof(appkey));

    cfg_defaults(c);

    memcpy(c->deveui, deveui, sizeof(deveui));
    memcpy(c->appeui, appeui, sizeof(appeui));
    memcpy(c->appkey, appkey, sizeof(appkey));
}

/* ------------------------------------------------------------------------- */
/* CRC                                                                        */
/* ------------------------------------------------------------------------- */

uint16_t cfg_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ------------------------------------------------------------------------- */
/* Serializacao little-endian                                                 */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t pos;
} writer_t;

static void w8(writer_t *w, uint8_t v)
{
    if (w->pos < w->len) {
        w->buf[w->pos] = v;
    }
    w->pos++;
}

static void w16(writer_t *w, uint16_t v)
{
    w8(w, (uint8_t)(v & 0xFFu));
    w8(w, (uint8_t)(v >> 8));
}

static void w32(writer_t *w, uint32_t v)
{
    w16(w, (uint16_t)(v & 0xFFFFu));
    w16(w, (uint16_t)(v >> 16));
}

static void wblob(writer_t *w, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        w8(w, src[i]);
    }
}

static uint8_t r8(const uint8_t *in, size_t len, size_t *pos)
{
    if (*pos >= len) {
        return 0;
    }
    return in[(*pos)++];
}

static uint16_t r16(const uint8_t *in, size_t len, size_t *pos)
{
    uint16_t lo = r8(in, len, pos);
    uint16_t hi = r8(in, len, pos);
    return (uint16_t)(lo | (hi << 8));
}

static uint32_t r32(const uint8_t *in, size_t len, size_t *pos)
{
    uint32_t lo = r16(in, len, pos);
    uint32_t hi = r16(in, len, pos);
    return lo | (hi << 16);
}

#define CFG_BLOB_BYTES \
    (2 * CFG_EUI_LEN + 3 * CFG_KEY_LEN + 2 * CFG_ADDR_LEN)

size_t cfg_encode(const config_t *c, uint8_t *out, size_t max)
{
    writer_t w = {out, max, 0};
    w16(&w, CFG_MAGIC);
    w8(&w, (uint8_t)CFG_VERSION);
    w8(&w, c->mode);
    w8(&w, c->nj_mode);
    w8(&w, c->adr);
    w8(&w, c->dr);
    w8(&w, c->txp);
    w8(&w, c->dev_class);
    w8(&w, c->pnm);
    w8(&w, c->dcs);
    w8(&w, c->dwellt);
    w8(&w, c->rpl);
    w8(&w, c->decrypt);
    w8(&w, c->dismacans);
    w8(&w, c->disfcntcheck);
    w8(&w, c->maxnbtrans);
    w8(&w, c->ddetect);
    w8(&w, c->rxdatest);
    w8(&w, c->debug);
    w8(&w, c->port);
    w8(&w, c->cfm);
    w8(&w, c->che);
    w8(&w, c->rx2dr);
    w8(&w, c->rx1wto);
    w8(&w, c->rx2wto);
    w8(&w, c->conf_status);
    for (int i = 0; i < 3; i++) {
        w8(&w, c->intmod[i]);
    }
    w32(&w, c->tdc_ms);
    w32(&w, c->rjtdc_min);
    w32(&w, c->fcu);
    w32(&w, c->fcd);
    w32(&w, c->chs_freq_hz);
    w32(&w, c->rx2freq_hz);
    w32(&w, c->setcnt);
    w16(&w, c->five_vt_ms);
    w16(&w, c->rx1dl_ms);
    w16(&w, c->rx2dl_ms);
    w16(&w, c->jn1dl_ms);
    w16(&w, c->jn2dl_ms);
    w16(&w, c->weigap_x10);
    w32(&w, (uint32_t)c->weigre_zero);
    wblob(&w, c->deveui, CFG_EUI_LEN);
    wblob(&w, c->appeui, CFG_EUI_LEN);
    wblob(&w, c->appkey, CFG_KEY_LEN);
    wblob(&w, c->nwkskey, CFG_KEY_LEN);
    wblob(&w, c->appskey, CFG_KEY_LEN);
    wblob(&w, c->devaddr, CFG_ADDR_LEN);
    wblob(&w, c->nwkid, CFG_ADDR_LEN);

    /* CRC sobre tudo o que foi escrito, incluido o proprio campo do CRC */
    if (w.pos + 2 > max) {
        return 0;
    }
    uint16_t crc = cfg_crc16(out, w.pos);
    w16(&w, crc);
    return (w.pos <= max) ? w.pos : 0;
}

bool cfg_decode(const uint8_t *in, size_t len, config_t *c)
{
    if (len < 3 + 2) {
        return false;
    }
    size_t pos = 0;
    uint16_t magic = r16(in, len, &pos);
    uint8_t version = r8(in, len, &pos);
    if (magic != CFG_MAGIC || version != CFG_VERSION) {
        return false;
    }
    if (cfg_crc16(in, len - 2) != (uint16_t)(in[len - 2] | (in[len - 1] << 8))) {
        return false;
    }

    memset(c, 0, sizeof(*c));
    c->mode = r8(in, len, &pos);
    c->nj_mode = r8(in, len, &pos);
    c->adr = r8(in, len, &pos);
    c->dr = r8(in, len, &pos);
    c->txp = r8(in, len, &pos);
    c->dev_class = r8(in, len, &pos);
    c->pnm = r8(in, len, &pos);
    c->dcs = r8(in, len, &pos);
    c->dwellt = r8(in, len, &pos);
    c->rpl = r8(in, len, &pos);
    c->decrypt = r8(in, len, &pos);
    c->dismacans = r8(in, len, &pos);
    c->disfcntcheck = r8(in, len, &pos);
    c->maxnbtrans = r8(in, len, &pos);
    c->ddetect = r8(in, len, &pos);
    c->rxdatest = r8(in, len, &pos);
    c->debug = r8(in, len, &pos);
    c->port = r8(in, len, &pos);
    c->cfm = r8(in, len, &pos);
    c->che = r8(in, len, &pos);
    c->rx2dr = r8(in, len, &pos);
    c->rx1wto = r8(in, len, &pos);
    c->rx2wto = r8(in, len, &pos);
    c->conf_status = r8(in, len, &pos);
    for (int i = 0; i < 3; i++) {
        c->intmod[i] = r8(in, len, &pos);
    }
    c->tdc_ms = r32(in, len, &pos);
    c->rjtdc_min = r32(in, len, &pos);
    c->fcu = r32(in, len, &pos);
    c->fcd = r32(in, len, &pos);
    c->chs_freq_hz = r32(in, len, &pos);
    c->rx2freq_hz = r32(in, len, &pos);
    c->setcnt = r32(in, len, &pos);
    c->five_vt_ms = r16(in, len, &pos);
    c->rx1dl_ms = r16(in, len, &pos);
    c->rx2dl_ms = r16(in, len, &pos);
    c->jn1dl_ms = r16(in, len, &pos);
    c->jn2dl_ms = r16(in, len, &pos);
    c->weigap_x10 = r16(in, len, &pos);
    c->weigre_zero = (int32_t)r32(in, len, &pos);
    for (size_t i = 0; i < CFG_EUI_LEN; i++) {
        c->deveui[i] = r8(in, len, &pos);
    }
    for (size_t i = 0; i < CFG_EUI_LEN; i++) {
        c->appeui[i] = r8(in, len, &pos);
    }
    for (size_t i = 0; i < CFG_KEY_LEN; i++) {
        c->appkey[i] = r8(in, len, &pos);
    }
    for (size_t i = 0; i < CFG_KEY_LEN; i++) {
        c->nwkskey[i] = r8(in, len, &pos);
    }
    for (size_t i = 0; i < CFG_KEY_LEN; i++) {
        c->appskey[i] = r8(in, len, &pos);
    }
    for (size_t i = 0; i < CFG_ADDR_LEN; i++) {
        c->devaddr[i] = r8(in, len, &pos);
    }
    for (size_t i = 0; i < CFG_ADDR_LEN; i++) {
        c->nwkid[i] = r8(in, len, &pos);
    }
    return pos <= len;
}

/* ------------------------------------------------------------------------- */
/* Hex                                                                        */
/* ------------------------------------------------------------------------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool cfg_hex_to_bytes(const char *text, uint8_t *out, size_t len)
{
    size_t n = 0;
    int hi = -1;
    for (const char *p = text; *p; p++) {
        if (*p == ' ' || *p == ':' || *p == '-' || *p == '\t') {
            continue;
        }
        if (*p == '\r' || *p == '\n') {
            break;
        }
        int v = hexval(*p);
        if (v < 0) {
            return false;
        }
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= len) {
                return false;
            }
            out[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    return (n == len) && (hi < 0);
}

size_t cfg_bytes_to_hex(const uint8_t *in, size_t len, char *out, size_t max,
                        bool upper, char sep)
{
    static const char *lower_digits = "0123456789abcdef";
    static const char *upper_digits = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (n + 3 >= max) {
            break;
        }
        out[n++] = digits[(in[i] >> 4) & 0x0F];
        out[n++] = digits[in[i] & 0x0F];
        if (sep && i + 1 < len) {
            out[n++] = sep;
        }
    }
    out[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------------- */
/* Endereco (o LoRaWAN transporta o DevAddr em big-endian na rede)            */
/* ------------------------------------------------------------------------- */

void cfg_set_devaddr(config_t *c, uint32_t addr)
{
    c->devaddr[0] = (uint8_t)((addr >> 24) & 0xFFu);
    c->devaddr[1] = (uint8_t)((addr >> 16) & 0xFFu);
    c->devaddr[2] = (uint8_t)((addr >> 8) & 0xFFu);
    c->devaddr[3] = (uint8_t)(addr & 0xFFu);
}

uint32_t cfg_get_devaddr(const config_t *c)
{
    return ((uint32_t)c->devaddr[0] << 24) | ((uint32_t)c->devaddr[1] << 16) |
           ((uint32_t)c->devaddr[2] << 8) | (uint32_t)c->devaddr[3];
}

/*
 * lorawan.c - LoRaWAN 1.0.2 Class A para AU915 (subconjunto documentado).
 *
 * Estruturas de quadro conforme o pacote LoRaWAN:
 *   JoinRequest : MHDR(0x00) AppEUI(8 LE) DevEUI(8 LE) DevNonce(2) MIC(4)
 *   JoinAccept  : MHDR(0x20) + 16/28 bytes cifrados com AES-decrypt(AppKey)
 *   DataUp      : MHDR DevAddr(4 LE) FCtrl FCnt(2 LE) [FPort] [FRMPayload] MIC(4)
 *
 * Chaves de sessao OTAA (specl. 1.0.2):
 *   NwkSKey = AES_dec(AppKey, 0x01 | AppNonce(3) | NetID(3) | DevNonce(2) | 0^7)
 *   AppSKey = AES_dec(AppKey, 0x02 | AppNonce(3) | NetID(3) | DevNonce(2) | 0^7)
 *
 * O firmware original identifica-se como "LoRaWan Stack: DR-LWS-007"
 * (string em 0x080082b8) e usa os mesmos campos do AT (AT+NWKSKEY/APPSKEY
 * passam a exibir as chaves derivadas depois do join).
 */
#include "lorawan.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "console.h"
#include "crypto.h"
#include "hw.h"
#include "sx1276.h"

#define MHDR_JOIN_REQ 0x00u
#define MHDR_JOIN_ACC 0x20u
#define MHDR_UP_UNCONF 0x40u
#define MHDR_UP_CONF 0x80u
#define MHDR_DOWN_UNCONF 0x60u
#define MHDR_DOWN_CONF 0x80u

#define LORAWAN_PUBLIC_SYNC_WORD 0x34u
#define LORAWAN_PRIVATE_SYNC_WORD 0x12u

static config_t *g_cfg;
static bool (*g_apply_at)(const char *);
static bool g_joined;
static uint16_t g_devnonce;
static int16_t g_last_rssi;
static int16_t g_last_snr_x4;
static uint8_t g_downlink[64];
static size_t g_downlink_len;
static uint8_t g_downlink_port;

/* ------------------------------------------------------------------------ */
/* Plano de frequencia AU915 (manual/AT doc, secao 5.19 "AT+CHE")            */
/* ------------------------------------------------------------------------ */
/* CHE=1: 915,2 915,4 ... 916,6 MHz (200 kHz de espacamento, 125 kHz de BW) */
#define AU915_UP_FIRST_HZ 915200000u
#define AU915_UP_STEP_HZ 200000u
#define AU915_DOWN_FIRST_HZ 923300000u /* RX1 do grupo 0-7 */
#define AU915_RX2_HZ 923300000u

static uint32_t uplink_freq_hz(uint8_t channel)
{
    if (g_cfg && g_cfg->chs_freq_hz) {
        return g_cfg->chs_freq_hz; /* AT+CHS: canal unico */
    }
    return AU915_UP_FIRST_HZ + (uint32_t)channel * AU915_UP_STEP_HZ;
}

static uint32_t rx1_freq_hz(uint8_t channel)
{
    if (g_cfg && g_cfg->chs_freq_hz) {
        return g_cfg->chs_freq_hz;
    }
    return AU915_DOWN_FIRST_HZ + (uint32_t)(channel / 8u) * 600000u;
}

static void dr_to_radio(uint8_t dr, uint8_t *sf, uint8_t *bw)
{
    /* AU915: DR0..DR5 = SF12..SF7 em 125 kHz; DR8..DR13 = SF12..SF7 em 500 kHz */
    if (dr <= 5u) {
        *sf = (uint8_t)(12u - dr);
        *bw = SX_BW_125;
    } else if (dr >= 8u && dr <= 13u) {
        *sf = (uint8_t)(12u - (dr - 8u));
        *bw = SX_BW_500;
    } else {
        *sf = 12u;
        *bw = SX_BW_125;
    }
}

/* ------------------------------------------------------------------------ */
/* Utilitarios                                                               */
/* ------------------------------------------------------------------------ */

/* EUI/endereco vao no ar em little-endian; a config os guarda em big-endian
 * (ordem de exibicao dos AT commands). */
static void le_copy(uint8_t *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[len - 1 - i];
    }
}

static void mic_compute(const uint8_t key[16], uint8_t dir, const uint8_t *addr_le,
                        uint32_t fcnt, const uint8_t *msg, size_t len,
                        uint8_t mic[4])
{
    uint8_t buf[16 + 256];
    if (len > 256u) {
        len = 256u;
    }
    memset(buf, 0, 16);
    buf[0] = 0x49u;                     /* direcao uplink (bloco B0) */
    buf[5] = (uint8_t)(dir ? 0x01u : 0x00u);
    memcpy(&buf[6], addr_le, 4);
    buf[11] = (uint8_t)(fcnt & 0xFFu);
    buf[12] = (uint8_t)((fcnt >> 8) & 0xFFu);
    buf[13] = (uint8_t)((fcnt >> 16) & 0xFFu);
    buf[14] = (uint8_t)((fcnt >> 24) & 0xFFu);
    buf[15] = (uint8_t)len;
    memcpy(buf + 16, msg, len);

    uint8_t mac[16];
    aes128_cmac(key, buf, 16 + len, mac);
    memcpy(mic, mac, 4);
}

/* Cifra/decifra o FRMPayload (AES-128 ECB com blocos Ai). */
static void payload_crypt(const uint8_t key[16], const uint8_t *addr_le,
                          uint32_t fcnt, uint8_t dir, uint8_t *data, size_t len)
{
    aes128_ctx_t ctx;
    aes128_init(&ctx, key);
    for (size_t block = 0; block * 16u < len; block++) {
        uint8_t a[16];
        memset(a, 0, sizeof(a));
        a[0] = 0x01u;
        a[5] = (uint8_t)(dir ? 0x01u : 0x00u);
        memcpy(&a[6], addr_le, 4);
        a[10] = (uint8_t)(fcnt & 0xFFu);
        a[11] = (uint8_t)((fcnt >> 8) & 0xFFu);
        a[12] = (uint8_t)((fcnt >> 16) & 0xFFu);
        a[13] = (uint8_t)((fcnt >> 24) & 0xFFu);
        a[15] = (uint8_t)(block + 1u);

        uint8_t ks[16];
        aes128_encrypt_block(&ctx, a, ks);
        for (size_t i = 0; i < 16u && block * 16u + i < len; i++) {
            data[block * 16u + i] ^= ks[i];
        }
    }
}

/* DevNonce persistido fora do blob de configuracao, para nao inflar o codec. */
static void devnonce_store(uint16_t value)
{
    hw_eeprom_write(BOARD_CFG_OFFSET_B, &value, sizeof(value));
}

static uint16_t devnonce_load(void)
{
    uint16_t v = 0;
    if (!hw_eeprom_read(BOARD_CFG_OFFSET_B, &v, sizeof(v))) {
        return 0;
    }
    return (v == 0xFFFFu) ? 0 : v;
}

/* ------------------------------------------------------------------------ */
/* Inicializacao                                                             */
/* ------------------------------------------------------------------------ */

void lw_init(config_t *cfg, bool (*apply_at)(const char *line))
{
    g_cfg = cfg;
    g_apply_at = apply_at;
    g_joined = false;
    g_devnonce = devnonce_load();

    sx1276_set_sync_word(cfg->pnm ? LORAWAN_PUBLIC_SYNC_WORD
                                  : LORAWAN_PRIVATE_SYNC_WORD);
    sx1276_set_preamble(8);
    sx1276_set_crc(true);
    uint8_t sf, bw;
    dr_to_radio(cfg->dr, &sf, &bw);
    sx1276_set_sf(sf);
    sx1276_set_bw(bw);
    sx1276_set_cr(1);

    /* TXP: 0..5 -> 20..14 dBm (Max EIRP AU915 = 30 dBm, mas o chip entrega 20);
     * 40..50 -> 10..20 dBm direto, como no AT+TXP do original. */
    int8_t dbm;
    if (cfg->txp >= 40u) {
        dbm = (int8_t)(cfg->txp - 30);
    } else {
        dbm = (int8_t)(20 - (cfg->txp > 5u ? 5u : cfg->txp));
    }
    sx1276_set_tx_power_dbm(dbm);

    sx1276_sleep();
}

bool lw_radio_ok(void)
{
    return sx1276_present();
}

bool lw_joined(void)
{
    return g_joined;
}

void lw_set_joined(bool joined)
{
    g_joined = joined;
}

int16_t lw_last_rssi(void)
{
    return g_last_rssi;
}

int16_t lw_last_snr_x4(void)
{
    return g_last_snr_x4;
}

size_t lw_last_downlink(const uint8_t **data, uint8_t *port)
{
    *data = g_downlink;
    *port = g_downlink_port;
    return g_downlink_len;
}

/* ------------------------------------------------------------------------ */
/* Join                                                                      */
/* ------------------------------------------------------------------------ */

static bool handle_join_accept(const uint8_t *frame, size_t len)
{
    uint8_t plain[33];
    uint8_t mac[16];

    if (len < 17u || len > 33u) {
        return false;
    }
    /* O JoinAccept e' cifrado com AES-decrypt usando a AppKey */
    aes128_ctx_t ctx;
    aes128_init(&ctx, g_cfg->appkey);
    for (size_t i = 0; i + 16 <= len - 1u; i += 16) {
        uint8_t block[16];
        memcpy(block, frame + 1 + i, 16);
        aes128_decrypt_block(&ctx, block, plain + i);
    }

    /* MIC do JoinAccept (LoRaWAN 1.0.2): CMAC(AppKey, MHDR|AppNonce|NetID
     * |DevAddr|DLSettings|RxDelay|CFList) - sem bloco B0 - truncado em 4. */
    aes128_cmac(g_cfg->appkey, frame, len - 4u, mac);
    if (memcmp(mac, frame + len - 4u, 4) != 0) {
        console_puts("Join Accept com MIC invalido\r\n");
        return false;
    }

    /* plain = AppNonce(3) NetID(3) DevAddr(4) [DLSettings RxDelay] [CFList] */
    uint8_t app_nonce[3] = {plain[0], plain[1], plain[2]};
    uint8_t net_id[3] = {plain[3], plain[4], plain[5]};
    uint8_t dev_addr[4] = {plain[6], plain[7], plain[8], plain[9]};

    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x01u;
    memcpy(&buf[1], app_nonce, 3);
    memcpy(&buf[4], net_id, 3);
    buf[7] = (uint8_t)(g_devnonce & 0xFFu);
    buf[8] = (uint8_t)(g_devnonce >> 8);
    aes128_ecb_decrypt(g_cfg->appkey, buf, g_cfg->nwkskey);

    memset(buf, 0, sizeof(buf));
    buf[0] = 0x02u;
    memcpy(&buf[1], app_nonce, 3);
    memcpy(&buf[4], net_id, 3);
    buf[7] = (uint8_t)(g_devnonce & 0xFFu);
    buf[8] = (uint8_t)(g_devnonce >> 8);
    aes128_ecb_decrypt(g_cfg->appkey, buf, g_cfg->appskey);

    /* DevAddr chega em little-endian; guardamos na ordem de exibicao */
    for (int i = 0; i < 4; i++) {
        g_cfg->devaddr[i] = dev_addr[3 - i];
    }
    memset(g_cfg->nwkid, 0, 4);
    g_cfg->nwkid[0] = net_id[2];
    g_cfg->nwkid[1] = net_id[1];
    g_cfg->nwkid[2] = net_id[0];

    g_cfg->fcu = 0;
    g_cfg->fcd = 0;
    g_joined = true;
    console_printf("Join Accept:\r\n");
    console_printf("Rx1DrOffset:%d\r\n", 0);
    console_printf("Rx2Datarate:%d\r\n", (int)g_cfg->rx2dr);
    console_printf("ReceiveDelay1:%lu ms\r\n", (unsigned long)g_cfg->rx1dl_ms);
    console_printf("ReceiveDelay2:%lu ms\r\n", (unsigned long)g_cfg->rx2dl_ms);
    return true;
}

bool lw_join(config_t *cfg)
{
    if (cfg->nj_mode == 0) { /* ABP: nada a fazer */
        g_joined = true;
        return true;
    }

    uint8_t frame[23];
    frame[0] = MHDR_JOIN_REQ;
    le_copy(&frame[1], cfg->appeui, 8);
    le_copy(&frame[9], cfg->deveui, 8);
    g_devnonce++;
    frame[17] = (uint8_t)(g_devnonce & 0xFFu);
    frame[18] = (uint8_t)(g_devnonce >> 8);

    /* MIC do JoinRequest: CMAC(AppKey, MHDR|AppEUI|DevEUI|DevNonce) */
    uint8_t mac[16];
    aes128_cmac(cfg->appkey, frame, 19, mac);
    memcpy(&frame[19], mac, 4);

    uint8_t sf, bw;
    dr_to_radio(cfg->dr, &sf, &bw);
    sx1276_set_sf(sf);
    sx1276_set_bw(bw);
    sx1276_set_frequency(uplink_freq_hz(0));

    if (!sx1276_tx(frame, 23, 3000)) {
        console_puts("Falha ao transmitir JoinRequest\r\n");
        return false;
    }

    /* Janelas de join: RX1 em JN1DL, RX2 em JN2DL */
    uint32_t windows[2] = {cfg->jn1dl_ms, cfg->jn2dl_ms};
    for (int w = 0; w < 2; w++) {
        uint32_t previous = (w == 0) ? 0u : windows[0];
        if (windows[w] > previous) {
            hw_delay_ms(windows[w] - previous);
        }
        sx1276_set_frequency(w ? cfg->rx2freq_hz : rx1_freq_hz(0));
        if (w == 1) {
            uint8_t rx_sf, rx_bw;
            dr_to_radio((uint8_t)cfg->rx2dr, &rx_sf, &rx_bw);
            sx1276_set_sf(rx_sf);
            sx1276_set_bw(rx_bw);
        }
        sx1276_rx_start(true, 33, false);
        uint32_t start = hw_millis();
        while ((uint32_t)(hw_millis() - start) < 1200u) {
            if (sx1276_rx_done()) {
                uint8_t buf[64];
                size_t len = sx1276_rx_read(buf, sizeof(buf));
                g_last_rssi = sx1276_packet_rssi_dbm();
                g_last_snr_x4 = sx1276_packet_snr_x4();
                if (len >= 17u && (buf[0] & 0xE0u) == MHDR_JOIN_ACC) {
                    if (handle_join_accept(buf, len)) {
                        sx1276_rx_abort();
                        devnonce_store(g_devnonce);
                        return true;
                    }
                }
                break;
            }
        }
        sx1276_rx_abort();
    }
    devnonce_store(g_devnonce);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Uplink                                                                    */
/* ------------------------------------------------------------------------ */

bool lw_uplink(config_t *cfg, const uint8_t *data, size_t len, uint8_t port,
               bool confirm)
{
    uint8_t frame[256];
    uint8_t addr_le[4];

    if (!g_joined && cfg->nj_mode == 1) {
        return false;
    }
    if (len > 240u) {
        return false;
    }

    le_copy(addr_le, cfg->devaddr, 4);

    size_t pos = 0;
    frame[pos++] = confirm ? MHDR_UP_CONF : MHDR_UP_UNCONF;
    memcpy(&frame[pos], addr_le, 4);
    pos += 4;
    frame[pos++] = (uint8_t)((cfg->adr ? 0x80u : 0x00u) | 0x00u); /* FCtrl */
    frame[pos++] = (uint8_t)(cfg->fcu & 0xFFu);
    frame[pos++] = (uint8_t)((cfg->fcu >> 8) & 0xFFu);
    frame[pos++] = port;
    memcpy(&frame[pos], data, len);
    payload_crypt(cfg->appskey, addr_le, cfg->fcu, 0, &frame[pos], len);
    pos += len;

    uint8_t mic[4];
    mic_compute(cfg->nwkskey, 0, addr_le, cfg->fcu, frame, pos, mic);
    memcpy(&frame[pos], mic, 4);
    pos += 4;

    uint8_t channel = (uint8_t)(cfg->fcu % 8u);
    sx1276_set_frequency(uplink_freq_hz(channel));
    uint8_t sf, bw;
    dr_to_radio(cfg->dr, &sf, &bw);
    sx1276_set_sf(sf);
    sx1276_set_bw(bw);

    if (!sx1276_tx(frame, pos, 4000)) {
        return false;
    }
    cfg->fcu++;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Downlink                                                                  */
/* ------------------------------------------------------------------------ */

/* Comandos MAC especificos do LSN50 (manual 2.5.2):
 *   0x0A aa       -> AT+MOD=aa
 *   0x06 00 00 bb -> AT+INTMOD=bb
 *   0x07 aa bb    -> AT+5VT=aabb
 *   0x08 01       -> AT+WEIGRE
 *   0x08 02 aa bb -> AT+WEIGAP=aabb/10
 */
static bool apply_mac_commands(const uint8_t *data, size_t len)
{
    char cmd[48];
    bool changed = false;

    for (size_t i = 0; i < len;) {
        uint8_t id = data[i++] >> 4; /* parte alta: comando */
        uint8_t n = data[i - 1] & 0x0Fu;
        if (i + n > len) {
            break;
        }
        switch (id) {
        case 0x0A:
            if (n >= 1u) {
                snprintf(cmd, sizeof(cmd), "AT+MOD=%u", (unsigned)data[i]);
                changed |= g_apply_at && g_apply_at(cmd);
            }
            break;
        case 0x06:
            if (n >= 1u) {
                snprintf(cmd, sizeof(cmd), "AT+INTMOD=%u", (unsigned)data[i + (n - 1)]);
                changed |= g_apply_at && g_apply_at(cmd);
            }
            break;
        case 0x07:
            if (n >= 2u) {
                uint16_t v = (uint16_t)((data[i] << 8) | data[i + 1]);
                snprintf(cmd, sizeof(cmd), "AT+5VT=%u", (unsigned)v);
                changed |= g_apply_at && g_apply_at(cmd);
            }
            break;
        case 0x08:
            if (n >= 1u && data[i] == 0x01u) {
                changed |= g_apply_at && g_apply_at("AT+WEIGRE");
            } else if (n >= 3u && data[i] == 0x02u) {
                uint16_t v = (uint16_t)((data[i + 1] << 8) | data[i + 2]);
                snprintf(cmd, sizeof(cmd), "AT+WEIGAP=%u.%u", (unsigned)(v / 10u),
                         (unsigned)(v % 10u));
                changed |= g_apply_at && g_apply_at(cmd);
            }
            break;
        default:
            console_printf("MAC command 0x%02X nao tratado\r\n", (unsigned)id);
            break;
        }
        i += n;
    }
    return changed;
}

static bool apply_downlink(const uint8_t *frame, size_t len)
{
    if (len < 12u) {
        return false;
    }
    uint8_t fctrl = frame[5];
    uint8_t fopts_len = (uint8_t)(fctrl & 0x0Fu);
    size_t pos = (size_t)8u + (size_t)fopts_len;
    if (pos > len - 4u) {
        return false;
    }

    bool changed = false;
    if (fopts_len) {
        changed |= apply_mac_commands(&frame[8], fopts_len);
    }

    if (pos < len - 4u) { /* ha FPort + FRMPayload */
        uint8_t port = frame[pos++];
        size_t payload_len = len - 4u - pos;
        if (payload_len == 0u) {
            return changed;
        }
        memcpy(g_downlink, &frame[pos], payload_len);
        g_downlink_len = payload_len;
        g_downlink_port = port;

        /* O FRMPayload é cifrado; a decifragem usa a AppSKey e o FCnt do
         * downlink (AT+FCD). */
        uint8_t addr_le[4];
        le_copy(addr_le, g_cfg->devaddr, 4);
        payload_crypt(g_cfg->appskey, addr_le, g_cfg->fcd, 1, g_downlink,
                      g_downlink_len);
        g_cfg->fcd++;

        if (port == 0u) {
            changed |= apply_mac_commands(g_downlink, g_downlink_len);
        } else {
            /* Texto de comando AT na porta de aplicacao - o original tambem
             * aceita configuracao por downlink ("Change config if needed" no
             * fluxograma do manual 2.1). */
            char line[64];
            size_t n = g_downlink_len < sizeof(line) - 1u ? g_downlink_len
                                                          : sizeof(line) - 1u;
            memcpy(line, g_downlink, n);
            line[n] = '\0';
            console_printf("Downlink porta %u: %s\r\n", (unsigned)port, line);
            changed |= g_apply_at && g_apply_at(line);
        }
    }
    return changed;
}

bool lw_receive_windows(config_t *cfg, bool expect_downlink)
{
    uint8_t buf[64];
    uint8_t channel = (uint8_t)((cfg->fcu == 0 ? 0u : cfg->fcu - 1u) % 8u);
    bool changed = false;

    (void)expect_downlink;

    hw_delay_ms(cfg->rx1dl_ms);
    sx1276_set_frequency(rx1_freq_hz(channel));
    {
        /* No AU915 a janela RX1 usa o mesmo data rate do uplink */
        uint8_t sf, bw;
        dr_to_radio(cfg->dr, &sf, &bw);
        sx1276_set_sf(sf);
        sx1276_set_bw(bw);
    }
    sx1276_rx_start(true, 64, false);

    uint32_t start = hw_millis();
    while ((uint32_t)(hw_millis() - start) < 1000u) {
        if (sx1276_rx_done()) {
            size_t len = sx1276_rx_read(buf, sizeof(buf));
            g_last_rssi = sx1276_packet_rssi_dbm();
            g_last_snr_x4 = sx1276_packet_snr_x4();
            if (len >= 12u) {
                changed |= apply_downlink(buf, len);
            }
            sx1276_rx_abort();
            return changed;
        }
    }
    sx1276_rx_abort();

    /* RX2 */
    hw_delay_ms(cfg->rx2dl_ms > cfg->rx1dl_ms
                    ? (uint32_t)(cfg->rx2dl_ms - cfg->rx1dl_ms)
                    : 0u);
    sx1276_set_frequency(cfg->rx2freq_hz);
    {
        uint8_t sf, bw;
        dr_to_radio((uint8_t)cfg->rx2dr, &sf, &bw);
        sx1276_set_sf(sf);
        sx1276_set_bw(bw);
    }
    sx1276_rx_start(true, 64, false);
    start = hw_millis();
    while ((uint32_t)(hw_millis() - start) < 1000u) {
        if (sx1276_rx_done()) {
            size_t len = sx1276_rx_read(buf, sizeof(buf));
            g_last_rssi = sx1276_packet_rssi_dbm();
            g_last_snr_x4 = sx1276_packet_snr_x4();
            if (len >= 12u) {
                changed |= apply_downlink(buf, len);
            }
            break;
        }
    }
    sx1276_rx_abort();
    return changed;
}

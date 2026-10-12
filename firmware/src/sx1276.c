/*
 * sx1276.c - driver minimo do SX1276/78 sobre SPI1.
 *
 * Sequencia de inicializacao seguindo a secao 4 do datasheet do SX1276
 * (modo LoRa, PA_BOOST) e as notas de aplicacao do LoRaWAN 1.0.2 para o modo
 * de recepcao com cabecalho implicito.
 */
#include "sx1276.h"

#include "board.h"
#include "hw.h"

/* Registradores */
#define REG_FIFO 0x00
#define REG_OP_MODE 0x01
#define REG_FRF_MSB 0x06
#define REG_FRF_MID 0x07
#define REG_FRF_LSB 0x08
#define REG_PA_CONFIG 0x09
#define REG_PA_RAMP 0x0A
#define REG_OCP 0x0B
#define REG_LNA 0x0C
#define REG_FIFO_ADDR_PTR 0x0D
#define REG_FIFO_TX_BASE 0x0E
#define REG_FIFO_RX_BASE 0x0F
#define REG_FIFO_RX_CURRENT 0x10
#define REG_IRQ_FLAGS_MASK 0x11
#define REG_IRQ_FLAGS 0x12
#define REG_RX_NB_BYTES 0x13
#define REG_PKT_SNR_VALUE 0x19
#define REG_PKT_RSSI_VALUE 0x1A
#define REG_MODEM_CONFIG1 0x1D
#define REG_MODEM_CONFIG2 0x1E
#define REG_SYMB_TIMEOUT_LSB 0x1F
#define REG_PREAMBLE_MSB 0x20
#define REG_PREAMBLE_LSB 0x21
#define REG_PAYLOAD_LENGTH 0x22
#define REG_MODEM_CONFIG3 0x26
#define REG_DETECT_OPTIMIZE 0x31
#define REG_DETECTION_THRESHOLD 0x37
#define REG_SYNC_WORD 0x39
#define REG_DIO_MAPPING1 0x40
#define REG_VERSION 0x42
#define REG_PA_DAC 0x4D

/* Modos do RegOpMode */
#define MODE_SLEEP 0x00
#define MODE_STDBY 0x01
#define MODE_TX 0x03
#define MODE_RX_CONT 0x05
#define MODE_RX_SINGLE 0x06
#define MODE_LORA 0x80

/* Flags de IRQ */
#define IRQ_RX_TIMEOUT 0x80
#define IRQ_RX_DONE 0x40
#define IRQ_CRC_ERROR 0x20
#define IRQ_VALID_HEADER 0x10
#define IRQ_TX_DONE 0x08
#define IRQ_CAD_DONE 0x04
#define IRQ_FHSS_CHANGE 0x02
#define IRQ_CAD_DETECTED 0x01

static uint8_t g_sf = 12;
static uint8_t g_bw = SX_BW_125;
static uint8_t g_cr = 1;
static bool g_crc = true;
static uint8_t g_sync = 0x34; /* rede publica (LoRaWAN) */
static uint32_t g_freq = 915200000u;

static void cs(bool low)
{
    hw_gpio_write(BOARD_RADIO_NSS_PORT, BOARD_RADIO_NSS_PIN, !low);
}

static uint8_t read_reg(uint8_t reg)
{
    uint8_t v;
    cs(true);
    hw_spi_xfer(reg & 0x7Fu);
    v = hw_spi_xfer(0x00);
    cs(false);
    return v;
}

static void write_reg(uint8_t reg, uint8_t value)
{
    cs(true);
    hw_spi_xfer((uint8_t)(reg | 0x80u));
    hw_spi_xfer(value);
    cs(false);
}

static void write_burst(uint8_t reg, const uint8_t *data, size_t len)
{
    cs(true);
    hw_spi_xfer((uint8_t)(reg | 0x80u));
    for (size_t i = 0; i < len; i++) {
        hw_spi_xfer(data[i]);
    }
    cs(false);
}

static void read_burst(uint8_t reg, uint8_t *data, size_t len)
{
    cs(true);
    hw_spi_xfer(reg & 0x7Fu);
    for (size_t i = 0; i < len; i++) {
        data[i] = hw_spi_xfer(0x00);
    }
    cs(false);
}

static uint8_t bw_bits(uint8_t bw)
{
    switch (bw) {
    case SX_BW_500:
        return 1u;
    case SX_BW_250:
        return 2u;
    default:
        return 7u; /* 125 kHz */
    }
}

static void apply_modem_config1(bool implicit_header, uint8_t payload_len)
{
    uint8_t v = (uint8_t)((bw_bits(g_bw) << 4) | ((g_cr & 0x07u) << 1) |
                          (implicit_header ? 1u : 0u));
    write_reg(REG_MODEM_CONFIG1, v);
    if (implicit_header) {
        write_reg(REG_PAYLOAD_LENGTH, payload_len);
    }
}

static void apply_modem_config2(void)
{
    uint8_t v = (uint8_t)((g_sf << 4) | (g_crc ? 0x04u : 0u) | 0x03u);
    write_reg(REG_MODEM_CONFIG2, v);
    /* 0x26: LNA gain automatico + AGC */
    write_reg(REG_MODEM_CONFIG3, 0x04u);
    if (g_sf >= 11u) {
        write_reg(REG_DETECT_OPTIMIZE, 0xC3u);
        write_reg(REG_DETECTION_THRESHOLD, 0x0Au);
    } else {
        write_reg(REG_DETECT_OPTIMIZE, 0xC1u);
        write_reg(REG_DETECTION_THRESHOLD, 0x04u);
    }
    write_reg(REG_SYMB_TIMEOUT_LSB, 0x64u);
}

bool sx1276_init(void)
{
    hw_spi_init();
    hw_gpio_write(BOARD_RADIO_RST_PORT, BOARD_RADIO_RST_PIN, false);
    hw_delay_ms(10);
    hw_gpio_write(BOARD_RADIO_RST_PORT, BOARD_RADIO_RST_PIN, true);
    hw_delay_ms(20);

    if (read_reg(REG_VERSION) != 0x12u) {
        return false;
    }

    sx1276_sleep();
    write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_STDBY));
    write_reg(REG_FIFO_TX_BASE, 0x00u);
    write_reg(REG_FIFO_RX_BASE, 0x00u);
    write_reg(REG_LNA, read_reg(REG_LNA) | 0x03u); /* LNA boost */
    write_reg(REG_OCP, 0x2Bu);                     /* 100 mA */
    write_reg(REG_PA_RAMP, 0x08u);                 /* 50 us */
    write_reg(REG_PA_DAC, 0x84u);                  /* habilita +20 dBm */
    write_reg(REG_PA_CONFIG, (uint8_t)(0x80u | 0x0Fu));
    write_reg(REG_SYNC_WORD, g_sync);

    sx1276_set_frequency(g_freq);
    apply_modem_config1(false, 0);
    apply_modem_config2();
    sx1276_set_preamble(8);
    return true;
}

bool sx1276_present(void)
{
    return read_reg(REG_VERSION) == 0x12u;
}

void sx1276_sleep(void)
{
    write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_SLEEP));
}

void sx1276_standby(void)
{
    write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_STDBY));
}

void sx1276_set_frequency(uint32_t hz)
{
    g_freq = hz;
    uint64_t frf = ((uint64_t)hz << 19) / 32000000u;
    write_reg(REG_FRF_MSB, (uint8_t)(frf >> 16));
    write_reg(REG_FRF_MID, (uint8_t)(frf >> 8));
    write_reg(REG_FRF_LSB, (uint8_t)(frf >> 0));
}

void sx1276_set_tx_power_dbm(int8_t dbm)
{
    /* PA_BOOST: 2..17 dBm direto; 20 dBm com PaDac = 0x87 */
    if (dbm > 17) {
        write_reg(REG_PA_DAC, 0x87u);
        dbm = 20;
    } else {
        write_reg(REG_PA_DAC, 0x84u);
    }
    if (dbm < 2) {
        dbm = 2;
    }
    write_reg(REG_PA_CONFIG, (uint8_t)(0x80u | (dbm - 2)));
}

void sx1276_set_sf(uint8_t sf)
{
    g_sf = sf;
    apply_modem_config2();
}

void sx1276_set_bw(uint8_t bw)
{
    g_bw = bw;
    apply_modem_config1(false, 0);
}

void sx1276_set_cr(uint8_t cr)
{
    g_cr = cr;
    apply_modem_config1(false, 0);
}

void sx1276_set_sync_word(uint8_t sw)
{
    g_sync = sw;
    write_reg(REG_SYNC_WORD, sw);
}

void sx1276_set_preamble(uint16_t symbols)
{
    write_reg(REG_PREAMBLE_MSB, (uint8_t)(symbols >> 8));
    write_reg(REG_PREAMBLE_LSB, (uint8_t)(symbols & 0xFFu));
}

void sx1276_set_crc(bool on)
{
    g_crc = on;
    apply_modem_config2();
}

bool sx1276_tx(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (len > 255u) {
        return false;
    }
    sx1276_standby();
    write_reg(REG_IRQ_FLAGS, 0xFFu);
    write_reg(REG_FIFO_ADDR_PTR, 0x00u);
    apply_modem_config1(false, 0); /* cabecalho explicito no TX */
    write_reg(REG_IRQ_FLAGS_MASK, (uint8_t)~IRQ_TX_DONE);
    write_burst(REG_FIFO, data, len);
    write_reg(REG_PAYLOAD_LENGTH, (uint8_t)len);
    write_reg(REG_OP_MODE, (uint8_t)(MODE_LORA | MODE_TX));

    uint32_t start = hw_millis();
    while ((read_reg(REG_IRQ_FLAGS) & IRQ_TX_DONE) == 0) {
        if ((uint32_t)(hw_millis() - start) > timeout_ms) {
            sx1276_standby();
            return false;
        }
    }
    write_reg(REG_IRQ_FLAGS, IRQ_TX_DONE);
    sx1276_standby();
    return true;
}

void sx1276_rx_start(bool implicit_header, uint8_t expected_len, bool continuous)
{
    sx1276_standby();
    write_reg(REG_IRQ_FLAGS, 0xFFu);
    write_reg(REG_FIFO_ADDR_PTR, 0x00u);
    write_reg(REG_FIFO_RX_BASE, 0x00u);
    apply_modem_config1(implicit_header, expected_len);
    apply_modem_config2();
    write_reg(REG_IRQ_FLAGS_MASK, (uint8_t)~(IRQ_RX_DONE | IRQ_RX_TIMEOUT));
    write_reg(REG_OP_MODE,
              (uint8_t)(MODE_LORA | (continuous ? MODE_RX_CONT : MODE_RX_SINGLE)));
}

bool sx1276_rx_done(void)
{
    uint8_t flags = read_reg(REG_IRQ_FLAGS);
    if (flags & IRQ_RX_DONE) {
        write_reg(REG_IRQ_FLAGS, IRQ_RX_DONE);
        if ((flags & IRQ_CRC_ERROR) && g_crc) {
            return false;
        }
        return true;
    }
    if (flags & IRQ_RX_TIMEOUT) {
        write_reg(REG_IRQ_FLAGS, IRQ_RX_TIMEOUT);
    }
    return false;
}

size_t sx1276_rx_read(uint8_t *out, size_t max)
{
    uint8_t len = read_reg(REG_RX_NB_BYTES);
    uint8_t cur = read_reg(REG_FIFO_RX_CURRENT);
    if (len > max) {
        len = (uint8_t)max;
    }
    write_reg(REG_FIFO_ADDR_PTR, cur);
    read_burst(REG_FIFO, out, len);
    return len;
}

int16_t sx1276_packet_rssi_dbm(void)
{
    /* RSSI = -157 + PktRssi (HF port); o pacote de baixa frequencia soma
     * offset diferente, ver datasheet secao 5.5.5. */
    return (int16_t)(-157 + read_reg(REG_PKT_RSSI_VALUE));
}

int16_t sx1276_packet_snr_x4(void)
{
    return (int16_t)((int8_t)read_reg(REG_PKT_SNR_VALUE));
}

void sx1276_rx_abort(void)
{
    sx1276_standby();
    write_reg(REG_IRQ_FLAGS, 0xFFu);
}

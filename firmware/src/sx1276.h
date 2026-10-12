/*
 * sx1276.h - driver do radio SX1276/SX1278 (SPI1).
 */
#ifndef SX1276_H
#define SX1276_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Regioes declaradas no manual (1.3): CN470/EU433/KR920/US915/IN865/EU868/AS923/AU915 */
enum { SX_BW_125 = 0, SX_BW_250 = 1, SX_BW_500 = 2 };

bool sx1276_init(void);
bool sx1276_present(void);
void sx1276_sleep(void);
void sx1276_standby(void);

void sx1276_set_frequency(uint32_t hz);
void sx1276_set_tx_power_dbm(int8_t dbm);
void sx1276_set_sf(uint8_t sf);
void sx1276_set_bw(uint8_t bw);
void sx1276_set_cr(uint8_t cr); /* 1..4 -> 4/5..4/8 */
void sx1276_set_sync_word(uint8_t sw);
void sx1276_set_preamble(uint16_t symbols);
void sx1276_set_crc(bool on);

/* Transmissao explicita (LoRaWAN): payload com cabecalho explicito. */
bool sx1276_tx(const uint8_t *data, size_t len, uint32_t timeout_ms);

/* Recepcao: o LoRaWAN usa cabecalho implicito e tamanho conhecido. */
void sx1276_rx_start(bool implicit_header, uint8_t expected_len, bool continuous);
bool sx1276_rx_done(void);
size_t sx1276_rx_read(uint8_t *out, size_t max);
int16_t sx1276_packet_rssi_dbm(void);
int16_t sx1276_packet_snr_x4(void);
void sx1276_rx_abort(void);

#endif /* SX1276_H */

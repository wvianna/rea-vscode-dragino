/*
 * lorawan.h - camada LoRaWAN Class A (ABP e OTAA) sobre o SX1276.
 *
 * Subconjunto implementado: join OTAA, uplink nao confirmado/confirmado,
 * janelas RX1/RX2 e downlink (comandos MAC documentados para o LSN50 e, na
 * porta de aplicacao, texto de comando AT).
 */
#ifndef LORAWAN_H
#define LORAWAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/* Deve ser chamada depois do console e do radio. */
void lw_init(config_t *cfg, bool (*apply_at)(const char *line));
bool lw_radio_ok(void);

bool lw_joined(void);
/* Faz o join quando NJM=OTAA; com NJM=ABP apenas marca a sessao como ativa. */
bool lw_join(config_t *cfg);
void lw_set_joined(bool joined);

/* Envia o payload e abre as janelas de recepcao.
 * Retorna true se o pacote foi transmitido; o status de confirmacao fica em
 * cfg->conf_status (AT+CFS). */
bool lw_uplink(config_t *cfg, const uint8_t *data, size_t len, uint8_t port,
               bool confirm);
/* Abre RX1/RX2 e aplica o downlink. Retorna true se a config mudou. */
bool lw_receive_windows(config_t *cfg, bool expect_downlink);

int16_t lw_last_rssi(void);
int16_t lw_last_snr_x4(void);
size_t lw_last_downlink(const uint8_t **data, uint8_t *port);

#endif /* LORAWAN_H */

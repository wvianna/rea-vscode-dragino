/*
 * at_cmds.h - tabela de comandos AT do no' (equivalente ao conjunto do
 * firmware LSN50), ligada ao nucleo portatil em core/at.h.
 */
#ifndef AT_CMDS_H
#define AT_CMDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at.h"
#include "config.h"
#include "payload.h"

typedef struct {
    config_t cfg;
    bool cfg_dirty;       /* precisa gravar na EEPROM */
    bool reboot;          /* ATZ */
    bool join_request;    /* AT+JOIN */
    bool send_request;    /* AT+SEND / AT+SENDB */
    bool sample_request;  /* AT+GETSENSORVALUE */
    bool confirm;         /* AT+CFM=1 */
    bool have_sample;
    sample_t sample;
    uint8_t tx_port;
    uint8_t tx_data[64];
    size_t tx_len;
    bool send_hex;        /* estado interno de AT+SENDB */
} app_t;

extern app_t g_app;

void at_cmds_init(app_t *app);
const at_cmd_t *at_cmd_table(size_t *count);
at_ctx_t *at_cmds_context(void);

/* Executa uma linha AT vinda de outra fonte (downlink). Retorna true se a
 * configuracao mudou. */
bool at_cmd_apply_text(const char *line);

/* Grava a configuracao na EEPROM se algum handler a alterou. */
void at_cmds_save_if_dirty(void);

/* Atualiza a ultima amostra lida (AT+GETSENSORVALUE / AT+CFG). */
void at_cmds_set_sample(const sample_t *s);
size_t at_cmds_build_payload(uint8_t *out, size_t max);

#endif /* AT_CMDS_H */

/*
 * at.h - motor de comandos AT.
 *
 * Reproduz o contrato documentado em "Dragino LoRa AT Command Sets v1.6.3"
 * (secao 1.1) e observado na tabela de descritores do firmware v1.8.1
 * (0x08013e34, 61 entradas de 24 bytes: nome, id, get, set, help):
 *
 *   AT+<CMD>?         ajuda do comando
 *   AT+<CMD>          executa o comando (set sem valor)
 *   AT+<CMD>=?        le o valor
 *   AT+<CMD>=<valor>  define o valor
 *
 * Formato de saida (manual 1.1): o valor, quando existe, vem primeiro; o
 * status e' sempre precedido e seguido de <CR><LF>:
 *
 *   <valor><CR><LF><CR><LF><Status><CR><LF>
 *
 * Portatil: a saida e' entregue a um callback, o que permite testar o motor
 * no PC sem UART.
 */
#ifndef CORE_AT_H
#define CORE_AT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AT_LINE_MAX 160

/* Status do manual 1.1 (mesmas strings do firmware original). */
typedef enum {
    AT_OK = 0,
    AT_ERROR,
    AT_PARAM_ERROR,
    AT_BUSY_ERROR,
    AT_TEST_PARAM_OVERFLOW,
    AT_NO_NETWORK_JOINED,
    AT_RX_ERROR
} at_status_t;

/* Escreve o valor do parametro em `out` (sem <CR><LF>). */
typedef at_status_t (*at_get_fn)(char *out, size_t max, void *user);
/* Recebe o argumento textual (NULL quando o comando foi chamado sem "="). */
typedef at_status_t (*at_set_fn)(const char *arg, char *out, size_t max,
                                 void *user);

typedef struct {
    const char *name;  /* "+VER"; o comando ATZ usa "Z" */
    uint8_t id;        /* campo id da entrada original */
    const char *help;  /* "AT+VER: Get current image version and Frequency Band" */
    at_get_fn get;
    at_set_fn set;
} at_cmd_t;

typedef void (*at_write_fn)(const char *text, void *ctx);

typedef struct {
    const at_cmd_t *table;
    size_t count;
    at_write_fn write;
    void *user;              /* repassado aos handlers */
    void (*on_reset)(void *user); /* chamado pelo ATZ */
    char line[AT_LINE_MAX];
    size_t len;
    bool overflow;
} at_ctx_t;

const char *at_status_str(at_status_t st);

void at_init(at_ctx_t *at, const at_cmd_t *table, size_t count,
             at_write_fn write, void *user);

/* Alimenta um caractere recebido da UART. Ao encontrar <CR> ou <LF> a linha
 * acumulada e' executada. */
void at_feed(at_ctx_t *at, char c);

/* Executa uma linha completa (usada pelos testes de host). */
at_status_t at_exec(at_ctx_t *at, const char *line);

/* Busca a entrada da tabela pelo nome ("+VER" ou "VER"). */
const at_cmd_t *at_find(const at_ctx_t *at, const char *name);

#endif /* CORE_AT_H */

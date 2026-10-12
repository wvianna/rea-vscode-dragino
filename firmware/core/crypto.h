/*
 * crypto.h - AES-128 (FIPS-197) e AES-CMAC (RFC 4493), o minimo exigido pelo
 * LoRaWAN 1.0.2 (MIC e cifra do payload).
 *
 * Portatil e sem alocacao dinamica: os testes de host validam contra os
 * vetores publicados nas duas RFCs.
 */
#ifndef CORE_CRYPTO_H
#define CORE_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t round_key[176]; /* 11 x 16 bytes */
} aes128_ctx_t;

void aes128_init(aes128_ctx_t *ctx, const uint8_t key[16]);
void aes128_encrypt_block(const aes128_ctx_t *ctx, const uint8_t in[16],
                          uint8_t out[16]);
void aes128_decrypt_block(const aes128_ctx_t *ctx, const uint8_t in[16],
                          uint8_t out[16]);
/* Conveniencia: chave + bloco unico */
void aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16],
                        uint8_t out[16]);
void aes128_ecb_decrypt(const uint8_t key[16], const uint8_t in[16],
                        uint8_t out[16]);

/* AES-CMAC (RFC 4493). `mac` recebe 16 bytes. */
void aes128_cmac(const uint8_t key[16], const uint8_t *msg, size_t len,
                 uint8_t mac[16]);

#endif /* CORE_CRYPTO_H */

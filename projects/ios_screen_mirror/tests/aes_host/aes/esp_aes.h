// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only implementation of the public ESP AES interface for adapter tests.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { uint8_t key[16]; } esp_aes_context;
void esp_aes_init(esp_aes_context *ctx);
void esp_aes_free(esp_aes_context *ctx);
int esp_aes_setkey(esp_aes_context *ctx, const unsigned char *key, unsigned keybits);
int esp_aes_crypt_ctr(esp_aes_context *ctx, size_t length, size_t *offset,
                      unsigned char nonce[16], unsigned char stream[16],
                      const unsigned char *input, unsigned char *output);
int esp_aes_crypt_cbc(esp_aes_context *ctx, int mode, size_t length, unsigned char iv[16],
                      const unsigned char *input, unsigned char *output);

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "aes/esp_aes.h"
#include "crypto.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if !CONFIG_MBEDTLS_AES_HW_SMALL_DATA_LEN_OPTIM
#error "AirPlay AES requires the pinned IDF's allocation-free small-block path"
#endif

// On pinned ESP32-S31 IDF, <=128 bytes uses AES registers rather than DMA.
// Keep that bound even for large, unaligned PSRAM-backed video packets.
enum { AES_REGISTER_CHUNK_BYTES = 128 };
struct aes_ctx_s {
    esp_aes_context aes;
    uint8_t iv0[AES_128_BLOCK_SIZE];
    uint8_t iv[AES_128_BLOCK_SIZE];
    uint8_t stream[AES_128_BLOCK_SIZE];
    size_t nc_off;
    bool failed;
};

static aes_ctx_t *aes_new(const uint8_t *key, const uint8_t *iv)
{
    if (!key || !iv) return NULL;
    aes_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    esp_aes_init(&ctx->aes);
    if (esp_aes_setkey(&ctx->aes, key, 128) != 0) {
        esp_aes_free(&ctx->aes);
        free(ctx);
        return NULL;
    }
    memcpy(ctx->iv0, iv, sizeof(ctx->iv0));
    memcpy(ctx->iv, iv, sizeof(ctx->iv));
    return ctx;
}

static void aes_free(aes_ctx_t *ctx)
{
    if (!ctx) return;
    esp_aes_free(&ctx->aes);
    free(ctx);
}

static void aes_restart(aes_ctx_t *ctx)
{
    if (!ctx) return;
    memcpy(ctx->iv, ctx->iv0, sizeof(ctx->iv));
    memset(ctx->stream, 0, sizeof(ctx->stream));
    ctx->nc_off = 0;
    ctx->failed = false;
}

static int aes_failure(aes_ctx_t *ctx, uint8_t *out, int len, const char *mode)
{
    if (ctx) ctx->failed = true;
    if (out && len > 0) memset(out, 0, len);
    ESP_LOGE("airplay_crypto", "AES %s failed; stream must reconnect", mode);
    return -1;
}

aes_ctx_t *aes_ctr_init(const uint8_t *key, const uint8_t *iv) { return aes_new(key, iv); }
void aes_ctr_reset(aes_ctx_t *ctx) { aes_restart(ctx); }
void aes_ctr_destroy(aes_ctx_t *ctx) { aes_free(ctx); }

int aes_ctr_encrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len)
{
    if (!ctx || ctx->failed || len < 0 || (len && (!in || !out)))
        return aes_failure(ctx, out, len, "CTR");
    int offset = 0;
    while (offset < len) {
        size_t chunk = (size_t)(len - offset);
        if (chunk > AES_REGISTER_CHUNK_BYTES) chunk = AES_REGISTER_CHUNK_BYTES;
        if (esp_aes_crypt_ctr(&ctx->aes, chunk, &ctx->nc_off, ctx->iv, ctx->stream,
                              in + offset, out + offset) != 0)
            return aes_failure(ctx, out, len, "CTR");
        offset += chunk;
    }
    return 0;
}

int aes_ctr_decrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len)
{
    return aes_ctr_encrypt(ctx, in, out, len);
}

void aes_ctr_start_fresh_block(aes_ctx_t *ctx)
{
    if (ctx) ctx->nc_off = 0;
}

aes_ctx_t *aes_cbc_init(const uint8_t *key, const uint8_t *iv, aes_direction_t direction)
{
    (void)direction;
    return aes_new(key, iv);
}
void aes_cbc_reset(aes_ctx_t *ctx) { aes_restart(ctx); }
void aes_cbc_destroy(aes_ctx_t *ctx) { aes_free(ctx); }

static void aes_cbc_process(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len, int mode)
{
    if (!ctx || ctx->failed || len < 0 || len % AES_128_BLOCK_SIZE || (len && (!in || !out))) {
        aes_failure(ctx, out, len, "CBC");
        return;
    }
    for (int offset = 0; offset < len;) {
        size_t chunk = (size_t)(len - offset);
        if (chunk > AES_REGISTER_CHUNK_BYTES) chunk = AES_REGISTER_CHUNK_BYTES;
        if (esp_aes_crypt_cbc(&ctx->aes, mode, chunk, ctx->iv, in + offset, out + offset) != 0) {
            aes_failure(ctx, out, len, "CBC");
            return;
        }
        offset += chunk;
    }
}

void aes_cbc_encrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len)
{
    aes_cbc_process(ctx, in, out, len, AES_ENCRYPT);
}
void aes_cbc_decrypt(aes_ctx_t *ctx, const uint8_t *in, uint8_t *out, int len)
{
    aes_cbc_process(ctx, in, out, len, AES_DECRYPT);
}

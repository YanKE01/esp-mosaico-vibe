// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "psa/crypto.h"
// Legacy mbedTLS crypto headers were removed in IDF 6.2. This small adapter
// keeps the source port's SHA-512 lifecycle using the public PSA API.
typedef struct { psa_hash_operation_t op; bool failed; } mirror_sha512_context;
static inline void mirror_sha512_init(mirror_sha512_context *ctx)
{
    ctx->op = psa_hash_operation_init();
    ctx->failed = false;
}
static inline void mirror_sha512_starts(mirror_sha512_context *ctx, int is384)
{
    psa_status_t aborted = psa_hash_abort(&ctx->op);
    ctx->failed = aborted != PSA_SUCCESS || psa_crypto_init() != PSA_SUCCESS ||
                  psa_hash_setup(&ctx->op, PSA_ALG_SHA_512) != PSA_SUCCESS;
}
static inline void mirror_sha512_update(mirror_sha512_context *ctx, const uint8_t *data, size_t len)
{
    if (!ctx->failed && psa_hash_update(&ctx->op, data, len) != PSA_SUCCESS) ctx->failed = true;
}
static inline void mirror_sha512_finish(mirror_sha512_context *ctx, uint8_t *out)
{
    size_t len = 0;
    if (ctx->failed || psa_hash_finish(&ctx->op, out, 64, &len) != PSA_SUCCESS || len != 64) {
        memset(out, 0, 64);
        ctx->failed = true;
        ESP_LOGE("airplay_crypto", "PSA SHA-512 failed");
    }
}
static inline void mirror_sha512_free(mirror_sha512_context *ctx)
{
    psa_hash_abort(&ctx->op);
}

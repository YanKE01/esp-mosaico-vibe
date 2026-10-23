/*
 * Copyright (c) 2019 dsafa22, All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "mirror_buffer.h"
#include "raop_rtp.h"
#include "raop_rtp.h"
#include <stdint.h>
#include "crypto.h"
#include "compat.h"
#include <math.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

//#define DUMP_KEI_IV
struct mirror_buffer_s {
    logger_t *logger;
    aes_ctx_t *aes_ctx;
    /* AES key and IV */
    // Need secondary processing to use
    unsigned char aeskey[RAOP_AESKEY_LEN];
    unsigned char ecdh_secret[32];
};

void
mirror_buffer_init_aes(mirror_buffer_t *mirror_buffer, uint64_t streamConnectionID)
{
    aes_ctr_destroy(mirror_buffer->aes_ctx);
    mirror_buffer->aes_ctx = NULL;
    sha_ctx_t *ctx = sha_init();
    if (!ctx) {
        logger_log(mirror_buffer->logger, LOGGER_ERR, "No memory for mirror key derivation");
        return;
    }
    unsigned int digest_len = 0;
    unsigned char eaeskey[64] = {};
    memcpy(eaeskey, mirror_buffer->aeskey, 16);
    sha_update(ctx, eaeskey, 16);
    sha_update(ctx, mirror_buffer->ecdh_secret, 32);
    sha_final(ctx, eaeskey, &digest_len);
    if (digest_len != sizeof(eaeskey)) goto hash_failed;

    unsigned char hash1[64];
    unsigned char hash2[64];
    char* skey = "AirPlayStreamKey";
    char* siv = "AirPlayStreamIV";
    unsigned char skeyall[255];
    unsigned char sivall[255];
    sprintf((char*) skeyall, "%s%" PRIu64, skey, streamConnectionID);
    sprintf((char*) sivall, "%s%" PRIu64, siv, streamConnectionID);
    sha_reset(ctx);
    sha_update(ctx, skeyall, strlen((char*) skeyall));
    sha_update(ctx, eaeskey, 16);
    sha_final(ctx, hash1, &digest_len);
    if (digest_len != sizeof(hash1)) goto hash_failed;

    sha_reset(ctx);
    sha_update(ctx, sivall, strlen((char*) sivall));
    sha_update(ctx, eaeskey, 16);
    sha_final(ctx, hash2, &digest_len);
    if (digest_len != sizeof(hash2)) goto hash_failed;
    sha_destroy(ctx);

    unsigned char decrypt_aeskey[16];
    unsigned char decrypt_aesiv[16];
    memcpy(decrypt_aeskey, hash1, 16);
    memcpy(decrypt_aesiv, hash2, 16);
#ifdef DUMP_KEI_IV
    FILE* keyfile = fopen("/sdcard/111.keyiv", "wb");
    fwrite(decrypt_aeskey, 16, 1, keyfile);
    fwrite(decrypt_aesiv, 16, 1, keyfile);
    fclose(keyfile);
#endif
    // Need to be initialized externally
    mirror_buffer->aes_ctx = aes_ctr_init(decrypt_aeskey, decrypt_aesiv);
    if (!mirror_buffer->aes_ctx) {
        logger_log(mirror_buffer->logger, LOGGER_ERR, "No memory for mirror AES context");
    }
    return;
hash_failed:
    sha_destroy(ctx);
    logger_log(mirror_buffer->logger, LOGGER_ERR, "Mirror key derivation failed");
}

mirror_buffer_t *
mirror_buffer_init(logger_t *logger,
                   const unsigned char *aeskey,
                   const unsigned char *ecdh_secret)
{
    mirror_buffer_t *mirror_buffer;
    assert(aeskey);
    assert(ecdh_secret);
    mirror_buffer = calloc(1, sizeof(mirror_buffer_t));
    if (!mirror_buffer) {
        return NULL;
    }
    memcpy(mirror_buffer->aeskey, aeskey, RAOP_AESKEY_LEN);
    memcpy(mirror_buffer->ecdh_secret, ecdh_secret, 32);
    mirror_buffer->logger = logger;
    //mirror_buffer_init_aes(mirror_buffer, aeskey, ecdh_secret, streamConnectionID);
    return mirror_buffer;
}

int mirror_buffer_decrypt(mirror_buffer_t *mirror_buffer, unsigned char *input,
                          unsigned char *output, int inputLen) {
    if (!mirror_buffer || !mirror_buffer->aes_ctx) return -1;
    // The AES adapter retains counter/partial-block state across TCP payloads.
    // It also handles payloads shorter than the preceding block's remainder.
    return aes_ctr_decrypt(mirror_buffer->aes_ctx, input, output, inputLen);
}

void
mirror_buffer_destroy(mirror_buffer_t *mirror_buffer)
{
    if (mirror_buffer) {
        aes_ctr_destroy(mirror_buffer->aes_ctx);
        free(mirror_buffer);
    }
}

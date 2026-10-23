// SPDX-License-Identifier: GPL-3.0-or-later
// Tests use OpenSSL as the independent AES/SHA provider. This is never built
// into firmware; DMA selection is checked through the adapter call-size bound.
#include <stdlib.h>
#include <string.h>
#include "crypto.h"
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/modes.h>
#include "aes/esp_aes.h"
#include "logger.h"

static size_t calls, largest, allocations, failure_call;
static int allocation_failure;
void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size)
{
    ++allocations;
    return allocation_failure ? NULL : __real_calloc(n, size);
}
void test_crypto_log(const char *fmt, ...) { (void)fmt; }
void logger_log(logger_t *logger, int level, const char *fmt, ...)
{ (void)logger; (void)level; (void)fmt; }
void test_driver_reset(void) { calls = largest = failure_call = 0; allocation_failure = 0; }
void test_driver_fail(size_t n) { failure_call = n; }
void test_calloc_fail(int fail) { allocation_failure = fail; }
size_t test_driver_calls(void) { return calls; }
size_t test_driver_largest(void) { return largest; }
size_t test_allocations(void) { return allocations; }
static int check_call(size_t length)
{
    ++calls;
    if (length > largest) largest = length;
    return failure_call && calls == failure_call;
}
void esp_aes_init(esp_aes_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
void esp_aes_free(esp_aes_context *ctx) { memset(ctx, 0, sizeof(*ctx)); }
int esp_aes_setkey(esp_aes_context *ctx, const unsigned char *key, unsigned keybits)
{
    if (keybits != 128) return -1;
    memcpy(ctx->key, key, sizeof(ctx->key));
    return 0;
}
static void encrypt_block(const unsigned char *in, unsigned char *out, const void *key)
{
    AES_encrypt(in, out, key);
}
int test_ctr_reference(const unsigned char *key, const unsigned char *iv,
                       const unsigned char *input, unsigned char *output, size_t length)
{
    AES_KEY schedule;
    unsigned char nonce[16], stream[16] = {0};
    unsigned offset = 0;
    memcpy(nonce, iv, sizeof(nonce));
    if (AES_set_encrypt_key(key, 128, &schedule)) return -1;
    CRYPTO_ctr128_encrypt(input, output, length, &schedule, nonce, stream, &offset, encrypt_block);
    return 0;
}
int esp_aes_crypt_ctr(esp_aes_context *ctx, size_t length, size_t *offset,
                      unsigned char nonce[16], unsigned char stream[16],
                      const unsigned char *input, unsigned char *output)
{
    if (check_call(length)) { nonce[15] ^= 1; return -1; }
    AES_KEY key;
    unsigned n = *offset;
    if (AES_set_encrypt_key(ctx->key, 128, &key)) return -1;
    CRYPTO_ctr128_encrypt(input, output, length, &key, nonce, stream, &n, encrypt_block);
    *offset = n;
    return 0;
}
int esp_aes_crypt_cbc(esp_aes_context *ctx, int mode, size_t length, unsigned char iv[16],
                      const unsigned char *input, unsigned char *output)
{
    if (check_call(length)) return -1;
    AES_KEY key;
    int err = mode == AES_ENCRYPT ? AES_set_encrypt_key(ctx->key, 128, &key)
                                 : AES_set_decrypt_key(ctx->key, 128, &key);
    if (err) return err;
    AES_cbc_encrypt(input, output, length, &key, iv, mode);
    return 0;
}
struct sha_ctx_s { EVP_MD_CTX *hash; };
sha_ctx_t *sha_init(void)
{
    sha_ctx_t *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->hash = EVP_MD_CTX_new();
    if (!h->hash || EVP_DigestInit_ex(h->hash, EVP_sha512(), NULL) != 1) {
        EVP_MD_CTX_free(h->hash); free(h); return NULL;
    }
    return h;
}
void sha_update(sha_ctx_t *h, const uint8_t *p, int len) { EVP_DigestUpdate(h->hash, p, len); }
void sha_reset(sha_ctx_t *h) { EVP_DigestInit_ex(h->hash, EVP_sha512(), NULL); }
void sha_final(sha_ctx_t *h, uint8_t *out, unsigned *len)
{
    unsigned n = 0;
    if (EVP_DigestFinal_ex(h->hash, out, &n) != 1) n = 0;
    if (len) *len = n;
}
void sha_destroy(sha_ctx_t *h) { if (h) { EVP_MD_CTX_free(h->hash); free(h); } }

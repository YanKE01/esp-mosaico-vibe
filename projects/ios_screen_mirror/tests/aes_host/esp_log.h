// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
void test_crypto_log(const char *fmt, ...);
#define ESP_LOGE(tag, ...) do { (void)(tag); test_crypto_log(__VA_ARGS__); } while (0)

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { AIRPLAY_PACKET_MAX = 256 * 1024, AIRPLAY_CONFIG_MAX = 1024 };
bool airplay_packet_to_annexb(uint8_t *data, size_t size);
size_t airplay_config_to_annexb(const uint8_t *data, size_t size, uint8_t *out, size_t capacity);

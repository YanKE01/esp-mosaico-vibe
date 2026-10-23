// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint8_t profile, constraints, level;
    uint32_t width, height, coded_width, coded_height, crop_left, crop_top, refs;
    bool full_range, bt709;
} mirror_h264_sps_t;
bool mirror_h264_has_nal(const uint8_t *data, size_t len, unsigned type);
// Parse 8-bit 4:2:0 Baseline/Main/High SPS, including coded size/display crop.
// Truncated/malformed data, unsupported profiles and interlacing fail closed.
bool mirror_h264_read_sps(const uint8_t *data, size_t len, mirror_h264_sps_t *out);

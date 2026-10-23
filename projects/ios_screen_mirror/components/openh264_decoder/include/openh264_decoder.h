// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct openh264_decoder_t *openh264_decoder_handle_t;
typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t width, height;
    uint64_t pts;
    int codec_status;
} openh264_frame_t;

// Single-owner, synchronous decoder. No internal decode threads are enabled.
esp_err_t openh264_decoder_create(uint32_t max_pixels, openh264_decoder_handle_t *out);
// One Annex-B access unit. A null data pointer flushes a delayed output picture.
// Packed I420 output is borrowed until the next process() or delete() call.
esp_err_t openh264_decoder_process(openh264_decoder_handle_t h, const uint8_t *data,
                                  size_t size, uint64_t pts, openh264_frame_t *out);
void openh264_decoder_delete(openh264_decoder_handle_t h);

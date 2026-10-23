// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "airplay_receiver.h"
#include "mirror_lcd.h"
typedef struct mirror_video_t *mirror_video_handle_t;
esp_err_t mirror_video_create(mirror_lcd_handle_t lcd, mirror_video_handle_t *out);
esp_err_t mirror_video_start(mirror_video_handle_t h);
// Receiver must be stopped/joined before stop/delete. LCD outlives this object.
void mirror_video_stop(mirror_video_handle_t h);
void mirror_video_delete(mirror_video_handle_t h);
// Match airplay_receiver callbacks. No decoding/DMA waits in protocol threads.
void mirror_video_submit(const uint8_t *data, size_t size, uint64_t pts, bool config, void *ctx);
void mirror_video_flush(void *ctx);

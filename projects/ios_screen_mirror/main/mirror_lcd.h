// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_err.h"
#include "mirror_h264.h"
typedef struct mirror_lcd_t *mirror_lcd_handle_t;
// Single rendering task owns this handle. BSP owns panel/IO for app lifetime.
esp_err_t mirror_lcd_create(mirror_lcd_handle_t *out);
esp_err_t mirror_lcd_show(mirror_lcd_handle_t h, const uint8_t *i420, size_t size,
                        const mirror_h264_sps_t *sps);
esp_err_t mirror_lcd_clear(mirror_lcd_handle_t h);
void mirror_lcd_delete(mirror_lcd_handle_t h);

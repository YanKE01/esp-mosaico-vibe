// SPDX-License-Identifier: GPL-3.0-or-later
#include "mirror_lcd.h"
#include "mirror_pixels.h"
#include <stdlib.h>
#include <string.h>
#include "bsp/esp_mosaico.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
enum { FRAME_BYTES = BSP_LCD_H_RES * BSP_LCD_V_RES * 2, CACHE_ALIGNMENT = 64 };
struct mirror_lcd_t {
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    SemaphoreHandle_t done;
    uint16_t *pixels;
    bool callback_registered;
};
static bool transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    mirror_lcd_handle_t h = ctx;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(h->done, &wake);
    return wake == pdTRUE;
}
static esp_err_t present(mirror_lcd_handle_t h)
{
    esp_err_t err = esp_cache_msync(h->pixels, FRAME_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (err != ESP_OK) return err;
    err = esp_lcd_panel_draw_bitmap(h->panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, h->pixels);
    // Never reuse/free the DMA buffer before its completion callback.
    if (err == ESP_OK) xSemaphoreTake(h->done, portMAX_DELAY);
    return err;
}
esp_err_t mirror_lcd_create(mirror_lcd_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    mirror_lcd_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    h->done = xSemaphoreCreateBinary();
    h->pixels = heap_caps_aligned_calloc(CACHE_ALIGNMENT, 1, FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!h->done || !h->pixels) { mirror_lcd_delete(h); return ESP_ERR_NO_MEM; }
    esp_err_t err = bsp_display_new(NULL, &h->panel);
    if (err == ESP_OK) {
        h->io = bsp_display_get_panel_io();
        const esp_lcd_panel_io_callbacks_t cb = {.on_color_trans_done = transfer_done};
        err = esp_lcd_panel_io_register_event_callbacks(h->io, &cb, h);
        h->callback_registered = err == ESP_OK;
    }
    if (err == ESP_OK) err = present(h);
    if (err != ESP_OK) { mirror_lcd_delete(h); return err; }
    *out = h;
    return ESP_OK;
}
esp_err_t mirror_lcd_show(mirror_lcd_handle_t h, const uint8_t *src, size_t size,
                        const mirror_h264_sps_t *sps)
{
    if (!h || !mirror_pixels_render(src, size, sps, h->pixels, BSP_LCD_H_RES, BSP_LCD_V_RES)) return ESP_ERR_INVALID_ARG;
    return present(h);
}
esp_err_t mirror_lcd_clear(mirror_lcd_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    memset(h->pixels, 0, FRAME_BYTES);
    return present(h);
}
void mirror_lcd_delete(mirror_lcd_handle_t h)
{
    if (!h) return;
    if (h->callback_registered) {
        const esp_lcd_panel_io_callbacks_t cb = {0};
        esp_lcd_panel_io_register_event_callbacks(h->io, &cb, NULL);
    }
    if (h->done) vSemaphoreDelete(h->done);
    free(h->pixels);
    free(h);
}

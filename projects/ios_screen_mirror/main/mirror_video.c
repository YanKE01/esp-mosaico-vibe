// SPDX-License-Identifier: GPL-3.0-or-later
#include "mirror_video.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "openh264_decoder.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

enum { PACKET_COUNT = 6, PACKET_BYTES = 256 * 1024, CONFIG_BYTES = 1024,
       DECODER_MEMORY_LIMIT = 8 * 1024 * 1024, REPORT_US = 5000000,
       DECODER_TASK_STACK_BYTES = 32768, DECODER_TASK_PRIORITY = 6, DECODER_TASK_CORE = 1 };
typedef struct {
    uint8_t *data;
    size_t size;
    uint64_t pts;
    uint32_t epoch;
} packet_t;
struct mirror_video_t {
    mirror_lcd_handle_t lcd;
    QueueHandle_t free_packets, packets;
    SemaphoreHandle_t control_lock, exited;
    TaskHandle_t task;
    packet_t pool[PACKET_COUNT];
    // Producer configuration and generation are guarded by control_lock.
    uint8_t config[CONFIG_BYTES];
    size_t config_size;
    mirror_h264_sps_t sps;
    uint32_t epoch;
    bool stopping;
    // Only the decode task touches the fields below.
    uint8_t decode_config[CONFIG_BYTES];
    openh264_decoder_handle_t decoder;
    mirror_h264_sps_t decode_sps;
    bool waiting_idr, first_picture, streaming;
    uint32_t active_epoch, decoded, shown;
    int64_t report_at, decode_us, display_us;
};
static const char *TAG = "mirror_video";

static bool allowed_sps(const uint8_t *data, size_t size, mirror_h264_sps_t *sps)
{
    if (!mirror_h264_read_sps(data, size, sps)) return false;
    uint64_t pixels = (uint64_t)sps->coded_width * sps->coded_height;
    return pixels <= CONFIG_MIRROR_MAX_PIXELS && pixels * 3 / 2 * (sps->refs + 2) <= DECODER_MEMORY_LIMIT;
}
static void close_decoder(mirror_video_handle_t h)
{
    openh264_decoder_delete(h->decoder);
    h->decoder = NULL;
    h->waiting_idr = true;
}
static bool decode(mirror_video_handle_t h, uint8_t *data, size_t size, uint64_t pts, bool display)
{
    openh264_frame_t frame = {0};
    int64_t started = esp_timer_get_time();
    esp_err_t err = openh264_decoder_process(h->decoder, data, size, pts, &frame);
    h->decode_us += esp_timer_get_time() - started;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OpenH264 error 0x%x (codec=0x%x); waiting for next IDR", err, frame.codec_status);
        return false;
    }
    if (!frame.size) return true;
    ++h->decoded;
    mirror_h264_sps_t render_sps = h->decode_sps;
    if (frame.width != render_sps.coded_width || frame.height != render_sps.coded_height) {
        if (frame.width != render_sps.width || frame.height != render_sps.height) {
            ESP_LOGE(TAG, "Unexpected decoder output: %" PRIu32 "x%" PRIu32, frame.width, frame.height);
            return false;
        }
        render_sps.coded_width = frame.width;
        render_sps.coded_height = frame.height;
        render_sps.crop_left = render_sps.crop_top = 0;
    }
    if (display) {
        started = esp_timer_get_time();
        esp_err_t shown = mirror_lcd_show(h->lcd, frame.data, frame.size, &render_sps);
        h->display_us += esp_timer_get_time() - started;
        if (shown != ESP_OK) { ESP_LOGE(TAG, "LCD: %s", esp_err_to_name(shown)); return false; }
        ++h->shown;
        if (!h->first_picture) {
            h->first_picture = true;
            ESP_LOGI(TAG, "First picture: %" PRIu32 "x%" PRIu32, h->decode_sps.width, h->decode_sps.height);
        }
    }
    return true;
}

static bool update_epoch(mirror_video_handle_t h)
{
    xSemaphoreTake(h->control_lock, portMAX_DELAY);
    bool changed = h->active_epoch != h->epoch;
    size_t size = h->config_size;
    if (changed) {
        h->active_epoch = h->epoch;
        h->decode_sps = h->sps;
        memcpy(h->decode_config, h->config, size);
    }
    bool stop = h->stopping;
    xSemaphoreGive(h->control_lock);
    if (changed) {
        close_decoder(h);
        if (!size || stop) {
            esp_err_t cleared = mirror_lcd_clear(h->lcd);
            if (cleared != ESP_OK) ESP_LOGE(TAG, "Clear LCD: %s", esp_err_to_name(cleared));
            h->streaming = false;
            h->first_picture = false;
        }
        if (size && !stop) {
            if (!h->streaming) {
                // Exclude the idle time before the phone starts mirroring.
                h->report_at = esp_timer_get_time();
                h->decoded = h->shown = 0;
                h->decode_us = h->display_us = 0;
                h->streaming = true;
            }
            ESP_LOGI(TAG, "OpenH264 decoder: max_pixels=%d internal=%u dma_largest=%u psram=%u",
                     CONFIG_MIRROR_MAX_PIXELS,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            esp_err_t err = openh264_decoder_create(CONFIG_MIRROR_MAX_PIXELS, &h->decoder);
            if (err != ESP_OK) ESP_LOGE(TAG, "OpenH264 create: %s", esp_err_to_name(err));
            if (err != ESP_OK || !decode(h, h->decode_config, size, 0, false)) close_decoder(h);
        }
    }
    return !stop;
}
static void video_task(void *arg)
{
    mirror_video_handle_t h = arg;
    h->report_at = esp_timer_get_time();
    while (update_epoch(h)) {
        packet_t *p;
        if (xQueueReceive(h->packets, &p, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        // A concurrent flush/config change invalidates queued reference frames.
        if (!update_epoch(h)) { xQueueSend(h->free_packets, &p, 0); break; }
        if (h->decoder && p->epoch == h->active_epoch) {
            bool valid = true;
            if (mirror_h264_has_nal(p->data, p->size, 7)) {
                mirror_h264_sps_t sps;
                valid = allowed_sps(p->data, p->size, &sps) &&
                        memcmp(&sps, &h->decode_sps, sizeof(sps)) == 0;
            }
            bool idr = mirror_h264_has_nal(p->data, p->size, 5);
            if (valid && h->waiting_idr && idr) h->waiting_idr = false;
            // Show recovery/key pictures even when decode is behind the source.
            // Keep the last picture visible during reference resynchronization.
            bool display = idr || uxQueueMessagesWaiting(h->packets) <= 1;
            if (!valid || (!h->waiting_idr && !decode(h, p->data, p->size, p->pts, display))) {
                // Do not feed P frames after losing references. Recreate from
                // cached SPS/PPS, preserving the generation's configuration.
                xSemaphoreTake(h->control_lock, portMAX_DELAY);
                if (h->epoch == p->epoch) ++h->epoch;
                xSemaphoreGive(h->control_lock);
            }
        }
        xQueueSend(h->free_packets, &p, 0);
        int64_t now = esp_timer_get_time();
        if (now - h->report_at >= REPORT_US) {
            ESP_LOGI(TAG, "decoded=%" PRIu32 " shown=%" PRIu32 " interval=%" PRId64 "ms decode=%" PRId64 "ms display=%" PRId64 "ms",
                     h->decoded, h->shown, (now - h->report_at) / 1000, h->decode_us / 1000, h->display_us / 1000);
            ESP_LOGI(TAG, "heap: internal=%u dma_largest=%u psram=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            h->decoded = h->shown = 0;
            h->decode_us = h->display_us = 0;
            h->report_at = now;
        }
    }
    close_decoder(h);
    xSemaphoreGive(h->exited);
    vTaskDelete(NULL);
}
esp_err_t mirror_video_create(mirror_lcd_handle_t lcd, mirror_video_handle_t *out)
{
    if (!lcd || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    mirror_video_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    h->lcd = lcd;
    h->epoch = 1;
    h->free_packets = xQueueCreate(PACKET_COUNT, sizeof(packet_t *));
    h->packets = xQueueCreate(PACKET_COUNT, sizeof(packet_t *));
    h->control_lock = xSemaphoreCreateMutex();
    h->exited = xSemaphoreCreateBinary();
    if (!h->free_packets || !h->packets || !h->control_lock || !h->exited) goto nomem;
    for (unsigned i = 0; i < PACKET_COUNT; ++i) {
        packet_t *p = &h->pool[i];
        p->data = heap_caps_malloc(PACKET_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p->data) goto nomem;
        xQueueSend(h->free_packets, &p, 0);
    }
    *out = h;
    return ESP_OK;
nomem:
    mirror_video_delete(h);
    return ESP_ERR_NO_MEM;
}
esp_err_t mirror_video_start(mirror_video_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    if (h->task) return ESP_ERR_INVALID_STATE;
    h->stopping = false;
    return xTaskCreatePinnedToCore(video_task, "mirror_decode", DECODER_TASK_STACK_BYTES, h,
                                   DECODER_TASK_PRIORITY, &h->task, DECODER_TASK_CORE) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void mirror_video_flush(void *ctx)
{
    mirror_video_handle_t h = ctx;
    xSemaphoreTake(h->control_lock, portMAX_DELAY);
    h->config_size = 0;
    ++h->epoch;
    xSemaphoreGive(h->control_lock);
}
void mirror_video_submit(const uint8_t *data, size_t size, uint64_t pts, bool config, void *ctx)
{
    mirror_video_handle_t h = ctx;
    if (!data || !size) return;
    if (config) {
        mirror_h264_sps_t sps = {0};
        bool valid = size <= CONFIG_BYTES && allowed_sps(data, size, &sps);
        xSemaphoreTake(h->control_lock, portMAX_DELAY);
        h->config_size = valid ? size : 0;
        if (valid) { memcpy(h->config, data, size); h->sps = sps; }
        ++h->epoch;
        xSemaphoreGive(h->control_lock);
        if (valid) ESP_LOGI(TAG, "SPS: profile=%u constraints=0x%02x level=%u visible=%" PRIu32 "x%" PRIu32 " coded=%" PRIu32 "x%" PRIu32 " refs=%" PRIu32,
                           sps.profile, sps.constraints, sps.level, sps.width, sps.height, sps.coded_width, sps.coded_height, sps.refs);
        else ESP_LOGE(TAG, "Rejected SPS: profile=%u constraints=0x%02x level=%u; visible=%" PRIu32 "x%" PRIu32 " coded=%" PRIu32 "x%" PRIu32 "; need 8-bit progressive 4:2:0 Baseline/Main/High, <=%d coded pixels and bounded reference memory",
                      sps.profile, sps.constraints, sps.level, sps.width, sps.height, sps.coded_width, sps.coded_height, CONFIG_MIRROR_MAX_PIXELS);
        return;
    }
    packet_t *p;
    if (size > PACKET_BYTES || xQueueReceive(h->free_packets, &p, 0) != pdTRUE) {
        xSemaphoreTake(h->control_lock, portMAX_DELAY);
        ++h->epoch;
        xSemaphoreGive(h->control_lock);
        return;
    }
    xSemaphoreTake(h->control_lock, portMAX_DELAY);
    bool enabled = h->config_size && !h->stopping;
    p->epoch = h->epoch;
    xSemaphoreGive(h->control_lock);
    if (!enabled) { xQueueSend(h->free_packets, &p, 0); return; }
    memcpy(p->data, data, size);
    p->size = size;
    p->pts = pts;
    if (xQueueSend(h->packets, &p, 0) != pdTRUE) xQueueSend(h->free_packets, &p, 0);
}
void mirror_video_stop(mirror_video_handle_t h)
{
    if (!h || !h->task) return;
    xSemaphoreTake(h->control_lock, portMAX_DELAY);
    h->stopping = true;
    xSemaphoreGive(h->control_lock);
    xSemaphoreTake(h->exited, portMAX_DELAY);
    h->task = NULL;
    packet_t *p;
    while (xQueueReceive(h->packets, &p, 0) == pdTRUE) xQueueSend(h->free_packets, &p, 0);
    h->active_epoch = 0;
}
void mirror_video_delete(mirror_video_handle_t h)
{
    if (!h) return;
    mirror_video_stop(h);
    for (unsigned i = 0; i < PACKET_COUNT; ++i) free(h->pool[i].data);
    if (h->packets) vQueueDelete(h->packets);
    if (h->free_packets) vQueueDelete(h->free_packets);
    if (h->control_lock) vSemaphoreDelete(h->control_lock);
    if (h->exited) vSemaphoreDelete(h->exited);
    free(h);
}

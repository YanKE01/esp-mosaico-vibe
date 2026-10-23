// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <string.h>
#include "airplay_receiver.h"
#include "esp_log.h"
#include "raop.h"

struct airplay_receiver_t {
    airplay_receiver_config_t config;
    char *name;
    raop_t *raop;
    dnssd_t *dnssd;
    bool started;
};
static void audio_process(void *ctx, raop_ntp_t *ntp, aac_decode_struct *data) {}
static void video_process(void *ctx, raop_ntp_t *ntp, h264_decode_struct *data)
{
    airplay_receiver_handle_t h = ctx;
    if (data->data_len > 0) h->config.video(data->data, data->data_len, data->pts,
                                         data->frame_type == 0, h->config.ctx);
}
static void video_flush(void *ctx)
{
    airplay_receiver_handle_t h = ctx;
    if (h->config.flush) h->config.flush(h->config.ctx);
}
static void log_message(void *ctx, int level, const char *msg)
{
    if (level <= RAOP_LOG_WARNING) ESP_LOGW("airplay", "%s", msg);
    else if (level <= RAOP_LOG_INFO) ESP_LOGI("airplay", "%s", msg);
}
esp_err_t airplay_receiver_create(const airplay_receiver_config_t *config,
                                 airplay_receiver_handle_t *out)
{
    if (!config || !config->name || !config->video || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (strlen(config->name) > 48) return ESP_ERR_INVALID_ARG;
    airplay_receiver_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    h->config = *config;
    h->name = strdup(config->name);
    if (!h->name) { free(h); return ESP_ERR_NO_MEM; }
    int error;
    h->dnssd = dnssd_init(h->name, strlen(h->name), (const char *)config->mac, 6, &error);
    raop_callbacks_t callbacks = {
        .cls = h, .audio_process = audio_process, .video_process = video_process,
        .video_flush = video_flush,
    };
    h->raop = raop_init(2, &callbacks);
    if (!h->dnssd || !h->raop) {
        airplay_receiver_delete(h);
        return ESP_ERR_NO_MEM;
    }
    raop_set_dnssd(h->raop, h->dnssd);
    raop_set_log_level(h->raop, RAOP_LOG_INFO);
    raop_set_log_callback(h->raop, log_message, h);
    *out = h;
    return ESP_OK;
}
esp_err_t airplay_receiver_start(airplay_receiver_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    if (h->started) return ESP_ERR_INVALID_STATE;
    unsigned short port = h->config.port;
    if (raop_start(h->raop, &port) < 0) return ESP_FAIL;
    raop_set_port(h->raop, port);
    if (dnssd_register_raop(h->dnssd, port) < 0 ||
        dnssd_register_airplay(h->dnssd, port) < 0) {
        dnssd_unregister_raop(h->dnssd);
        dnssd_unregister_airplay(h->dnssd);
        raop_stop(h->raop);
        return ESP_FAIL;
    }
    h->started = true;
    ESP_LOGI("airplay", "Receiver '%s' listening on %u", h->name, port);
    return ESP_OK;
}
void airplay_receiver_stop(airplay_receiver_handle_t h)
{
    if (!h || !h->started) return;
    dnssd_unregister_airplay(h->dnssd);
    dnssd_unregister_raop(h->dnssd);
    raop_stop(h->raop);
    h->started = false;
}
void airplay_receiver_delete(airplay_receiver_handle_t h)
{
    if (!h) return;
    airplay_receiver_stop(h);
    if (h->raop) raop_destroy(h->raop);
    if (h->dnssd) dnssd_destroy(h->dnssd);
    free(h->name);
    free(h);
}

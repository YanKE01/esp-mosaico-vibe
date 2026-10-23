// SPDX-License-Identifier: GPL-3.0-or-later
#include "mirror_wifi.h"
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
enum { CONNECTED = BIT0, FAILED = BIT1, MAX_RETRIES = 8 };
struct mirror_wifi_t {
    esp_netif_t *netif;
    esp_event_handler_instance_t wifi_event, ip_event;
    EventGroupHandle_t state;
    wifi_config_t config;
    unsigned retries;
    bool loop_created, initialized, started;
};
static void event(void *ctx, esp_event_base_t base, int32_t id, void *data)
{
    mirror_wifi_handle_t h = ctx;
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        xEventGroupClearBits(h->state, CONNECTED);
        if (h->retries++ < MAX_RETRIES && esp_wifi_connect() == ESP_OK) return;
        ESP_LOGE("mirror_wifi", "Wi-Fi connection failed after %u attempts", h->retries);
        xEventGroupSetBits(h->state, FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = data;
        h->retries = 0;
        ESP_LOGI("mirror_wifi", "IP: " IPSTR, IP2STR(&ip->ip_info.ip));
        xEventGroupSetBits(h->state, CONNECTED);
    }
}
esp_err_t mirror_wifi_create(mirror_wifi_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    mirror_wifi_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    esp_err_t err = ESP_ERR_NO_MEM;
    h->state = xEventGroupCreate();
    if (!h->state) goto fail;
    err = esp_netif_init();
    if (err != ESP_OK) goto fail;
    err = esp_event_loop_create_default();
    h->loop_created = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    h->netif = esp_netif_create_default_wifi_sta();
    if (!h->netif) { err = ESP_ERR_NO_MEM; goto fail; }
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    h->initialized = true;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_get_config(WIFI_IF_STA, &h->config);
    if (err != ESP_OK) goto fail;
    if (CONFIG_MIRROR_WIFI_SSID[0]) {
        memset(&h->config, 0, sizeof(h->config));
        if (strlen(CONFIG_MIRROR_WIFI_SSID) > sizeof(h->config.sta.ssid) ||
            strlen(CONFIG_MIRROR_WIFI_PASSWORD) > sizeof(h->config.sta.password)) { err = ESP_ERR_INVALID_ARG; goto fail; }
        memcpy(h->config.sta.ssid, CONFIG_MIRROR_WIFI_SSID, strlen(CONFIG_MIRROR_WIFI_SSID));
        memcpy(h->config.sta.password, CONFIG_MIRROR_WIFI_PASSWORD, strlen(CONFIG_MIRROR_WIFI_PASSWORD));
    }
    if (!h->config.sta.ssid[0]) {
        ESP_LOGE("mirror_wifi", "No saved Wi-Fi; configure MIRROR_WIFI_SSID/PASSWORD using menuconfig");
        err = ESP_ERR_NOT_FOUND; goto fail;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &h->config);
    if (err == ESP_OK) err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, h, &h->wifi_event);
    if (err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, h, &h->ip_event);
    if (err != ESP_OK) goto fail;
    *out = h;
    return ESP_OK;
fail:
    mirror_wifi_delete(h);
    return err;
}
esp_err_t mirror_wifi_start(mirror_wifi_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) return err;
    h->started = true;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) return err;
    EventBits_t state = xEventGroupWaitBits(h->state, CONNECTED | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(45000));
    return state & CONNECTED ? ESP_OK : ESP_ERR_TIMEOUT;
}
void mirror_wifi_delete(mirror_wifi_handle_t h)
{
    if (!h) return;
    if (h->wifi_event) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h->wifi_event);
    if (h->ip_event) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h->ip_event);
    // Default event-loop calls execute in order. Drain in-flight callbacks
    // before freeing user context (unregister synchronizes event-loop access).
    if (h->started) esp_wifi_stop();
    if (h->initialized) esp_wifi_deinit();
    if (h->netif) esp_netif_destroy_default_wifi(h->netif);
    if (h->loop_created) esp_event_loop_delete_default();
    if (h->state) vEventGroupDelete(h->state);
    memset(&h->config, 0, sizeof(h->config));
    free(h);
}

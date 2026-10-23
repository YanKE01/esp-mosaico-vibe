// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include "airplay_receiver.h"
#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "mirror_lcd.h"
#include "mirror_usb_console.h"
#include "mirror_video.h"
#include "mirror_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

typedef struct {
    mirror_wifi_handle_t wifi;
    mirror_lcd_handle_t lcd;
    mirror_video_handle_t video;
    airplay_receiver_handle_t receiver;
    bool mdns_started;
} mirror_app_t;

void app_main(void)
{
    ESP_ERROR_CHECK(mirror_usb_console_start());

    mirror_app_t *app = calloc(1, sizeof(*app));
    if (!app) { ESP_LOGE("mirror", "No memory for application"); return; }
    // Never erase saved credentials to work around an NVS initialization error.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) goto fail;
    err = mirror_lcd_create(&app->lcd);
    if (err != ESP_OK) goto fail;
    err = mirror_video_create(app->lcd, &app->video);
    if (err != ESP_OK) goto fail;
    err = mirror_video_start(app->video);
    if (err != ESP_OK) goto fail;
    err = mirror_wifi_create(&app->wifi);
    if (err != ESP_OK) goto fail;
    err = mirror_wifi_start(app->wifi);
    if (err != ESP_OK) goto fail;
    err = mdns_init();
    if (err != ESP_OK) goto fail;
    app->mdns_started = true;
    err = mdns_hostname_set("mosaico-mirror");
    if (err != ESP_OK) goto fail;
    airplay_receiver_config_t receiver = {
        .name = CONFIG_MIRROR_NAME, .port = 7000,
        .video = mirror_video_submit, .flush = mirror_video_flush, .ctx = app->video,
    };
    err = esp_wifi_get_mac(WIFI_IF_STA, receiver.mac);
    if (err != ESP_OK) goto fail;
    err = airplay_receiver_create(&receiver, &app->receiver);
    if (err != ESP_OK) goto fail;
    err = airplay_receiver_start(app->receiver);
    if (err != ESP_OK) goto fail;
    ESP_LOGI("mirror", "Ready: iPhone and board must share Wi-Fi; select '%s' in Screen Mirroring", CONFIG_MIRROR_NAME);
    ESP_LOGI("mirror", "Advertised %dx%d @ %d Hz; verify actual profile/dimensions in SPS log",
             CONFIG_MIRROR_WIDTH, CONFIG_MIRROR_HEIGHT, CONFIG_MIRROR_FPS);
    // app_main owns the service handles for the entire standalone app lifetime.
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
fail:
    ESP_LOGE("mirror", "Startup failed: %s", esp_err_to_name(err));
    airplay_receiver_delete(app->receiver);
    mirror_video_delete(app->video);
    mirror_lcd_delete(app->lcd);
    if (app->mdns_started) mdns_free();
    mirror_wifi_delete(app->wifi);
    free(app);
}

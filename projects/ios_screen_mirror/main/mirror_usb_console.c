// SPDX-License-Identifier: GPL-3.0-or-later
#include "mirror_usb_console.h"

#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"
#include "vfs_tinyusb.h"

enum { USB_TASK_CORE = 0, USB_TASK_PRIORITY = 7, HOST_POLL_MS = 10 };
static const char *TAG = "usb_console";
// Single startup-task owner; no concurrent initialization or runtime teardown.
static bool s_started;

esp_err_t mirror_usb_console_start(void)
{
    if (s_started) return ESP_OK;

    // On S31 the sole OTG controller is HS, even though esp_tinyusb's port-0
    // enum is named FULL_SPEED_0. Its S31 default config selects the HS PHY.
    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    // Keep USB on CPU0 and the synchronous OpenH264 video worker on CPU1.
    // Console service remains responsive while decoding consumes CPU time.
    usb.task.xCoreID = USB_TASK_CORE;
    usb.task.priority = USB_TASK_PRIORITY;
    esp_err_t err = tinyusb_driver_install(&usb);
    if (err != ESP_OK) return err;

    const tinyusb_config_cdcacm_t cdc = {.cdc_port = TINYUSB_CDC_ACM_0};
    err = tinyusb_cdcacm_init(&cdc);
    if (err != ESP_OK) goto uninstall;

    // The component's VFS redirects the shared standard streams, including
    // ESP_LOG's vprintf output and printf/fprintf calls from other tasks.
    err = tinyusb_console_init(TINYUSB_CDC_ACM_0);
    if (err != ESP_OK) {
        esp_vfs_tusb_cdc_unregister(NULL);
        goto deinit_cdc;
    }
    esp_vfs_tusb_cdc_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    esp_log_set_vprintf(vprintf);
    s_started = true;
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0 ||
        setvbuf(stderr, NULL, _IONBF, 0) != 0) {
        ESP_LOGW(TAG, "Could not disable stdio buffering");
    }

    const TickType_t started = xTaskGetTickCount();
    const TickType_t wait = pdMS_TO_TICKS(CONFIG_MIRROR_USB_CONSOLE_WAIT_MS);
    while (!tud_cdc_n_connected(TINYUSB_CDC_ACM_0) &&
           (TickType_t)(xTaskGetTickCount() - started) < wait) {
        vTaskDelay(pdMS_TO_TICKS(HOST_POLL_MS));
    }
    // CDC VFS uses nonblocking writes: a closed terminal or full TX FIFO may
    // drop logs, but must not hold up Wi-Fi or the video decoder.
    ESP_LOGI(TAG, "USB-HS CDC console ready: %s", CONFIG_TINYUSB_DESC_PRODUCT_STRING);
    return ESP_OK;

deinit_cdc:
    (void)tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
uninstall:
    (void)tinyusb_driver_uninstall();
    return err;
}

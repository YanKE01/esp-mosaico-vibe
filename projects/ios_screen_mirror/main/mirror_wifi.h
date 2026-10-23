// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_err.h"
typedef struct mirror_wifi_t *mirror_wifi_handle_t;
// One STA interface and event loop owned by this app. Saved NVS credentials are
// read; Kconfig overrides use RAM storage and never overwrite saved credentials.
esp_err_t mirror_wifi_create(mirror_wifi_handle_t *out);
esp_err_t mirror_wifi_start(mirror_wifi_handle_t h);
void mirror_wifi_delete(mirror_wifi_handle_t h);

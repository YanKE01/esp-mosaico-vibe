// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "esp_err.h"

// Application-wide TinyUSB/stdio singleton, owned for the firmware lifetime.
// Call only from app_main before creating application tasks. It intentionally
// stays available after a later startup failure so USB logs remain reachable.
esp_err_t mirror_usb_console_start(void);

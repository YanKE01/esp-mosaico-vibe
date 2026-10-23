// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct airplay_receiver_t *airplay_receiver_handle_t;
typedef struct {
    const char *name;
    uint8_t mac[6];
    uint16_t port;
    // Protocol threads invoke callbacks; borrowed data expires on return.
    // Copy without waiting for decode/display. Callbacks may overlap.
    void (*video)(const uint8_t *data, size_t size, uint64_t pts_us, bool config, void *ctx);
    void (*flush)(void *ctx);
    void *ctx;
} airplay_receiver_config_t;

// App owns mDNS initialization/hostname and the lifetime of config.ctx.
// Only one receiver may advertise _airplay/_raop on process-wide mDNS.
esp_err_t airplay_receiver_create(const airplay_receiver_config_t *config,
                                 airplay_receiver_handle_t *out);
esp_err_t airplay_receiver_start(airplay_receiver_handle_t handle);
// Stop joins protocol threads before returning. Delete also stops the receiver.
void airplay_receiver_stop(airplay_receiver_handle_t handle);
void airplay_receiver_delete(airplay_receiver_handle_t handle);

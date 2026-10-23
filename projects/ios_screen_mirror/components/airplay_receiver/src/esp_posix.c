// SPDX-License-Identifier: GPL-3.0-or-later
#include <errno.h>
#include <pthread.h>
#include "esp_pthread.h"
#include "esp_posix.h"
int airplay_thread_create(pthread_t *thread, void *(*entry)(void *), void *arg)
{
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    // FairPlay/curve arithmetic needs a 16 KiB stack for its deep call chain.
    cfg.stack_size = 16384;
    cfg.prio = 5;
    cfg.thread_name = "airplay";
    cfg.pin_to_core = 0;
    if (esp_pthread_set_cfg(&cfg) != ESP_OK) return ENOMEM;
    return pthread_create(thread, NULL, entry, arg);
}

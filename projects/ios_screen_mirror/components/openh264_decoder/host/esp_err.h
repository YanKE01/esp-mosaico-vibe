// SPDX-License-Identifier: GPL-3.0-or-later
// Host-test definitions for the small ESP error API used by this component.
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104

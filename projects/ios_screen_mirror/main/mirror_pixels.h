// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mirror_h264.h"
// Packed I420, coded stride, SPS crop. Destination is RGB565 in LCD wire order.
bool mirror_pixels_render(const uint8_t *i420, size_t size, const mirror_h264_sps_t *sps,
                          uint16_t *rgb, unsigned width, unsigned height);

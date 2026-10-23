// SPDX-License-Identifier: GPL-3.0-or-later
#include "mirror_pixels.h"
#include <string.h>
static unsigned clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : (unsigned)v; }
bool mirror_pixels_render(const uint8_t *src, size_t size, const mirror_h264_sps_t *s,
                          uint16_t *dst, unsigned width, unsigned height)
{
    if (!src || !s || !dst || !width || !height || width > 4096 || height > 4096 ||
        !s->width || !s->height || s->coded_width > 4096 || s->coded_height > 4096 ||
        (s->coded_width & 1) || (s->coded_height & 1) ||
        s->crop_left > s->coded_width || s->width > s->coded_width - s->crop_left ||
        s->crop_top > s->coded_height || s->height > s->coded_height - s->crop_top) return false;
    size_t plane = (size_t)s->coded_width * s->coded_height;
    if (size < plane + plane / 2) return false;
    unsigned w = width, h = s->height * width / s->width;
    if (h > height) { h = height; w = s->width * height / s->height; }
    if (!w || !h) return false;
    memset(dst, 0, (size_t)width * height * sizeof(*dst));
    unsigned x0 = (width - w) / 2, y0 = (height - h) / 2;
    const uint8_t *u = src + plane, *v = u + plane / 4;
    uint32_t xstep = (s->width << 16) / w, ystep = (s->height << 16) / h;
    for (unsigned y = 0; y < h; ++y) {
        unsigned sy = s->crop_top + ((y * ystep) >> 16);
        for (unsigned x = 0; x < w; ++x) {
            unsigned sx = s->crop_left + ((x * xstep) >> 16);
            int yy = src[(size_t)sy * s->coded_width + sx];
            size_t uv = (size_t)(sy / 2) * (s->coded_width / 2) + sx / 2;
            int cb = u[uv] - 128, cr = v[uv] - 128;
            int scale = s->full_range ? 256 : 298;
            int luminance = scale * (yy - (s->full_range ? 0 : 16));
            int r, g, b;
            if (s->bt709) {
                r = luminance + (s->full_range ? 403 : 459) * cr;
                g = luminance - (s->full_range ? 48 : 55) * cb - (s->full_range ? 120 : 136) * cr;
                b = luminance + (s->full_range ? 475 : 541) * cb;
            } else {
                r = luminance + (s->full_range ? 359 : 409) * cr;
                g = luminance - (s->full_range ? 88 : 100) * cb - (s->full_range ? 183 : 208) * cr;
                b = luminance + (s->full_range ? 454 : 516) * cb;
            }
            uint16_t pixel = ((clamp((r + 128) >> 8) & 0xf8) << 8) |
                             ((clamp((g + 128) >> 8) & 0xfc) << 3) | (clamp((b + 128) >> 8) >> 3);
            dst[(size_t)(y + y0) * width + x + x0] = (uint16_t)((pixel << 8) | (pixel >> 8));
        }
    }
    return true;
}

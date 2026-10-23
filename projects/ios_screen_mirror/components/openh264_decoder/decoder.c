// SPDX-License-Identifier: GPL-3.0-or-later
#include "openh264_decoder.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "codec_api.h"
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

struct openh264_decoder_t {
    ISVCDecoder *codec;
    uint8_t *pixels;
    size_t capacity;
    uint32_t max_pixels;
    SBufferInfo picture;
    bool initialized;
};

esp_err_t openh264_decoder_create(uint32_t max_pixels, openh264_decoder_handle_t *out)
{
    if (!out || !max_pixels || max_pixels > 4096U * 4096U) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    openh264_decoder_handle_t h = calloc(1, sizeof(*h));
    if (!h) return ESP_ERR_NO_MEM;
    h->max_pixels = max_pixels;
    h->capacity = (size_t)max_pixels * 3 / 2;
#if defined(ESP_PLATFORM)
    h->pixels = heap_caps_malloc(h->capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    h->pixels = malloc(h->capacity);
#endif
    if (!h->pixels || WelsCreateDecoder(&h->codec) != 0 || !h->codec) {
        openh264_decoder_delete(h);
        return ESP_ERR_NO_MEM;
    }
    int threads = 0, trace = WELS_LOG_ERROR;
    if ((*h->codec)->SetOption(h->codec, DECODER_OPTION_NUM_OF_THREADS, &threads) != 0 ||
        (*h->codec)->SetOption(h->codec, DECODER_OPTION_TRACE_LEVEL, &trace) != 0) {
        openh264_decoder_delete(h);
        return ESP_FAIL;
    }
    const SDecodingParam cfg = {
        .uiTargetDqLayer = 255,
        .eEcActiveIdc = ERROR_CON_DISABLE,
        .sVideoProperty = {.size = sizeof(SVideoProperty), .eVideoBsType = VIDEO_BITSTREAM_AVC},
    };
    if ((*h->codec)->Initialize(h->codec, &cfg) != 0) {
        openh264_decoder_delete(h);
        return ESP_ERR_NO_MEM;
    }
    h->initialized = true;
    *out = h;
    return ESP_OK;
}

esp_err_t openh264_decoder_process(openh264_decoder_handle_t h, const uint8_t *data,
                                  size_t size, uint64_t pts, openh264_frame_t *out)
{
    if (!h || !out || size > INT_MAX || (!data && size)) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    memset(&h->picture, 0, sizeof(h->picture));
    h->picture.uiInBsTimeStamp = pts;
    uint8_t *planes[3] = {0};
    DECODING_STATE status = data
        ? (*h->codec)->DecodeFrameNoDelay(h->codec, data, size, planes, &h->picture)
        : (*h->codec)->FlushFrame(h->codec, planes, &h->picture);
    out->codec_status = status;
    if (status & ~dsFramePending) return ESP_FAIL;
    if (h->picture.iBufferStatus != 1) return ESP_OK;
    const SSysMEMBuffer *p = &h->picture.UsrData.sSystemBuffer;
    if (p->iWidth <= 0 || p->iHeight <= 0 || (p->iWidth & 1) || (p->iHeight & 1) ||
        (uint64_t)p->iWidth * p->iHeight > h->max_pixels ||
        p->iStride[0] < p->iWidth || p->iStride[1] < p->iWidth / 2 ||
        !planes[0] || !planes[1] || !planes[2]) return ESP_ERR_INVALID_SIZE;
    size_t pos = 0;
    for (unsigned plane = 0; plane < 3; ++plane) {
        unsigned width = p->iWidth >> (plane != 0), height = p->iHeight >> (plane != 0);
        size_t stride = p->iStride[plane != 0];
        for (unsigned y = 0; y < height; ++y) {
            memcpy(h->pixels + pos, planes[plane] + y * stride, width);
            pos += width;
        }
    }
    out->data = h->pixels;
    out->size = pos;
    out->width = p->iWidth;
    out->height = p->iHeight;
    out->pts = h->picture.uiOutYuvTimeStamp;
    return ESP_OK;
}

void openh264_decoder_delete(openh264_decoder_handle_t h)
{
    if (!h) return;
    if (h->codec) {
        if (h->initialized) (*h->codec)->Uninitialize(h->codec);
        WelsDestroyDecoder(h->codec);
    }
    free(h->pixels);
    free(h);
}

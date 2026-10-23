// SPDX-License-Identifier: GPL-3.0-or-later
#include "mirror_h264.h"
#include <string.h>

enum { SPS_BYTES = 256, MAX_REFS = 16, MAX_MB_DIMENSION = 256 };
typedef struct { const uint8_t *p; size_t size, pos; bool ok; } bits_t;

static uint32_t bits(bits_t *b, unsigned n)
{
    if (n > 32 || b->pos > b->size * 8 || n > b->size * 8 - b->pos) {
        b->ok = false;
        return 0;
    }
    uint32_t value = 0;
    while (n--) {
        value = (value << 1) | ((b->p[b->pos / 8] >> (7 - b->pos % 8)) & 1);
        ++b->pos;
    }
    return value;
}
static uint32_t ue(bits_t *b)
{
    unsigned zeros = 0;
    while (b->ok && !bits(b, 1)) {
        if (++zeros > 30) { b->ok = false; return 0; }
    }
    return ((1U << zeros) - 1) + bits(b, zeros);
}
static bool scaling_list(bits_t *b, unsigned count)
{
    int last = 8, next = 8;
    for (unsigned i = 0; i < count; ++i) {
        if (next) {
            uint32_t code = ue(b);
            if (!b->ok || code > 256) return false;
            int delta = (code & 1) ? (int)((code + 1) / 2) : -(int)(code / 2);
            next = (last + delta + 256) & 255;
        }
        if (next) last = next;
    }
    return b->ok;
}
static size_t start_code(const uint8_t *p, size_t len)
{
    if (len >= 3 && p[0] == 0 && p[1] == 0) {
        if (p[2] == 1) return 3;
        if (len >= 4 && p[2] == 0 && p[3] == 1) return 4;
    }
    return 0;
}
static const uint8_t *find_nal(const uint8_t *p, size_t len, unsigned type, size_t *size)
{
    for (size_t i = 0; i + 3 < len; ++i) {
        size_t prefix = start_code(p + i, len - i);
        if (!prefix || i + prefix >= len) continue;
        size_t begin = i + prefix, end = begin + 1;
        while (end < len && !start_code(p + end, len - end)) ++end;
        if ((p[begin] & 0x9f) == type) { *size = end - begin; return p + begin; }
        i = end - 1;
    }
    return NULL;
}
bool mirror_h264_has_nal(const uint8_t *data, size_t len, unsigned type)
{
    size_t size;
    return data && find_nal(data, len, type, &size);
}
bool mirror_h264_read_sps(const uint8_t *data, size_t len, mirror_h264_sps_t *out)
{
    if (!data || !out) return false;
    memset(out, 0, sizeof(*out));
    size_t size;
    const uint8_t *nal = find_nal(data, len, 7, &size);
    if (!nal || size < 4 || size > SPS_BYTES) return false;
    // A bounded parser stack budget of 256 bytes; no untrusted-sized VLA.
    uint8_t rbsp[SPS_BYTES];
    size_t used = 0;
    unsigned zeros = 0;
    for (size_t i = 1; i < size; ++i) {
        uint8_t c = nal[i];
        if (zeros == 2 && c == 3) {
            if (i + 1 >= size || nal[i + 1] > 3) return false;
            zeros = 0;
            continue;
        }
        rbsp[used++] = c;
        zeros = c == 0 ? zeros + 1 : 0;
    }
    bits_t b = {.p = rbsp, .size = used, .ok = true};
    out->profile = bits(&b, 8);
    out->constraints = bits(&b, 8);
    out->level = bits(&b, 8);
    if ((out->profile != 66 && out->profile != 77 && out->profile != 100) ||
        (out->constraints & 3) || ue(&b) > 31) return false;
    if (out->profile == 100) {
        // High profile adds chroma/bit-depth/scaling syntax before the common
        // SPS fields. Parse it rather than relabeling the stream as Baseline.
        if (ue(&b) != 1 || ue(&b) != 0 || ue(&b) != 0) return false;
        if (bits(&b, 1)) return false; // QP-prime transform bypass unsupported.
        if (bits(&b, 1)) {
            for (unsigned i = 0; i < 8; ++i) {
                if (bits(&b, 1) && !scaling_list(&b, i < 6 ? 16 : 64)) return false;
            }
        }
    }
    if (ue(&b) > 12) return false;
    uint32_t poc = ue(&b);
    if (poc == 0) { if (ue(&b) > 12) return false; }
    else if (poc == 1) {
        bits(&b, 1); ue(&b); ue(&b);
        uint32_t count = ue(&b);
        if (count > 255) return false;
        for (uint32_t i = 0; i < count; ++i) ue(&b);
    } else if (poc != 2) return false;
    out->refs = ue(&b);
    bits(&b, 1);
    uint32_t mbw = ue(&b), mbh = ue(&b);
    if (!b.ok || mbw >= MAX_MB_DIMENSION || mbh >= MAX_MB_DIMENSION || out->refs > MAX_REFS) return false;
    out->coded_width = (mbw + 1) * 16;
    out->coded_height = (mbh + 1) * 16;
    if (!bits(&b, 1)) return false; // Progressive 4:2:0 only.
    bits(&b, 1);
    uint32_t left = 0, right = 0, top = 0, bottom = 0;
    if (bits(&b, 1)) {
        left = ue(&b); right = ue(&b); top = ue(&b); bottom = ue(&b);
    }
    if (!b.ok || left >= out->coded_width / 2 || right >= out->coded_width / 2 ||
        top >= out->coded_height / 2 || bottom >= out->coded_height / 2 ||
        left + right >= out->coded_width / 2 || top + bottom >= out->coded_height / 2) return false;
    out->crop_left = left * 2;
    out->crop_top = top * 2;
    out->width = out->coded_width - (left + right) * 2;
    out->height = out->coded_height - (top + bottom) * 2;
    if (bits(&b, 1)) { // VUI: enough to determine matrix/range.
        if (bits(&b, 1) && bits(&b, 8) == 255) { bits(&b, 16); bits(&b, 16); }
        if (bits(&b, 1)) bits(&b, 1);
        if (bits(&b, 1)) {
            bits(&b, 3);
            out->full_range = bits(&b, 1);
            if (bits(&b, 1)) { bits(&b, 8); bits(&b, 8); out->bt709 = bits(&b, 8) == 1; }
        }
    }
    return b.ok;
}

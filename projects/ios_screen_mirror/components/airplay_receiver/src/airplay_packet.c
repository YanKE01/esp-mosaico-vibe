// SPDX-License-Identifier: GPL-3.0-or-later
#include "airplay_packet.h"
#include <string.h>
bool airplay_packet_to_annexb(uint8_t *p, size_t size)
{
    if (!p || !size || size > AIRPLAY_PACKET_MAX) return false;
    size_t pos = 0;
    while (pos < size) {
        if (size - pos < 4) return false;
        uint32_t n = ((uint32_t)p[pos] << 24) | ((uint32_t)p[pos + 1] << 16) |
                     ((uint32_t)p[pos + 2] << 8) | p[pos + 3];
        if (!n || n > size - pos - 4) return false;
        pos += n + 4;
    }
    pos = 0;
    while (pos < size) {
        uint32_t n = ((uint32_t)p[pos] << 24) | ((uint32_t)p[pos + 1] << 16) |
                     ((uint32_t)p[pos + 2] << 8) | p[pos + 3];
        p[pos] = p[pos + 1] = p[pos + 2] = 0;
        p[pos + 3] = 1;
        pos += n + 4;
    }
    return true;
}
size_t airplay_config_to_annexb(const uint8_t *p, size_t size, uint8_t *out, size_t capacity)
{
    if (!p || !out || size < 7 || p[0] != 1 || (p[4] & 3) != 3) return 0;
    size_t pos = 6, used = 0;
    unsigned count = p[5] & 31;
    if (!count) return 0;
    for (unsigned group = 0; group < 2; ++group) {
        for (unsigned i = 0; i < count; ++i) {
            if (pos > size || size - pos < 2) return 0;
            size_t n = ((size_t)p[pos] << 8) | p[pos + 1];
            pos += 2;
            if (!n || n > size - pos || used > capacity || capacity - used < 4 || n > capacity - used - 4) return 0;
            out[used] = out[used + 1] = out[used + 2] = 0;
            out[used + 3] = 1;
            memcpy(out + used + 4, p + pos, n);
            used += n + 4;
            pos += n;
        }
        if (group == 0) {
            if (pos >= size) return 0;
            count = p[pos++];
            if (!count) return 0;
        }
    }
    return used;
}

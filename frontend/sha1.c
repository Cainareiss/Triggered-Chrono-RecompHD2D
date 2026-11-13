/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SHA-1 (sha1.h). */
#include "sha1.h"

#include <stdio.h>
#include <string.h>

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int k = 0; k < 16; k++)
        w[k] = (uint32_t)p[4 * k] << 24 | (uint32_t)p[4 * k + 1] << 16 |
               (uint32_t)p[4 * k + 2] << 8 | p[4 * k + 3];
    for (int k = 16; k < 80; k++)
        w[k] = rol(w[k - 3] ^ w[k - 8] ^ w[k - 14] ^ w[k - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int k = 0; k < 80; k++) {
        uint32_t f, K;
        if (k < 20) {
            f = (b & c) | (~b & d);
            K = 0x5A827999;
        } else if (k < 40) {
            f = b ^ c ^ d;
            K = 0x6ED9EBA1;
        } else if (k < 60) {
            f = (b & c) | (b & d) | (c & d);
            K = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            K = 0xCA62C1D6;
        }
        uint32_t t = rol(a, 5) + f + e + K + w[k];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void sha1_hex(const void *data, size_t n, char out[41])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    const uint8_t *p = data;
    size_t left = n;
    for (; left >= 64; left -= 64, p += 64)
        block(h, p);
    /* The tail, a 1 bit, zeros, and the length in bits (big-endian). */
    uint8_t tail[128] = {0};
    memcpy(tail, p, left);
    tail[left] = 0x80;
    size_t len = left + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++)
        tail[len - 1 - k] = (uint8_t)(bits >> (8 * k));
    block(h, tail);
    if (len == 128)
        block(h, tail + 64);
    for (int k = 0; k < 5; k++)
        snprintf(out + 8 * k, 9, "%08x", h[k]);
}

/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Save states' serialization (savestate.h: ss_io), in the runtime library
 * with the parts of the machine that use it; savestate.c puts them
 * together. */
#include "savestate.h"

#include <stdlib.h>
#include <string.h>

/* ---- serialization ---- */

static void fail(ss_io *s, const char *why)
{
    if (!s->err)
        s->err = why;
}

void ss_bytes(ss_io *s, void *p, size_t n)
{
    if (s->err)
        return;
    if (s->loading) {
        if (n > s->in_len - s->pos) {
            fail(s, "the state is cut short");
            return;
        }
        if (s->loading == 1)
            memcpy(p, s->in + s->pos, n);
        s->pos += n;
        return;
    }
    if (s->len + n > s->cap) {
        size_t cap = s->cap ? s->cap : 1u << 20;
        while (cap < s->len + n)
            cap *= 2;
        uint8_t *b = realloc(s->buf, cap);
        if (!b) {
            fail(s, "out of memory");
            return;
        }
        s->buf = b;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, p, n);
    s->len += n;
}

static void put32(uint8_t *b, uint32_t v)
{
    for (int k = 0; k < 4; k++)
        b[k] = (uint8_t)(v >> (8 * k));
}

static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* A section: its tag and length, so a load from another build fails on
   the first part whose size differs instead of reading garbage. */
void ss_begin(ss_io *s, const char tag[4])
{
    uint8_t head[8];
    if (s->loading) {
        if (s->err)
            return;
        if (s->in_len - s->pos < 8 || memcmp(s->in + s->pos, tag, 4)) {
            fail(s, "a section is missing (another format)");
            return;
        }
        s->mark = s->pos + 8 + get32(s->in + s->pos + 4);
        s->pos += 8;
        if (s->mark > s->in_len)
            fail(s, "the state is cut short");
        return;
    }
    memcpy(head, tag, 4);
    put32(head + 4, 0);
    s->mark = s->len + 4;
    ss_bytes(s, head, sizeof head);
}

void ss_end(ss_io *s)
{
    if (s->err)
        return;
    if (s->loading) {
        if (s->pos != s->mark)
            fail(s, "a section's size differs: the state is from a different build");
        return;
    }
    put32(s->buf + s->mark, (uint32_t)(s->len - s->mark - 4));
}


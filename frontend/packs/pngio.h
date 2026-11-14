/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PNG files for asset packs: an RGBA writer (stored deflate blocks, no
 * dependencies) and a reader for the PNGs people edit them into (any
 * non-interlaced 8-bit gray, gray+alpha, RGB, RGBA or palette image, and
 * 1/2/4-bit palette and gray ones, with a small inflate of its own). */
#ifndef CT_PACKS_PNGIO_H
#define CT_PACKS_PNGIO_H

#include <stddef.h>
#include <stdint.h>

/* rgba: w x h pixels, bytes R G B A, rows top first. Returns 0 or -1. */
int pngio_write_rgba(const char *path, const uint8_t *rgba, int w, int h);

/* Read path as RGBA (malloc'd, free it); *w, *h get its size. NULL on an
   error or an unsupported file (16-bit channels, interlacing), with the
   reason in *err if err is not NULL. */
uint8_t *pngio_read_rgba(const char *path, int *w, int *h, const char **err);

/* The same from memory. */
uint8_t *pngio_decode_rgba(const uint8_t *data, size_t n, int *w, int *h, const char **err);

/* zlib inflate (RFC 1950/1951) of src into dst: returns the number of
   bytes written, or -1 on bad or truncated data or if dst is too small. */
long pngio_inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);

#endif

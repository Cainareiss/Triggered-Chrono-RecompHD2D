/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SHA-1 (FIPS 180-4), for identifying files: the first-run flow checks
 * the user's ROM against the game's game.toml and records which ROM a
 * game module was built from. Not for anything security-related. */
#ifndef CT_SHA1_H
#define CT_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* The SHA-1 of data[0..n) as 40 lowercase hex digits and a NUL. */
void sha1_hex(const void *data, size_t n, char out[41]);

#endif

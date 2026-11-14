/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: stb_truetype's implementation (third_party/stb: public domain or
 * MIT), in a file of its own so the vendored code builds without our
 * warning flags; hdtext.c uses its header. */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

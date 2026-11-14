/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the audio pack decoders' implementations, in a file of their
 * own so that the vendored code (third_party/stb, third_party/dr_libs:
 * public domain, or MIT / MIT-0) builds without our warning flags.
 * music.c uses their headers. */
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

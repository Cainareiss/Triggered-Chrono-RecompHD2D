<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Vendored stb libraries

Source: https://github.com/nothings/stb, commit
`2838bd5823c095dd1d5b5d8fac0b4fc8aa72a36d` (2026-10-29).

| File | Version | SHA-256 | Used by |
| --- | --- | --- | --- |
| `stb_vorbis.c` | v1.22 | `7cd2d3d8128c0f7633e819c05bc2f115f57452c0b221a50d5aa59a3778f14734` | `frontend/packs/music.c`: Ogg Vorbis files in audio packs |
| `stb_truetype.h` | v1.26 | `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab` | `frontend/hdtext.c`: the player's TrueType / OpenType font for HD text |

Vendored unmodified. Both are by Sean Barrett and available under two licenses, the
reader's choice (the notice is at the end of each file): public domain (the Unlicense,
`LICENSES/Unlicense.txt`) or MIT (Copyright (c) 2017 Sean Barrett,
`LICENSES/MIT.txt`). Either is compatible with GPL-3.0-or-later.

Each is compiled in exactly one file of ours, which defines the implementation macro
(`frontend/packs/music.c` includes `stb_vorbis.c`; `frontend/hdtext.c` defines
`STB_TRUETYPE_IMPLEMENTATION`). They read only files the player chose: audio pack
files and a font in the data folder. Nothing from them is shipped with data.

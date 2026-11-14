<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Vendored dr_libs

Source: https://github.com/mackron/dr_libs (David Reid).

| File | Version | Commit | SHA-256 |
| --- | --- | --- | --- |
| `dr_flac.h` | v0.13.4 (2026-10-31) | `806ee9ada8ca9827b22f76827f739c290f37ba48` | `b8078eeb5d523502eb7964508cd7ec8450bc79d383bec90aa17fd9e2fefd9c24` |
| `dr_wav.h` | v0.14.6 (2026-10-31) | `35b5c392cc15456736c440a68dbe073178bbffe0` | `fad60f1e360d6de726efc1b9a7985610c89c551719e3f110e8b992f0e1f308cd` |

Vendored unmodified. Each is available under two licenses, the reader's choice (the
notice is at the end of each file): public domain (the Unlicense,
`LICENSES/Unlicense.txt`) or MIT No Attribution (Copyright 2023 David Reid,
`LICENSES/MIT-0.txt`). Either is compatible with GPL-3.0-or-later.

`frontend/packs/music.c` is the only file that defines `DR_FLAC_IMPLEMENTATION` and
`DR_WAV_IMPLEMENTATION`: they decode the FLAC and WAV files of audio packs
(docs/ASSET-PACKS.md). The tests also write their synthetic test tones with dr_wav's
writer.

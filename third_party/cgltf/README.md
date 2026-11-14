<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Vendored cgltf

Source: https://github.com/jkuhlmann/cgltf, tag `v1.14`, file `cgltf.h`
(SHA-256 `19db3bdf85f6aa991a3b9d056641bcb39a2ef11cd5211e7a9f6b6c307d9ebc64`).

Vendored unmodified. MIT License, Copyright (c) 2018-2021 Johannes Kuhlmann; the full
notice is at the end of `cgltf.h` (and `LICENSES/MIT.txt`).

Used by `frontend/packs/packs_model.c`, the only file that defines
`CGLTF_IMPLEMENTATION`, to read and check the glTF 2.0 models that asset packs name
in `[[model]]` entries (docs/ASSET-PACKS.md).

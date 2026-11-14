<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Vendored AMD FidelityFX FSR 1 and CAS

For the HD-2D renderer's 3D diorama (`frontend/hd2d/`, docs/HD2D.md): FSR 1's EASU
upscales the 3D scene from its render scale to the picture's size, and its RCAS
sharpens the result; CAS sharpens at the native render scale.

| File | Source | Version | SHA-256 |
| --- | --- | --- | --- |
| `ffx_fsr1.h` | https://github.com/GPUOpen-Effects/FidelityFX-FSR, tag `v1.0.2`, commit `a21ffb8f6c13233ba336352bdff293894c706575`, `ffx-fsr/ffx_fsr1.h` | FSR 1 v1.20210629 | `93c3922362ea7fc99cbcc698ca30c98de4f8c246d1fbb0b09e015ddef38ce3a5` |
| `ffx_a.h` | the same commit, `ffx-fsr/ffx_a.h` | (FSR's copy) | `f60e2722fcd13989523b9164d776ab382b3692791767f3bf8bb19967f763f3fb` |
| `LICENSE-FSR.txt` | the same commit, `license.txt` | | `db089274ce766da70f5b7d791029c3486f9f9e27c8c79c652689603d3192e802` |
| `ffx_cas.h` | https://github.com/GPUOpen-Effects/FidelityFX-CAS, tag `v1.0.2`, commit `9fabcc9a2c45f958aff55ddfda337e74ef894b7f`, `ffx-cas/ffx_cas.h` | CAS 1.20190610 | `3224935584202390ca9bc0149a45973ad2269f4d874abb2ff13625263fc036e0` |
| `LICENSE-CAS.txt` | the same commit, `LICENSE.txt` | | `e008031a95f6f655def9eaf914118531c9e1055b8ea201eb98b9f95f1af9bec4` |

These five are unmodified, MIT License, Copyright (c) 2017-2021 Advanced Micro Devices,
Inc. (the notice is at the top of each header and in the two license files; also
`LICENSES/MIT.txt`). CAS's repository has its own, older `ffx_a.h`; the helpers the
port uses (`APrxLoRcpF1`, `APrxLoRsqF1`, `APrxMedRcpF1`, `APrxLoSqrtF1`, the min/max
ones) are the same in both.

They aren't compiled. `ffx_a.h`'s GLSL section needs GLSL 4.00 / ES 3.10 functions
(`bitfieldExtract` and others) that the renderer's OpenGL 3.3 core / OpenGL ES 3.0
targets don't have, and EASU's reference callbacks use `textureGather`, which ES 3.0
lacks. They are kept as the source of:

- `ffx_fsr1_cas.glsl`: our port of the parts we use (EASU, RCAS, FSR's reversible
  tonemap, CAS without scaling) to GLSL 3.30 / ES 3.00 at 32-bit float. The math is
  the headers' non-packed 32-bit paths, unchanged, apart from the differences listed
  at its top (the taps are passed in, so the caller can `texelFetch` them; RCAS's
  limiter guarded against 0/0). Derived from MIT code, it keeps AMD's notice and is
  MIT too (copyright AMD and jerbanpls). `CMakeLists.txt` embeds it with the
  renderer's shaders (`hd2d_src_ffx_fsr1_cas_glsl`); `frontend/hd2d/hd2d.c` compiles
  it in front of `fsr_easu.frag` and `scene3d_pic.frag`.

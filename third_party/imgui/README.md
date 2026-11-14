<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Vendored Dear ImGui

Source: https://github.com/ocornut/imgui, release tag `v1.92.9b` (2026-07-31),
tarball https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
(SHA-256 `21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99`).

Vendored unmodified. MIT License, Copyright (c) 2014-2026 Omar Cornut; the full
notice is `LICENSE.txt` here (and `LICENSES/MIT.txt`).

Files taken, as they are in the release:

- the core: `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`,
  `imgui.h`, `imgui_internal.h`, `imconfig.h`, `imstb_rectpack.h`,
  `imstb_textedit.h`, `imstb_truetype.h` (the stb parts are public domain / MIT,
  Sean Barrett, as their headers say);
- `backends/`: the SDL2 platform backend (`imgui_impl_sdl2.*`), the OpenGL 3 / ES 3
  renderer (`imgui_impl_opengl3.*`, `imgui_impl_opengl3_loader.h`) and the
  SDL_Renderer 2 renderer (`imgui_impl_sdlrenderer2.*`);
- `fonts/Roboto-Medium.ttf` from the release's `misc/fonts/` (Roboto by Christian
  Robertson, Apache License 2.0, `LICENSES/Apache-2.0.txt`; SHA-256
  `8559132c89ad51d8a2ba5b171887a44a7ba93776e205f553573de228e64b45f8`). The build
  embeds it (`tools/embed_binary.cmake`) as the menu's font.

Not taken: the demo window, the examples, the other backends, the docs.

Used only by the frontend's settings menu (`frontend/menu.cpp`, `frontend/menu.h`).
To update: replace these files with the same ones from a newer release and change the
tag and hashes here and in THIRD_PARTY.md.

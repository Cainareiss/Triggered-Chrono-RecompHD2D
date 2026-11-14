<!-- SPDX-FileCopyrightText: 2026 jerbanpls -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Button glyphs

The pictures the on-screen messages use for buttons and keys
(`frontend/glyphs.c`, `frontend/osd_text.c`). At build time
`tools/embed_glyphs.py` scales each one to 44 x 44 and stores it as a
coverage mask (alpha times luminance) in `glyphs_gen/glyphs_data.h`.

- **What:** Kenney's *Input Prompts*, version 1.3 (its `License.txt`, copied
  here unchanged, says "Input Prompts (1.3)", created 03-04-2025). 145 of
  the pack's 64 px PNGs from its `Default/` folders, unmodified: the Steam
  Deck, the Steam Controller, Xbox Series, PlayStation, Nintendo Switch, and
  keyboard keys.
- **License:** CC0 1.0 Universal (public domain dedication), as stated in
  `License.txt` and on <https://kenney.nl/assets/input-prompts>. Crediting
  Kenney is welcome but not required. Full text: `LICENSES/CC0-1.0.txt`.
- **Downloaded from:** <https://github.com/Maaack/Kenney-Input-Prompts>,
  commit `dd37497091c176e91ce81a14e203729ea12d8e4b` (2025-04-03), a mirror of
  the pack (its README links the source above). kenney.nl, where 1.5a is now
  the current version, refused scripted downloads (HTTP 403) on 2026-11-13.
  The license file at the mirror was checked: CC0.

The file names are the pack's. To add a picture, copy it from the pack's
`<set>/Default/` folder into this directory and name it in the tables in
`frontend/glyphs.c`; CMake picks up every `*.png` here.

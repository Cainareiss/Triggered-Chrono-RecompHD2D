<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Progress log

## 2026-11-13: overnight session

### Read this first
- **Branch:** all work is on `hd2d`, built on `gpl-base`. Nothing has been pushed and
  `main` is untouched.
- **Previews:** the before/after screenshots are in `shots/hd2d-preview/`, which is
  git-ignored and local only, since the screenshots come from your ROM:
  - `compare_*.png`: split view, with the original pixels on the left and HD-2D on the
    right
  - `hd2d_*.png`: the full HD-2D look
- **Try it:**
  ```
  CT_ROM="$PWD/rom/Chrono Trigger (USA).sfc" cmake -S . -B build-hd && cmake --build build-hd -j
  build-hd/ct_sdl --hd2d          # F1 / R3: preset, F2: debug views, F3 / L3: aspect, F11: fullscreen
  ```
  On Bazzite it builds against the system `libSDL2` with official SDL 2.32.10 headers,
  fetched automatically and SHA-256 pinned.

### Done
1. **Licensing** (`gpl-base`, `ff4bb15`):
   - ct-recomp imported with its full history, and the project relicensed as
     GPL-3.0-or-later.
   - The MIT and ISC parts keep their notices.
   - A `REUSE.toml` file map passes `reuse lint`.
   - The full upstream test suite passed on that tree: 63/63, with `sync_determinism`
     skipped by design.
2. **SDL build on Bazzite** (`842ad07`).
3. **HD-2D renderer** (`59b3083`, `ea03109`), documented in `docs/HD2D.md`:
   - The PPU now outputs a per-pixel layer/priority G-buffer. Plain output is untouched:
     all 9,200 frames of the Leene Square replay are hash-identical to before.
   - A GL 3.3 / GLES 3.0 pipeline:
     - layer classes, with the UI and HUD kept sharp
     - relief lighting, actor shadows, contact occlusion and rim light
     - tilt-shift depth of field that keeps actors sharp
     - bloom, a filmic grade, and side fill around the picture
   - Presets, debug views, a split-compare view, controller shortcuts and saved settings.
   - Frame time: about 2 ms at roughly 1080p on this Ally's Radeon 780M.
   - Tests: 59/59 pass on the `hd2d` build. The long `diff_all_*` shards weren't rerun
     after the PPU change, but frame and state hashes match on the replay.

### Things that need your decision
- **krtp0028 fork merge:** the permission check blocked building and running this
  unreviewed third-party code while you were away, so I aborted the merge. Its Windows
  and PSP work includes the `ucontext` compatibility layer that the Android port needs.
  Review it with `git log a6b4925..krtp/main` (the `krtp` remote is set up). Tell me if
  it's OK to merge.

### Mistake caught and fixed
- The first SDL commit accidentally included the `build-hd/` folder, which contained
  binaries built from ROM-derived code.
- I amended that unpushed commit, added `build-*/` to `.gitignore`, and pruned the
  unreachable objects (`git gc --prune=now`).
- I checked every commit on every branch: none contains build output or ROM data.
- I now stage explicit paths and review them before committing.

### Next, in plan order
1. **HD-2D, next level:**
   - per-layer PPU output (not just the winning pixel), for real parallax and camera tilt
   - point lights from emissive pixels
   - a settings overlay
2. **Engine and game-module split:** needed before any package can ship without
   ROM-derived code.
3. **AppImage and Flatpak.**
4. **Android:** needs the fork merge.

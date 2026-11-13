<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Engine and game module

The C this project compiles is translated from the ROM, so any program that contains it
also contains code derived from the game. To keep that code out of anything distributed,
the build is split in two:

| Part | What it is | Can be distributed? |
| --- | --- | --- |
| **Engine:** `ct_sdl_engine`, `ct_boot_engine` | runtime, scheduler, interpreter, frontend, HD-2D renderer, the translator scripts | yes (GPL-3.0-or-later, plus the MIT and ISC parts) |
| **Game module:** `ct_game.so` | the generated code, built on the user's machine from their own ROM | **no:** it never leaves that machine |

The engine reads its function tables through `ct_tables_cur` (`runtime/func_table.h`).
A module replaces them when it loads (`runtime/module.h`). Without a module the engine
runs every function in the interpreter. That's slower, but it measured about 290 fps
headless on a Ryzen Z1 Extreme, so it still works as a fallback.

## Building a module

With CMake, as a developer:

```
CT_ROM=rom/your_dump.sfc cmake -S . -B build && cmake --build build -j
build/ct_sdl_engine --module build/ct_game.so --hd2d
```

Without CMake, which is what an installed engine will run on first launch:

```
python3 tools/build_module.py --game game/ct --rom rom/your_dump.sfc \
    --out ~/.local/share/ct-recomp --native
```

The script does five things:
1. checks the ROM's hash
2. decodes the overlays
3. translates the game to C
4. compiles the C in parallel and links `ct_game.so`
5. deletes the generated C

It took about 40 s on a Ryzen Z1 Extreme. `--native` tunes the code for the local CPU.

## Guarantees

- `module_parity` (ctest, needs the ROM): the engine plus the module matches the static
  build frame for frame. It compares CPU, memory, video and audio hashes over 3,000
  frames of the Leene Square replay. It has also been verified over all 9,200 frames,
  for both the CMake-built and the script-built `-march=native` module.
- The module exports only `ct_module_get`. The loader checks the module ABI and the
  struct layouts before installing the tables.
- `ct_boot_engine` contains no generated functions (checked with
  `nm build/ct_boot_engine | grep ' f_'`).

## Packages

The Flatpak and the AppImage hold the engine only: `-DCT_ENGINE_ONLY=ON` builds it with
no game directory and no `$CT_ROM`. They also bundle what `build_module.py` needs, a
Python and zig as the C compiler. See [PACKAGING.md](PACKAGING.md).

## Not done yet

- A scan that checks every distributed package for ROM-derived content.
- The first-run flow in the frontend: pick the ROM, build the module, then play.
- Android: build an arm64 module on the PC and copy it over. This needs the `ucontext`
  compatibility layer from the krtp0028 fork.

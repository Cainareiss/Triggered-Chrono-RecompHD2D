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

## First run

`ct_sdl_engine` started without `--module` sets itself up (`frontend/firstrun.c`):

1. **The ROM:** `--rom FILE`, then `$CT_ROM`, then the last ROM that passed, else
   it waits for a file dropped on the window. Each candidate is checked against
   the size and `rom_sha1` in `game/<id>/game.toml`. A wrong file is named on
   the screen, with a hint if it has a copier header.
2. **The module:** `ct_game.so` in the per-game preferences folder
   (`SDL_GetPrefPath("ct-recomp", <id>)`, so `~/.local/share/ct-recomp/<id>/` on
   Linux). `ct_game.stamp` beside it records the ROM's SHA-1 and the module ABI it
   was built for. If the module is missing, or the stamp doesn't match this ROM
   and this engine, the engine runs `tools/build_module.py ... --native` as a
   child process and shows its step lines on a progress screen. A module that
   matches but won't load is rebuilt once.
3. **Play:** the module is loaded and the game starts. If the build fails, the
   screen shows the last error lines and any key or button starts the game in
   the interpreter instead.

`--interp-only` skips the module (only step 1 runs). Ctrl+Q or closing the window
quits at any point, and also stops a running build.

The engine contains no game names: the game id comes from the build (the game
directory's name, compiled in as the default) or `--game ID`. The game's files and
the tools come from `--data-dir DIR`. Its default is the source tree; packagers
set it with `-DCT_ENGINE_DATA_DIR=...` to where they install `game/`, `tools/`,
`recomp/` and `runtime/` (the build tool compiles the module against
`runtime/`'s headers). Python 3.11+ and a C compiler must be installed, and
`$CT_PYTHON` overrides `python3`.

To see the screens headless: `--shot status:PATH` writes the setup screen each
time it changes, and a `%d` in PATH numbers the shots.

## Checking a package

```
python3 tools/check_dist.py PACKAGE_DIR_OR_FILE... --rom rom/your_dump.sfc
```

It exits 1 if anything in the package was built from the ROM, and also looks
inside `.zip`, `.tar(.gz/.bz2/.xz)` and `.AppImage` files (the last needs
`unsquashfs`). It looks for:

- a module's or a generated source's file name (`ct_game*.so`, `bank*.c`,
  `ct_funcs.c`, `ct_overlays.c`)
- an ELF file that exports `ct_module_get`, which catches a renamed, stripped
  module
- generated function names (`f_XXXXXX_mNxN`)
- any of about 4,000 sampled 64-byte windows of the ROM's non-padding data,
  checked with one hash-set lookup per anchor-byte offset. A whole ROM is always
  found. In a test with random pieces hidden in other data, it found about 99% of
  the 4 KiB pieces and all of the 16 KiB ones. `--samples N` raises coverage for
  shorter pieces.

The ctest `check_dist` runs it on the engine programs, which must pass, and on
`ct_game.so`, `ct_boot` and `ct_sdl`, and on ROM pieces hidden in nested archives,
which must all fail. It takes about 3 s.

## Not done yet

- A stripped module renamed to something else is caught only by its
  `ct_module_get` export. Generated code that is linked statically into some
  other program, stripped and renamed, would be found only through the ROM
  windows, and compiled code doesn't contain those.
- The module stamp doesn't cover the game's data files (`funcs.toml` and the
  others). An engine update that changes them without bumping the module ABI
  keeps the old module until you delete it.
- Android: build an arm64 module on the PC and copy it over. This needs the `ucontext`
  compatibility layer from the krtp0028 fork.

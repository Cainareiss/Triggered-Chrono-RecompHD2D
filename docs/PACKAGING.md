<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Packaging

The packages contain the engine and nothing from the game. The game code is built on
the user's machine, from the user's own ROM, the first time they use it (see
[MODULES.md](MODULES.md)). This page covers how to build the packages and what keeps
game code out of them.

## What a package contains

Everything comes from one CMake install of an engine-only build:

```
bin/ct_sdl_engine                    the engine (no generated code)
bin/ct-recomp                        launcher (packaging/ct-recomp.sh)
libexec/ct-recomp/zig-cc             runs the bundled zig as a C compiler
share/ct-recomp/                     read-only data for tools/build_module.py:
    tools/*.py  recomp/*.py
    runtime/*.h  runtime/module_entry.c
    game/<id>/*.toml  game/<id>/hooks.c  game/<id>/tools/*.py
share/applications/io.github.jerbanpls.CtRecomp.desktop
share/icons/hicolor/scalable/apps/io.github.jerbanpls.CtRecomp.svg
share/metainfo/io.github.jerbanpls.CtRecomp.metainfo.xml
share/licenses/ct-recomp/            LICENSE, LICENSES/, THIRD_PARTY.md
```

The package also bundles what the module build needs at run time:
- **A C compiler:** zig 0.15.2 (MIT), from the official tarball, pinned by SHA-256.
  It runs as `zig cc`, trimmed to its x86_64 Linux libc files by `packaging/trim-zig.sh`.
- **Python 3.11 or later:** the Flatpak uses the runtime's `python3`, which is 3.13 in
  org.freedesktop.Platform 25.08. The AppImage bundles python-build-standalone 3.13.16,
  pinned by SHA-256.

The data dir holds source files from this repository: symbol tables, hooks and the
translator. None of it is derived from a ROM.

## The engine-only build

```
cmake -S . -B build-engine -DCT_ENGINE_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-engine -j
DESTDIR=$PWD/stage cmake --install build-engine
```

`CT_ENGINE_ONLY=ON` does three things:
- It never adds `game/<id>/CMakeLists.txt`, even when `$CT_ROM` is set, so no code is
  generated and neither `ct_game.so` nor the static `ct_sdl` or `ct_boot` is built.
- It builds the engine-only programs `ct_sdl_engine` and `ct_boot_engine`. These now
  live in the top-level CMakeLists.txt, and game builds use them too.
- It fails at configure time if SDL2 is missing, rather than quietly building no
  frontend.

The `install()` rules name their files one by one: `ct_sdl_engine`, the launcher and
the data dir. No rule installs a module, a generated file, or anything else from a
build tree.

## Launching

The `.desktop` file runs `ct-recomp`. The launcher:
1. Puts the bundled Python on `PATH` if there is one. It sets `CC` to the bundled zig
   (`libexec/ct-recomp/zig-cc`) unless `CC` is already set.
2. If the engine supports `--data-dir`, starts it with `--data-dir <prefix>/share/ct-recomp`,
   and the engine's own first-run flow does the rest.
3. Otherwise, starts it with `--module $XDG_DATA_HOME/ct-recomp/ct_game.so`, and with
   `CT_ROM` from the last `ct-recomp --build-module`.

To build the module from the command line:

```
ct-recomp --build-module /path/to/your/rom.sfc
```

This runs `tools/build_module.py --native` from the data dir. The module goes to
`$XDG_DATA_HOME/ct-recomp/` (in the Flatpak, `~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/`).
The launcher also saves the ROM's path there. Nothing is written to the data dir, which
may be read-only.

## Flatpak

`packaging/flatpak/io.github.jerbanpls.CtRecomp.yml` uses org.freedesktop.Platform
25.08. Build it from the repo root:

```
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.jerbanpls.CtRecomp.yml
flatpak run io.github.jerbanpls.CtRecomp --build-module ~/Downloads/rom.sfc
```

The manifest has three modules:
- SDL2 2.32.10, built from the release tarball (pinned by SHA-256).
- zig, installed in `/app/lib/zig`.
- The engine, built with `CT_ENGINE_ONLY=ON` and `CT_SDL2_SYSTEM_LIB=OFF`.

The engine comes from a `git` source pointing at the repository, so only **committed**
files go into the build: never `rom/`, a build tree, or uncommitted changes. Commit
before you build. Its `post-install` step fails the build if `ct_sdl_engine` has a
generated `f_XXXXXX` symbol, or if `/app` contains a module, a ROM image or a generated
table.

Permissions: Wayland with X11 as a fallback, PulseAudio, `--device=all` (controllers and
GPU), and read-only Downloads and Documents for picking the ROM. A ROM stored anywhere
else needs `flatpak override --filesystem=<dir>:ro`.

## AppImage

```
packaging/appimage/build.sh [WORKDIR]      # default WORKDIR: build-appimage/
```

The script:
1. Downloads zig, python-build-standalone, appimagetool 1.9.1 and the type2 runtime
   (release 20251108), and checks each against its pinned SHA-256.
2. Builds the engine with `CT_ENGINE_ONLY=ON` and an `$ORIGIN/../lib` rpath.
3. Installs it into `AppDir/usr`, and adds SDL2, Python and zig.
4. Checks the engine for generated symbols and the AppDir for modules, ROM images and
   generated tables.
5. Packs `CT_Recomp-x86_64.AppImage` with the pinned runtime, which avoids
   appimagetool's own download.

SDL2 comes from one of two places:
- `SDL2=system` (the default) copies the `libSDL2-2.0.so.0` the engine links against.
  When that library is sdl2-compat, as on Fedora 42 and later, it copies the
  `libSDL3.so.0` it loads as well.
- `SDL2=build` builds 2.32.10 from the pinned tarball. This needs the X11, Wayland and
  audio development headers, or SDL ends up without those backends.

The engine and SDL link against the build host's glibc, so build on the oldest
distribution the image should run on.

## The compliance guarantee

No package contains code or data derived from a ROM:
- **Build:** `CT_ENGINE_ONLY` never adds a game directory, so the translator never runs.
- **Install:** only the named engine files and repository sources are installed.
- **Package checks:** the Flatpak's `post-install` and the AppImage script both check
  for generated functions (`nm`, `f_XXXXXX`), modules (`ct_game*`), ROM images and
  generated tables, and fail the build if they find any.
- **Sources:** the Flatpak builds from committed files, and `rom/`, `build*/`, `*.so`
  and `*.AppImage` are git-ignored.

The module the user builds, and the ROM path, stay in the user's data dir and never
enter a package.

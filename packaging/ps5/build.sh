#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 ct-recomp contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# port: PlayStation 5 (docs/PS5.md): cross-compile the PS5 payloads.
#
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk packaging/ps5/build.sh [BUILD_DIR]
#
# Without CT_ROM: the engine only, build/ct_sdl_engine.elf (distributable).
# With CT_ROM=path/to/your.sfc: also build/ct_sdl.elf, the game compiled in
# (for your console only: it holds code made from your ROM), and
# build/ct_game.so, the same game code as a module for ct_sdl_engine.elf.
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK to your ps5-payload-sdk}"
src=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:-$src/build-ps5}
args=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF)
targets=(ct_sdl_engine)
if [ -n "${CT_ROM:-}" ]; then
    targets+=(ct_sdl ct_game_module)
else
    args+=(-DCT_ENGINE_ONLY=ON)
fi
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$src" -B "$out" "${args[@]}"
for t in "${targets[@]}"; do
    cmake --build "$out" -j "$(nproc)" --target "$t"
done
for f in "$out"/ct_sdl_engine.elf "$out"/ct_sdl.elf; do
    [ -f "$f" ] && "$PS5_PAYLOAD_SDK/bin/prospero-strip" -o "${f%.elf}.stripped.elf" "$f"
done
# The homebrew launcher's folder (websrv): copy it to /data/homebrew/ or
# a USB drive's homebrew/ folder, and put rom.sfc in it.
app=$out/homebrew/ct-recomp
rm -rf "$app" && mkdir -p "$app/sce_sys"
if [ -f "$out/ct_sdl.stripped.elf" ]; then
    cp "$out/ct_sdl.stripped.elf" "$app/eboot.elf"
else
    cp "$out/ct_sdl_engine.stripped.elf" "$app/eboot.elf"
    [ -f "$out/ct_game.so" ] && cp "$out/ct_game.so" "$app/"
fi
cp "$src/packaging/ps5/homebrew/homebrew.js" "$app/"
cp "$src/packaging/icons/512x512/ct-recomp.png" "$app/sce_sys/icon0.png"
ls -lR "$out/homebrew"

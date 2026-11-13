#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launcher for the installed engine (docs/PACKAGING.md), as bin/ct-recomp.
#
#   ct-recomp [ENGINE ARGS...]       play
#   ct-recomp --build-module ROM     build the game module from your own ROM,
#                                    without starting the game
#
# The engine and its read-only data dir (share/ct-recomp) come from the
# package. The engine's first-run flow keeps the game module, its stamp,
# the ROM's path and the saves in the user's per-game folder,
# $XDG_DATA_HOME/ct-recomp/<game id>, never in a package. --build-module
# runs that same flow and stops (ct_sdl_engine --build-only), so the
# module, the stamp and the remembered ROM are the ones a normal start
# uses. $CT_RECOMP_GAME picks a game other than the engine's default.
set -eu

bindir=$(dirname "$(readlink -f "$0")")
prefix=$(dirname "$bindir")
engine="$bindir/ct_sdl_engine"
data="${CT_RECOMP_DATA_DIR:-$prefix/share/ct-recomp}"

# A Python 3.11+ for tools/build_module.py: the bundled one (AppImage),
# else python3 on PATH (the Flatpak runtime has one).
if [ -x "$prefix/lib/python/bin/python3" ]; then
    PATH="$prefix/lib/python/bin:$PATH"
    export PATH
fi
# The data dir may be read-only: no bytecode caches there.
PYTHONDONTWRITEBYTECODE=1
export PYTHONDONTWRITEBYTECODE
# A C compiler for the module: $CC, else the bundled zig (as zig cc), else cc.
if [ -z "${CC:-}" ]; then
    if [ -x "$prefix/lib/zig/zig" ]; then
        CT_RECOMP_ZIG="$prefix/lib/zig/zig"
        CC="$prefix/libexec/ct-recomp/zig-cc"
        export CT_RECOMP_ZIG
    else
        CC=cc
    fi
fi
export CC

if [ "${1:-}" = --build-module ]; then
    [ $# -eq 2 ] || { echo "usage: ct-recomp --build-module ROM" >&2; exit 2; }
    set -- --rom "$2" --build-only
fi
if [ -n "${CT_RECOMP_GAME:-}" ]; then
    set -- --game "$CT_RECOMP_GAME" "$@"
fi
exec "$engine" --data-dir "$data" "$@"

#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launcher for the installed engine (docs/PACKAGING.md), as bin/ct-recomp.
#
#   ct-recomp [ENGINE ARGS...]       play
#   ct-recomp --build-module ROM     build the game module from your own ROM
#
# The engine and its read-only data dir (share/ct-recomp) come from the
# package. The game module and the ROM's path live in the user's data dir,
# $XDG_DATA_HOME/ct-recomp, and are never part of any package.
#
# An engine with a first-run flow of its own (one that knows --data-dir)
# gets the data dir and does the rest; an older one gets the module and
# CT_ROM from a previous --build-module.
set -eu

bindir=$(dirname "$(readlink -f "$0")")
prefix=$(dirname "$bindir")
engine="$bindir/ct_sdl_engine"
data="${CT_RECOMP_DATA_DIR:-$prefix/share/ct-recomp}"
user="${XDG_DATA_HOME:-$HOME/.local/share}/ct-recomp"
game="${CT_RECOMP_GAME:-ct}"

# A Python 3.11+ for tools/build_module.py: the bundled one (AppImage),
# else python3 on PATH (the Flatpak runtime has one).
if [ -x "$prefix/lib/python/bin/python3" ]; then
    PATH="$prefix/lib/python/bin:$PATH"
    export PATH
fi
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
    rom=$(readlink -f "$2")
    # A first-run engine keeps the module (and its stamp) in its own
    # per-game folder: let it build there, then stop after one frame.
    if grep -qa -- '--data-dir' "$engine"; then
        exec "$engine" --data-dir "$data" --rom "$rom" --frames 1 --fast
    fi
    mkdir -p "$user"
    # -B: the data dir is read-only, so no bytecode caches there.
    python3 -B "$data/tools/build_module.py" --game "$data/game/$game" --rom "$rom" \
        --out "$user" --native
    printf '%s\n' "$rom" > "$user/rom.path"
    exit 0
fi

if [ -z "${CT_ROM:-}" ] && [ -f "$user/rom.path" ]; then
    CT_ROM=$(cat "$user/rom.path")
    export CT_ROM
fi

# The engine's own first-run flow, once it has one.
if grep -qa -- '--data-dir' "$engine"; then
    exec "$engine" --data-dir "$data" "$@"
fi

case " $* " in
    *" --module "*) ;;
    *) if [ -f "$user/ct_game.so" ]; then set -- --module "$user/ct_game.so" "$@"; fi ;;
esac
if [ -z "${CT_ROM:-}" ]; then
    echo "ct-recomp: no ROM yet: run 'ct-recomp --build-module /path/to/your/rom.sfc'" >&2
    exit 1
fi
exec "$engine" "$@"

#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 ct-recomp contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# port: PlayStation 5 (docs/PS5.md): build the libraries the PS5 programs
# link (libsamplerate, then the ps5-payload-dev SDL2 fork) into the
# ps5-payload-sdk sysroot. Run once per SDK install.
#
#   PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk packaging/ps5/build-deps.sh [WORKDIR]
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK to your ps5-payload-sdk (v0.45 or later)}"
work=${1:-${TMPDIR:-/tmp}/ct-ps5-deps}
sysroot=$PS5_PAYLOAD_SDK/target
mkdir -p "$work"

LSR_TAG=0.2.2
SDL_BRANCH=release-2.30.x-ps5

if [ ! -d "$work/libsamplerate" ]; then
    git clone --depth 1 -b "$LSR_TAG" https://github.com/libsndfile/libsamplerate "$work/libsamplerate"
fi
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$work/libsamplerate" -B "$work/build-lsr" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sysroot" \
    -DBUILD_TESTING=OFF -DLIBSAMPLERATE_EXAMPLES=OFF -DCMAKE_VERBOSE_MAKEFILE=OFF
cmake --build "$work/build-lsr" -j "$(nproc)" --target install

if [ ! -d "$work/SDL" ]; then
    git clone --depth 1 -b "$SDL_BRANCH" https://github.com/ps5-payload-dev/SDL "$work/SDL"
fi
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$work/SDL" -B "$work/build-sdl" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$sysroot" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DCMAKE_VERBOSE_MAKEFILE=OFF
cmake --build "$work/build-sdl" -j "$(nproc)" --target install

echo "ct-recomp PS5 deps installed into $sysroot"

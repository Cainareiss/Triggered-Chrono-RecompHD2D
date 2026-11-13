#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the AppImage inside an old distribution, so it runs on old ones too
# (docs/PACKAGING.md): the engine and SDL link against the build host's glibc,
# and an image built on a current Fedora needs glibc 2.43.
#
#   packaging/appimage/build-container.sh [WORKDIR]
#
# Runs packaging/appimage/build.sh (SDL2=build) in ubuntu:22.04 (glibc 2.35)
# with podman or docker. The checkout is mounted read-only; WORKDIR (default
# build-appimage-ubuntu2204/ in the checkout, git-ignored) holds everything
# that is written. Environment: IMAGE (default docker.io/library/ubuntu:22.04),
# ENGINE (podman or docker), JOBS.
set -eu

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mkdir -p "${1:-$repo/build-appimage-ubuntu2204}" && cd "${1:-$repo/build-appimage-ubuntu2204}" && pwd)
image=${IMAGE:-docker.io/library/ubuntu:22.04}
engine=${ENGINE:-$(command -v podman || command -v docker || true)}
[ -n "$engine" ] || { echo "build-container.sh: need podman or docker" >&2; exit 2; }

# CMake 3.24+ is newer than the distribution's 3.22, so it comes from PyPI.
"$engine" run --rm --security-opt label=disable -v "$repo:/src:ro" -v "$work:/work:rw" \
    -e JOBS="${JOBS:-$(nproc)}" "$image" sh -ec '
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends ca-certificates curl xz-utils \
    build-essential ninja-build pkg-config python3-pip file \
    libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev \
    libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols \
    libdecor-0-dev libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
    libpulse-dev libasound2-dev libudev-dev libpipewire-0.3-dev libdbus-1-dev \
    >/dev/null
pip install -q cmake
cd /src
CMAKE=cmake SDL2=build sh packaging/appimage/build.sh /work
ldd --version | head -1
'
echo "build-container.sh: $work/CT_Recomp-x86_64.AppImage"

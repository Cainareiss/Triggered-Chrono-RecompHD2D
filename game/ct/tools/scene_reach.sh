#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reach scenes headlessly that no replay reaches (docs/SCENE.md, W11-W13),
# with one ct_boot run and a one-byte WRAM poke. A research aid.
#
#   scene_reach.sh m7 ID FRAMES OUTDIR [LOGFROM]
#       Start a new game from reset (world_map_save.txt's first frames) and
#       make the opening's mode 7 scene command ask for scene ID instead of
#       the title screen's (2). The command is event command $FF, its
#       argument byte at $7F2061 while the boot script runs (frame 59, just
#       before EventCmd_Mode7Scene runs at frame 60): the poke XORs it with
#       (2 ^ ID). Writes OUTDIR/sceneID.log (the PPU writes $2105-$2133 from
#       frame LOGFROM, default FRAMES-1; see m7_matrix.py), OUTDIR/sceneID.png
#       (the last frame), OUTDIR/sceneID.wram (WRAM at FRAMES).
#   scene_reach.sh world N OUTDIR [FRAMES]
#       The world map $1F0+N (N 1-7) in place of $1F0: the replay leaves
#       Crono's house for the world map at frame 7252 (a write of $0100 =
#       $1F0), the map descriptor is chosen at frame 7254 (Overworld_SetMapDescPtr,
#       C2274D); the poke at frame 7253 XORs $0100's low byte with N.
#       Writes OUTDIR/worldN.wram, .vram, .png. FRAMES default 7450. N = 0
#       runs the unmodified map.
#
# Needs CT_ROM; CT_BOOT (default: build/ct_boot, or build-mod/ct_boot);
# the outputs are ROM-derived: keep OUTDIR out of git.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
boot=${CT_BOOT:-}
if [ -z "$boot" ]; then
    for d in build build-mod; do
        [ -x "$repo/$d/ct_boot" ] && boot="$repo/$d/ct_boot" && break
    done
fi
[ -n "$boot" ] || { echo "no ct_boot: set CT_BOOT" >&2; exit 2; }
: "${CT_ROM:?set CT_ROM}"
replay="$repo/game/ct/test/replay/world_map_save.txt"
cmd=${1:?usage: scene_reach.sh m7 ID FRAMES OUTDIR | world N OUTDIR}
case "$cmd" in
m7)
    id=${2:?}; frames=${3:?}; out=${4:?}; from=${5:-$((frames - 1))}
    mkdir -p "$out"
    poke=
    x=$((id ^ 2))
    [ "$x" -ne 0 ] && poke="--poke-wram 59:12061^$(printf %x "$x")"
    # shellcheck disable=SC2086
    "$boot" --frames "$frames" --script "$replay" $poke \
        --log-ppu "2105:2133@$from" "$out/scene$id.log" \
        --shot "$frames:$out/scene$id.png" --wram "$out/scene$id.wram" 2>&1 | tail -1
    ;;
world)
    n=${2:?}; out=${3:?}; frames=${4:-7450}
    mkdir -p "$out"
    poke=
    [ "$n" -ne 0 ] && poke="--poke-wram 7253:100^$n"
    # shellcheck disable=SC2086
    "$boot" --frames "$frames" --script "$replay" $poke \
        --wram "$out/world$n.wram" --vram "$out/world$n.vram" 2>&1 | tail -1
    python3 "$here/worldmap_probe.py" "$out/world$n.wram" --vram "$out/world$n.vram" \
        --png "$out/world$n.png" | head -3
    ;;
*) echo "usage: scene_reach.sh m7 ID FRAMES OUTDIR | world N OUTDIR" >&2; exit 2 ;;
esac

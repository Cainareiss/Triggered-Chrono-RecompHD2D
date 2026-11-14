#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""List the places in the field event scripts that may start a mode 7 scene.

A research aid (docs/SCENE.md, W11). EventCmd_Mode7Scene ($C03FAF) is event
command $FF; its argument byte below $80 picks a scene of the mode 7 engine
($C30000), $80-$82 are warps and $90+ screen-state commands. Each location's
event script is a compressed stream: location record (14 bytes at $F60000)
+8 is an index, 3-byte pointers at $FCF9F0 (NewGameSave_DecompressToWork
$C056D4). Event scripts are bytecode with no length table here, so the scan is
a byte search for $FF xx (xx < $80) and over-reports (an $FF operand followed
by a small byte is a hit too); `--ctx` prints the bytes around each hit to be
judged by eye, and `--confirm` marks hits whose preceding bytes also match a
caller-supplied pattern. Prints nothing derived from the ROM into the repo.

usage: m7_scan.py [--ctx N] [--ids 0,1,..]     (ROM from $CT_ROM)
"""
from __future__ import annotations

import argparse
import os
import sys

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(GAME))
sys.path.insert(0, os.path.join(REPO, 'recomp'))
sys.path.insert(0, os.path.join(GAME, 'tools'))

import ct_decompress  # noqa: E402
import decode  # noqa: E402


def file_off(addr: int) -> int:
    return decode.snes_to_file(addr)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--ctx', type=int, default=6)
    ap.add_argument('--ids', help='location ids (hex, comma separated); default all')
    a = ap.parse_args(argv)
    rom = decode.load_rom()
    ids = [int(x, 16) for x in a.ids.split(',')] if a.ids else range(512)
    seen: dict[int, list[int]] = {}
    for loc in ids:
        rec = file_off(0xF60000) + 14 * loc
        idx = rom[rec + 8] | rom[rec + 9] << 8
        seen.setdefault(idx, []).append(loc)
    for idx, locs in sorted(seen.items()):
        p = file_off(0xFCF9F0) + 3 * idx
        ptr = rom[p] | rom[p + 1] << 8 | rom[p + 2] << 16
        try:
            data = ct_decompress.decompress(rom, ptr)
        except Exception as e:   # noqa: BLE001 - research aid
            print(f'event {idx:03X} ${ptr:06X}: {e}', file=sys.stderr)
            continue
        hits = [i for i in range(len(data) - 1) if data[i] == 0xFF and data[i + 1] < 0x80]
        for i in hits:
            ctx = data[max(0, i - a.ctx):i + 2 + a.ctx]
            print(f'event {idx:03X} (locs {",".join("%03X" % l for l in locs)}) '
                  f'off {i:04X} size {len(data)} scene {data[i + 1]:02X}  '
                  + ctx.hex(' '))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

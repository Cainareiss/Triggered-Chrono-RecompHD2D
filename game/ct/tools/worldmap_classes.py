#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Terrain classes of a world map against what its tiles look like.

A research aid (docs/SCENE.md, W12) for the 3D world map's height guess
(game/ct/world_scene.c, relief[]). Reads ct_boot dumps of any of the eight
world maps ($1F0-$1F7) as worldmap_probe.py does (WRAM, and VRAM + CGRAM with
--vram) and prints, for the map:

  - the cell count per terrain nibble (0 walkable, 1 blocked, 2 water or
    high ground, 4 cover; W3) and how many ground (BG2) and overlay (BG1)
    metatile indices the map uses;
  - for each ground metatile that has a blocked (1) or water (2) nibble, its
    mean colour and a crude "looks like": water (blue), forest (green),
    ice/snow (light), rock/dirt (the rest), the cells it covers, and whether a
    BG1 overlay stands on it;
  - the share of class-2 cells that look like water, i.e. how reliable "2 =
    sea" is on this map; the rest is high ground (mountain tops) or a map
    where the sea is not class 2 at all.

The colours are sampled from the metatile's 16 x 16 pixels (4 tilemap words,
palette and flips as drawn), so no ROM data is shipped: the output is for
reading, not for the repo.

usage: worldmap_classes.py WRAM --vram FILE [--list N]
"""
from __future__ import annotations

import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scene_probe as sp  # noqa: E402
import worldmap_probe as wp  # noqa: E402


def metatile_rgb(w, vram, pal, chr_word, defb, idx):
    """Mean RGB of the opaque pixels of a metatile; None if all transparent."""
    tot = [0, 0, 0]
    n = 0
    for q in range(4):
        e = wp.u16(w, defb + idx * 8 + q * 2)
        for r in range(8):
            row = sp.tile_px(vram, chr_word, e & 0x3FF, 7 - r if e & 0x8000 else r)
            for c in row:
                if c:
                    p = pal[((e >> 10) & 7) * 16 + c]
                    tot[0] += p[0]
                    tot[1] += p[1]
                    tot[2] += p[2]
                    n += 1
    if not n:
        return None
    return tuple(t / n for t in tot)


def looks(rgb):
    if rgb is None:
        return 'empty'
    r, g, b = rgb
    if b > r + 12 and b >= g - 4:
        return 'water'
    if g > r + 8 and g > b + 8:
        return 'forest'
    if min(rgb) > 150:
        return 'ice'
    return 'rock'


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('wram')
    ap.add_argument('--vram', required=True)
    ap.add_argument('--list', type=int, default=12, help='metatiles to list per class')
    a = ap.parse_args(argv)
    w = open(a.wram, 'rb').read()
    v = open(a.vram, 'rb').read()
    vram, cg = v[:0x10000], v[0x10000:0x10200]
    if not wp.is_world_map(w):
        print('not a world map dump')
        return 1
    pal = [sp.bgr555(cg[2 * i] | cg[2 * i + 1] << 8) for i in range(256)]
    loc = wp.u16(w, wp.LOC) & 0x1FF
    nib = collections.Counter()
    cover = collections.defaultdict(collections.Counter)   # ground idx -> nibble counts
    over = collections.defaultdict(int)                    # ground idx -> cells with BG1 on top
    used_bg, used_fg = set(), set()
    for my in range(wp.MAP_H):
        for mx in range(wp.MAP_W):
            bg = w[wp.MAP_BG + my * wp.MAP_W + mx]
            fg = w[wp.MAP_FG + my * wp.MAP_W + mx]
            used_bg.add(bg)
            if fg:
                used_fg.add(fg)
                over[bg] += 1
            for q in range(4):
                n = wp.terrain(w, 2 * mx + (q & 1), 2 * my + (q >> 1))
                nib[n] += 1
                cover[bg][n] += 1
    print(f'map ${loc:03X}: {sum(nib.values())} cells; nibbles '
          + ', '.join(f'{k}:{nib[k]}' for k in sorted(nib)))
    print(f'  ground metatiles used {len(used_bg)}, overlay metatiles used {len(used_fg)}, '
          f'overlay cells {sum(over.values())}')
    chr_word = 0x2000
    kinds = collections.defaultdict(lambda: collections.Counter())
    rows = collections.defaultdict(list)
    for idx in sorted(used_bg):
        cnt = cover[idx]
        rgb = metatile_rgb(w, vram, pal, chr_word, wp.DEFS_BG, idx)
        lk = looks(rgb)
        for n, c in cnt.items():
            kinds[n][lk] += c
        top = max(cnt, key=cnt.get)
        if top in (1, 2) or cnt.get(1) or cnt.get(2):
            rows[top].append((sum(cnt.values()) * -1, idx, lk, rgb, dict(cnt), over.get(idx, 0)))
    for n in sorted(kinds):
        tot = sum(kinds[n].values())
        print(f'  nibble {n}: {tot} cells by what the ground tile looks like: '
              + ', '.join(f'{k} {100 * c // tot}%' for k, c in kinds[n].most_common()))
    for n in (1, 2):
        rs = sorted(rows.get(n, []))[:a.list]
        print(f'  ground metatiles whose commonest nibble is {n} (cells, index, looks, mean rgb, '
              f'nibble mix, overlay cells):')
        for c, idx, lk, rgb, cnt, ov in rs:
            print(f'    {-c:5d}  #{idx:3d}  {lk:6s} '
                  + ('rgb %3d,%3d,%3d' % tuple(rgb) if rgb else 'rgb -') + f'  {cnt}  ov {ov}')
    # What stands on the ground: overlay metatile -> colour, for the biggest few.
    ovc = collections.Counter()
    for my in range(wp.MAP_H):
        for mx in range(wp.MAP_W):
            f = w[wp.MAP_FG + my * wp.MAP_W + mx]
            if f:
                ovc[f] += 1
    print('  most used overlay metatiles (index: cells, looks):')
    for idx, c in ovc.most_common(a.list):
        print(f'    #{idx:3d}: {c:4d}  {looks(metatile_rgb(w, vram, pal, chr_word, wp.DEFS_FG, idx))}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print the field "scene" the game holds in WRAM, from ct_boot dumps.

Reads a 128 KB WRAM dump (ct_boot --wram, or one --snap-at file) and
prints what docs/SCENE.md describes: location, layer sizes, camera, the
leader/follower slots and the object table. With --vram (ct_boot --vram:
VRAM, CGRAM, OAM) and --png, also renders BG1+BG2 of the WHOLE map from
WRAM tilemaps + VRAM tiles, to show the map is resident beyond the
screen, and with --oam lists the OAM entries grouped by owning object.

The PNG is made from the running game's data: keep it in the build dir,
never commit it.

usage: scene_probe.py WRAM [--vram FILE] [--png OUT] [--oam] [--chr WORD]
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib

# Field WRAM addresses (docs/SCENE.md, A0). Object arrays are indexed by
# 2*slot; 64 slots.
LOC = 0x0100            # u16 location id (field DP is $0100)
LEADER, FOL1, FOL2 = 0x0197, 0x0199, 0x019B   # 2*slot, $80 = none
RING = (0x01AB, 0x01AC, 0x01AD)               # follow ring indices
OBJ_COUNT = 0x12000     # $7F2000: objects in this location's script
L1_W, L1_H, L2_W, L2_H, L3_W, L3_H = 0x0BCB, 0x0BCD, 0x0BCF, 0x0BD1, 0x0BD3, 0x0BD5
L3_ON = 0x0BCA
CAM_TX, CAM_TY = 0x1D0A, 0x1D0E   # camera left/top, 8 px units
BG1HOFS, BG1VOFS = 0x1D87, 0x1D89  # u16 scroll register values
MAP = 0x3000            # rows of 256 bytes: L1 +$00, L2 +$40, L3 +$80, hi bits +$C0
PROPS = 0x7000          # rows of 256 bytes: A +$00, B +$40, exits +$80, zones +$C0
META12 = 0xB000         # 512 x 8 bytes: BG1/BG2 metatiles (4 tilemap words)
TYPE_NAMES = {0: 'pc-party1', 1: 'pc-party2', 2: 'pc-party3', 3: 'pc-actor',
              4: 'npc', 7: 'none'}


def u16(w, a):
    return w[a] | w[a + 1] << 8


def scene(w):
    s = {'location': u16(w, LOC) & 0x1FF}
    s['layers'] = [(w[L1_W], w[L1_H]), (w[L2_W], w[L2_H]),
                   (w[L3_W], w[L3_H]) if w[L3_ON] & 1 else None]
    s['camera_px'] = (w[CAM_TX] * 8 + (u16(w, BG1HOFS) & 7),
                      w[CAM_TY] * 8 + (u16(w, BG1VOFS) & 7))
    s['party_slots'] = [w[LEADER], w[FOL1], w[FOL2]]
    s['ring'] = [w[a] for a in RING]
    objs = []
    for i in range(min(w[OBJ_COUNT], 64)):
        o = 2 * i
        typ, oid = w[0x1100 + o], w[0x1101 + o]
        objs.append({
            'slot': i, 'type': typ, 'id': oid,
            'x16': u16(w, 0x1800 + o), 'y16': u16(w, 0x1880 + o),  # 1/16 px
            'screen': (u16(w, 0x0A00 + o), u16(w, 0x0A80 + o)),
            'active': bool(w[0x0F00 + o] & 0x80), 'frame': w[0x0F01 + o],
            'gfx': (w[0x1200 + o] << 16) | u16(w, 0x1280 + o),
            'anim': 0xE40000 | u16(w, 0x1500 + o),
            'facing': w[0x1600 + o],
            'prio': (w[0x0C00 + o], w[0x0C01 + o]),
            'oam_tile': u16(w, 0x0D00 + o),
        })
    s['objects'] = objs
    return s


def print_scene(s):
    print(f"location ${s['location']:03X}")
    for n, l in zip(('L1', 'L2', 'L3'), s['layers']):
        print(f'  {n}: ' + (f'{l[0]}x{l[1]} metatiles (16 px)' if l else 'off'))
    print(f"camera (map px) {s['camera_px']}")
    lead, f1, f2 = s['party_slots']
    show = lambda v: '-' if v & 0x80 else str(v // 2)
    print(f'leader slot {show(lead)}, follower1 {show(f1)}, follower2 {show(f2)}; '
          f"ring head/f1/f2 {s['ring']}")
    for o in s['objects']:
        if o['type'] & 0x80:
            continue
        print(f"  obj {o['slot']:2d} {TYPE_NAMES.get(o['type'], o['type']):12s} id {o['id']:3d}"
              f" pos {o['x16'] / 16:7.1f},{o['y16'] / 16:7.1f} px"
              f" {'ON ' if o['active'] else 'off'} frame {o['frame']:3d}"
              f" facing {o['facing']:02X} gfx ${o['gfx']:06X} anim ${o['anim']:06X}"
              f" prio {o['prio'][0]:02X}/{o['prio'][1]:02X} oamtile {o['oam_tile']:03X}")


def bgr555(c):
    return ((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31)


def tile_px(vram, chr_word, tnum, row):
    """One row of a 4bpp 8x8 tile: 8 color indices."""
    a = (chr_word * 2 + tnum * 32) & 0xFFFF
    b0, b1 = vram[(a + row * 2) & 0xFFFF], vram[(a + row * 2 + 1) & 0xFFFF]
    b2, b3 = vram[(a + 16 + row * 2) & 0xFFFF], vram[(a + 17 + row * 2) & 0xFFFF]
    return [((b0 >> (7 - x)) & 1) | ((b1 >> (7 - x)) & 1) << 1 |
            ((b2 >> (7 - x)) & 1) << 2 | ((b3 >> (7 - x)) & 1) << 3 for x in range(8)]


def render(w, vram, cgram, chr_word, out):
    """BG2 then BG1 of the whole map (64x64 metatiles max) to a PNG."""
    wt, ht = max(w[L1_W], w[L2_W]), max(w[L1_H], w[L2_H])
    W, H = wt * 16, ht * 16
    pal = [bgr555(cgram[2 * i] | cgram[2 * i + 1] << 8) for i in range(128)]
    img = bytearray(W * H * 3)
    for layer, col in ((1, 0x40), (0, 0x00)):        # BG2 first, BG1 over it
        lw, lh = (w[L2_W], w[L2_H]) if layer else (w[L1_W], w[L1_H])
        for ty in range(lh):
            for tx in range(lw):
                off = MAP + ty * 256 + tx
                idx = w[off + col] | ((w[off + 0xC0] >> layer) & 1) << 8
                for q in range(4):
                    e = u16(w, META12 + idx * 8 + q * 2)
                    tnum, p = e & 0x3FF, (e >> 10) & 7
                    hf, vf = e & 0x4000, e & 0x8000
                    for r in range(8):
                        row = tile_px(vram, chr_word, tnum, 7 - r if vf else r)
                        if hf:
                            row.reverse()
                        py = ty * 16 + (q >> 1) * 8 + r
                        for x, c in enumerate(row):
                            if c:
                                px = tx * 16 + (q & 1) * 8 + x
                                img[(py * W + px) * 3:(py * W + px) * 3 + 3] = bytes(pal[p * 16 + c])
    raw = b''.join(b'\0' + bytes(img[y * W * 3:(y + 1) * W * 3]) for y in range(H))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    with open(out, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
    print(f'wrote {out} ({W}x{H})')


def print_oam(s, oam):
    """OAM entries grouped by object: an object's pieces use tiles from its
    VRAM slot ($0D00,X: 9-bit OAM tile number of the slot)."""
    owners = {}
    for o in s['objects']:
        if o['active'] and not o['type'] & 0x80:
            owners.setdefault(o['oam_tile'] & 0x1F8, []).append(o['slot'])
    for i in range(128):
        x, y, t, a = oam[4 * i:4 * i + 4]
        hi = (oam[512 + i // 4] >> (2 * (i % 4))) & 3
        if y >= 0xE0:
            continue
        tile9 = t | (a & 1) << 8
        print(f'  oam {i:3d} x {x | (hi & 1) << 8:3d} y {y:3d} tile {tile9:03X} pal {(a >> 1) & 7}'
              f' prio {(a >> 4) & 3} {"16x16" if hi & 2 else "8x8"} -> obj {owners.get(tile9 & 0x1F8, "?")}')


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('wram')
    ap.add_argument('--vram')
    ap.add_argument('--png')
    ap.add_argument('--oam', action='store_true')
    ap.add_argument('--chr', default='1000', help='BG1/BG2 character base, VRAM word (hex; $210B)')
    a = ap.parse_args(argv)
    w = open(a.wram, 'rb').read()
    s = scene(w)
    print_scene(s)
    if a.vram:
        v = open(a.vram, 'rb').read()
        vram, cgram, oam = v[:0x10000], v[0x10000:0x10200], v[0x10200:0x10420]
        if a.oam:
            print_oam(s, oam)
        if a.png:
            render(w, vram, cgram, int(a.chr, 16), a.png)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

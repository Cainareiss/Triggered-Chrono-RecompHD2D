#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cut an edited pose sheet back into an asset pack's pose pictures
(docs/ASSET-PACKS.md, "Sprite sheets").

    pack_import_sheet.py SHEET.png --pack PACK [--index SHEET.json]
                         [--changed-from ORIGINAL.png] [--game ID]

SHEET.png is a sheet ct_sdl --pack-export-sheets wrote (sheet_NAME_1x.png,
sheet_NAME_4x.png, ...), edited in any painting program, at any whole
multiple of the index's size: the factor is worked out from its width.
The index (sheet_NAME.json beside it, unless --index says) gives each
pose's rectangle; everything outside the rectangles (the grid, the
foot-point ticks, the gutters) is ignored.

For each pose, the rectangle is written to PACK/poses/KEY.png at the
sheet's factor, with a [[pose]] entry in PACK/pack.toml (made if needed;
an entry for the same key is replaced). An empty (fully transparent)
rectangle is skipped, unless the index says the pose is the mirror image
of another one that was drawn: then that picture is used, flipped
(flip = "h"), so one facing is enough. With --changed-from (the sheet as
exported, at the same size), rectangles left as they were are skipped,
so only your edits become replacements.

PACK is a pack directory, or a pack NAME under the per-user data folder
(as for tools/pack_upscale.py).

Pictures cut from an exported sheet are the game's own art, even when
edited: such a pack is for your personal use only. Never share it.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pack_upscale import png_read, png_write, resolve_pack  # noqa: E402

HEADER = ('# Asset pack with poses cut from sprite sheets by tools/pack_import_sheet.py.\n'
          '# Pictures made from the game\'s own art are for your personal use only:\n'
          '# never share or distribute them. Format: docs/ASSET-PACKS.md.\n'
          '[pack]\nversion = 1\nname = "{name}"\n')


def cut(w, rgba, x, y, cw, ch):
    out = bytearray()
    for r in range(y, y + ch):
        out += rgba[(r * w + x) * 4:(r * w + x + cw) * 4]
    return out


def empty(img):
    return not any(img[3::4])


def flip_h(img, w, h):
    out = bytearray(len(img))
    for y in range(h):
        for x in range(w):
            o, i = (y * w + x) * 4, (y * w + w - 1 - x) * 4
            out[o:o + 4] = img[i:i + 4]
    return out


def blocks(text):
    """pack.toml as (table name, text) blocks, the first being what comes
    before any [[...]] entry."""
    out, cur, name = [], [], None
    for line in text.splitlines(True):
        s = line.strip()
        if s.startswith('[['):
            out.append((name, ''.join(cur)))
            cur, name = [], s.strip('[]').strip()
        cur.append(line)
    out.append((name, ''.join(cur)))
    return out


def block_key(text):
    m = re.search(r'^\s*key\s*=\s*"([0-9A-Fa-f]+)"', text, re.M)
    return m.group(1).lower() if m else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('sheet')
    ap.add_argument('--pack', required=True)
    ap.add_argument('--index')
    ap.add_argument('--changed-from')
    ap.add_argument('--game')
    a = ap.parse_args()

    index = a.index
    if not index:
        base = re.sub(r'_\d+x\.png$', '', a.sheet)
        if base == a.sheet:
            sys.exit('pack_import_sheet: give --index (the sheet\'s name is not sheet_NAME_Nx.png)')
        index = base + '.json'
    ix = json.load(open(index, encoding='utf-8'))
    w, h, rgba = png_read(a.sheet)
    W, H = ix['size']
    if w % W or h % H or w // W != h // H:
        sys.exit(f'pack_import_sheet: {a.sheet} is {w}x{h}, not a whole multiple of the '
                 f'index\'s {W}x{H}')
    s = w // W
    orig = None
    if a.changed_from:
        ow, oh, orgba = png_read(a.changed_from)
        if (ow, oh) != (w, h):
            sys.exit(f'pack_import_sheet: {a.changed_from} is {ow}x{oh}, the sheet {w}x{h}')
        orig = orgba

    pack = resolve_pack(a.pack, a.game)
    os.makedirs(os.path.join(pack, 'poses'), exist_ok=True)
    drawn, entries, skipped = {}, {}, 0
    for p in ix['poses']:
        x, y, pw, ph = p['x'] * s, p['y'] * s, p['w'] * s, p['h'] * s
        img = cut(w, rgba, x, y, pw, ph)
        if empty(img) or (orig is not None and cut(w, orig, x, y, pw, ph) == img):
            skipped += 1
            continue
        key = p['key'].lower()
        f = f'poses/{key}.png'
        png_write(os.path.join(pack, f), pw, ph, img)
        drawn[key] = f
        entries[key] = (f, p['w'], p['h'], '')
    for p in ix['poses']:   # mirror images of drawn poses, left empty: the drawn one, flipped
        key, m = p['key'].lower(), p.get('mirror_of', '').lower()
        if key not in entries and m in drawn:
            entries[key] = (drawn[m], p['w'], p['h'], 'h')
            skipped -= 1

    toml = os.path.join(pack, 'pack.toml')
    old = open(toml, encoding='utf-8').read() if os.path.exists(toml) else \
        HEADER.format(name=os.path.basename(os.path.normpath(pack)))
    kept = [t for name, t in blocks(old) if not (name == 'pose' and block_key(t) in entries)]
    out = ''.join(kept)
    if not out.endswith('\n'):
        out += '\n'
    for key, (f, pw, ph, flip) in entries.items():
        out += f'\n[[pose]]\nkey = "{key}"\nfile = "{f}"\nw = {pw}\nh = {ph}\n'
        if flip:
            out += f'flip = "{flip}"\n'
        out += f'sheet = "{ix.get("name", "")}"\n'
    open(toml, 'w', encoding='utf-8').write(out)
    print(f'pack_import_sheet: {len(entries)} poses into {pack} at {s}x '
          f'({sum(1 for e in entries.values() if e[3])} mirrored), {skipped} left out')
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed button-prompt pictures (PNG) in a C header as coverage masks.

usage: embed_glyphs.py OUT.h SIZE FILE.png...

Each picture is scaled to SIZE x SIZE with an area filter and stored as
one byte per pixel: alpha times luminance, so a light button with a
see-through label (the prompt style) becomes a bright shape with the label
cut out. The header defines `glyph_data[]`, entries {name, pixels} sorted
by name (the file name without .png), and GLYPH_DATA_PX = SIZE.

Only the Python standard library: PNG colour types 0, 2, 3, 4 and 6,
bit depths 1-8 (and 16 for grey / truecolour), not interlaced.
"""
import os
import struct
import sys
import zlib


def read_png(path: str) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, plte, trns = 8, b'', None, None
    w = h = depth = ctype = interlace = 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            plte = [tuple(body[k:k + 3]) for k in range(0, len(body), 3)]
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
    if interlace:
        raise ValueError(f'{path}: interlaced PNGs are not supported')
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)
    stride = (w * chans * depth + 7) // 8
    raw = zlib.decompress(idat)
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        ft = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif ft == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pr) & 255
        rows.append(bytes(line))
        prev = line
    px = []
    for row in rows:
        if depth < 8:
            vals = []
            for byte in row:
                for s in range(8 - depth, -1, -depth):
                    vals.append((byte >> s) & ((1 << depth) - 1))
            vals = vals[:w * chans]
        elif depth == 16:
            vals = [row[k] for k in range(0, len(row), 2)]   # the high byte
        else:
            vals = list(row)
        for x in range(w):
            v = vals[x * chans:(x + 1) * chans]
            if ctype == 3:
                r, g, b = plte[v[0]]
                a = trns[v[0]] if trns and v[0] < len(trns) else 255
            elif ctype == 0:
                g = v[0] * 255 // ((1 << depth) - 1) if depth < 8 else v[0]
                r = b = g
                a = 255
            elif ctype == 4:
                r = g = b = v[0]
                a = v[1]
            elif ctype == 2:
                r, g, b = v
                a = 255
            else:
                r, g, b, a = v
            px.append((r, g, b, a))
    return w, h, px


def coverage(w: int, h: int, px: list, size: int) -> bytes:
    """Area-filtered SIZE x SIZE mask of alpha * luminance."""
    src = [a * (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0 for r, g, b, a in px]
    out = bytearray()
    sx, sy = w / size, h / size
    for oy in range(size):
        y0, y1 = oy * sy, (oy + 1) * sy
        for ox in range(size):
            x0, x1 = ox * sx, (ox + 1) * sx
            acc = area = 0.0
            for y in range(int(y0), min(h, int(y1 + 0.999999))):
                wy = min(y1, y + 1) - max(y0, y)
                if wy <= 0:
                    continue
                for x in range(int(x0), min(w, int(x1 + 0.999999))):
                    wx = min(x1, x + 1) - max(x0, x)
                    if wx <= 0:
                        continue
                    acc += src[y * w + x] * wx * wy
                    area += wx * wy
            out.append(max(0, min(255, round(acc / area)) if area else 0))
    return bytes(out)


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    out, size, files = argv[1], int(argv[2]), argv[3:]
    entries = []
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        w, h, px = read_png(path)
        entries.append((name, coverage(w, h, px, size)))
    entries.sort()
    lines = ['/* Generated by tools/embed_glyphs.py: do not edit. Pictures: Kenney Input',
             ' * Prompts (CC0-1.0), third_party/glyphs/. */',
             '#pragma once', '', '#include <stdint.h>', '',
             f'#define GLYPH_DATA_PX {size}', f'#define GLYPH_DATA_COUNT {len(entries)}', '',
             'static const struct {', '    const char *name;',
             f'    uint8_t px[{size * size}];', '} glyph_data[] = {']
    for name, mask in entries:
        lines.append(f'    {{"{name}", {{')
        for k in range(0, len(mask), 24):
            lines.append('        ' + ','.join(str(v) for v in mask[k:k + 24]) + ',')
        lines.append('    }},')
    lines.append('};')
    tmp = out + '.tmp'
    with open(tmp, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(tmp, out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

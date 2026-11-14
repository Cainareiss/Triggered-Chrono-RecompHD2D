#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Upscale an asset pack's pictures (docs/ASSET-PACKS.md).

    pack_upscale.py PACK --scale 4 [--out PACK2 | --in-place]
    pack_upscale.py PACK --scale 4 --out PACK2 --external 'realesrgan -i {in} -o {out} -s {scale}'

PACK is a pack directory, or a pack NAME under the per-user data folder
(<XDG_DATA_HOME or ~/.local/share>/ct-recomp/<game>/packs/NAME; --game
picks the game when there are several).

The built-in scaler is edge-directed pixel-art scaling in the manner of
xBRZ, written for this project from the published idea, not from any
implementation: for every 2x2 block of source pixels it compares the
color differences along the two diagonals over the surrounding 4x4
pixels; where one diagonal is clearly the more continuous, the corner
between the four is cut along it. The cut is a 45 degree line, or a
shallow or steep one when the neighbors continue the edge that way,
and its coverage of each output pixel is supersampled, so edges come
out smooth while flat areas and the palette stay as they were. Alpha is
a channel like the others (fully transparent pixels count as far from
every opaque color), so outlines stay crisp and holes stay holes.

--external runs your own upscaler (for example an AI one you installed)
once per picture; {in}, {out} and {scale} in the command are replaced by
the input PNG, the output PNG to write and the factor. If what it writes
has lost the transparency, the alpha channel is taken from the built-in
scaler's result instead.

The pack's pack.toml is copied with `scale` set; entry sizes (w, h) stay
the original console sizes, which is what the renderer maps a picture
onto, whatever its resolution.
"""
import argparse
import multiprocessing
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

# ---- PNG ----


def png_read(path):
    """-> (w, h, bytearray RGBA). 8-bit (and 1/2/4-bit palette or gray),
    non-interlaced."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, plte, trns = 8, [], b'', b''
    w = h = depth = ctype = None
    while pos + 8 <= len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
            if inter:
                raise ValueError(f'{path}: interlaced PNG')
        elif typ == b'PLTE':
            plte = body
        elif typ == b'tRNS':
            trns = body
        elif typ == b'IDAT':
            idat.append(body)
        elif typ == b'IEND':
            break
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    if depth == 16:
        raise ValueError(f'{path}: 16-bit PNGs are not supported')
    bits = ch * depth
    stride = (w * bits + 7) // 8
    fb = max(1, bits // 8)
    raw = zlib.decompress(b''.join(idat))
    out = bytearray(stride * h)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - fb] if x >= fb else 0
            b = prev[x]
            c = prev[x - fb] if x >= fb else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    rgba = bytearray(w * h * 4)
    for y in range(h):
        row = out[y * stride:(y + 1) * stride]
        for x in range(w):
            o = (y * w + x) * 4
            if depth < 8:
                per = 8 // depth
                v = (row[x // per] >> ((per - 1 - x % per) * depth)) & ((1 << depth) - 1)
                if ctype == 3:
                    rgba[o:o + 3] = plte[v * 3:v * 3 + 3]
                    rgba[o + 3] = trns[v] if v < len(trns) else 255
                else:
                    g = v * 255 // ((1 << depth) - 1)
                    rgba[o:o + 4] = bytes((g, g, g, 255))
                continue
            s = row[x * ch:(x + 1) * ch]
            if ctype == 6:
                rgba[o:o + 4] = s
            elif ctype == 2:
                rgba[o:o + 4] = bytes((s[0], s[1], s[2], 255))
            elif ctype == 3:
                rgba[o:o + 3] = plte[s[0] * 3:s[0] * 3 + 3]
                rgba[o + 3] = trns[s[0]] if s[0] < len(trns) else 255
            elif ctype == 4:
                rgba[o:o + 4] = bytes((s[0], s[0], s[0], s[1]))
            else:
                rgba[o:o + 4] = bytes((s[0], s[0], s[0], 255))
    return w, h, rgba


def png_write(path, w, h, rgba):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgba[y * w * 4:(y + 1) * w * 4]

    def chunk(typ, body):
        return struct.pack('>I', len(body)) + typ + body + struct.pack('>I', zlib.crc32(typ + body))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        f.write(chunk(b'IEND', b''))


# ---- the scaler ----

def _dist(a, b):
    """Perceptual difference of two RGBA pixels (tuples): YCbCr with luma
    weighted most; transparent is far from everything opaque."""
    if a[3] < 128 or b[3] < 128:
        return 0.0 if (a[3] < 128 and b[3] < 128) else 255.0
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    y = 0.299 * dr + 0.587 * dg + 0.114 * db
    cb = 0.5 * (db - y) / 0.886
    cr = 0.5 * (dr - y) / 0.701
    return (2.0 * y * y + cb * cb + cr * cr) ** 0.5


EQ = 30.0          # colors closer than this count as the same
DOMINANT = 3.6     # a diagonal this much more continuous gets a bolder cut
STEEP = 2.2        # neighbor test for shallow / steep lines

# Corner blends, per corner of a pixel: 0 none, 1 normal, 2 dominant.


def upscale(w, h, rgba, s):
    """Edge-directed pixel-art upscale by integer factor s (2-6)."""
    px = [tuple(rgba[k * 4:k * 4 + 4]) for k in range(w * h)]

    def P(x, y):
        x = 0 if x < 0 else w - 1 if x >= w else x
        y = 0 if y < 0 else h - 1 if y >= h else y
        return px[y * w + x]

    dcache = {}

    def d(a, b):
        if a == b:
            return 0.0
        k = (a, b) if a < b else (b, a)
        v = dcache.get(k)
        if v is None:
            v = dcache[k] = _dist(a, b)
        return v

    # Blend at the corner shared by (x, y), (x+1, y), (x, y+1), (x+1, y+1):
    # for each 2x2 block, which of its pixels gets its inner corner cut.
    # corner[(x, y)] = [tl, tr, bl, br] blend types for pixel (x, y).
    corner = [[0, 0, 0, 0] for _ in range(w * h)]
    for y in range(-1, h):
        for x in range(-1, w):
            # 4x4 around the block: rows y-1 .. y+2, columns x-1 .. x+2
            b_, c_ = P(x, y - 1), P(x + 1, y - 1)
            e_, f_, g_, h_ = P(x - 1, y), P(x, y), P(x + 1, y), P(x + 2, y)
            i_, j_, k_, l_ = P(x - 1, y + 1), P(x, y + 1), P(x + 1, y + 1), P(x + 2, y + 1)
            n_, o_ = P(x, y + 2), P(x + 1, y + 2)
            if (f_ == g_ and j_ == k_) or (f_ == j_ and g_ == k_):
                continue   # flat or a straight edge: nothing to cut
            # Continuity along each diagonal over the neighborhood.
            jg = d(i_, f_) + d(f_, c_) + d(n_, k_) + d(k_, h_) + 4 * d(j_, g_)
            fk = d(e_, j_) + d(j_, o_) + d(b_, g_) + d(g_, l_) + 4 * d(f_, k_)
            if jg < fk:      # j-g is the line: cut f's and k's corners
                t = 2 if DOMINANT * jg < fk else 1
                if 0 <= x < w and 0 <= y < h:
                    corner[y * w + x][3] = max(corner[y * w + x][3], t)
                if 0 <= x + 1 < w and 0 <= y + 1 < h:
                    corner[(y + 1) * w + x + 1][0] = max(corner[(y + 1) * w + x + 1][0], t)
            elif fk < jg:    # f-k is the line: cut g's and j's corners
                t = 2 if DOMINANT * fk < jg else 1
                if 0 <= x + 1 < w and 0 <= y < h:
                    corner[y * w + x + 1][2] = max(corner[y * w + x + 1][2], t)
                if 0 <= x < w and 0 <= y + 1 < h:
                    corner[(y + 1) * w + x][1] = max(corner[(y + 1) * w + x][1], t)

    W, H = w * s, h * s
    out = bytearray(W * H * 4)
    SS = 4   # supersamples per axis per output pixel
    # Corner frames: (dx, dy) to the two edge neighbors and the diagonal,
    # and a mapping from local coordinates (u, v in 0..1, origin at the
    # pixel's top-left) to the corner's frame, where the corner is (1, 1).
    frames = [
        ((-1, 0), (0, -1), lambda u, v: (1 - u, 1 - v)),   # top-left
        ((1, 0), (0, -1), lambda u, v: (u, 1 - v)),        # top-right
        ((-1, 0), (0, 1), lambda u, v: (1 - u, v)),        # bottom-left
        ((1, 0), (0, 1), lambda u, v: (u, v)),             # bottom-right
    ]
    for y in range(h):
        for x in range(w):
            e = P(x, y)
            cuts = []
            for ci, t in enumerate(corner[y * w + x]):
                if not t:
                    continue
                (hx, hy), (vx, vy), tf = frames[ci]
                fcol, hcol = P(x + hx, y + hy), P(x + vx, y + vy)   # side, below/above
                col = fcol if d(e, fcol) <= d(e, hcol) else hcol
                # Line shape: does the edge continue along a row (shallow)
                # or a column (steep)? Look one further out.
                far_h = P(x + 2 * hx + vx, y + 2 * hy + vy)
                far_v = P(x + hx + 2 * vx, y + hy + 2 * vy)
                shallow = d(hcol, far_h) < EQ and d(fcol, far_h) > EQ and t == 2
                steep = d(fcol, far_v) < EQ and d(hcol, far_v) > EQ and t == 2
                if shallow and steep:
                    shallow = steep = False
                cuts.append((tf, col, shallow, steep, t))
            for oy in range(s):
                for ox in range(s):
                    o = ((y * s + oy) * W + x * s + ox) * 4
                    if not cuts:
                        out[o:o + 4] = bytes(e)
                        continue
                    acc = [0.0, 0.0, 0.0, 0.0]
                    for sy in range(SS):
                        for sx in range(SS):
                            u = (ox + (sx + 0.5) / SS) / s
                            v = (oy + (sy + 0.5) / SS) / s
                            c = e
                            for tf, col, shallow, steep, t in cuts:
                                a, b = tf(u, v)
                                # The region past the line near the corner (1, 1).
                                # Dominant: a line through the pixel
                                # (half of it at 45 degrees); normal: the
                                # corner only, rounded off.
                                if shallow:
                                    inside = a * 0.5 + b > 1.0
                                elif steep:
                                    inside = a + b * 0.5 > 1.0
                                else:
                                    inside = a + b > (1.5 if t == 1 else 1.0)
                                if inside:
                                    c = col
                            al = c[3] / 255.0
                            acc[0] += c[0] * al
                            acc[1] += c[1] * al
                            acc[2] += c[2] * al
                            acc[3] += al
                    n = SS * SS
                    a = acc[3] / n
                    if a > 0:
                        out[o:o + 4] = bytes((min(255, round(acc[0] / acc[3])),
                                              min(255, round(acc[1] / acc[3])),
                                              min(255, round(acc[2] / acc[3])),
                                              min(255, round(a * 255))))
                    else:
                        out[o:o + 4] = b'\0\0\0\0'
    return W, H, out


# ---- packs ----

def resolve_pack(arg, game):
    if os.sep in arg or '/' in arg or os.path.isdir(arg):
        return os.path.abspath(arg)
    base = os.environ.get('XDG_DATA_HOME') or os.path.join(os.path.expanduser('~'), '.local', 'share')
    root = os.path.join(base, 'ct-recomp')
    if not game:
        games = [g for g in sorted(os.listdir(root)) if os.path.isdir(os.path.join(root, g, 'packs'))] \
            if os.path.isdir(root) else []
        if len(games) != 1:
            sys.exit(f'pack_upscale: say which game (--game): packs found for {games or "none"}')
        game = games[0]
    return os.path.join(root, game, 'packs', arg)


def manifest_files(path):
    """The picture files named in pack.toml, and its [pack] scale."""
    files, scale, table = [], 1, None
    for line in open(path, encoding='utf-8'):
        s = line.strip()
        if s.startswith('['):
            table = s.strip('[]')
            continue
        if '=' not in s or s.startswith('#'):
            continue
        k, v = (p.strip() for p in s.split('=', 1))
        if k == 'file' and table in ('tile', 'object'):
            files.append(v.strip('"'))
        elif k == 'scale' and table == 'pack':
            scale = int(float(v))
    return files, scale


def set_scale(text, scale):
    out, table = [], None
    for line in text.splitlines(True):
        s = line.strip()
        if s.startswith('['):
            table = s.strip('[]')
        if table == 'pack' and s.split('=')[0].strip() == 'scale':
            line = f'scale = {scale}\n'
        out.append(line)
    return ''.join(out)


def job(args):
    src, dst, scale, external = args
    try:
        w, h, rgba = png_read(src)
        if external:
            W, H, ours = upscale(w, h, rgba, scale)
            with tempfile.TemporaryDirectory() as td:
                tin, tout = os.path.join(td, 'in.png'), os.path.join(td, 'out.png')
                png_write(tin, w, h, rgba)
                cmd = [a.replace('{in}', tin).replace('{out}', tout).replace('{scale}', str(scale))
                       for a in shlex.split(external)]
                r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                if r.returncode or not os.path.exists(tout):
                    return f'{src}: external upscaler failed ({r.returncode}): ' \
                           f'{r.stderr.decode(errors="replace")[-300:]}'
                ew, eh, ext = png_read(tout)
            if (ew, eh) != (W, H):
                return f'{src}: the external upscaler made {ew}x{eh}, not {W}x{H}'
            had_alpha = any(rgba[k] < 255 for k in range(3, len(rgba), 4))
            lost = had_alpha and all(ext[k] == 255 for k in range(3, len(ext), 4))
            if lost:   # keep our alpha
                for k in range(3, len(ext), 4):
                    ext[k] = ours[k]
            png_write(dst, W, H, ext)
        else:
            W, H, out = upscale(w, h, rgba, scale)
            png_write(dst, W, H, out)
    except Exception as ex:   # report and go on with the rest
        return f'{src}: {ex}'
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('pack', help='pack directory or NAME')
    ap.add_argument('--scale', type=int, default=4, choices=range(2, 7), metavar='2-6')
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--out', help='write a new pack (directory or NAME)')
    g.add_argument('--in-place', action='store_true', help='replace the pictures in PACK')
    ap.add_argument('--external', metavar='CMD',
                    help="your own upscaler, run per picture: '{in}', '{out}', '{scale}' are replaced")
    ap.add_argument('--game', help='game id, for pack NAMEs (default: the one with packs)')
    ap.add_argument('--jobs', type=int, default=0, help='processes (default: CPU count)')
    ap.add_argument('--force', action='store_true', help='upscale a pack whose scale is not 1')
    a = ap.parse_args()

    src = resolve_pack(a.pack, a.game)
    man = os.path.join(src, 'pack.toml')
    if not os.path.exists(man):
        sys.exit(f'pack_upscale: no {man}')
    files, old_scale = manifest_files(man)
    if old_scale != 1 and not a.force:
        sys.exit(f'pack_upscale: {src} is already at scale {old_scale} (--force to go on)')
    dst = src if a.in_place else resolve_pack(a.out, a.game)
    if not a.in_place:
        if os.path.abspath(dst) == os.path.abspath(src):
            sys.exit('pack_upscale: --out is the pack itself (use --in-place)')
        os.makedirs(dst, exist_ok=True)
    jobs = []
    for f in files:
        sp, dp = os.path.join(src, f), os.path.join(dst, f)
        if not os.path.exists(sp):
            continue
        os.makedirs(os.path.dirname(dp), exist_ok=True)
        jobs.append((sp, dp, a.scale, a.external))
    n = a.jobs or os.cpu_count() or 1
    errors = 0
    with multiprocessing.Pool(n) as pool:
        for k, err in enumerate(pool.imap_unordered(job, jobs, chunksize=8)):
            if err:
                errors += 1
                print(f'pack_upscale: {err}', file=sys.stderr)
            if (k + 1) % 500 == 0:
                print(f'pack_upscale: {k + 1}/{len(jobs)}', file=sys.stderr)
    text = set_scale(open(man, encoding='utf-8').read(), a.scale * old_scale)
    if not a.in_place:
        name = os.path.basename(os.path.normpath(dst))
        text = text.replace('\nname = ', f'\nupscaled_from = "{os.path.basename(src)}"\nname = ', 1)
        lines = text.split('\n')
        text = '\n'.join(f'name = "{name}"' if l.startswith('name = ') else l for l in lines)
    with open(os.path.join(dst, 'pack.toml'), 'w', encoding='utf-8') as f:
        f.write(text)
    print(f'pack_upscale: {len(jobs) - errors} pictures at {a.scale}x into {dst}'
          + (f', {errors} failed' if errors else ''))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())

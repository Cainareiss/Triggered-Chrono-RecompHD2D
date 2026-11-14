#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original test art for asset packs (docs/ASSET-PACKS.md): simple shapes
made here from code, never from a game, so tests and examples can show
every pack feature without any game's pictures.

    pack_test_art.py house DIR      a house (walls textured with planks
                                    and a door, a red roof): DIR/house.gltf
                                    (buffer embedded) and DIR/house_walls.png
    pack_test_art.py cube DIR       a 16 px cube, one color per face (vertex
                                    colors) times a smiley texture, as one
                                    binary file: DIR/cube.glb
    pack_test_art.py smiley PNG W H a yellow smiley face on transparent,
                                    W x H px (any size: a pose replacement)
    pack_test_art.py sheet INDEX OUT [--scale N]
                                    a blank copy of a pose sheet's layout
                                    (tools/pack_import_sheet.py's input)
                                    with a smiley in every cell

Model units are map pixels: x east, y up, z south (toward the camera);
the origin is the middle of the footprint, on the ground.
"""
import base64
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pack_upscale import png_write  # noqa: E402


# ---- pictures ----

def smiley(w, h):
    """A smiley filling a w x h picture (RGBA bytes): yellow disc with a
    dark outline, two eyes and a smile; transparent around it."""
    img = bytearray(w * h * 4)
    cx, cy, r = w / 2, h / 2, min(w, h) / 2 - 0.5
    for y in range(h):
        for x in range(w):
            px, py = (x + 0.5 - cx) / r, (y + 0.5 - cy) / r
            d = math.hypot(px, py)
            if d > 1:
                continue
            col = (255, 214, 40)
            if d > 0.88:
                col = (60, 30, 10)
            for ex in (-0.35, 0.35):   # eyes
                if math.hypot(px - ex, py + 0.3) < 0.13:
                    col = (60, 30, 10)
            if 0.45 < math.hypot(px, py + 0.05) < 0.6 and py > 0.12:   # the smile
                col = (60, 30, 10)
            img[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((*col, 255))
    return img


def planks(w, h):
    """Wall texture: horizontal planks with dark seams, and a door."""
    img = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            band = y // 8
            c = (170, 120, 70) if band % 2 else (186, 136, 82)
            if y % 8 == 0 or (x + band * 11) % 32 == 0:
                c = (90, 60, 35)
            if w * 0.4 <= x < w * 0.6 and y >= h * 0.62:   # the door
                c = (70, 40, 25) if (x - int(w * 0.42)) % 6 else (50, 28, 18)
            if w * 0.1 <= x < w * 0.28 and h * 0.55 <= y < h * 0.75:   # a window
                c = (120, 190, 230) if (x % 6 and y % 6) else (240, 240, 230)
            img[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((*c, 255))
    return img


def png_bytes(w, h, rgba):
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'a.png')
        png_write(p, w, h, rgba)
        return open(p, 'rb').read()


# ---- glTF ----

class Mesh:
    """Flat-shaded triangles, per material: pos, normal, uv, color."""

    def __init__(self):
        self.parts = {}

    def quad(self, mat, a, b, c, d, uv=((0, 0), (1, 0), (1, 1), (0, 1)), col=(1, 1, 1, 1)):
        """Corners a b c d counter-clockwise seen from the front."""
        self.poly(mat, [a, b, c, d], uv, col)

    def poly(self, mat, pts, uv, col=(1, 1, 1, 1)):
        e1 = [pts[1][k] - pts[0][k] for k in range(3)]
        e2 = [pts[2][k] - pts[0][k] for k in range(3)]
        n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        ln = math.sqrt(sum(v * v for v in n)) or 1
        n = [v / ln for v in n]
        p = self.parts.setdefault(mat, {'v': [], 'i': []})
        base = len(p['v'])
        for k, q in enumerate(pts):
            p['v'].append((q, n, uv[k], col))
        for k in range(1, len(pts) - 1):
            p['i'] += [base, base + k, base + k + 1]


def gltf(mesh, materials, images, glb=False):
    """materials: name -> dict(color, image index or None); images: list of
    PNG bytes or file names. Returns the .gltf JSON text, or the .glb bytes."""
    bin_ = bytearray()
    views, accessors, prims = [], [], []

    def view(data, target=None):
        while len(bin_) % 4:
            bin_.append(0)
        v = {'buffer': 0, 'byteOffset': len(bin_), 'byteLength': len(data)}
        if target:
            v['target'] = target
        bin_.extend(data)
        views.append(v)
        return len(views) - 1

    def acc(data, ctype, count, typ, lo=None, hi=None, target=34962):
        a = {'bufferView': view(data, target), 'componentType': ctype, 'count': count, 'type': typ}
        if lo is not None:
            a['min'], a['max'] = lo, hi
        accessors.append(a)
        return len(accessors) - 1

    mats = list(materials)
    for mi, name in enumerate(mats):
        if name not in mesh.parts:
            continue
        p = mesh.parts[name]
        vs = p['v']
        pos = b''.join(struct.pack('<3f', *v[0]) for v in vs)
        lo = [min(v[0][k] for v in vs) for k in range(3)]
        hi = [max(v[0][k] for v in vs) for k in range(3)]
        attrs = {
            'POSITION': acc(pos, 5126, len(vs), 'VEC3', lo, hi),
            'NORMAL': acc(b''.join(struct.pack('<3f', *v[1]) for v in vs), 5126, len(vs), 'VEC3'),
            'TEXCOORD_0': acc(b''.join(struct.pack('<2f', *v[2]) for v in vs), 5126, len(vs), 'VEC2'),
            'COLOR_0': acc(b''.join(struct.pack('<4f', *v[3]) for v in vs), 5126, len(vs), 'VEC4'),
        }
        idx = acc(b''.join(struct.pack('<H', i) for i in p['i']), 5123, len(p['i']), 'SCALAR',
                  target=34963)
        prims.append({'attributes': attrs, 'indices': idx, 'material': mi})
    doc = {
        'asset': {'version': '2.0', 'generator': 'ct-recomp tools/pack_test_art.py'},
        'scene': 0, 'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': 'model'}],
        'meshes': [{'primitives': prims}],
        'materials': [], 'textures': [], 'images': [],
        'samplers': [{'magFilter': 9728, 'minFilter': 9728}],
        'accessors': accessors, 'bufferViews': views,
    }
    for name in mats:
        m = materials[name]
        pbr = {'baseColorFactor': list(m['color']), 'metallicFactor': 0, 'roughnessFactor': 1}
        if m.get('image') is not None:
            pbr['baseColorTexture'] = {'index': m['image']}
        doc['materials'].append({'name': name, 'pbrMetallicRoughness': pbr})
    for k, im in enumerate(images):
        doc['textures'].append({'source': k, 'sampler': 0})
        if isinstance(im, str):
            doc['images'].append({'uri': im})
        else:
            doc['images'].append({'bufferView': view(bytes(im)), 'mimeType': 'image/png'})
    if not doc['images']:
        for k in ('textures', 'images'):
            del doc[k]
    if glb:
        doc['buffers'] = [{'byteLength': len(bin_)}]
        js = json.dumps(doc).encode()
        js += b' ' * (-len(js) % 4)
        while len(bin_) % 4:
            bin_.append(0)
        body = struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(bin_), 0x004E4942) + bin_
        return struct.pack('<III', 0x46546C67, 2, 12 + len(body)) + body
    doc['buffers'] = [{'byteLength': len(bin_),
                       'uri': 'data:application/octet-stream;base64,' + base64.b64encode(bin_).decode()}]
    return json.dumps(doc, indent=1)


def house(out_dir):
    """A 40 x 32 px house, 40 px to the ridge, which runs north-south: its
    front (south, toward the camera) is a gable wall with a door."""
    m = Mesh()
    x0, x1, z0, z1, top, ridge, o = -20, 20, -16, 16, 24, 40, 4
    g = top / ridge   # the eaves' height on the texture
    # the front and back: a wall and its gable, one pentagon each
    m.poly('walls', [(x0, 0, z1), (x1, 0, z1), (x1, top, z1), (0, ridge, z1), (x0, top, z1)],
           ((0, 1), (1, 1), (1, 1 - g), (0.5, 0), (0, 1 - g)))
    m.poly('walls', [(x1, 0, z0), (x0, 0, z0), (x0, top, z0), (0, ridge, z0), (x1, top, z0)],
           ((0, 1), (1, 1), (1, 1 - g), (0.5, 0), (0, 1 - g)))
    # the sides (no door: the texture's left part)
    m.quad('walls', (x1, 0, z1), (x1, 0, z0), (x1, top, z0), (x1, top, z1),
           ((0, 1), (0.35, 1), (0.35, 1 - g), (0, 1 - g)))
    m.quad('walls', (x0, 0, z0), (x0, 0, z1), (x0, top, z1), (x0, top, z0),
           ((0, 1), (0.35, 1), (0.35, 1 - g), (0, 1 - g)))
    # the roof: two slopes, east and west, overhanging the walls
    lo = top - o * (ridge - top) / x1
    m.quad('roof', (x1 + o, lo, z1 + o), (x1 + o, lo, z0 - o), (0, ridge, z0 - o), (0, ridge, z1 + o))
    m.quad('roof', (x0 - o, lo, z0 - o), (x0 - o, lo, z1 + o), (0, ridge, z1 + o), (0, ridge, z0 - o))
    os.makedirs(out_dir, exist_ok=True)
    png_write(os.path.join(out_dir, 'house_walls.png'), 64, 64, planks(64, 64))
    text = gltf(m, {'walls': {'color': (1, 1, 1, 1), 'image': 0},
                    'roof': {'color': (0.75, 0.2, 0.16, 1), 'image': None}}, ['house_walls.png'])
    open(os.path.join(out_dir, 'house.gltf'), 'w').write(text)


def cube(out_dir):
    """A 16 px cube standing on the ground, a color per face, a smiley on each."""
    m = Mesh()
    s = 8
    cols = [(1, 0.3, 0.3, 1), (0.3, 1, 0.3, 1), (0.4, 0.5, 1, 1), (1, 1, 0.3, 1), (0.3, 1, 1, 1),
            (1, 0.4, 1, 1)]
    uv = ((0, 1), (1, 1), (1, 0), (0, 0))
    f = [((-s, 0, s), (s, 0, s), (s, 2 * s, s), (-s, 2 * s, s)),       # front
         ((s, 0, -s), (-s, 0, -s), (-s, 2 * s, -s), (s, 2 * s, -s)),   # back
         ((s, 0, s), (s, 0, -s), (s, 2 * s, -s), (s, 2 * s, s)),       # east
         ((-s, 0, -s), (-s, 0, s), (-s, 2 * s, s), (-s, 2 * s, -s)),   # west
         ((-s, 2 * s, s), (s, 2 * s, s), (s, 2 * s, -s), (-s, 2 * s, -s)),   # top
         ((-s, 0, -s), (s, 0, -s), (s, 0, s), (-s, 0, s))]             # bottom
    for k, q in enumerate(f):
        m.quad('face', *q, uv=uv, col=cols[k])
    os.makedirs(out_dir, exist_ok=True)
    face = bytearray(smiley(16, 16))
    for k in range(0, len(face), 4):   # white behind the face: the vertex color shows
        if not face[k + 3]:
            face[k:k + 4] = b'\xff\xff\xff\xff'
    data = gltf(m, {'face': {'color': (1, 1, 1, 1), 'image': 0}}, [png_bytes(16, 16, face)], glb=True)
    open(os.path.join(out_dir, 'cube.glb'), 'wb').write(data)


def sheet(index, out, scale):
    """A sheet with index's layout: grid lines in the gutters, a smiley
    in each pose's rectangle (what an artist's edit would hold)."""
    ix = json.load(open(index))
    W, H = ix['size']
    w, h = W * scale, H * scale
    img = bytearray(w * h * 4)
    for p in ix['poses']:
        pw, ph = p['w'] * scale, p['h'] * scale
        face = smiley(pw, ph)
        for y in range(ph):
            o = ((p['y'] * scale + y) * w + p['x'] * scale) * 4
            img[o:o + pw * 4] = face[y * pw * 4:(y + 1) * pw * 4]
    png_write(out, w, h, img)


def main(argv):
    if len(argv) >= 3 and argv[1] == 'house':
        house(argv[2])
    elif len(argv) >= 3 and argv[1] == 'cube':
        cube(argv[2])
    elif len(argv) >= 5 and argv[1] == 'smiley':
        w, h = int(argv[3]), int(argv[4])
        png_write(argv[2], w, h, smiley(w, h))
    elif len(argv) >= 4 and argv[1] == 'sheet':
        scale = int(argv[argv.index('--scale') + 1]) if '--scale' in argv else 1
        sheet(argv[2], argv[3], scale)
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Asset packs with original art in the game (docs/ASSET-PACKS.md): models,
whole poses and the sheet round trip, through the HD-2D renderer.

usage: test_pack_art.py FRONTEND MODULE SCRIPT [--keep DIR]

FRONTEND is ct_sdl_engine (run with --module MODULE) or ct_sdl (MODULE
"-"); SCRIPT the Leene Square replay. All in a scratch data folder, so
nothing touches the user's:

1. models: a pack made here by tools/pack_test_art.py (a house, a second
   one over a stall with hide_cells, a cube, none from the game) on the
   fair's map ($005), and a house on the world map ($1F0), the 3D look at
   frames 7600 (the world map) and 8700 with --pack-export-sheets:
   the roofs' red shows where the houses stand, and the sheets come out
   with an index for the leader (sheet pc00).
2. poses: a smiley drawn into every cell of that sheet's layout at 4x
   (pack_test_art.py sheet: no pixel of the game's) and cut into a pack
   with tools/pack_import_sheet.py; at frame 8650 the leader is that
   smiley as one picture, in the 2D look and as the 3D billboard.

Writes the screenshots it checks into DIR with --keep. Skips (exit 0)
where SDL's offscreen driver has no OpenGL.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from pack_upscale import png_read  # noqa: E402

FAILS = []

PACK_TOML = '''# Original test shapes (tools/pack_test_art.py): no art from any game.
[pack]
version = 1
name = "art"

[[model]]
map = 0x005
cells = [27, 33, 29, 35]
file = "models/house.gltf"

[[model]]
map = 0x005
cells = [28, 36, 31, 38]
file = "models/house.gltf"
scale = [1.4, 1.0, 1.0]
hide_cells = true

[[model]]
map = 0x005
cell = [19, 35]
file = "models/cube.glb"
scale = 1.5
rotate = [0, 30, 0]

[[model]]
map = 0x1F0
cells = [47, 34, 52, 38]
file = "models/house.gltf"
scale = 0.6
rotate = [0, -20, 0]
'''


def check(cond, what):
    print(('ok   ' if cond else 'FAIL ') + what)
    if not cond:
        FAILS.append(what)


def run(fe, module, script, env, frames, args):
    cmd = [fe] + (['--module', module] if module != '-' else []) + [
        '--script', script, '--frames', str(frames), '--fast', '--no-save', '--mute', '--hd2d',
        '--shot-size', '1280x720'] + args
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=900)
    return r.returncode, r.stdout + r.stderr


def count(path, box, test):
    w, h, px = png_read(path)
    x0, y0, x1, y1 = box
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            o = (y * w + x) * 4
            n += test(px[o], px[o + 1], px[o + 2])
    return n


def red(r, g, b):
    return r > 120 and g < 70 and b < 60 and r > 2 * g


def yellow(r, g, b):
    return r > 170 and g > 140 and b < 90 and r - b > 120


def main(argv):
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    fe, module, script = argv[1:4]
    keep = argv[argv.index('--keep') + 1] if '--keep' in argv else None
    with tempfile.TemporaryDirectory(dir=os.getcwd()) as d:
        data = os.path.join(d, 'data')
        packs = os.path.join(data, 'ct-recomp', 'ct', 'packs')
        art = os.path.join(packs, 'art')
        os.makedirs(os.path.join(art, 'models'))
        tool = os.path.join(ROOT, 'tools', 'pack_test_art.py')
        for what in ('house', 'cube'):
            subprocess.run([sys.executable, tool, what, os.path.join(art, 'models')], check=True)
        open(os.path.join(art, 'pack.toml'), 'w').write(PACK_TOML)
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
                   XDG_DATA_HOME=data, XDG_CONFIG_HOME=data, CT_HD2D_GLERR='1')
        shots = os.path.join(d, 'shots')
        os.makedirs(shots)
        sheets = os.path.join(d, 'sheets')

        # 1. models, and the export
        a = os.path.join(shots, 'models_3d_8700.png')
        wm = os.path.join(shots, 'models_world_7600.png')
        rc, log = run(fe, module, script, env, 8702, ['--preset', 'hd2d3d', '--pack', 'art',
                                                      '--pack-export-sheets', sheets,
                                                      '--shot', f'7600:{wm}', '--shot', f'8700:{a}'])
        if 'no usable OpenGL' in log or 'cannot create an OpenGL' in log:
            print('test_pack_art: no OpenGL here: skipped')
            return 0
        check(rc == 0 and os.path.exists(a), f'models run (exit {rc})')
        if rc or not os.path.exists(a):
            print(log[-2000:])
            return 1
        check('GL error' not in log, 'no GL errors (CT_HD2D_GLERR)')
        check('4 models' in log, 'four [[model]] entries loaded')
        n = count(wm, (440, 260, 620, 400), red)
        check(n > 1500, f'the world map: its house stands where placed ({n} red pixels)')
        # the roofs: the house at cells 27-29 x 33-35, the wide one over the stall
        n1 = count(a, (700, 290, 880, 400), red)
        n2 = count(a, (760, 400, 1000, 480), red)
        check(n1 > 4000 and n2 > 4000, f'the two roofs stand where placed ({n1}, {n2} red pixels)')
        index = os.path.join(sheets, 'sheet_pc00.json')
        check(os.path.exists(index) and os.path.exists(os.path.join(sheets, 'sheet_pc00_4x.png')),
              'the leader\'s sheet and index')
        if not os.path.exists(index):
            return 1
        ix = json.load(open(index))
        check(len(ix['poses']) > 10 and all(len(p['key']) == 8 for p in ix['poses']),
              f'{len(ix["poses"])} poses in the index')
        check(os.path.exists(os.path.join(sheets, 'maps', 'map_005.txt')), 'the map\'s cells listed')

        # 2. poses: a smiley sheet, cut into a pack
        sm = os.path.join(d, 'smiley_4x.png')
        subprocess.run([sys.executable, tool, 'sheet', index, sm, '--scale', '4'], check=True)
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'pack_import_sheet.py'), sm,
                            '--index', index, '--pack', os.path.join(packs, 'poses')],
                           capture_output=True, text=True)
        check(r.returncode == 0 and 'poses into' in r.stdout, 'pack_import_sheet: ' + r.stdout.strip())
        toml = open(os.path.join(packs, 'poses', 'pack.toml')).read()
        check(toml.count('[[pose]]') == len(ix['poses']), 'one [[pose]] per pose')
        results = {}
        for look in ('hd2d', 'hd2d3d'):
            p = os.path.join(shots, f'pose_{look}_8650.png')
            rc, log = run(fe, module, script, env, 8652, ['--preset', look, '--pack', 'poses',
                                                          '--pack', 'art', '--shot', f'8650:{p}'])
            check(rc == 0 and os.path.exists(p), f'poses run, {look} (exit {rc})')
            results[look] = p
        # the leader stands by the fountain, in the middle of the picture
        if os.path.exists(results.get('hd2d', '')):
            n = count(results['hd2d'], (620, 300, 780, 420), yellow)
            check(n > 3500, f'2D: the leader is the smiley ({n} yellow pixels)')
        if os.path.exists(results.get('hd2d3d', '')):
            n = count(results['hd2d3d'], (600, 260, 780, 420), yellow)
            check(n > 2800, f'3D: the leader\'s billboard is the smiley ({n} yellow pixels)')
        if keep:
            os.makedirs(keep, exist_ok=True)
            for f in os.listdir(shots):
                shutil.copy(os.path.join(shots, f), keep)
    print(f'test_pack_art: {len(FAILS)} failed')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

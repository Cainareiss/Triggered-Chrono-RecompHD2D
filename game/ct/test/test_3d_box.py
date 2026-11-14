# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The 3D diorama keeps the text box (docs/QA-3D.md, Q2).

usage: test_3d_box.py FRONTEND MODULE SCRIPT

At frame 4300 of leene_square.txt Mom's first text box is open at the
bottom of the screen. Its window is drawn on BG1 in the console's tilemap
only, so the 3D scene (the map as the game keeps it) doesn't hold it;
hd2d.c's s3d_box_mask lays those tiles flat over the 3D picture. The box's
middle must look as it does in the plain look (`off`): its mean color
within a few levels, and a grey-blue, not the dark room. Shots stay in a
temporary directory; nothing ROM-derived is written elsewhere.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from pack_upscale import png_read  # noqa: E402

FRAME = 4300
SIZE = (1038, 556)   # the user's Ally window: a 4:3 picture, 741 px wide, with side fill
# the box's middle, below its text line: picture rows 360-470, the middle of its width
BOX = (330, 360, 710, 470)


def shot(fe, module, script, env, d, preset):
    path = os.path.join(d, preset + '.png')
    r = subprocess.run([fe, '--module', module, '--script', script, '--frames', str(FRAME + 1),
                        '--fast', '--no-save', '--mute', '--hd2d', '--preset', preset,
                        '--shot-size', '%dx%d' % SIZE, '--shot', '%d:%s' % (FRAME, path)],
                       capture_output=True, text=True, env=env, timeout=900)
    log = r.stdout + r.stderr
    if 'no usable OpenGL' in log or 'cannot create an OpenGL' in log:
        return None, log
    return (path if r.returncode == 0 and os.path.exists(path) else ''), log


def mean(path):
    w, h, px = png_read(path)
    x0, y0, x1, y1 = BOX
    s = [0, 0, 0]
    for y in range(y0, y1):
        for x in range(x0, x1):
            o = (y * w + x) * 4
            for c in range(3):
                s[c] += px[o + c]
    n = (x1 - x0) * (y1 - y0)
    return [v / n for v in s]


def main(argv):
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    fe, module, script = argv[1:4]
    with tempfile.TemporaryDirectory(dir=os.getcwd()) as d:
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
                   SDL_JOYSTICK_HIDAPI='0', XDG_DATA_HOME=os.path.join(d, 'data'),
                   XDG_CONFIG_HOME=os.path.join(d, 'config'))
        means = {}
        for preset in ('off', 'hd2d3d'):
            path, log = shot(fe, module, script, env, d, preset)
            if path is None:
                print('test_3d_box: no OpenGL here: skipped')
                return 0
            if not path:
                print(log[-2000:])
                print('FAIL %s run' % preset)
                return 1
            means[preset] = mean(path)
            print('%-7s box mean rgb %s' % (preset, ' '.join('%.1f' % v for v in means[preset])))
    a, b = means['off'], means['hd2d3d']
    bad = 0
    if max(abs(x - y) for x, y in zip(a, b)) > 12:
        print('FAIL the 3D box differs from the plain one')
        bad = 1
    if not (b[2] > 40 and b[2] >= b[0]):   # the window's grey-blue, not the dark brown room
        print('FAIL no box in the 3D picture')
        bad = 1
    print('ok   text box in 3D' if not bad else 'test_3d_box: failed')
    return bad


if __name__ == '__main__':
    sys.exit(main(sys.argv))

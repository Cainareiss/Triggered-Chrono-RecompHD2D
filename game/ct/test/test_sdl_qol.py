#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""The SDL frontend's quality-of-life features (frontend/qol.h), headless
(SDL dummy drivers), with scripted actions (--qol-action F:NAME):

- states: a quick save from ct_sdl runs on in ct_boot exactly as ct_boot's
  own run of the same replay; slots and thumbnails; a quick load.
- resume: SIGTERM saves the resume point; the next start asks, and yes
  loads it; no keeps it aside as auto-previous; a (simulated) sleep saves
  it too.
- screenshots: the plain picture at the window's size, in
  $XDG_PICTURES_DIR/ct-recomp; the CRT look changes it.
- pause on a controller going, and going on.
- fast-forward: frames go by faster than real time.
- battery: on a (fake) battery the saver mode says so.
- --import-srm / --export-srm: the size check, the backup, the copy.

usage: test_sdl_qol.py CT_SDL CT_BOOT SCRIPT
"""
import os
import signal
import struct
import subprocess
import sys
import tempfile
import time

FAILS = []


def check(cond: bool, what: str, log: str = '') -> None:
    if not cond:
        FAILS.append(what)
        print(f'FAIL: {what}')
        if log:
            print('  ' + '\n  '.join(log.splitlines()[-25:]))


def sdl(exe: str, args: list[str], env: dict, timeout: float = 120) -> tuple[int, str]:
    r = subprocess.run([exe] + args, capture_output=True, text=True, env=env, timeout=timeout)
    return r.returncode, r.stdout + r.stderr


def png_size(path: str) -> tuple[int, int]:
    with open(path, 'rb') as f:
        head = f.read(24)
    return struct.unpack('>II', head[16:24])


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    ct_sdl, ct_boot, script = argv[1:]
    with tempfile.TemporaryDirectory() as d:
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   XDG_DATA_HOME=os.path.join(d, 'data'), XDG_CONFIG_HOME=os.path.join(d, 'cfg'),
                   XDG_PICTURES_DIR=os.path.join(d, 'pics'), HOME=d)
        st = os.path.join(d, 'states')
        base = ['--no-save', '--states-dir', st]

        # -- states --
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '700', '--script', script,
                                      '--qol-action', '600:quicksave', '--qol-action', '650:save2'],
                      env)
        quick = os.path.join(st, 'quick.state')
        check(rc == 0 and os.path.exists(quick) and os.path.exists(os.path.join(st, 'quick.png'))
              and os.path.exists(os.path.join(st, 'slot2.state')), 'quick save and slot 2', log)
        saved = [l for l in log.splitlines() if 'state saved to' in l and 'quick' in l]
        if os.path.exists(quick) and saved:
            f0 = int(saved[0].split('(frame ')[1].rstrip(')'))
            plain, loaded = os.path.join(d, 'plain'), os.path.join(d, 'loaded')
            subprocess.run([ct_boot, '--script', script, '--frames', '900', '--hash-log', plain],
                           capture_output=True, check=True)
            subprocess.run([ct_boot, '--script', script, '--frames', '900', '--load-state', quick,
                            '--hash-log', loaded], capture_output=True, check=True)
            with open(plain) as a, open(loaded) as b:
                tail = [x for x in a if int(x.split()[0]) > f0]
                got = b.readlines()
            check(tail == got and len(got) == 900 - f0,
                  f'ct_sdl state at frame {f0} runs on as ct_boot ({len(got)} frames)')
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '700', '--qol-action',
                                      '660:quickload'], env)
        check(rc == 0 and 'Quick slot: loaded' in log, 'quick load', log)

        # -- resume --
        p = subprocess.Popen([ct_sdl] + base, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
        time.sleep(4)
        p.send_signal(signal.SIGTERM)
        out = p.communicate(timeout=60)[0]
        auto = os.path.join(st, 'auto.state')
        check(p.returncode == 0 and 'resume point saved (quit)' in out and os.path.exists(auto),
              'SIGTERM saves the resume point', out)
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '400', '--qol-action', '0:resume-yes'],
                      env)
        check(rc == 0 and 'paused (a resume point is there)' in log and
              'Resume point: loaded' in log, 'resume: yes loads it', log)
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '400', '--qol-action', '0:resume-no'],
                      env)
        check(rc == 0 and 'Starting over' in log and
              os.path.exists(os.path.join(st, 'auto-previous.state')) and not os.path.exists(auto),
              'resume: no keeps it aside', log)
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '200', '--no-resume',
                                      '--qol-action', '120:sleep'], env)
        check(rc == 0 and 'resume point saved (sleep)' in log and os.path.exists(auto),
              'sleep saves the resume point', log)

        # -- screenshots, CRT --
        pics = os.path.join(d, 'pics', 'ct-recomp')
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '1600', '--no-resume',
                                      '--qol-action', '1500:screenshot'], env)
        shots = sorted(os.listdir(pics)) if os.path.isdir(pics) else []
        check(rc == 0 and len(shots) == 1 and 'Screenshot: ct-recomp-' in log, 'screenshot', log)
        if shots:
            check(png_size(os.path.join(pics, shots[0])) == (768, 672),
                  f'screenshot at the window size: {png_size(os.path.join(pics, shots[0]))}')
            rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '1600', '--no-resume',
                                          '--qol-set', 'crt=on', '--qol-action', '1500:screenshot'],
                          env)
            new = [x for x in sorted(os.listdir(pics)) if x not in shots]
            if new:
                with open(os.path.join(pics, shots[0]), 'rb') as a, \
                        open(os.path.join(pics, new[0]), 'rb') as b:
                    check(a.read() != b.read(), 'the CRT look changes the picture')
            else:
                check(False, 'CRT screenshot', log)

        # -- pause --
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '200', '--no-resume',
                                      '--qol-action', '100:unpad', '--qol-action', '101:pause'],
                      env)
        check(rc == 0 and 'paused (a controller went)' in log and 'playing' in log,
              'pause on a controller going', log)

        # -- fast-forward: 480 frames take 8 s in real time; x4 about 2 --
        t = time.monotonic()
        rc, log = sdl(ct_sdl, base + ['--frames', '480', '--no-resume', '--qol-set', 'ffwd_speed=4',
                                      '--qol-action', '1:ffwd'], env)
        took = time.monotonic() - t
        check(rc == 0 and 'Fast-forward x4' in log and took < 6.0,
              f'fast-forward x4: 480 frames in {took:.1f} s', log)

        # -- battery --
        ps = os.path.join(d, 'ps', 'BAT0')
        os.makedirs(ps)
        for name, text in (('type', 'Battery'), ('status', 'Discharging')):
            with open(os.path.join(ps, name), 'w') as f:
                f.write(text + '\n')
        rc, log = sdl(ct_sdl, base + ['--fast', '--frames', '100', '--no-resume', '--qol-set',
                                      'battery_mode=saver'],
                      dict(env, CT_POWER_SUPPLY_DIR=os.path.dirname(ps)))
        check(rc == 0 and 'On battery: lighter look, 30 Hz picture' in log, 'battery saver', log)

        # -- save RAM import / export --
        srm = os.path.join(d, 'game.srm')
        good, bad, out = (os.path.join(d, x) for x in ('good.srm', 'bad.srm', 'out.srm'))
        with open(srm, 'wb') as f:
            f.write(bytes([1]) * 8192)
        with open(good, 'wb') as f:
            f.write(bytes(range(256)) * 32)
        with open(bad, 'wb') as f:
            f.write(bytes(8000))
        rc, log = sdl(ct_sdl, ['--save', srm, '--import-srm', bad], env)
        with open(srm, 'rb') as f:
            check(rc == 1 and f.read() == bytes([1]) * 8192, 'a wrong-size import is refused', log)
        rc, log = sdl(ct_sdl, ['--save', srm, '--import-srm', good], env)
        kept = [x for x in os.listdir(d) if x.startswith('game.srm.before-import-')]
        with open(srm, 'rb') as f:
            check(rc == 0 and f.read() == bytes(range(256)) * 32 and len(kept) == 1,
                  'import, with the old save kept', log)
        rc, log = sdl(ct_sdl, ['--save', srm, '--export-srm', out], env)
        with open(out, 'rb') as f:
            check(rc == 0 and f.read() == bytes(range(256)) * 32, 'export', log)
    print(f'sdl_qol: {len(FAILS)} failed')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

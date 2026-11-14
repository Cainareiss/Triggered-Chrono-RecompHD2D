#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a game module (runtime/module.h) from the user's own ROM, without
CMake: what an installed engine runs on first launch.

  1. check the ROM against GAME/game.toml (tools/check_rom.py)
  2. decode the game's WRAM overlays (GAME/tools/overlay_images.py)
  3. translate to C (recomp/emit.py)
  4. compile every file in parallel and link OUT/ct_game.so

usage: build_module.py --game DIR --rom FILE --out DIR [--cc CC]
                       [--native] [--jobs N] [--keep-c]

--native compiles for this machine's CPU (-march=native); the module is
never copied anywhere else, so it may as well be. The generated C is
deleted after a successful build unless --keep-c: it is derived from the
ROM and has no use once compiled.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def step(msg: str) -> None:
    print(f'build_module: {msg}', flush=True)


def run(cmd: list[str], env: dict | None = None, capture: bool = False) -> str:
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'build_module: failed: {" ".join(cmd)}')
    return r.stdout if capture else ''


# One compile of a big generated file takes up to about 600 MB. On a handheld
# with 16 cores, one job per core can run the machine out of memory (the
# kernel then kills the game or the desktop), so fit the jobs to the memory
# that is free now, keeping 1 GB spare.
JOB_MB = 600


def default_jobs() -> int:
    cpus = os.cpu_count() or 4
    try:
        with open('/proc/meminfo') as f:
            avail_kb = next(int(l.split()[1]) for l in f if l.startswith('MemAvailable:'))
    except (OSError, StopIteration, ValueError):
        return cpus
    return max(1, min(cpus, (avail_kb // 1024 - 1024) // JOB_MB))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', required=True, help='game directory, e.g. game/<id>')
    ap.add_argument('--rom', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--jobs', type=int, default=default_jobs())
    ap.add_argument('--keep-c', action='store_true')
    a = ap.parse_args(argv[1:])

    game = os.path.abspath(a.game)
    rom = os.path.abspath(a.rom)
    out = os.path.abspath(a.out)
    gen = os.path.join(out, 'gen')
    objs = os.path.join(out, 'obj')
    os.makedirs(gen, exist_ok=True)
    os.makedirs(objs, exist_ok=True)
    py = sys.executable
    env = dict(os.environ, CT_ROM=rom)
    t0 = time.time()

    step('checking the ROM')
    run([py, os.path.join(ROOT, 'tools', 'check_rom.py'), game, rom])

    step('decoding overlays')
    run([py, os.path.join(game, 'tools', 'overlay_images.py'), os.path.join(game, 'overlays.toml'),
         os.path.join(gen, 'overlays')], env)

    step('translating to C')
    funcs = os.path.join(game, 'funcs.toml')
    run([py, os.path.join(ROOT, 'recomp', 'emit.py'), '--out', gen, '--funcs', funcs,
         '--overlays', os.path.join(gen, 'overlays', 'manifest.toml')], env)
    modules = run([py, os.path.join(ROOT, 'recomp', 'emit.py'), '--list-modules', '--funcs', funcs],
                  capture=True).replace(';', ' ').split()   # a CMake list

    sources = [os.path.join(gen, f'{m}.c') for m in modules]
    # The game's own C (its extern hooks, its scene description): every .c
    # at the top of its directory.
    game_c = sorted(os.path.join(game, f) for f in os.listdir(game) if f.endswith('.c'))
    sources += [os.path.join(gen, 'ct_overlays.c'), os.path.join(gen, 'ct_funcs.c'), *game_c,
                os.path.join(ROOT, 'runtime', 'module_entry.c')]
    cflags = ['-O2', '-fPIC', '-fvisibility=hidden', '-std=c11', '-DCT_TEST_BUILD',
              '-DCT_TABLES_DEFINE', '-I', gen, '-I', os.path.join(ROOT, 'runtime'),
              '-I', game]   # the game's own headers (e.g. game/<id>/*.h)
    if a.native:
        cflags.append('-march=native')

    step(f'compiling {len(sources)} files with {a.cc} ({a.jobs} jobs)')

    def compile_one(src: str) -> str:
        obj = os.path.join(objs, os.path.basename(src) + '.o')
        run([a.cc, *cflags, '-c', src, '-o', obj])
        return obj

    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        # Biggest first: the long poles start early.
        order = sorted(sources, key=lambda s: -os.path.getsize(s))
        obj_files = list(ex.map(compile_one, order))

    module = os.path.join(out, 'ct_game.so')
    step('linking')
    run([a.cc, '-shared', '-o', module + '.tmp', *obj_files])
    os.replace(module + '.tmp', module)
    if not a.keep_c:
        shutil.rmtree(gen)
    shutil.rmtree(objs)
    step(f'done: {module} ({os.path.getsize(module) // 1024} KiB, {time.time() - t0:.0f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

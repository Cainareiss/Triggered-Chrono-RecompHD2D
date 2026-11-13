#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pseudo-random input replay generator (runtime/replay.h format).

usage: replay_gen.py --seed N --from F1 --to F2 [--prefix FILE]
                     [--buttons NAME[:WEIGHT],...] [--hold MIN-MAX]
                     [--gap MIN-MAX] [--combo P] [--out FILE]

Writes a replay script to FILE (default stdout): the --prefix script's
lines first, verbatim (e.g. a scripted route to some point of a game), then
random spans from frame F1 to F2. Each span holds one button (or, with
probability P, two at once) for MIN..MAX frames, followed by a gap of
MIN..MAX frames with nothing held. Buttons are picked by weight, so a
pattern can lean on menu buttons or on the d-pad:

  --buttons up:3,down:3,left:3,right:3,a:2,b:1,x:1       (exploration)
  --buttons x:2,a:3,b:3,up:2,down:2,left:1,right:1       (menu-heavy)

Same seed and options, same file: the generator uses Python's own Mersenne
Twister (random.Random(seed)), which is stable across Python versions for
randrange/random. Button names are the replay format's: b y select start
up down left right a x l r. Nothing here knows about any game.
"""
import argparse
import random
import sys

NAMES = ('b', 'y', 'select', 'start', 'up', 'down', 'left', 'right', 'a', 'x', 'l', 'r')


def span(text: str) -> tuple[int, int]:
    lo, _, hi = text.partition('-')
    lo_i, hi_i = int(lo), int(hi or lo)
    if lo_i < 0 or hi_i < lo_i:
        raise argparse.ArgumentTypeError(f'bad range {text!r}')
    return lo_i, hi_i


def weights(text: str) -> list[tuple[str, int]]:
    out = []
    for item in text.split(','):
        name, _, w = item.partition(':')
        name = name.strip().lower()
        if name not in NAMES:
            raise argparse.ArgumentTypeError(f'unknown button {name!r}')
        out.append((name, int(w or 1)))
    if not out or sum(w for _, w in out) <= 0:
        raise argparse.ArgumentTypeError('no buttons')
    return out


def pick(rng: random.Random, table: list[tuple[str, int]]) -> str:
    k = rng.randrange(sum(w for _, w in table))
    for name, w in table:
        if k < w:
            return name
        k -= w
    raise AssertionError


def generate(seed: int, first: int, last: int, table, hold, gap, combo: float) -> list[str]:
    rng = random.Random(seed)
    lines = []
    f = first
    while f <= last:
        n = rng.randint(*hold)
        names = {pick(rng, table)}
        if rng.random() < combo:
            names.add(pick(rng, table))
        end = min(last, f + n - 1)
        lines.append(f'{f}-{end}:' + '+'.join(sorted(names)))
        f = end + 1 + rng.randint(*gap)
    return lines


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--seed', type=int, required=True)
    ap.add_argument('--from', dest='first', type=int, required=True)
    ap.add_argument('--to', dest='last', type=int, required=True)
    ap.add_argument('--prefix')
    ap.add_argument('--buttons', type=weights,
                    default=weights('up,down,left,right,a,b,x,y,l,r,start,select'))
    ap.add_argument('--hold', type=span, default=(2, 30))
    ap.add_argument('--gap', type=span, default=(0, 20))
    ap.add_argument('--combo', type=float, default=0.1)
    ap.add_argument('--out')
    a = ap.parse_args(argv)
    if a.first < 1 or a.last < a.first or a.hold[0] < 1:
        ap.error('need 1 <= --from <= --to and a hold of at least 1 frame')
    head = []
    if a.prefix:
        with open(a.prefix) as f:
            head = [line.rstrip('\n') for line in f]
    body = generate(a.seed, a.first, a.last, a.buttons, a.hold, a.gap, a.combo)
    text = '\n'.join(head + [f'# replay_gen.py --seed {a.seed}: frames {a.first}-{a.last}']
                     + body) + '\n'
    if a.out:
        with open(a.out, 'w') as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())

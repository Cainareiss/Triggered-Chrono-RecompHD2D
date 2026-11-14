#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Linear 65816 listing of ROM code, with funcs.toml names, for reading.

Unlike recomp/decode.py this does not follow control flow: it decodes
straight from ADDR for COUNT instructions, tracking M/X through REP/SEP
only (so a listing after a branch join may be mis-sized), and stops at
RTS/RTL/RTI unless --through. Calls and jumps to known entries get their
funcs.toml name. A research aid (docs/SCENE.md), not part of the build.

usage: ldis.py NAME|ADDR [--state m1x0] [--count N] [--through]
                [--overlay FILE@ADDR]
       (ROM from $CT_ROM). --overlay reads the bytes of FILE as if they sat at
       ADDR (e.g. a decompressed code blob, ct_decompress.py, at 7E3000), so
       WRAM overlay code can be listed; addresses outside it read the ROM.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(os.path.dirname(GAME))
sys.path.insert(0, os.path.join(REPO, 'recomp'))

import decode  # noqa: E402


def load_names() -> tuple[dict[int, str], dict[int, str]]:
    names, states = {}, {}
    name = None
    with open(os.path.join(GAME, 'funcs.toml')) as f:
        for line in f:
            m = re.match(r'\s*name\s*=\s*"([^"]+)"', line)
            if m:
                name = m.group(1)
                continue
            m = re.match(r'\s*addr\s*=\s*0x([0-9A-Fa-f]+)', line)
            if m and name:
                addr = int(m.group(1), 16)
                names[addr] = name
                continue
            m = re.match(r'\s*states\s*=\s*\["([mx01]+)"', line)
            if m and name:
                for a, n in names.items():
                    if n == name and a not in states:
                        states[a] = m.group(1)
    return names, states


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('where')
    ap.add_argument('--state')
    ap.add_argument('--count', type=int, default=200)
    ap.add_argument('--through', action='store_true')
    ap.add_argument('--overlay', help='FILE@ADDR: bytes of FILE mapped at ADDR (hex)')
    a = ap.parse_args(argv)
    names, states = load_names()
    by_name = {v: k for k, v in names.items()}
    addr = by_name.get(a.where)
    if addr is None:
        addr = int(a.where.lstrip('$'), 16)
    st = decode.parse_state(a.state or states.get(addr, 'm1x0'))
    rom = decode.load_rom(None)
    if a.overlay:
        path, _, base = a.overlay.rpartition('@')
        with open(path, 'rb') as f:
            rom = decode.Image(rom, int(base, 16), f.read())
    for _ in range(a.count):
        lab = names.get(addr)
        if lab:
            print(f'{lab}:')
        i = decode.decode_insn(rom, addr, st)
        txt = i.text()
        tgt = None
        if i.mnemonic in ('JSR', 'JMP') and i.mode == 'abs':
            tgt = (addr & 0xFF0000) | i.operand
        elif i.mnemonic in ('JSL', 'JML') and i.mode == 'long':
            tgt = i.operand
        note = f'   ; {names[tgt]}' if tgt in names else ''
        mx = f"m{int(bool(i.m))}x{int(bool(i.x))}"
        print(f'  {addr:06X} {mx}  {txt}{note}')
        st = decode.next_state(i, st)
        addr = i.next_addr
        if i.mnemonic in ('RTS', 'RTL', 'RTI', 'BRL', 'BRA', 'JMP', 'JML') and not a.through:
            break
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

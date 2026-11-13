<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Known issues

Stops (`ct_fatal`) and other faults found by playing, and what became of them.
Frames are `ct_boot` frames (1-based) with the named replay script.

## Open

None known.

## Fixed

| Found | Where | What | Fix | Regression |
| --- | --- | --- | --- | --- |
| Opening the menu on the world map (leene_square route, X at frame 7700) | frame 7737, `$C2F15E` | `interp $C2F15E: decimal mode`: ADC/SBC with D=1 was not implemented | `runtime/alu.h` (decimal ADC/SBC, both paths), `CT_MODULE_ABI` 2 | `decimal`, `save_replay`, `lockstep_world_map_save` |

## Crash hunts

Headless `ct_boot` runs with input from `tools/replay_gen.py` (fixed seeds) after a
scripted route, each run native and `--interp-only`. Rounds 2 and 3 also compared the
two paths' `--hash-log` frame by frame. The replay files are rebuilt from the
commands below; they aren't committed. `R=game/ct/test/replay`.

| Pattern | Route, then `replay_gen.py` | Frames | Result |
| --- | --- | --- | --- |
| exploration | `gato_win.txt`, `--seed 1 --from 13951 --buttons up:4,down:4,left:4,right:4,a:3,b:1,x:1 --hold 4-60 --gap 0-30` | 50,000 | no stop |
| mashing | `leene_square.txt`, `--seed 2 --from 9201` (defaults) | 40,000 | no stop |
| menus | leene_square to frame 7315, `--seed 3 --from 7316 --buttons x:3,a:4,b:3,up:2,down:2,left:2,right:2,start:1 --hold 2-8 --gap 4-30` | 40,000 | no stop |
| title mashing | none, `--seed 4 --from 1500` | 30,000 | no stop |
| exploration | `gato_win.txt`, `--seed 11 --from 13951 --buttons up:5,down:5,left:5,right:5,a:3,b:1 --hold 10-120 --gap 0-20` | 60,000 | no stop, hashes match |
| menus | `gato_win.txt`, `--seed 12 --from 13951 --buttons x:3,a:4,b:3,up:2,down:2,left:2,right:2,y:1,l:1,r:1 --hold 2-8 --gap 4-30` | 45,000 | no stop, hashes match |
| mashing | `leene_square.txt`, `--seed 13 --from 9201 --buttons up:4,down:4,left:4,right:4,a:4,b:2,x:2,y:1,l:1,r:1,start:1,select:1 --hold 2-40 --gap 0-10 --combo 0.3` | 45,000 | no stop, hashes match |
| after a load | `world_map_save.txt`, `--seed 14 --from 10101 --buttons up:3,down:3,left:3,right:3,a:3,b:2,x:2 --hold 4-80 --gap 0-20` | 40,000 | no stop, hashes match, 3 more saves |
| exploration | `leene_square.txt`, `--seed 21 --from 9201 --buttons up:5,down:5,left:5,right:5,a:4,b:1,x:1 --hold 20-200 --gap 0-10` | 60,000 | no stop, hashes match |
| mashing | `gato_win.txt`, `--seed 22 --from 13951 --combo 0.4 --hold 1-15 --gap 0-5` | 50,000 | no stop, hashes match |
| menus | `world_map_save.txt`, `--seed 23 --from 10101 --buttons x:4,a:5,b:4,up:3,down:3,left:3,right:3 --hold 2-6 --gap 6-20` | 40,000 | no stop, hashes match, 10 more saves |
| boot mashing | none, `--seed 24 --from 1 --combo 0.5 --hold 1-10 --gap 0-3` | 30,000 | no stop, hashes match |

Each run's `--to` is its frame count. Every round-3 run ran the decimal-mode code at
`$C2F15E` (`--at`), the code that stopped the game before the fix.

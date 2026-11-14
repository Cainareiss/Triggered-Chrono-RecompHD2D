<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Known issues

Stops (`ct_fatal`) and other faults found by playing, and what became of them.
Frames are `ct_boot` frames (1-based) with the named replay script.

## Open

- **TAS timing: the movie falls out of step in the first Roly Rider battle.**
  With `ct_boot --resync` (below) the TASVideos movie stays in step with its video
  through about frame 31,450 (Guardia Forest, 600 AD); from about 31,500 the battle
  runs slower here than on bsnes (the movie's attacks land on Roly Riders it has not
  reached yet) and no input shift brings it back (`resync: no shift gets past frame
  32729`). Earlier, the Leene Square telepod cutscene (frames 13,550-13,750) also
  takes about 40 frames more than on bsnes. Both are emulation timing (lag frames),
  not stops: native and interpreter agree. Not yet compared against Mesen 2 on the
  same input (`tools/ref_compare.py`): running the downloaded Mesen binary was not
  permitted in that session.

## Fixed

| Found | Where | What | Fix | Regression |
| --- | --- | --- | --- | --- |
| Opening the menu on the world map (leene_square route, X at frame 7700) | frame 7737, `$C2F15E` | `interp $C2F15E: decimal mode`: ADC/SBC with D=1 was not implemented | `runtime/alu.h` (decimal ADC/SBC, both paths), `CT_MODULE_ABI` 2 | `decimal`, `save_replay`, `lockstep_world_map_save` |
| TAS movie, played unsynced (`t7.txt` below, native) | frame 171,567, `$C28006` (native only) | `$C28006: callee returned to $C008, expected $8009`: a soft reset (L+R+Start+Select; `$C28523` JML to `$C0FF00`, then `$FDC000` reloads S) abandons the native frames under it, which then stopped on their return check | `runtime/ops.h` `cpu_return_ok`, `recomp/emit.py`: under the scheduler a caller whose callee came back elsewhere hands its rest to the executor (frames the new S is above unwind at once); still fatal without the scheduler. `CT_MODULE_ABI` 3 | `soft_reset_replay`, `lockstep_soft_reset` (`replay/soft_reset.txt`, frame 9220), `native` |
| TAS movie with `--resync`, in step (first Roly Rider battle) | frame 31,405, `$C25839` (native only) | `$C25839: jump table index $0004 out of range`: the text engine's mode table was recorded with 2 entries but holds 4 (`$58B2 $5BF5 $5C3E $5C77`); text mode 2 (a battle name longer than one frame's budget) carried over a frame edge | `game/ct/funcs.toml` (`count = 4`); `recomp/emit.py`: past its recorded entries a table's default case reads the word from the bus and runs it through the dispatcher (as a table of unknown length), strict without the scheduler. `CT_MODULE_ABI` 3 | `native` (test_native: a table used past its entries, == interpreter) |

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

## TAS movie

TASVideos [5232M](https://tasvideos.org/5232M) (SNES Chrono Trigger any%, arandomgameTASer
& keylie, BizHawk 2.8 bsnes core, 490,911 frames from power-on, the same ROM).
Movie: https://tasvideos.org/5232M?handler=Download (a zip holding the .bk2); video:
the archive.org encode `chronotrigger-tas-arandomgametaser_keylie.mp4`. Neither is
committed. The movie's frame k is script frame k + 7 (name entry needs it), and video
frame s + 111 shows script frame s.

    python3 -I tools/tas_convert.py movie.bk2 t7.txt --offset 7
    ffmpeg -i enc.mp4 -vf scale=64:56:flags=area,format=gray -f rawvideo ref64.gray
    ct_boot --script t7.txt --frames 491000 --hash-log native.hash        # and --interp-only
    ct_boot --script t7.txt --frames 520000 --resync ref64.gray 111 played.txt --resync-jobs 13

| Run | Frames | In step with the video | Result |
| --- | --- | --- | --- |
| plain, native and `--interp-only` | 491,000 | to about 13,750 (Leene Square telepod: a text box's X lands early), then the inputs play on as mashing | before the fixes: native stopped at 171,567 (soft reset, above); after: no stop, the two paths' hashes equal on all 491,000 frames |
| `--resync` | 520,000 | through about 31,450 (5 shifts: +39 at 13749, +1 at 14578, +1 at 16613, +5 at 18392, +1 at 20319), Guardia Forest 600 AD | before the table fix: native stopped at 31,405 (above); after: no stop through 520,000 frames, native and `--interp-only` hashes equal on every frame (the played script, `--script played.txt`); after the battle the inputs play unshifted (as mashing) |

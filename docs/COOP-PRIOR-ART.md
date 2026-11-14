<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Co-op prior art: the "Chrono Trigger Multiplayer" ROM hack

Notes from 2026-11-13 on an existing battle co-op hack for CT, to check our own co-op
design (the `coop` branch, `game/ct/coop.c`) against it. This is a study of how the
patch **behaves**. Nothing from it is in this repository: not the patch, not a patched
ROM, not a listing of its code. Everything below is a description in my own words. The
addresses are the game's own (our `funcs.toml` names), plus the places where the patch
puts its code.

## Source and version

- **Author:** Colmines92.
- **Romhacking.net:** [hack 8337](https://www.romhacking.net/hacks/8337/), "Chrono Trigger:
  Multiplayer", v1.2, February 2024 (from the survey in [PRIOR-ART.md](PRIOR-ART.md)). The page
  and its download sit behind a Cloudflare check. Scripted fetches got HTTP 403 even with a
  browser-like request. The user downloaded the zip by hand.
- **The RHDN zip:** `Chrono Trigger Coop v1.2.zip` (337 bytes). It has one member,
  `Chrono Trigger Multiplayer v1.2.ips` (144 bytes, dated 2024-02-03), and nothing else.
  There is **no readme** and no zip comment.
- **The author's GitHub:** [Colmines92/MultiplayerHacks](https://github.com/Colmines92/MultiplayerHacks)
  (created 2024-02-03) has `Chrono Trigger Multiplayer.ips` (144 bytes) next to a Super Mario
  RPG one.
- **The two copies are byte-identical** (SHA-256 `47906bf5...c92ac1`). The zip's "Coop" name
  is only the archive name; it is the same v1.2 patch.
- **Target:** the headerless USA ROM (the patch applies cleanly to our verified ROM; the
  bytes it overwrites hold the code it expects).

## License status

**None stated.** The zip has no readme or license text. The GitHub repository has no
LICENSE file and no README, and GitHub detects no license. I could not read the RHDN page
itself (the Cloudflare check), so any permission text there is **unverified**. With nothing
stated, we treat it as all rights reserved: we may study it and describe what it does, and
we take nothing from it. That is also all we need. The idea fits in one paragraph, and our
implementation is separate (a runtime patch in `coop.c`, not a ROM change).

## Features (from the survey and confirmed by the patch)

- Up to three players in **battle only**. There are no field, menu or world map changes.
- Player 2 commands **party slot 2** (the first follower). Player 3 commands **party
  slot 3**. Player 1 commands the leader, plus any slot whose player isn't connected. The
  mapping is fixed by slot, so whoever stands in slot 2 after a party change belongs to
  player 2.
- Player 3 uses the second data line of controller port 2, i.e. **a multitap** (the
  third pad of a multitap, `$421E`). With only a plain pad 2, slot 3 stays with player 1. A
  second pad does not pick up slot 3.
- **Hot-plug:** the patch checks every frame whether a pad is connected. If a pad is
  unplugged, its slot goes back to player 1 on the next frame.
- With one controller, the game plays as the original.

## What it hooks

The patch is three small overwrites in one routine, plus about 80 bytes of new code in an
empty (zero-filled) stretch of bank `$C3` (`$C37A2D-$C37A7D`).

| Where | Our name | What changes |
|---|---|---|
| `$C28502` (12 bytes) | Pad_ReadAndDecode (`$C284EC`) | The two reads of `$4218/$4219` that build `$00F2/$00F3` (held buttons) become calls that read the *selected* pad's register pair. The routine is also forced to 8-bit index registers there. |
| `$C28510` (4 bytes) | same | The second read of `$4219` (the d-pad nibble folded into `$F2`) goes through the same selection. |
| `$C28517` (7 bytes) | same | The soft-reset test (`L+R+Select+Start` compared at `$C2851E`) now reads the *selected* pad as well. |
| `$C37A2D-$C37A7D` | (new) | The selector and four small read wrappers. |

**The selector, in words:** it reads controller port 2's serial line directly, after the
automatic read has finished. A standard pad then reads 1 and an empty port 0, so the read
says which pads are connected (bit 0: pad 2; bit 1: the multitap's pad 3). With nothing
connected it picks pad 1. Otherwise it takes **`$7EA6DD`** as the slot. If that slot's pad
is connected, slot 1 reads pad 2 (`$421A`) and slot 2 reads pad 3 (`$421E`). Slot 0, any
value above 2, or a missing pad reads pad 1 (`$4218`). The selector runs four times per
call of the routine (once per read), and each run reads the port bit again.

What `$7EA6DD` is: in battle, `$A6D9-$A6DB` is a per-slot "ready" list (the slot number
when that member is ready, `$FF` when not), and `$A6DD` is the cursor into it, so its value
is the party slot whose command menu is open. BattleMenu_CycleActivePcPrev/Next
(`$C111E1/$C11218`, left/right) move it among the ready slots. The menu-close path at
`$C1172C-$C11732` writes the same value to `$A6DD` and to `$95D5`. So in battle the hack
keys on what our patch keys on (`$95D5`), only one step earlier.

What it doesn't touch: Pad_ApplyButtonRemap (`$C28545`), BattleSys_UpdatePadRepeat
(`$CD0340`), any battle logic, the field follower routines. Because it swaps the raw pad at
the very first read, everything downstream follows the selected pad: held (`$F2`), new
presses (`$F0/$F4`, worked out against the previous frame's value in `$00FE`), the remapped
`$F8/$F9` (player 1's button config applies to every player), the battle auto-repeat, and
the soft-reset combination.

## Observed behaviour, with evidence

**Running the patched ROM: not possible here.** Our runtime doesn't model the manual
joypad ports `$4016/$4017`, and the hack writes and then reads `$4017` directly. `CT_ROM=<patched copy>
ct_boot --interp-only` stops in frame 33 at `$C37A30` ("write8 $004017: unhooked hardware
register"), on its first pad read. The hd2d `ct_boot` also takes no pad 2 input (the `p2`
script lines and `--coop` are on the `coop` branch only). So the behaviour below comes
from the patch's code plus traces of the **unpatched** game on our routes, which show the
value the selector would act on at every point.

1. **In battle the selector picks the open menu's owner.** On the `coop` branch's
   `coop_battle.txt` route (its `p2` lines dropped; Crono and Marle against Gato),
   `--watch A6D9:5 --watch 95D5:1` shows the following:
   - Frame 13962, the battle starts: `$A6D9-$A6DD` and `$95D5` are all `$FF` (pad 1).
   - Frame 14053, Crono's menu opens: `$A6DD` becomes `00` at `$C11BA6` and `$95D5` becomes
     `00` (pad 1).
   - Frame 14461, Crono's command is picked: `$A6DD` and `$95D5` become `01` together at
     `$C1172F/$C11732`. With a pad 2 connected, the hack hands every button to pad 2 from
     the next frame.
   - Frame 14563, Marle's command is picked: both are back to `$FF` (pad 1).
2. **Outside battle it does nothing, at least on the routes we have.** On the same route
   and on `world_map_save.txt` (title, name entry, field, world map, the pause menu and a
   save, a reset, Continue), `$A6DD` only ever holds `$80` (field and world map, written
   by `$C0A72D`), `$41` (title and attract, `$C30633`), and `$00` (menus and name entry,
   `$C2953C`). Each of these makes the selector pick pad 1. The hack doesn't check the
   game mode, though, so a screen that leaves 1 or 2 in `$A6DD` would hand the whole game
   to pad 2 or 3. None was seen, but the routes don't cover every menu.
3. **Hand-off without a guard (from the code).** At a hand-off, the "previous" buttons in
   `$00FE` are the old owner's and the new held value is the new owner's. Anything the new
   owner is already holding counts as a **new press** on the first frame. Mashing A on pad
   2 while Crono's menu closes can pick Marle's first command at once.
   (`coop_battle.txt` exercises this case for our patch: pad 2 holds A across frame
   14461.)
4. **The active player owns everything, including reset.** While slot 2's menu is open,
   player 1's pad isn't read at all. Player 2's L+R+Select+Start then soft-resets the game
   and player 1's doesn't.
5. **Cycling hands control around.** Left/right in a menu moves `$A6DD` to another ready
   member, and the pad moves with it. Player 2 can pass to player 1 by cycling to
   Crono, and player 1 can pass to player 2 the same way. There is no restriction.
6. **No ATB "wait" handling, no indicator.** Nothing shows whose turn it is, and the battle
   doesn't pause while a player looks away.

## Compared with our `coop` branch

| | The hack | Our `coop` (B1) |
|---|---|---|
| Where it swaps | The raw pad read, for every mode | `$00F8` at BattleSys_UpdatePadRepeat (`$CD0340`), battle only |
| Owner signal | `$A6DD` (ready-list cursor) | `$95D5` (open menu slot); the same value in battle |
| Players | 3, slot 2 fixed to P2, slot 3 to P3 (needs a multitap) | 3; `coop_chars` / `coop_chars3` choose the slots (default: P2 gets slots 2 and 3) |
| Unplug | Next frame, by the port's connection bit | At once, the frontend lowers `sched_players` |
| Hand-off | No guard: the new owner's held button is a new press | Buttons held at the hand-off are masked until released |
| Soft reset | By whoever owns the input | Pad 1 always (it reads `$4218` before our hook) |
| Cycling (left/right) | Hands control to the new member's owner | The same (control follows `$95D5`) |
| Field | Nothing | P2 walks follower 1 (B2), split screen prototype (B3) |
| Button config | P1's config for every pad | The same (we remap P2's pad with the game's config) |

Ours is safer in the places where the two differ. It is limited to battle, it guards the
hand-off, and reset stays with player 1. The hack has one thing we lack, and it suggests
a few cheap improvements.

## Recommendations for the `coop` branch

1. **Add a "one character each" preset** that matches the hack: with three pads, P2 gets
   slot 2 only and P3 slot 3 only. With two pads, offer both "P2 has slots 2+3" (our default)
   and "P2 has slot 2, P1 keeps slot 3" (the hack's). It is only a settings choice, since
   `sched_set_slot_player` already supports it. Players who know the hack will expect it.
2. **Keep our hand-off guard**, and add a `coop_battle`-style ctest for a *mashing* player
   2. The hack's main weakness is the phantom first press, and that test pins our fix.
3. **Keep the swap battle-only and keyed on `$95D5`.** The hack's whole-game swap depends
   on `$A6DD` never holding 1 or 2 outside battle. That held on our routes, but the
   ROM doesn't promise it.
4. **Decide the cycling policy explicitly.** Both designs let the menu owner press
   left/right and give the menu to another player's member, which hands control over. That
   is arguably good (players can pass a turn), but SCENE.md once proposed masking P2's
   left/right. Document the choice, and consider letting a player cycle only among their
   own ready members, so nobody can grab or dump a turn by accident.
5. **Keep soft reset on player 1 only** (and say so in USER-GUIDE.md). In the hack,
   player 2 can reset the game while their menu is open. That is a real footgun with
   a second player mashing buttons.
6. **Show who is choosing.** Neither design shows whose turn it is. The hack is a pure
   ROM patch and can't draw outside the game's own screen. A frontend overlay (a "P2" tag
   by the open menu, or a colored cursor) would fix this cheaply for us.
7. **Per-character assignment, as an option.** Both designs bind players to party slots,
   so a party change mid-game silently swaps who plays whom. Binding by character id
   (read from the party list) would let a player keep "their" character.
8. **For ROM hacks in general (low priority):** our runtime stops on `$4016/$4017` reads.
   Modelling them would let patched ROMs like this one run under `ct_boot` for comparison
   tests. A pad on the port reads 1 after the 16 bits of the automatic read and an empty
   port reads 0; a write to `$4017` does nothing.

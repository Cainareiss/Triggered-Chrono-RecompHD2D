<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Scene description: what the game keeps, and what it would take to use it

Research spike for two planned features:

- **(A) a 3D field renderer**: rebuild the current map as geometry at runtime (floors,
  walls that rise, roofs, a perspective camera, sprites as billboards);
- **(B) co-op**: player 2 picks commands for party members 2 and 3 in battle, then
  steers a follower in the field, then, if it can be done, a **split screen** with a
  second view that follows player 2's character.

Both need the game module to hand the engine a per-frame *scene description*. This
document records where the game keeps that data, how each address was checked, a
proposed generic `ct_scene` struct, and whether each feature is feasible.

Nothing here changes game logic. Tools added: `game/ct/tools/scene_probe.py` (prints
the scene from a WRAM dump; draws the whole map with `--png`, lists OAM by object
with `--oam`) and `game/ct/tools/ldis.py` (a linear listing of ROM code with
funcs.toml names). Both read the user's own ROM or dumps. Their output is
ROM-derived: keep it in the build dir and never commit it.

## Summary of verdicts

| Question | Verdict |
| --- | --- |
| Whole map in WRAM (all layers, every tile)? | **Yes.** Up to 64x64 metatiles of 16 px per layer, in bank $7E. |
| Per-tile collision and properties in WRAM? | **Yes.** Three property bytes per tile, plus exit and zone grids. |
| BG tile graphics fully in VRAM for the map? | **Yes.** Loaded at map load; animated tiles are changed in place. |
| Object state map-wide? | **Mostly.** Positions, scripts and movement run for all 64 slots. Animation frames only advance for objects near the camera. |
| OAM | **Culled** to the camera window (+4 tiles); one VRAM sprite slot per active object. Only the current frame is uploaded. |
| (A) 3D rebuild of the field | **Yes.** All inputs are resident. A height map has to be *inferred*, because the game has none (see A0.9). |
| (B1) battle co-op | **Yes.** One hook on the battle's pad word, keyed by the active-menu byte. |
| (B2) P2 steers a follower | **Partial.** Hook the follower routine. Proper wall collision needs the leader's collision code, which is tied to the camera. |
| (B3) true split screen | **Partial (feasible with module work).** Background: yes, from WRAM/VRAM alone. Sprites: no from VRAM. They need a module-side sprite renderer, or a hook that widens the activation window. Map exits, triggers and battles stay tied to the leader. |

## Conventions and how things were checked

- WRAM addresses are given as bank $7E/$7F offsets (`$7E1800` = WRAM offset `0x1800`;
  `$7F2000` = offset `0x12000` in a `--wram` dump).
- **The field engine runs with D = $0100** (checked: `--log-entry C0092B` prints
  `DP 0100`). Direct-page operands in field code such as `$97`, `$99` and `$FE` are therefore
  `$0197`, `$0199` and `$01FE`. Camera code runs with D = $1D00 (`$0A` is `$1D0A`).
  Battle code runs with D = $0000 and DB = $7E.
- Object arrays are indexed by **2 x slot**. There are 64 slots: each array is $80 bytes.
  `$7F2000` holds the number of objects in the location's script.
- Replays: `leene_square.txt` (frame 9200: bell plaza, location $1B7), the same replay
  to frame 8700 (Leene Square, location $005), and `gato_win.txt` (battle, frames
  10650-11000). For a follower, the run that stops at frame 8700 adds
  `--poke-wram 8000:2981^82`, which puts PC 2 in party slot 2 while the game is on the world map, before the fair loads
  (`$7E2980-$7E2982` = party PC ids, `$80` = empty). Lucca then loads as follower 1 and follows Crono.
- Per-frame series: `--snap-at C0A810@8600 DIR` (the field object update) writes one
  WRAM snapshot per frame, 256 in all.

Typical commands, from a build dir:

```
CT_ROM=... ./ct_boot --frames 9200 --script ../game/ct/test/replay/leene_square.txt \
    --wram spike/l.wram --vram spike/l.vram
python3 ../game/ct/tools/scene_probe.py spike/l.wram --vram spike/l.vram --png spike/map.png --oam
```

## A0. Field map data

### A0.1 Map load and map-change detection

- **Location id: `$7E0100`** (u16, 9 bits used). Field_CalcLocationRecordOffset
  ($C0092B) reads it as `$00/$01` with D=$0100 and multiplies by 14 into the location
  records at `$F60000`. With `--watch 100:2`, leene_square shows $1B1 (title demo), $1F0
  (world map), $10F, $002, $001 (Crono's house), $005 (Leene Square), $1B7 (bell plaza)
  and $006 (Gato's area). Each change is written at $C00CB6 or $C21138/$C224AC (world
  map exits).
- **Load sequence:** Field_LoadLocationResources ($C000F4) loads music, tileset gfx
  ($C00960), the L3 tileset, BG palettes, then Map_LoadAndApplyConfig ($C0A33B: map
  header and grids), then the tile assembly ($C009DD, $C00A14). `--at C0092B` fires once per
  map load. **Map-change hook:** the return of Field_LoadLocationResources. Bump a
  "map generation" counter there, so the renderer rebuilds its geometry once. Polling
  `$0100` is not enough, because a map can be reloaded under the same id. Event
  commands that copy tiles (EventCmd_CopyTiles $C03D97, Field_CopyMapRectLayers
  $C0AF4E, opened treasure Field_ReapplyOpenedTileGroups) also change the map in place.
  The renderer must either re-hash the map grids per frame (cheap: 3 x 16 KB) or hook those routines too.

### A0.2 Tilemaps: the whole map is in WRAM

Map_LoadAndApplyConfig decompresses the map to `$7EB500`. The header is
`$7EB500-$7EB505` (C0A385-C0A40F):

- `$B500` bits 0-1 / 2-3: L1 width / height.
- `$B500` bits 4-5 / 6-7: L2 width / height.
- `$B501` bits 0-1 / 2-3: L3 width / height.
- `$B501` bit 7: L3 present (`$0BCA`); bits 6, 5 and 4 go to `$0BC9` and `$1D36`.
- `$B504` and `$B505`: more flags (`$0BD7`, `$0BDF`).

Each 2-bit size field n gives a width of 16 x (n+1) and a height of 16 x (n+1) as well (the
code computes `((b & $0C) + 4) * 4`). The sizes are stored in 16 px metatiles:

| Layer | Width | Height |
| --- | --- | --- |
| L1 | `$0BCB` | `$0BCD` |
| L2 | `$0BCF` | `$0BD1` |
| L3 | `$0BD3` | `$0BD5` (`$0BCA` bit 0 = L3 on) |

Verified sizes: the bell plaza is 48x48 on all three layers, and Leene Square is 48x64.

Map_BuildTilePropGrid ($C0A521) then unpacks the data into **fixed 64 x 64 grids, with
rows of 256 bytes**:

| Address | Content |
| --- | --- |
| `$7E3000 + y*256 + x` | L1 metatile index, low 8 bits |
| `$7E3040 + y*256 + x` | L2 metatile index, low 8 bits |
| `$7E3080 + y*256 + x` | L3 metatile index |
| `$7E30C0 + y*256 + x` | property byte 0: bit 0 = L1 index bit 8, bit 1 = L2 index bit 8, bits 2-7 = solidity class (A0.3) |
| `$7E7000 + y*256 + x` | property byte A |
| `$7E7040 + y*256 + x` | property byte B |
| `$7E7080 + y*256 + x` | exit grid: index into the location's exits, `$80` = none (Map_BuildExitGrid $C0A66B) |
| `$7E70C0 + y*256 + x` | camera-zone / treasure grid (Camera_CheckZoneTable $C09AA1; `$80` = none) |

The wrap masks are `$1D1E`/`$1D1F` (`$3F`, 64 tiles).

**Metatiles** (Field_DrawL1TileRowStrip $C07612): L1 and L2 share one table of
**512 x 8 bytes at `$7EB000`**. Each entry holds four SNES BG tilemap words: TL, TR, BL, BR. Each word carries
the tile number, palette, priority, H flip and V flip. L3 has its own table at `$7EC000`
(Field_DrawL3TileRowStrip $C07D66).

**VRAM holds only a window.** BG1/BG2/BG3 tilemaps are 64x32 at VRAM words
$6000/$6800/$7000 (`$2107-$2109` = $61/$69/$71). Row and column strips are streamed in as
the camera moves: Field_Draw*Strip into a buffer, then Vblank_Upload*Col*
($C086AB...). So the renderer must build geometry from the **WRAM grids**, not from
VRAM tilemaps.

*Verified:* from one WRAM+VRAM dump, `scene_probe.py --png` draws all of BG2+BG1 for
the map. That is 768x768 px for the bell plaza (frame 9200, where the camera shows
256x224) and 768x1024 px for Leene Square (frame 8700). The result is a correct picture of the
whole map, including the parts far off-screen.

### A0.3 Per-tile properties

The meanings below come from the code that reads each bit. **Confirmed** means a run agrees.
Everything else is a reading of the code that still needs a test.

- **Solidity class** = `$7E30C0 >> 2`. This is the value Camera_UpdateScroll ($C09AD3) dispatches on.
  - 0 = walkable.
  - 1 = solid. *Confirmed:* across 256 frames of walking, the tile under the leader was never class 1.
    The class-1 cells trace the walls, trees and tents in an ASCII dump of
    Leene Square.
  - 2-$15 = shaped, partial solidity: corner and diagonal shapes looked up in a table.
  - $16 and up = special handlers ($17, $18, $19 and others). The leader stood on classes $18, $19 and $1D
    while climbing the central stairs, so these are probably stairs and ramps.
- **Byte B (`$7E7040`)**:
  - bits 0-1: z-plane. They go to `$6A`, then `$0155`, the leader's current plane. It was 1 in every
    Leene Square tile under the leader. Community docs describe two walk planes plus transition tiles:
    "plane 1 / plane 2 / transitional", where the plane you are not on is solid.
  - bit 2: plane-transition flag (`$6C`).
  - bit 6: sprite priority for the object's **bottom** half on this tile.
  - bit 7: blocks NPCs (C0AEE1: velocity zeroed).
  - bit 3: unknown.
- **Byte A (`$7E7000`)**:
  - bits 0-1: auto-move direction; bits 2-3: auto-move speed ($08/$10/$20). Used for conveyors, wind
    and slopes (C09D6E).
  - bit 4: touch trigger. When the leader is on such a tile, the code sets `$0117` bit 4 and stores the tile in `$015B` (C09D43).
  - bit 6: sprite priority for the object's **top** half.
- **Overhead**: the metatile words' BG priority bit. In Leene Square, 494 of 3072 L1 cells
  have at least one priority-1 quarter. These draw over sprites, the way roofs and treetops do.
  The per-object sprite priority (`$0C00`/`$0C01` = $20 or $30, top/bottom half) is
  taken from the tile under the object (C0ADA8/C04B9F), so a sprite goes behind
  such tiles.

### A0.4 Tile graphics and palettes

Field_LoadTilesetGfx ($C00960) loads 6 chunks of $1000 bytes per map into VRAM words
$2000-$4FFF (Field_LoadTilesetChunkTo3000, from `$F6:1C00 + 8*tileset`). The BG1/2 character base is
`$210B` = $11 (word $1000). BG3 has its own tileset (`$210C` = $05). Mode 1, with BG3
priority (`$2105` = $09). The main screen is BG1+BG2+OBJ, and BG3 goes to the subscreen for color math.
So **every BG tile the map uses is in VRAM** while the map is loaded.
Vblank_UpdateAnimTileWords ($C0ED15) and the palette animators change tiles and
colors in place. Reading VRAM and CGRAM each frame picks those changes up.

### A0.5 Camera

- **Camera position in map pixels** = (`$1D0A`*8 + (`$1D87` & 7), `$1D0E`*8 + (`$1D89` & 7)).
  `$1D0A`/`$1D0E` are the camera's left and top edges in 8 px units. `$1D0C`/`$1D10` are the right and bottom
  edges. `$1D87`/`$1D89` are the BG1 HOFS/VOFS values written to the PPU, and
  `$1D8B..` hold BG2 and BG3. The scroll registers carry a ring offset for the VRAM window, so they are
  not map coordinates. *Verified* over 256 frames: Obj_ComputeScreenPos ($C0AB45)
  computes screen = obj_px - camera exactly this way. Example: camera (272,489), Crono at
  (408,624.9), screen (136,135).
- The camera follows the leader (`$0197`): Camera_CommitFrameDeltas ($C09175) and
  Camera_UpdateScroll ($C09AD3). Map bounds are set in Camera_InitMapBoundsAndMasks ($C07399) and
  clamped by Camera_ClampScrollX/Y ($C07506/$C07553). Camera zones are in the `$70C0` grid.

### A0.6 The object table (64 slots, index = 2*slot)

| Address | Meaning (verified where marked) |
| --- | --- |
| `$1100` | type: 0/1/2 = PC in party slot 1/2/3, i.e. leader/follower1/follower2 (EventCmd_LoadPcIfInParty $C0421E, which writes $80 if the PC is not in the party); 3 = PC loaded by the script as an actor (EventCmd_LoadPc $C04476); 4 = NPC; 7 = empty or placeholder; bit 7 = removed. **v** |
| `$1101` | PC id or NPC sprite id. **v** |
| `$1800` / `$1880` | X / Y in **1/16 px** (u16). The high byte is the 16 px tile, i.e. the foot point. **v** |
| `$0A00` / `$0A80` | screen X/Y (9 bit), active objects only. **v** |
| `$1900` / `$1980` | per-frame velocity dx/dy (signed, 1/16 px). |
| `$1600` | facing: 0 up, 1 down, 2 left, 3 right (EventCmd_ObjectFace* $C0556C... store it at C0554D). **v** (the leader walking up: 0) |
| `$1000` | flags (bit 7 kept, bit 0 set when a PC loads) |
| `$0F00` | bit 7 = active, i.e. near the camera. **v** |
| `$0F01` | current animation frame ($FF = never shown). **v** |
| `$1200` / `$1280` | 24-bit source of the sprite's tile graphics. PCs: ROM, e.g. $D272C0. NPCs: decompressed into `$7F8000-$7FFFFF` at map load. **v** |
| `$1500` / `$1480` | animation table pointer (bank $E4) / size. **v** |
| `$1680`/`$1681`, `$1601`, `$1780` | animation index, timer and mode. |
| `$0C00` / `$0C01` | OAM priority for the top and bottom halves ($20/$30). **v** |
| `$0D00` | OAM tile number of the object's VRAM sprite slot. **v** (OAM pieces match it) |
| `$1700` | VRAM offset of that slot. |
| `$1180` | script pointer (into `$7F2000+`); `$1080` = script priority queue link. |
| `$1A81`, `$1B01`, `$1C80` | drawn flag, collision flags, etc. |

**Party slots:**

- `$0197` = leader slot*2. `$0199` = follower 1, `$019B` = follower 2 ($80 = none). These
  are set by EventCmd_LoadPcIfInParty ($C0421E: `STX $97` / `STX $99`). **v** (leader 1, follower 1 = 3
  after the poke).
- Party PC ids are at `$7E2980-$7E2982`.

**Simulated map-wide?** Script slices (Field_ReadHVLatch $C05A46 to
EventScript_RunObjectScriptSlice $C05A93) and movement (Field_UpdateDrawObjects
$C0A810 to Obj_ApplyMoveVelocity) run for every slot, whatever the camera. **v**: in 256
frames at Leene Square, NPC 20 moved on 249 frames without ever being active, and NPC 8 moved on
234 frames while off-screen. Animation does *not* advance off-screen.
Obj_AnimTickAndQueue and Obj_WriteOamEntry are called only after Obj_UpdateVisibility
($C0A88D) succeeds. So an off-screen object keeps its last `$0F01`, or $FF if it was never shown.
**Activation** happens when the object comes within the camera window +4 tiles
horizontally, +2/+5 vertically. It assigns a sprite palette and a VRAM tile slot
(Obj_AssignSpritePalette $C07170, Obj_AssignSpriteTileSlot $C06F9A). PCs have fixed
slots. NPCs share the rest: slot owners are in `$0B88-$0B97`, about 16 slots.

### A0.7 OAM

- Shadow OAM is **`$7E0700-$7E091F`**. Vblank_UploadOam ($C0ECA3) sends it by DMA to OAM.
  **v**: identical to OAM in the `--vram` dump at frame 9200.
- It is **culled and y-sorted**. Only active objects write entries: Obj_WriteOamEntry ($C0A98A) and
  Obj_YSortListInsert ($C0A9CD), which use the screen-row buckets at `$0E00/$0E80`.
- A normal field sprite is four 16x16 pieces from its slot, tiles t, t+2, t+4, t+6, with
  t = `$0D00` & ~7. `scene_probe.py --oam` groups OAM entries by object this way without
  ambiguity (*v*, both dumps).
- Sprite graphics: each frame, the *current* frame's tiles are copied from `$7F`
  staging buffers (`$7F3800`... ) into the object's VRAM slot by DMA. The queue is at `$0940-$09AF`
  (Vblank_UploadSpriteTileQueue $C0B1B2). VRAM therefore holds one frame per active object
  and nothing for the rest.

### A0.8 Followers (field)

The leader's script runs EventCmd_ActAsPartyMember ($C03002) each frame and dispatches on `$1100`:

- 0: Obj_RecordMovement ($C09E29). Each step is appended to a 128-entry ring in `$7F0C00`
  (dx), `$7F0C80` (dy), `$7F0D00/$7F0D80` (pad intent), `$7F0E00/$7F0E80` (sprite priority) and
  `$7F0F00` (move flags). The head index is `$01AB`.
- 1: **Field_MoveFollower1 ($C0A26B)** replays the ring at index `$01AC`, keeping 16 entries
  behind, or 24 when running.
- 2: **Field_MoveFollower2 ($C0A2CE)** replays it at index `$01AD`.

*Verified:* at frame 8700, after the poke, the head is 88 and follower 1 is at 65. Follower 1
(obj 3) is about 1 tile behind the leader, and its priority entries match.
Followers do no collision of their own. They retrace the leader's path.

### A0.9 What A (3D rebuild) can use

Floors come from the walkable cells: solidity class 0, plus the shaped classes. Walls come from
class-1 cells next to walkable cells. A wall's *height* is not stored anywhere. Options:

- a fixed height per wall run;
- rows of class-1 cells above a walkable row, counted until a priority/overhead tile or the
  next walkable row;
- per-map overrides (data the user's build can generate, not shipped).

Roofs and overhangs come from BG priority-1 quarters of L1/L2. Bridges and balconies come from z-plane
2 cells. Stairs come from classes $18/$19/$1D (to confirm). Billboards come from objects
(position, facing, frame), with their pixels from the OAM pieces of active objects (A0.7).
The camera is A0.5. Everything is in WRAM/VRAM/CGRAM at the end of the frame, so
the module can fill the scene with no game hooks except the map-change counter.

## B0. Split-screen gate

**Can the engine draw a second view of the same map with a different camera, from
WRAM/VRAM alone?**

| Part | From resident data? | Notes |
| --- | --- | --- |
| BG1/BG2/BG3 | **Yes** | WRAM grids + metatile tables + VRAM tiles + CGRAM (A0.2, A0.4). The engine composes its own tilemap. The VRAM window is never used. |
| Animated tiles and palettes | Yes | They are shared, and changed in VRAM/CGRAM. |
| Object positions | **Yes** | Map-wide (A0.6). |
| Sprite pixels for objects near camera 2 | **No** | Not in VRAM unless the object is also near camera 1. Graphics exist in ROM/`$7F8000+` (`$1200/$1280`), but the frame layout comes from the bank-$E4 animation tables and the per-frame assembly code (Obj_BuildSpriteFrameStep $C0CAD9). |
| Their animation frame | **No** | Frozen while inactive. |
| Their palette | **No** | Only active objects get a sprite palette slot in CGRAM. |
| HDMA, window and color-math effects, L3 fog | Approximate | They are screen-relative and tied to camera 1. |

**What breaks, and the minimum hooks:**

1. **Sprites in view 2.** There are two ways:
   - **(a) Widen activation.** Hook Obj_UpdateVisibility and Obj_RefreshVisibilityWindow so the
     window is the union of both cameras' windows. Cheap, and frames, palettes and VRAM slots then exist. Limits:
     about 16 VRAM sprite slots, 8 OBJ palettes, 128 OAM entries and the vblank DMA budget
     (the recomp can raise the last). It also changes game state: activation runs activate
     scripts, so a run with it no longer matches the original. That is acceptable in co-op mode only.
   - **(b) A module-side sprite rasterizer.** Recommended for the long term. Read `$1200/$1280`
     (gfx), the bank-$E4 tables (frame to pieces), the palette source and the frame number. Advance the
     animation in the module for objects in view 2 only. Game state is unaffected.
2. **Camera 2.** The engine or module computes it, following P2's object, clamped like
   Camera_ClampScrollX/Y. It needs no game hook.
3. **Leader-bound logic.** Exits (`$7080` grid), touch triggers (byte A bit 4), talking
   (Player_FindFacingObject $C01CFC), the camera, and event and battle starts all use `$0197`
   only. P2 cannot take an exit or start an event. A **tether** rule is needed:
   when the leader changes map, enters an event (player control off) or starts a battle, snap P2 back
   to the follower ring (zero `$01AC`, as Field restore does) and merge the views.
4. **Map changes** reload everything for the leader's map only. Both views must be on the same
   location. There is a map-generation hook (A0.1).
5. **Scripted follower pathing.** In cutscenes the event script moves PCs explicitly
   (EventCmd_MoveToCoords, FollowPc and so on). P2 input must be ignored while the event system
   owns the object.

**Verdict: PARTIAL.** The background, map and positions are fully resident, so a second
view's world is a pure renderer job. Sprites need hook 1(a), or the rasterizer in 1(b). P2 cannot
trigger map logic, so the views must be tethered on exits, events and battles.
Minimum for a playable prototype: the `ct_scene` export, the map-generation counter, hook
1(a), and the tether in 3.

## B1 prep

### Battle: whose menu, and where the pad is read

- **Active command menu: `$7E95D5`.** It holds the party slot (0-2) of the PC whose command menu is
  open, or $FF if none. *Verified* (gato_win, `--watch 95D5:2`): it goes $FF to $00 when Crono's
  gauge fills (frame 10753, $C1058A) and back to $FF after the command (frame 10821,
  $C11732). Related: `$A6D9-$A6DB` is the list of ready PCs, `$A6DD` the cursor in that
  list (BattleMenu_CycleActivePcPrev/Next $C111E1/$C11218, driven by left/right), and
  `$95D6-$95D8` the ready queue (BattleMenu_DequeueReadyBattler $C11B67).
- **Pad path (all WRAM copies; the game reads `$4218` once per frame):**
  1. Pad_ReadAndDecode ($C284EC) reads `$4218/$4219` (pad 1) into `$00F2/$00F3` (previous value in `$00FE`).
     Menu_VblankInputService also copies pad 2 (`$421A`) to `$041F`. The game never uses it.
  2. Pad_ApplyButtonRemap ($C28545) applies the player's button config (`$2993-$299A`) and
     writes **`$00F8/$00F9`** (held, remapped). Field movement reads `$00F9`
     (Player_ApplyPadMoveIntent $C088EE).
  3. In battle, BattleSys_UpdatePadRepeat ($CD0340) turns `$F8` into new presses with
     auto-repeat (`$CC97-$CC9B`) and ORs them into **`$00EE/$00EF`**.
     BattleMenu_ProcessInput ($C11153) and the list and target handlers read those bits: `$EE`
     $80 = A, $40 = X; `$EF` $08/$04/$02/$01 = up/down/left/right. Battle_ZeroResultEE
     ($C1179C) clears them. *Verified:* `$EE` becomes $80 at $CD0389 on the A presses in gato_win
     (frames 10780, 10820, ...).
- **B1 hook:** at the entry of BattleSys_UpdatePadRepeat ($CD0340), when `$95D5` is 1 or 2, give
  it P2's pad, remapped the same way, instead of `$F8`. Also mask P2's left/right so P2 cannot
  cycle to slot 0 (or let them cycle only among slots 1 and 2), and give P1 nothing while P2's menu is open. The engine already
  keeps four ports (`sched_set_joypad(port, ...)`). No battle-logic change is needed.

### Field: the follower routine to hook

- **Field_MoveFollower1 ($C0A26B)** (follower 2: $C0A2CE). It is entered from
  EventCmd_ActAsPartyMember ($C03037/$C03027) with `$016D` = the object. A P2 hook replaces
  the ring replay: it sets `$1900/$1980` from P2's d-pad and sets facing and animation the way
  Player_SelectMoveAnim ($C09E84) does for the leader.
- **Collision gap:** the leader's solidity test is inside Camera_UpdateScroll ($C09AD3)
  and the camera stepping (Camera_MoveDispatch $C08A6D), which also scroll the camera.
  Generic object movement (Obj_ApplyMoveVelocity $C0AA07, collision branch C0AD90) only
  honours byte B bit 7 (NPC-blocking). A P2 follower therefore needs either a module-side
  solidity check (read `$30C0` class and the z-plane, A0.3) or a refactored copy of the
  leader's test. On returning control, reset the ring indices so follower replay resumes
  cleanly (Obj_RecordMovement's reset path at C09DC3 zeroes `$01AB-$01AD`).

## Proposed `ct_scene` (generic, for runtime/module.h)

The module fills this struct once per frame at the frame edge. The engine never parses game memory. The
names are game-agnostic on purpose (tools/check_agnostic.py). This is a proposal. It is not added to
module.h yet, because it would change `CT_MODULE_ABI`.

```c
/* One frame's scene, filled by the module (optional export ct_module_scene). */
#define CT_SCENE_MAX_LAYERS  3
#define CT_SCENE_MAX_OBJECTS 64
#define CT_SCENE_MAX_PIECES  8

typedef struct {
    uint16_t w, h;            /* in cells */
    uint8_t  cell_px;         /* 16 */
    uint8_t  present;
    const uint16_t *cells;    /* w*h tile-assembly indices (module-owned, valid this frame) */
} ct_scene_layer;

typedef struct {
    uint16_t entry[4];        /* SNES BG tilemap words TL,TR,BL,BR (tile, pal, prio, flip) */
} ct_scene_metatile;

enum {                        /* ct_scene_cell.flags */
    CT_CELL_SOLID      = 1 << 0,
    CT_CELL_SHAPED     = 1 << 1,   /* partial solidity; shape in .shape */
    CT_CELL_STAIRS     = 1 << 2,
    CT_CELL_TRIGGER    = 1 << 3,
    CT_CELL_EXIT       = 1 << 4,
    CT_CELL_NPC_BLOCK  = 1 << 5,
    CT_CELL_SPR_FRONT_TOP = 1 << 6, /* sprites on this cell draw above priority-2 BG */
    CT_CELL_SPR_FRONT_BOT = 1 << 7,
};
typedef struct {
    uint8_t flags, shape, plane, move;   /* move: auto-move dir|speed */
} ct_scene_cell;

typedef struct {
    int16_t x, y;             /* OAM-style offset from the object's foot point, px */
    uint16_t tile;            /* 9-bit OBJ tile in VRAM, or a module texture id when .from_module */
    uint8_t size, palette, priority, flip;
} ct_scene_piece;

typedef struct {
    int32_t  x16, y16;        /* map position, 1/16 px, foot point */
    uint8_t  kind;            /* 0 none, 1 party, 2 npc, 3 other */
    uint8_t  party_slot;      /* 0 leader, 1, 2; 0xFF */
    uint8_t  facing;          /* 0 up 1 down 2 left 3 right */
    uint8_t  plane;
    uint8_t  on_screen;       /* has pieces in this frame's OAM */
    uint8_t  n_pieces;
    uint16_t frame;           /* animation frame id, 0xFFFF unknown */
    ct_scene_piece piece[CT_SCENE_MAX_PIECES];
} ct_scene_object;

typedef struct {
    uint32_t map_id;          /* location id */
    uint32_t map_generation;  /* bumped on every load or in-place tile change */
    uint8_t  mode;            /* 0 none, 1 field, 2 battle, 3 world map, 4 menu/cutscene */
    uint8_t  player_control;  /* leader takes pad input this frame */
    int32_t  camera_x, camera_y;   /* map px, top-left of the 256x224 view */
    uint16_t bound_w, bound_h;     /* camera limits, px */
    ct_scene_layer layer[CT_SCENE_MAX_LAYERS];
    const ct_scene_metatile *metatiles; uint16_t n_metatiles;  /* shared by layers 0-1 */
    const ct_scene_metatile *metatiles_l3; uint16_t n_metatiles_l3;
    const ct_scene_cell *cells;    /* layer-0 grid, w*h */
    uint16_t bg_chr_base[CT_SCENE_MAX_LAYERS];   /* VRAM word addresses */
    uint8_t  n_objects;
    ct_scene_object object[CT_SCENE_MAX_OBJECTS];
    /* co-op */
    int8_t   battle_menu_slot;     /* party slot whose battle menu is open, -1 none */
} ct_scene;

/* Module export (optional; NULL = no scene). Called at the frame edge. */
typedef int (*ct_module_scene_fn)(ct_scene *out);
/* Optional input routing for co-op: which port drives party slot s this frame. */
typedef int (*ct_module_input_owner_fn)(int party_slot);
```

How this game fills it:

- `map_id` = `$0100`.
- layers from A0.2, metatiles from `$7EB000`/`$7EC000`, cells from `$30C0`/`$7000`/`$7040`/`$7080`.
- camera from A0.5.
- objects from A0.6, with pieces from shadow OAM `$0700` grouped by `$0D00` (A0.7).
- `battle_menu_slot` = `$95D5`.
- `mode` from which main loop is running. That still has to be pinned down (see the open questions).

Pixels never travel through the struct. The engine already has VRAM and CGRAM.

## Open questions

- The exact solidity shapes for classes 2-$15, and what the special classes $16+ do
  (stairs, ledges, jump tiles). Next step: a dump in Crono's house (stairs, upper floor), and
  mapping the jump table after C09B86.
- Byte B bit 3, byte A bits 5 and 7, and the zone grid `$70C0` semantics.
- A reliable "player has control" flag (events lock movement). `$0138`
  (checked by Player_ApplyPadMoveIntent) is a candidate. It has not been verified.
- A field / battle / world map / menu mode byte for `ct_scene.mode`.
- The world map ($1F0) uses a different engine (bank $C2/$C3 code wrote `$0100`). None of
  the above has been checked there.
- The sprite frame format in bank $E4 (needed for the rasterizer in B0 1(b)), and where
  each object's palette comes from.
- Whether Marle (PC 1) can be forced into the party by a poke at the fair. Her object there is
  type 3 (EventCmd_LoadPc, a scripted actor), so the follower test used Lucca instead. A replay past the
  pendant scene would give a natural two-PC party for B1 battle tests.

## Sources

- The code and runs above (funcs.toml names come partly from the public-domain
  dscotton/ct_disassembly naming).
- Z-plane behaviour (two walk planes, transitional tiles), as Temporal Flux shows it: Chrono
  Compendium and Data Crystal community pages, e.g.
  [Data Crystal: Temporal Flux event tricks](https://datacrystal.tcrf.net/wiki/Chrono_Trigger_(SNES)/Temporal_Flux_Event_Tricks)
  (`$7E2980` = PC1),
  [Chrono Compendium: Temporal Flux FAQ](https://www.chronocompendium.com/Term/Temporal_Flux_FAQ.html),
  [romhacking.net: CT internals, Z-plane](https://www.romhacking.net/forum/index.php?topic=14449.0).
- Save/party layout: [Data Crystal RAM map](https://datacrystal.tcrf.net/wiki/Chrono_Trigger:RAM_map),
  [TASVideos submission 2240](https://tasvideos.org/2240S) (`$7E2980` party).

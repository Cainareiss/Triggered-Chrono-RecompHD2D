<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Prior art: what similar projects do, and what we can take from them

Research notes from 2026-11-13, written from web searches and a few fetched project
pages. Nothing here was built or run. **Status of the claims:** each project's license is
given as GitHub's license detector or the project's own page reported it on that day.
"Unverified" marks anything I could only find in a secondary source, or could not open.
Before copying any code, open the project's `LICENSE` file at the commit you copy from.

**Reuse rule.** Our code is GPL-3.0-or-later. Code under MIT, BSD, zlib, ISC, LGPL (with
care), GPL-2.0-or-later and GPL-3.0 can go in, with its notice kept (see
[GPL-REWRITE.md](GPL-REWRITE.md) for how notices are tracked). GPL-2.0-only is not
compatible. Proprietary, non-commercial (PolyForm Noncommercial, CC-NC), or *no license
at all* means the code is **idea only**: no license means all rights reserved. Texture
packs, models and other art are always separate works with their own terms, whatever the
renderer's license is.

Where this page says "ours", see [HD2D.md](HD2D.md) (renderer), [SCENE.md](SCENE.md)
(what the game keeps in memory, the co-op and split-screen gates) and
[MODULES.md](MODULES.md) (engine versus game module).

## Summary table

| Project | License | Code reusable? | Most useful for |
| --- | --- | --- | --- |
| 3dSen | Closed source (Unity) | No, ideas only | Per-game profile idea, voxel sprites |
| bsnes-hd | GPL-3.0 | Yes | HD mode 7, widescreen policy |
| Mesen / Mesen2 HD packs | GPL-3.0 | Yes | `hires.txt` pack format, pack builder |
| HDNes | Open source (license not checked) | Check first | Where HD packs started |
| Dolphin texture packs | GPL-2.0-or-later (file header seen) | Yes | Hash-named replacement, xxHash |
| RT64 | MIT | Yes | Texture-pack database, DDS, streaming, extended command set |
| N64Recomp | MIT | Yes | Recompiler patterns |
| N64ModernRuntime, Zelda64Recomp | GPL-3.0 | Yes | Mod format, hooks, `.rtz` packs |
| snesrev/zelda3, smw | MIT per README (GitHub: "other") | Yes | Widescreen on a recomp-like port, replay, RAM compare |
| RetroPortingToolKit/snesrecomp | **PolyForm Noncommercial 1.0.0** | **No, ideas only** | Adaptive view, `.snesmod` data-only mods |
| Ship of Harkinian (Shipwright) | **No license file found** | **No, ideas only** | Custom assets as archives |
| libultraship, Torch | MIT | Yes | Asset archive handling, if useful |
| Anchor (SoH co-op) | **No license detected** | **No, ideas only** | Relay-server netplay design |
| sm64coopdx, sm64ex-coop | **No license detected**; sits on the SM64 decomp | **No, ideas only** | Object ownership and sync design |
| Archipelago | Mixed ("NOASSERTION"; verify) | Verify | ROM patch plus memory-bridge design |
| xBRZ | GPL-3.0 | Yes | Pixel-art upscaling |
| HQx, xBR | LGPL-2.1(+) | Yes, with LGPL notice | Upscaling |
| MMPX | MIT | Yes | Upscaling designed for pixel art |
| ScaleFX | MIT (per file headers; check the file) | Yes | Upscaling |
| Sprite voxelizers (several) | MIT | Yes | Sprite to 3D extrusion |
| CT Multiplayer hack (RHDN 8337) | A patch, not code we can read | Idea only | Direct prior art for battle co-op |

## A. 2D-to-3D and HD rendering of retro games

### 3dSen (NES to 3D)

- **What:** an NES emulator that draws games as lit voxel dioramas, with a free camera.
  [3dSen](https://geodstudio.net/), [Steam page](https://store.steampowered.com/app/1147940/3dSen_PC/).
- **How (as reported by reviews, not by the author):** each game needs a hand-made
  *profile* that says how every sprite and background tile becomes a voxel shape and what
  its depth is. Profiles are `.3dn` files (protobuf). About 100 ship in the app, and a
  separate "3dSen Maker" lets fans write more. It is Unity/C#. Sources:
  [RetroBIOS](https://abdess.github.io/retrobios/emulators/3dsen/),
  [Insider Gaming](https://insider-gaming.com/the-3dsen-is-an-emulator-the-plays-nes-games-in-3d/).
- **License:** closed source. **Ideas only.**
- **For us:** it confirms that fully automatic 2D-to-3D lifting does not exist. They pay a
  per-game hand-authoring cost. Our "no per-game tables in the engine" rule
  (`tools/check_agnostic.py`) matches that: keep the *data* in `game/ct/` and the engine
  generic. A per-map "height hints" file is the equivalent of a 3dSen profile
  (SCENE.md A0.9 says the game has no height map, so hints will be needed).

### bsnes-hd

- **What:** a bsnes fork with HD mode 7 (supersampled, perspective-correct), widescreen
  (modes "none", "Mode 7", "all"), and 24-bit color options.
  [DerKoun/bsnes-hd](https://github.com/DerKoun/bsnes-hd). GPL-3.0 (GitHub detector).
  Upstream bsnes now lists HD mode 7 as a feature.
- **How:** re-renders mode 7 lines at a higher resolution inside the PPU rather than
  scaling the finished picture. Widescreen for "all" scenes shows artifacts, because the
  game never drew what is beyond the 256-pixel edge.
- **Reusable:** yes, GPL-3.0. Our Mode 7 note in HD2D.md says Mode 7 scenes get tilt-shift
  only; battle backgrounds and the world map in Chrono Trigger are partly mode 7
  (unverified for which exact scenes). **Idea:** read bsnes-hd's mode 7 sampling when we
  want a sharper mode 7, and note its widescreen artifacts are exactly what our
  "side fill" avoids by mirroring and dimming (HD2D.md step 9).
- **Caveat:** bsnes-hd is an emulator PPU. Our PPU is a modified MIT-licensed core, and a
  GPL-3.0 copy is fine to merge (the project is GPL-3.0-or-later), but it would block the
  "GPL-only rewrite" plan only if it came from a GPL-incompatible place. It does not.

### Mesen / Mesen2 HD packs, and HDNes

- **What:** tile-level texture replacement. A pack is a folder with `hires.txt` plus PNGs.
  [Mesen docs: HD packs](https://www.mesen.ca/docs/hdpacks.html),
  [Mesen2 repo](https://github.com/SourMesen/Mesen2) (GPL-3.0; the repo page says it is an
  archive near 2.1.1 and points at MesenCE for the latest).
- **How:** the emulator matches each 8x8 tile by a hash of its pixels and palette, and
  draws a 2x to 16x PNG tile instead. Tags: `<ver>`, `<scale>`, `<img>`, `<tile>`, with
  conditions, background images, `disableOriginalTiles`, and `.ips`/`.bps` patches and
  audio replacement. Origin: [HDNes](https://emulation.gametechwiki.com/index.php/HDNes)
  by mkwong98, later reimplemented in Mesen. Mesen includes an HD Pack Builder that
  records which tiles a game shows. Source for these details:
  [Emulation General Wiki](https://emulation.gametechwiki.com/index.php/HDNes) and the Mesen docs.
- **SNES:** I found only a community fork, [Beacher83/Mesen2-SNES-HD_Projekt](https://github.com/Beacher83/Mesen2-SNES-HD_Projekt),
  that adds SNES HD packs. Its maturity is **unverified**. Mainline Mesen2's SNES
  support for HD packs: **unverified** (the pages I read do not say).
- **License:** GPL-3.0. **The format is a spec we may implement; code may be copied.**
  Packs themselves are separate copyrighted art.
- **For us:**
  1. **Do not invent a pack format.** The SNES PPU gives us 8x8 (or 16x16) tiles from
     VRAM plus palette, exactly the keys `hires.txt` uses. Reading `hires.txt` (a subset)
     would make existing tooling, the pack builder idea and community habits transfer.
     The keying details matter: hash of tile bytes plus palette bytes, so one tile with
     two palettes (day and night) gets two entries.
  2. **Record, don't ship.** A "tile dump" mode (like Mesen's builder and RT64's
     developer mode) that writes the tiles the user's own ROM displays, to a local
     folder, is the legal way to start a pack: no game data in the repo.
  3. Sprite replacement in the SNES is different from NES: sprites are assembled from
     OAM pieces, and a character's frame is several pieces. Hash pieces, then offer a
     "group" tag. Mesen's NES packs do not need that (unverified for 16x16 and 32x32
     OBJ sizes). Done as `[[object]]` groups (a finding aid) and, with the game
     module's frame decoder, `[[pose]]` entries: a whole frame as one picture, keyed by
     the frame rather than its pieces ([ASSET-PACKS.md](ASSET-PACKS.md)). Mesen has no
     equivalent, and no 3D models; `[[model]]` (glTF, placed on map cells) is our own.

### Dolphin texture packs, and RT64

- **Dolphin:** [HiresTextures.cpp](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/HiresTextures.cpp)
  carries `GPL-2.0-or-later` in the header I saw, and uses xxHash. Textures are named by
  hash (`tex1_WxH_hash_fmt.png`-style names; exact scheme not checked) in a per-game
  folder. **Reusable** under GPL-2.0-or-later. Idea: hash-by-content naming makes packs
  survive a ROM re-dump.
- **RT64:** [github.com/rt64/rt64](https://github.com/rt64/rt64), MIT. Its
  [TEXTURE-PACKS.md](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md) says: a pack
  is `rt64.json` (a database mapping hash to file) plus DDS images (BC7 and mipmaps; PNG
  only for development); XXH3 hashing of TMEM bytes with Rice-hash compatibility; load
  modes *stream* (default, may pop in), *preload* and *stall*; a *low mip cache* for
  streaming; `.rtz` single-file packs (zstd); a developer mode that dumps textures and
  lets you replace one in-game. Memory use follows available VRAM. Hash-based
  replacement collisions are possible with the older Rice hashes.
- **RT64 renderer features** (per the repo and
  [Recompendium](https://recompendium.com/en/tool/rt64/)): arbitrary aspect ratios,
  interpolation to higher frame rates by decomposing transforms and interpolating each
  part, and an **extended command set**: a port or patch gives the renderer extra
  information so that widescreen and interpolation are correct for 2D elements. Game
  support for the last two is described as limited.
- **For us:**
  - Adopt the *shape* of RT64's pack: a JSON or TOML database keyed by content hash,
    DDS (BC7) for shipping, PNG for development, optional `.zip`/`.rtz`-like container,
    and a loud rule that PNG packs are for authors only. **Reusable code:** MIT, so
    the database loader and the `texture_packer` idea can be ported.
  - **Our upscaled placeholders** (the asset-pack track) should go through the same
    path, so they are just "a pack" and the user's hand-painted pack replaces them with
    no code change.
  - **Frame interpolation** (sprites at 60+ fps) is harder for us than for RT64, because
    the PPU draws a finished frame. The idea that transfers is the *extended command
    set*: the engine, not the renderer, knows an object moved, so the scene export
    (`ct_scene`, SCENE.md) is the place to carry per-object previous and current
    positions. Interpolation is **not** in our plan yet. Unverified whether it looks
    good on a 30 Hz-animated sprite game; many SNES games animate at lower rates than
    they move.

### snesrev zelda3 and smw (widescreen, extended view)

- **What:** a clean reimplementation of A Link to the Past (and Super Mario World) in C,
  with a LakeSnes-derived PPU. Widescreen is a setting (`ExtendedAspectRatio = 16:9`,
  also 16:10 or 18:9 in forks); a ROM is needed only to extract assets.
  [snesrev/zelda3](https://github.com/snesrev/zelda3), [snesrev/smw](https://github.com/snesrev/smw).
- **License:** MIT per the README and `LICENSE.txt` (GitHub's detector says "other"
  because the file also holds the Opus license). Yes, reusable.
- **How widescreen works there:** they *changed the game's own code*: the game keeps a
  larger visible region, loads tilemap columns past the edge, and the PPU is allowed a
  wider line. This is possible because the whole game is C they own and have named.
  (This is my reading of the project's approach; I did not read the code. Unverified.)
- **For us:** this is the clearest evidence for SCENE.md's split-screen option 1(a):
  widening what the game itself keeps active is cheap, but it changes game state, so
  it must be a flagged mode. Our setting is harder than theirs because our C is
  generated from the ROM, not named by hand: widening means hooks at known routine
  addresses (`Obj_UpdateVisibility` and the camera clamp in SCENE.md), each needing
  proof by the lockstep tests that off means identical.
- The `smw` README mentions two ideas worth copying for engine work: input-history
  snapshots that can be replayed in turbo mode, and runtime checks comparing emulated
  RAM against the reimplementation every frame. We already have both in spirit (replay
  scripts, lockstep, module parity). Nothing new to take, but it confirms the design.

### RetroPortingToolKit/snesrecomp (a peer SNES recompiler framework)

- **What:** a "game-agnostic SNES static recompiler ecosystem" with many game projects
  (Mega Man X, SMW, ALttP, Super Metroid, others) and features: adaptive widescreen,
  local and online co-op, rollback netplay, `.snesmod` mod packages, MSU-1.
  [RetroPortingToolKit/snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp).
- **License:** **PolyForm Noncommercial 1.0.0** (page text). That is not GPL
  compatible and not open source in the OSI sense. **Ideas only. Do not read its source
  while writing the equivalent**, to keep our work clean (the same rule as GPL-REWRITE.md).
  Maturity: it calls itself beta. Its claims of "co-op" and "rollback netplay" are
  **unverified**: I did not check any game project.
- **Ideas worth taking:**
  - **Data-only mod packages** (`.snesmod`: "never DLLs or arbitrary native code"), with
    features registered by plugin IDs compiled into the game, and the runtime verifying
    the stock ROM before it enables a mod. This fits our "module is built on the user's
    machine" model and keeps asset packs safe to share.
  - View width and pixel shape are separate settings (4:3, 8:7, square), which matches
    our F3 aspect cycle.
  - "Adaptive view": the view grows with the window, so background streaming and
    sprite visibility must grow with it. Same problem as our split screen.

## A2. HD-2D as a technique

Square Enix's engine for Octopath Traveler is Unreal Engine 4
([Unreal Engine spotlight](https://www.unrealengine.com/spotlights/octopath-traveler-s-hd-2d-art-style-and-story-make-for-a-jrpg-dream-come-true),
quoting Acquire's lead programmer, six programmers at peak). I could **not** find a
primary technical talk (GDC, CEDEC or Unreal presentation) with the details. What I found
is secondary:

- A DEV Community write-up, [Creating an HD-2D rendering pipeline on DX12](https://dev.to/gaurav_de/creating-an-hd-2d-rendering-pipeline-on-dx12-205k)
  (a third party's own pipeline, not Square Enix's): sprites are billboards in a
  deferred renderer, so a flat quad has a normal that always faces the camera and looks
  paper-thin; fix with tangent-space normal maps (made with Laigter) and an alpha-tested
  shadow pass so shadows follow the silhouette.
- A GitHub design issue,
  [Phaazoid/Godoiosis #176](https://github.com/Phaazoid/Godoiosis/issues/176), lists the
  common recipe: strong depth of field at the top and bottom (tilt-shift), bloom only on
  bright lights, light volumetric fog, grading and vignette, and "mixed pixel densities
  are the loudest amateur tell".
- [HD-2D on Wikipedia](https://en.wikipedia.org/wiki/HD-2D) and a gameplay guide say the
  same: billboarded sprites that cast real shadows, tilt-shift with a wide field of view.
  A Chinese roundup,
  [indie-guider.games](https://indie-guider.games/post/DevStudy-OCTOPATH-TRAVELER/),
  links a USgamer analysis and an Unreal Indie Dev Days 2019 talk, "Backbone: working
  with 2D pixel art in a 3D world" (I did not find or read either).

Treat all of this as **idea only**, and as consistent with what we already do.

- **"Mixed pixel densities"**: our diorama keeps everything at console resolution and
  then upscales (HD2D.md step 4 and 5), which avoids the tell. Keep it when sprites
  become 3D billboards: *render the billboard at the console pixel size* (snap its
  screen position and size to the source pixel grid), or a smooth-filtered sprite will
  look pasted on a pixel-art floor. This is the first thing to test in the 3D track.
- **Normals for sprites:** the same lighting as ours (relief normals from brightness)
  already avoids the "paper-thin" look at 2D. In real 3D with a perspective camera, use
  the two-source idea: a fake normal bulge, not a flat facing normal. Idea only.
- **Sprite shadows:** alpha-tested silhouette projected on the floor. We already
  fake it with a down-right soft shadow; in 3D, project the same alpha mask.

## A3. Upscalers (for the asset-pack placeholders)

| Filter | License (as found) | Notes for us |
| --- | --- | --- |
| [xBRZ](https://sourceforge.net/projects/xbrz/) | GPL-3.0 | Good for hand-drawn pixel art, integer scales 2x to 6x, edge-preserving. Reusable. One emulator project carries it under GPL with a MAME-style exception; ours does not need that. |
| HQx ([Wikipedia](https://en.wikipedia.org/wiki/Hqx_(algorithm))) | LGPL-2.1 (some ports "or later") | Reusable with notice. Older and blurrier. |
| xBR (Hyllian) | LGPL-2.1+ in some projects; libretro shader headers vary | Check each file. |
| MMPX | MIT (Morgan McGuire and Mara Gagiu; from a project license table and an issue, so verify) | Designed for pixel art and sprites; small kernel (a 2x filter), so cheap on a GPU. |
| ScaleFX (Sp00kyFox) | MIT per file headers in some ports; one issue first called it GPL-family, then corrected | Check the header of the exact shader. |

Source for the table: web search results listing these projects (an emulator's license
table, the xBRZ SourceForge page, Wikipedia, GitHub issues); I did not open every
header. **Recommendation:** use **MMPX or xBRZ** for the placeholder path. Their output
is *a guess*, so it must stay a **placeholder** that a real pack replaces, and it must
run on the tile dump (8x8 tiles with their palette), not on the whole frame, so tile
seams do not show. Image-model upscalers are a separate question (their output license
and model license vary); not covered here.

## A4. Sprite to 3D (voxel and extrusion)

Several MIT tools extrude sprites: [GazPrash/2d-to-3d-voxelizer](https://github.com/GazPrash/2d-to-3d-voxelizer)
(OBJ output, extrude by depth scale, multi-view), [CreggHancock/SpriteToVoxel](https://github.com/CreggHancock/SpriteToVoxel),
[jantepya/Unity-Sprite-Voxelizer](https://github.com/jantepya/Unity-Sprite-Voxelizer),
[Yatchanek/VoxelSpriteCreator](https://github.com/Yatchanek/VoxelSpriteCreator) (Godot),
and `spriteloom` (extrude, inflate or carve to `.vox`). They are small, Unity or Godot
oriented, and the algorithm is a few dozen lines: one voxel per opaque pixel, extruded
by a depth. **We should not import any of them.** What to take is the *method*, in the
same spirit as 3dSen: an optional "thickened sprite" mode (extrude each sprite piece by
one to three voxels, shade the sides darker) that gives a free "3D character" look
without glTF models. Quality warning from their own notes: extrusion suits blocky
art, and fails on thin or anti-aliased shapes.

## B. Co-op in single-player retro games

### Games that already did it

- **Final Fantasy VI (SNES).** In the original SNES version the Config menu has a
  *Controller: Single / Multiple* option: with Multiple, characters are assigned to
  controller 1 or 2, so a second player controls some party members **in battle only**;
  player 1 keeps the field, dialogue and menus. Reports: a blitz input fails if the other
  player presses buttons during it; the feature is absent in the mobile and PC ports.
  Sources: [Steam discussion](https://steamcommunity.com/app/382900/discussions/0/487877107149390292/),
  [FF wiki: Multiplayer](https://finalfantasy.fandom.com/wiki/Multiplayer),
  [FF6 Hacking: FF6 Multitap](https://www.ff6hacking.com/forums/thread-4115.html)
  (a fan patch that turns the option into Players 1/2/3/4). Sources are forum posts, so
  the exact assignment menu is **unverified**. **This is the direct model for our B1:**
  battle-only, per-character assignment, one pad owns the field.
  Idea: when player 2's controller is absent, characters fall back to player 1 (the
  CT hack below does this too). A character that is *waiting for input from a pad that
  is not plugged in* must never stall the ATB.
- **Chrono Trigger itself.** [Chrono Trigger: Multiplayer](https://www.romhacking.net/hacks/8337/)
  (by Colmines92, v1.2, February 2024, patch for the headerless USA ROM): up to three
  players; with one controller connected player 1 controls everything; extra controllers
  control the character in their slot during combat; unplugging a controller hands the
  character back to controller 1. (From a search summary; the page itself returned a 403
  to my fetch, so **unverified** in detail, and I do not know how it works inside.)
  It is a ROM patch, so there is nothing to reuse, and it changes the ROM, which we never
  ship. **Use it as a behavior spec for B1** and **test our B1 against it** (same
  fallback rule). Players report it as local-only; no online CT co-op was found.
- **Secret of Mana (SNES).** Up to three players with a multitap; AI controls the
  absent ones. The original camera keeps the party on screen; the 2018 remake instead
  follows the active character and has a clamped party, and its reviews note that in co-op
  players can get stuck behind scenery when one runs ahead
  ([Mana wiki](https://mana.fandom.com/wiki/Secret_of_Mana),
  [Nova Crystallis](https://novacrystallis.com/2018/02/secret-mana-remake-review/)).
  **I could not find how the original keeps everyone on screen** (a tether on distance
  to the camera center, or a bounding-box camera). **Unverified.** It is the right
  thing to check in a Mana disassembly (Data Crystal) before building B2: the proven
  answer to "one screen, several players" is a **tether**, not a split.
- **Co-op for field play in turn-based RPGs** barely exists on SNES. Games that allow
  two players to move separate characters in the field (Secret of Mana, Seiken Densetsu 3,
  Zelda-style) were designed for it; CT was not. That supports SCENE.md's verdict
  "partial", and suggests **tether first, split screen second**.

### Decomp-based netplay (sm64coopdx, Anchor)

- **sm64coopdx / sm64ex-coop** ([sm64coopdx](https://github.com/coop-deluxe/sm64coopdx),
  [sm64ex-coop](https://github.com/djoslin0/sm64ex-coop)): client-server, up to 32
  players, sync at three levels: player state, game objects ("sync objects" with ids and
  a nearest-player ownership rule), and Lua sync tables. Static level objects were first
  assumed identical on every client; PR
  [#1443](https://github.com/coop-deluxe/sm64coopdx/pull/1443) shows that assumption
  broke when a static object changed state. Known desyncs are listed in
  [issue #804](https://github.com/coop-deluxe/sm64coopdx/issues/804). Camera: **I found
  nothing**, and the Lua hook docs suggest it is simply local per client. Lua mods hook
  events (`HOOK_ON_SET_CAMERA_MODE`). **License: GitHub's detector found none for either
  repo**, and they are derivatives of the SM64 decomp. **Idea only.**
- **Ship of Harkinian "Anchor"** ([garrettjoecox/anchor](https://github.com/garrettjoecox/anchor),
  client in `soh/soh/Network/Anchor` of [Shipwright](https://github.com/HarbourMasters/Shipwright)):
  each player runs their own game; a generic relay server broadcasts packets to the room
  or to a team or target; syncs *save state, flags, items, room clears, player pose*.
  Players fight their own copies of enemies (no enemy sync). Forks add UDP for real-time
  packets and "host-authority" enemy sync ([full-sync test](https://github.com/Nahoy93/Anchor-full-sync-test),
  [OOT True Co-op](https://github.com/bghill95/OOT-True-Co-op)). **License: none
  detected** for the server and Shipwright. **Idea only.**
- **Lessons (idea only):**
  1. **Do not try to sync a full game state over the network** for the first version.
     Both projects show how large the object-desync surface is. Anchor's "sync only
     progress flags and let each client simulate" is cheap and sufficient for a game
     where combat is turn-based.
  2. Our determinism work (frame hashes, lockstep, replay) is a better base than either
     project had: **input-only (lockstep or rollback) netplay** is plausible because
     the engine is deterministic with a fixed input stream (the TAS replay proves
     this). That is the approach of emulator netplay, not of decomps. Not in our plan;
     local co-op first.
  3. Ownership by "nearest player" is a pattern for a *local* split screen too: who
     owns an object that both views can see.

### Memory-patching co-op: Archipelago and multiworld

- **What:** [Archipelago](https://github.com/ArchipelagoMW/Archipelago) (ALttP, Super
  Metroid, SMZ3). A ROM is patched at generation time (bsdiff4 base patch, direct writes,
  ASM injection; the player gets a small `.aplttp` or `.apsm` file and builds their own
  ROM from it), and at play time a client reads and writes emulator memory through SNI
  and a Lua connector, polling a few WRAM addresses and exchanging items through a
  reserved receive region
  ([ROM patching guide](https://deepwiki.com/ArchipelagoMW/Archipelago/7.4-rom-patching-guide),
  [SNI](https://deepwiki.com/ArchipelagoMW/Archipelago/5.2-sni-and-console-integration),
  [SM setup](https://archipelago.gg/tutorial/Super%20Metroid/multiworld_en)). License:
  the GitHub detector reports "NOASSERTION"; **verify before copying**.
- **For us:** two things. (1) The **"ship a patch, not a ROM"** model is the same as our
  rule that nothing derived from the game leaves the machine; we already do that with
  `ct_game.so`. (2) The WRAM-bridge idea (a small, documented set of addresses
  that an external program reads and writes, with a buffered batch write) is the right
  shape for a **`ct_scene`-style export** and for an external tool or a co-op relay,
  because the module already owns memory. If we add scripted mods, copy their rule:
  data in, never code in.

### Cameras, second views, and "events trigger only from the leader"

- **Split-screen in emulation and recomp projects:** I did not find any open project that
  renders a *second camera of an unmodified 2D console game*. Dolphin and others can run
  two instances. snesrecomp's authors advertise adaptive view but not split screen. Treat
  the split-screen prototype as **new ground**; the closest analog is the widescreen work
  (widen what the game keeps active). The sm64coopdx camera is local to each client,
  which is not the same problem (SM64 is 3D with one world; a second screen is the same
  world drawn twice).
- **"Only the leader triggers events":** every project I found solves it the same way:
  there is a *tether* or *merge* rule, not event logic for two actors. Secret of Mana
  (as far as the sources say) keeps everyone on one screen; the remake pulls followers to
  the leader on a new screen and when the ring menu opens. That is the same as the
  SCENE.md B0 proposal: when the leader changes map, starts an event or a battle, snap
  the second character back to the follower ring and merge the views. Nothing found
  contradicts it.

## C. Recompilation peers: engineering lessons

| Project | License | Lesson |
| --- | --- | --- |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | MIT | Symbol list plus per-function C output; a recompiler that is separate from the runtime. |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) | GPL-3.0 | Mod system, see below. |
| [RT64](https://github.com/rt64/rt64) | MIT | Renderer as a library with an extended command set. |
| snesrev [zelda3](https://github.com/snesrev/zelda3), [smw](https://github.com/snesrev/smw) | MIT | Hand-named reimplementation, replay and RAM-compare tests. |
| [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) | PolyForm Noncommercial | Same problem space as ours; ideas only. |
| [libultraship](https://github.com/Kenix3/libultraship), [Torch](https://github.com/HarbourMasters/Torch) | MIT | Asset-archive and extraction tooling. |

- **Mods in Zelda64Recomp:** from version 1.2, `.nrm` files (code patches and new code,
  with a manifest) and `.rtz` texture packs, dropped on the window or installed from a
  menu ([PR #499](https://github.com/Zelda64Recomp/Zelda64Recomp/pull/499)). Hooks and
  callbacks run in the load order of the mod menu, with return hooks in reverse order;
  optional dependencies; exports. Because N64ModernRuntime is GPL-3.0, the whole
  program is GPL-3.0. **Reusable (GPL-3.0)**: the manifest and ordering design, and the
  runtime pieces if we ever want native mods. Our situation differs in one important
  way: the *game code is never in the engine* (MODULES.md), so a mod has to hook the
  **module** through the function table (`ct_tables_cur`), not patch the engine. That
  gives us a natural hook point: a mod can wrap a function entry in the table.
  **Safety:** native-code mods run with full privileges. Data-only (snesrecomp's rule)
  is safer for packs; keep native mods to something the user builds themselves.
- **Where we are ahead of the peers (from what I read):** a game module that is built
  on the user's machine and never distributed; a game-agnostic engine with a lint
  (`check_agnostic.py`); frame, state and audio hash parity between the interpreter, the
  static build and the module; a PPU that outputs layer data. I did not find the same
  rigor in the READMEs, but I did not audit the projects, so do not read this as a claim
  about their quality.
- **Game Boy and other SNES recomps:** my search found **no Game Boy static recompiler**.
  The SNES results were the snesrecomp ecosystem and forks of it. Nothing else turned
  up, so "other SNES/GB recomps" is **unverified** beyond that.

## What we should change in our plan

Marked **[code]** if there is code under a compatible license we can use, or **[idea]**
if it is ideas or a format only (no license, non-commercial, closed, or no code exists).
Ordered by value for effort.

### Asset packs

1. **[code + idea] Read Mesen-style `hires.txt` packs (a subset) and add a tile dump mode.**
   Format and builder are GPL-3.0 (Mesen); the SNES PPU already has the tile and palette
   keys. Start with background tiles (static, many), then OBJ pieces. Never write dumps
   into the repo. *Verify first* that mainline Mesen2 has SNES HD packs; if not, we are
   defining the SNES dialect ourselves (the community fork is a hint). Track:
   asset packs.
2. **[code] Pack storage like RT64's:** a hash database, DDS (BC7) for shipping, PNG for
   authors, a single-file container, and stream or preload modes. MIT, so loader and
   packer ideas can be ported. Do this before the upscaled placeholders so they use the
   same path. Track: asset packs.
3. **[code] Placeholder upscaling with MMPX or xBRZ,** applied to the tile dump and
   cached, not per frame, and always replaceable. Check the license header of the exact
   file used. Track: asset packs.
4. **[idea] Data-only pack manifests** (snesrecomp's rule), so sharing a pack can never
   run code. Track: asset packs.

### 3D renderer

5. **[idea] Snap billboards to the console pixel grid** and light sprites with a fake
   normal. First test in the 3D prototype, because mixed pixel density is the commonly
   named failure of amateur HD-2D. Track: 3D renderer.
6. **[idea] Hand-authored per-map hints (heights, walls) in `game/ct/`,** as 3dSen does
   with profiles, instead of trying to infer a height map for every map. SCENE.md A0.9
   already says the game has none. Keep the engine generic. Track: 3D renderer.
7. **[idea] Optional extruded-sprite mode** (a few voxels deep, darker sides) before
   investing in glTF models. Track: 3D renderer.
8. **[code] Look at bsnes-hd's mode 7 sampling** (GPL-3.0) for the world map and battle
   backgrounds, if those scenes use mode 7 (check in our own PPU state dump).
   Track: 3D renderer.

### Co-op

9. **[idea] Use the existing CT Multiplayer hack and FF6's Single/Multiple option as
   behavior specs for B1:** battle-only, per-character assignment, absent pad hands the
   character back to player 1 (no stall). Write tests for the fallback. Track: co-op.
10. **[idea] Tether before split.** Every comparable project keeps one world and one
    leader; make the "snap back and merge on exit, event or battle" rule (SCENE.md B0.3)
    part of B2, not a split-screen extra. Before that, look up how Secret of Mana's
    original camera keeps three players on screen (unverified; read a disassembly).
    Track: co-op.
11. **[idea] Skip online co-op for now, and when it comes, do input-only lockstep** from
    our deterministic replay, not object sync. The decomp projects show that
    object sync is the expensive, desync-prone road; their code is unlicensed anyway.
    Track: co-op.

### Split screen

12. **[idea] There is no open prior art for a second camera on an unmodified 2D game.**
    Plan for the prototype to be research. Of the two sprite options in SCENE.md B0,
    **start with 1(a), widening the activation window** (as the zelda3 widescreen work
    widens what the game keeps active), flagged as "no longer matches the original",
    and put 1(b) (a module-side sprite rasterizer) on the long list. Add a regression
    check that with the flag off the frame hashes are unchanged. Track: split screen.
13. **[idea] Treat "nearest player owns the object" as the rule** when both views can
    see the same object (from sm64coopdx's ownership design). Track: split screen.

### Engine

14. **[idea + GPL-3.0 code] A mod hook point at the function table.** Zelda64Recomp's
    manifest, ordering and optional dependency design is GPL-3.0. Our hook is the
    `ct_tables_cur` entry. Do not start before the GPL-only rewrite settles the table
    layout, and keep mods data-only by default. Track: engine.
15. **[idea] A small, documented WRAM export (`ct_scene`)** in the style of Archipelago's
    SNI memory bridge: a fixed set of addresses and a read/write batch. Already planned;
    this says to keep it narrow and versioned. Track: engine.
16. **[idea] Frame interpolation** needs per-object previous and current positions in
    `ct_scene`; RT64's "extended command set" is the model. Add the fields now (cheap),
    decide on interpolation later. Track: engine and 3D renderer.
17. **Do not read or copy snesrecomp's source.** Its license is non-commercial and it
    solves our problem; reading it taints the GPL-only rewrite. Track: all.

### What I could not establish

- A primary source (talk or paper) for how Octopath Traveler keeps sprites in 3D.
- How the original Secret of Mana's camera keeps three players on screen.
- How the CT Multiplayer hack works internally, and the exact FF6 config options.
- Whether mainline Mesen2 supports SNES HD packs.
- The licenses of the decomp co-op projects beyond "none detected by GitHub"; the
  decomp projects they build on have their own terms.
- Any Game Boy recompiler.

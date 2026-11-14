<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# HD-2D renderer

An optional way of presenting the game. The picture is drawn as a lit diorama, with
depth of field, bloom, soft shadows under characters and a filmic color grade, in the
style of modern "HD-2D" releases.

**It draws nothing of its own.** Every pixel comes from the running game's own frame,
which is generated from your ROM. The renderer only changes how that frame is lit,
blurred and graded, so the project ships no game art.

## Using it

```
CT_ROM=rom/your_dump.sfc cmake -S . -B build && cmake --build build -j
build/ct_sdl --hd2d                 # window
build/ct_sdl --hd2d --fullscreen    # or press F11; automatic under gamescope
```

| Key | Action |
| --- | --- |
| F1 | Cycle the preset: `off`, `subtle`, `hd2d` (default), `deck`, `hd2d3d` (the 3D diorama, below) |
| F2 | Cycle the debug view: layer classes, depth, blur amount, split compare (original pixels on the left), background plane, off |
| F3 | Cycle the aspect: 4:3 (default), 8:7 pixel aspect, stretch |
| F11 | Toggle fullscreen |
| R3 / L3 (controller) | Same as F1 / F3, for handhelds without function keys |

Each change shows a short on-screen message. Preset and aspect are saved to
`~/.local/share/ct-recomp/hd2d/settings.ini` (SDL's per-user preferences folder), and
command-line options override them.

| Option | Effect |
| --- | --- |
| `--preset off\|subtle\|hd2d\|deck\|hd2d3d` | Choose the starting preset |
| `--aspect 4:3\|pixel\|stretch` | Choose the picture shape |
| `--debug-view N` | Start in debug view N (0 off, 1 classes, 2 depth, 3 blur, 4 split compare, 5 background plane) |
| `--log-gpu` | Print the renderer's frame time every 60 frames (stalls the GPU to measure) |
| `--shot F:PATH` | At frame F, render offscreen and save a PNG (repeatable) |
| `--shot-size WxH` | Size of those screenshots (default 1920x1080) |

To see where the time goes, set `CT_HD2D_PROFILE=FILE`. Every frame then appends a line
to FILE with the GPU time of each pass, from timestamp queries read back a few frames
later so they don't stall anything, plus the CPU time of the scene analysis and the
uploads, and with `--log-gpu` the frame's total. It needs timer queries: OpenGL 3.3, or
OpenGL ES with `EXT_disjoint_timer_query`.

To render headlessly, for example on CI or over SSH, set `SDL_VIDEODRIVER=offscreen`.
That gives a GPU context through EGL with no window:

```
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy build/ct_sdl --hd2d --fast \
    --frames 9200 --script game/ct/test/replay/leene_square.txt --shot 9200:leene.png
```

## How it works

1. **Layer G-buffer.** The PPU (`third_party/snes/ppu.c`) writes a second buffer next to
   the color frame. It holds one byte per pixel, saying which layer won that pixel and at
   what priority: `z << 4 | math << 3 | layer`. The scheduler presents it with the frame
   (`sched_layers()`), along with a snapshot of PPU state (`sched_ppu_state_get()`). This
   doesn't change normal output: frame and state hashes match the unmodified PPU on all
   9,200 frames of the Leene Square replay.

   **Behind buffer.** Optionally (`PpuSetBehindBuffer`, `sched_set_behind()`), the PPU
   also records, per pixel, the *runner-up*: the best layer that lost to the winner
   there, or the backdrop if none did. It stores that layer's color (B G R x, brightness
   applied, no color math) and its layer byte (`z << 4 | layer`). The scheduler presents
   them with the frame (`sched_behind()`, `sched_behind_layers()`), and returns NULL for
   frames drawn without them. The per-pixel renderer finds the runner-up by continuing
   down the mode's priority list past the winner. The whole-line renderer keeps a
   second z buffer: a beaten winner moves into it, and a loser that beats it replaces
   it. Its draw functions come in two compiled versions, so with the buffer off nothing
   extra runs. `test_snes_bus` checks that both renderers give the same runner-ups. The
   frontend turns the buffer on only for the presets that use it (subtle and HD-2D).
   Hashes stay identical with it on or off.
2. **Classification** (`shaders/light.frag`). The layer byte sorts each pixel into one of
   six classes:
   - *backdrop*
   - *far planes*: low BG3/BG4
   - *ground*: low-priority BG1/BG2
   - *foreground*: high-priority BG1/BG2, such as arches, roofs and canopies
   - *actors*: sprites
   - *UI*: BG3 at the top priority level in mode 1, such as text boxes, menus and the
     battle HUD

   UI pixels skip every effect and stay sharp.
3. **Point lights** (`shaders/emit.frag`), at 64×56:
   - Pixels that are bright, saturated, and clearly brighter than their surroundings
     (flames, lamps, windows, glowing magic) become light sources.
   - The light map is blurred twice and lights the surfaces around each source.
   - Its strength follows the scene's darkness: full in interiors and at night, a
     quarter in daylight.
   - UI is never a light source.
4. **Lighting at console resolution.** Pixel-scale lighting suits pixel art, so this pass
   runs at 256×224:
   - Relief normals from brightness give Lambert lighting from one warm key light.
   - Actor silhouettes cast soft shadows down and to the right.
   - Ground darkens where it meets foreground and actors (contact occlusion).
   - Actor edges facing the light get a rim light.
   - Distance haze is applied to the far planes.
   The pass also writes a *background plane*. It is the same picture, except that under
   foreground pixels it holds the behind buffer's pixel, lit the same way (shadows,
   point lights, haze; no relief). Where only the backdrop is behind a foreground pixel,
   nothing was drawn there, so the foreground stands in for it.
5. **Sharp-bilinear upscale** to the output size. This pass splits the picture into the
   background plane and a foreground plane (foreground color and coverage). With the
   **diorama tilt** (`tilt`: off in Subtle, 0.022 in HD-2D), the foreground is drawn
   about 2% larger around the focus point. Roofs, arches and canopies then shift a few
   pixels away from the focus, about 3 source pixels at the picture's edges, as tall
   things near the camera do in a tilted view. The pixels they move off show the
   background plane. The enlargement maps the picture into itself, so the edges never
   need filling. The foreground never covers UI or a pixel that was in front of it,
   such as a high-priority sprite. Where nothing is behind it, it stays where it was.
   The upscale is not a pass of its own. Storing two picture-size half-float planes and
   reading them back cost more than working them out again from the small console-size
   targets, so `shaders/upscale.frag` is compiled into the passes that read the picture
   (blur amount, bloom source, final). They evaluate it where they used to sample the
   stored planes.
6. **Tilt-shift depth of field** at half resolution, done separately for the two planes
   and then composited (foreground over background):
   - The background plane holds the hidden pixels under the foreground, so blurring it
     never smears an in-focus arch or roof into the blurred ground around it.
   - The foreground plane is blurred as color plus coverage, so a blurred roof's edge
     fades out over the sharp ground behind it instead of ending in a hard cut.
   - A sharp band follows the actors' vertical center, and blur grows above and below it.
   - Planes away from the focal depth also blur.
   - Each sample is weighted by its own blur amount, so in-focus pixels don't bleed halos.
   - Actors stay mostly sharp wherever they stand.
   - Small blurs take fewer samples (about 1.75 texels apart, up to 15).
   - The effect scales with how much of the screen is ground or foreground, so title
     cards and black-backed screens keep it off.
7. **Bloom**, from two Gaussian levels at 1/4 and 1/8 resolution. One pass makes both the
   bloom source and the plain copy that the side fill starts from.
8. **Grade:** exposure and a soft highlight shoulder (identity below 0.7, so blacks stay black), saturation, split toning (cool shadows, warm
   highlights), a vignette and light grain.
9. **Side fill:** outside the 4:3 picture, a blurred, mirrored and dimmed reflection of
   its edges. Skipped when the picture covers the whole output.

Menus that cover more than 15–30% of the screen with UI ease the scene effects off.

## The 3D diorama (`hd2d3d`)

The `hd2d3d` preset rebuilds map views as real geometry, in the manner of the HD-2D
remakes: floors, walls that rise, roofs on top of them, characters standing up as
billboards, a perspective camera tilted back from straight down, a key light with a
shadow map, and the depth of field driven by real depth. Everything else (battles,
menus, the world map, mode 7, title and transition screens) is the `hd2d` picture,
pixel for pixel, with a short crossfade (8 frames) between the two.

**It still draws nothing of its own.** The geometry comes from the game's map as the
running game holds it in WRAM, and every surface is textured with the console's own
tile pixels, decoded from VRAM and CGRAM as they are drawn.

### Where the scene comes from

The renderer is game-agnostic: it never reads game memory. The game module describes
what is on screen through an optional export, `ct_module_info.scene`
(`runtime/module.h`, `ct_scene`): the map's size, its tile layers as BG tilemap words
(tile, palette, priority, flips) with their VRAM character base, a class per cell
(floor, solid, wall-ish, water, stairs, unknown) with flags (overhead: drawn over
sprites; event), the objects with their foot point and the OAM entries that draw them,
the player's object, the camera in map pixels, a map generation counter and a mode
(map view or other). The engine adds the console's state (`ct_scene_capture`: VRAM,
CGRAM, OAM, the OBJ size and base, brightness). A module with no scene export, or a
mode other than `CT_SCENE_FIELD`, gets the 2D picture.

For this game, `game/ct/scene.c` fills it from the addresses in [SCENE.md](SCENE.md). A
frame is a map view while the field loop's NMI handler is installed (`$0500`); battles,
menus, the world map and the title install their own. The generation counter changes when
the map grids, property bytes or metatile tables hash differently, which also catches
tiles changed in place. The `scene_decode` test checks the description against an
independent decoding of the same WRAM and OAM (`scene_probe.py --text`).

### Heights

The game keeps no heights, so `frontend/hd2d/scene3d.c` infers them, per map column,
from the cells (all of them generic: a cell's class and its tiles' priority bits):

1. A cell is *raised* if it is blocked (solid or wall-ish) or has overhead tiles, and has
   pixels at all (the empty void around a map is nothing).
2. Each run of raised cells in a column, seen from the south, stands up from its south
   edge as a wall facing the camera: up to `hmax` cells (3, 48 px) high, fewer for a
   shorter run. The rest of the run lies flat on top of the wall, moved south by the
   wall's height. A tile's screen row is then `z - height`, where the original's
   three-quarter view drew it, so the art keeps its layout.
3. A run whose blocked cells go deeper than its wall is a box: side walls close it where
   the neighbouring columns differ, textured with the wall's own tiles. Shorter runs
   (posts, trunks, canopies over a trunk, arches) stay cut-outs, and see-through where
   the tiles are.
4. Overhead tiles over walkable cells belong to the blocked run north of them (a
   canopy over its trunk, a roof's eaves); in such a cell only the overhead tiles rise,
   the ground under them stays a floor. A long run of overhead tiles with nothing
   blocked in it (railings, edges drawn over sprites) stays on the floor.
5. Floor a wall leaves uncovered behind it is filled with the ground of the cell north
   of its run.
6. Sprites are billboards: each visible OAM entry, standing at its object's foot point
   on the floor, made taller by `cos(tilt) / sin(tilt)` (at most 1.4) so it keeps its
   drawn proportions under the tilt. An object standing in a raised cell whose priority
   puts it over that cell's tiles in the original (priority 3, or tiles that aren't
   overhead) stands in front of the wall, or on top of it, at the same screen row.
   Entries no object owns (effects, markers) stand at their own bottom edge.

Where these are wrong:

- Height is a guess. Walls are at most `hmax` cells; a tall building shows its upper
  facade lying on the roof. Ground-coloured blocked cells (invisible walls at the edge of
  a path) rise as low walls of ground.
- The walk planes (bridges, balconies: the cells' `plane`) and stairs are flat. The
  game's two walk levels aren't separated, so an upper floor is at floor level behind
  the wall that should hold it up.
- Shaped cells (wall-ish: corners, diagonals) count as blocked, so diagonal walls are
  stepped.
- BG3 (in map views mostly fog, light shafts and other color-math overlays) and color
  math in general aren't drawn in 3D; the UI from BG3's top level is laid flat over the
  picture as in the 2D path. Translucent or tinted sprites are opaque.
- Sprites are flat cut-outs; one that the original split between priorities (half
  behind a roof edge) is drawn whole at its foot point.
- The camera sees further and wider at the top of the picture than the console's, so
  it stops sooner at the map's edges, keeping the floor at the picture's edges on the
  map; it then doesn't follow the player all the way to an edge. A map smaller than the
  view is centered, on a dark ground.

The camera can be changed with `CT_HD2D_3D`, a comma-separated list:
`tilt=40` (degrees from straight down), `fov=30` (vertical field of view), `zoom=0.75`
(1: the console's 256 px across at the focus), `hmax=3` (wall cap in cells) and
`stretch=1.2` (billboard height scale; default from the tilt).

### Drawing

1. The map's quads (one per 8x8 tile of BG2 and BG1: floor ones first) are rebuilt when
   the generation changes, or when the set of empty tiles in VRAM does (a map whose
   tiles were still loading). The sprites' quads are rebuilt every frame. VRAM (as a
   16-bit texture) and CGRAM are uploaded every frame, so animated tiles and palettes
   show as the console changes them.
2. A 1024×1024 shadow map from the key light, low from the south-west, of the raised
   geometry and the sprites (cut-outs tested per pixel).
3. The surfaces into a picture-size target with a depth buffer: each pixel decodes its
   tile's bitplanes and palette, is lit relative to the sunlit floor (which keeps the
   original colors), shadowed with 4 PCF taps, lit by the 2D path's point-light map at
   the screen position the console drew it, hazed with distance and scaled by the
   console's brightness. Alpha holds the depth for the depth of field (0.5 at the focus).
4. The UI from the console frame on top, flat and sharp.
5. The 2D path's depth of field, bloom, grade and side fill, compiled again with
   `scene3d_pic.frag` in place of the upscale: one plane, real depth. The tilt-shift band
   is turned down to a quarter, as depth does most of the blur.

The `deck` preset has no 3D. A GPU or driver that cannot build the 3D programs keeps
`hd2d3d` on the 2D path (the log says so).

Two hook points are left for asset packs: the cell hook (`s3d_set_cell_hook` in
`scene3d.h`), where a pack can replace a cell's generated geometry with its own, and the
tile lookup (`tileTexel()` in `shaders/scene3d.frag`), the one function every 3D
surface samples its texture through, keyed by the tile (VRAM address, which gives the
tile id, and palette), where a pack can return its own texture.

## Performance

Measured on a ROG Ally (Ryzen Z1 Extreme, Radeon 780M, Mesa radeonsi) at 1706×1120
(`--scale 5`, about 1080p's pixel count), over the Leene Square replay at normal speed,
with `--log-gpu`:

| | Median | 95th percentile |
| --- | --- | --- |
| Before the upscale was folded into the passes that read it | 2.6–4.4 ms | 4.1–5.3 ms |
| Now | 1.4–2.3 ms | 1.9–3.2 ms |

These times include the uploads and the CPU-side scene analysis (about 0.03 ms). Each range
covers 3 to 6 interleaved runs, made while other work loaded the machine (load average
3–18). The variation between runs is larger than a few tenths of a millisecond: the
handheld's clocks follow the load, and the 95th percentile follows the GPU clock. The
quietest runs gave 2.6 / 4.1–4.5 ms before and 1.4 / 1.9–2.6 ms now.

On the GPU, from `CT_HD2D_PROFILE`, the frame went from about 2.2 ms to 0.9–1.4 ms. Most of
the gain is that the two picture-size half-float planes (about 27 MB written and read back
per frame) are gone. The depth-of-field blur is now the largest pass (0.3–0.5 ms), then the
final pass (0.25–0.3 ms). Desktop AMD and NVIDIA GPUs have a large margin.

The 3D diorama, from `CT_HD2D_PROFILE` on the same Ally at `--scale 5` (a 1493×1120
picture), over the Leene Square and bell plaza frames of the replay (8300–9200, unpaced,
while other work loaded the machine): a median of 2.4 ms of GPU time per frame, against
1.2 ms for `hd2d` in the same run. The surfaces pass is the largest (about 0.96 ms: every
pixel decodes its tile from VRAM and takes four shadow taps), then the final pass
(0.39 ms), the depth of field (0.35 ms) and its blur amount (0.28 ms); the shadow map
costs 0.05 ms. The CPU side (scene capture, sprite quads, uploads including VRAM) is about
0.18 ms; a map's mesh rebuild, on a map change, a few milliseconds once. Both presets
showed 95th percentiles of 15–19 ms in that run: the GPU's clock drops while other work
loads the machine, so those are the machine's, not the passes'.

The behind buffer costs CPU time in the PPU. Headless (`ct_boot`, 9,200 frames of the
Leene Square replay), the game runs at about 388 frames per second without it and
about 314 with it (`--behind`). That is about 0.6 ms more per frame, so it is only on
while a preset uses it.

## Limits and next steps

- The behind buffer holds one hidden layer, not all of them. A pixel with two layers
  stacked under the foreground shows only the upper one when revealed. Sprites hide
  other sprites at the same pixel before the PPU sees them, so a sprite is never the
  runner-up of another sprite.
- The tilt moves whole classes. An object drawn partly with high-priority tiles and
  partly with low-priority ones (such as a tent whose top is in front of the actors and
  whose bottom is not) splits at that line by the tilt's few pixels. Pixels changed by
  color math (translucent clouds, for example) carry the blended color with them.
- Only the per-pixel PPU path runs in the game. The whole-line renderer's behind
  tracking is covered by the unit test only. Its 4x4 mode 7 path doesn't write the
  buffer.
- Classification uses PPU state only, with no per-game tables, so it stays game-agnostic
  (`tools/check_agnostic.py`). Some scenes will misclassify. Mode 7 scenes get
  tilt-shift only.
- The renderer works under OpenGL ES 3.0 for Android, but that hasn't been tested on a
  device.

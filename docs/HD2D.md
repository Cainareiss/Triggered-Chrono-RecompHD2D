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
| F1 | Cycle the preset: `off`, `subtle`, `hd2d` (default) |
| F2 | Cycle the debug view: layer classes, depth, blur amount, split compare (original pixels on the left), background plane, off |
| F3 | Cycle the aspect: 4:3 (default), 8:7 pixel aspect, stretch |
| F11 | Toggle fullscreen |
| R3 / L3 (controller) | Same as F1 / F3, for handhelds without function keys |

Each change shows a short on-screen message. Preset and aspect are saved to
`~/.local/share/ct-recomp/hd2d/settings.ini` (SDL's per-user preferences folder), and
command-line options override them.

| Option | Effect |
| --- | --- |
| `--preset off\|subtle\|hd2d` | Choose the starting preset |
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
   - The effect scales with how much of the screen is ground or foreground, so title
     cards and black-backed screens keep it off.
7. **Bloom**, from two Gaussian levels at 1/4 and 1/8 resolution.
8. **Grade:** exposure and a soft highlight shoulder (identity below 0.7, so blacks stay black), saturation, split toning (cool shadows, warm
   highlights), a vignette and light grain.
9. **Side fill:** outside the 4:3 picture, a blurred, mirrored and dimmed reflection of
   its edges.

Menus that cover more than 15–30% of the screen with UI ease the scene effects off.

## Performance

On a ROG Ally (Ryzen Z1 Extreme, Radeon 780M, Mesa radeonsi), the renderer takes
about 2.6 ms per frame at 1706×1120 (`--scale 5`, about 1080p's pixel count), median over
the Leene Square replay at normal speed; 4.3 ms at the 95th percentile. Without the two
planes and the tilt it was 2.4 ms (3.9 ms). The times include the uploads and the CPU-side
scene analysis, and vary by a few tenths of a millisecond between runs. Desktop AMD and
NVIDIA GPUs have a large margin.

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

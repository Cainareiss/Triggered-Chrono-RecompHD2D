<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Asset packs

An asset pack replaces the game's art, one 8x8 background tile or one sprite at a time,
with pictures of any resolution. The renderer draws them at the screen's resolution,
not the console's 256×224.

The project ships no packs and no art. You make a pack on your own computer, from your
own ROM:

1. **Dump** the tiles and sprites the game shows while you play.
2. **Upscale** them into placeholders that are sharper than the originals.
3. **Edit** any of the PNGs by hand, or paint new ones.
4. **Reload** with F5 and see the change at once.

## Legal notes: read these first

- **A pack made by dumping or upscaling is the game's art.** The dumped pictures are the
  game's own pixels. An upscaled or edited copy of them is still derived from them. Such
  a pack is **for your personal use only**. Never share it, upload it or distribute it,
  in whole or in part, including single pictures. Every `pack.toml` the dump writes says
  so at the top.
- **Art you make yourself is yours.** A picture you painted from scratch belongs to you,
  and you can share it under any terms you choose. That includes a pack whose pictures
  are all your own work, keyed to the game's tiles. A picture traced over or edited from
  a dumped one is still derived from the game's art.
- **The project's packages never contain a pack.** `tools/check_dist.py` fails any
  package that holds a `pack.toml`, or a picture named by a content key under
  `tiles/`, `sprites/` or `objects/`
  ([PACKAGING.md](PACKAGING.md)). Packs live only in your data folder.

## Where packs live

Packs are kept in the game's folder in your user data directory:

```
~/.local/share/ct-recomp/ct/packs/NAME/        ($XDG_DATA_HOME/ct-recomp/ct/packs/NAME/)
~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/ct/packs/NAME/   (Flatpak)
```

A `NAME` that contains a `/` is taken as a directory path instead, anywhere.

## The workflow

```
# 1. Dump: play (or replay a script); every new tile and sprite is written once.
ct_sdl --pack-dump mydump

# 2. Upscale 4x into a new pack (the built-in pixel-art scaler).
python3 tools/pack_upscale.py mydump --scale 4 --out my4x

#    Or with an upscaler you installed, for example an AI one, run once per picture:
python3 tools/pack_upscale.py mydump --scale 4 --out my4x-ai \
    --external 'realesrgan-ncnn-vulkan -i {in} -o {out} -s {scale}'

# 3. Play with it. Packs stack: the first --pack wins where two have the same key.
ct_sdl --pack my4x
ct_sdl --pack myedits --pack my4x

# 4. Edit a PNG in my4x/sprites/ (or copy it into myedits/), save, press F5.
```

The engine-only build (`ct_sdl_engine`, the AppImage and Flatpak) takes the same options.

### Dumping

`--pack-dump NAME` turns on the PPU's provenance G-buffer (below). At the end of each
frame, every tile and sprite drawn that the pack doesn't have yet is written:

- `tiles/KEY.png`: an 8×8 background tile in its palette.
- `sprites/KEY.png`: one OAM entry's whole sprite (8×8 to 64×64) in its palette.
- `objects/KEY.png`: sprites drawn together, as one picture (see below).

Color 0 is transparent. Pictures are in the orientation they are stored in VRAM. A tile
the game shows flipped is the same tile, and the renderer flips its replacement the
same way.

Dumping again into the same `NAME` adds only what is new, so you can dump in several
sessions. The Leene Square replay (9,200 frames, the start of the game up to the fair)
gives about 2,900 tiles, 1,600 sprites and 2,600 objects, 42 MB. The dump writes
uncompressed PNGs; the upscaler's output is compressed.

**Objects.** The SNES draws a character from several OAM pieces. To help you find a
character's pieces, each frame's sprites whose boxes touch or overlap are grouped. A
combination not seen before gets a picture of the assembled object and an `[[object]]`
entry listing its pieces (their keys, offsets and flips). The objects are a finding aid:
replacing art still happens per piece. A character standing next to another forms one
group, and each animation frame forms a new one, so there are many.

### Upscaling

`tools/pack_upscale.py PACK --scale N (--out NEW | --in-place)`, with N from 2 to 6.

The built-in scaler is edge-directed pixel-art scaling in the manner of xBRZ, written
for this project from the published idea rather than from any implementation. For every
2×2 block of source pixels it compares color differences along the two diagonals over
the surrounding 4×4 pixels. Where one diagonal is clearly the more continuous, the
corner between the four pixels is cut along it, with a 45° line, or a shallow or steep
one where the neighbors continue the edge. Coverage is supersampled, so edges come out
smooth while flat areas and the palette stay as they are. Alpha is treated as a channel
like the others: outlines stay crisp, and holes stay holes. It runs on the dumped tiles,
never on the whole frame, so tile seams don't move. The Leene Square dump (7,085
pictures) takes about a minute on 4 cores (`--jobs`).

`--external CMD` runs your own program once per picture. `{in}`, `{out}` and `{scale}`
in CMD are replaced with the input PNG, the PNG to write and the factor. CMD is split
like a shell command line but not run through a shell. If the program drops the
transparency (many AI upscalers write RGB), the alpha channel is taken from the
built-in scaler's result.

The upscaled pack's `pack.toml` is the original's with `scale` set and `upscaled_from`
added. Entry sizes (`w`, `h`) stay the console sizes, since that is what a picture is
mapped onto, whatever its resolution. Upscaling a pack whose scale is not 1 needs
`--force`.

These upscales are **placeholders**: a guess at detail the original never had, to be
replaced by real art piece by piece. An MMPX scaler (MIT, designed for sprites) would
be a good second built-in option. It isn't implemented yet; `--external` can run one
now.

### Editing

Open any PNG in your editor and paint. A replacement can have any size, but it must keep
the piece's aspect ratio, because it is stretched over the original piece. Save it as
8-bit RGBA, RGB, gray or palette PNG (16-bit and interlaced PNGs are not read).
Transparent pixels show what is behind the piece. Press **F5** in the game to reload
every pack.

To keep your own work apart from the generated placeholders, put it in a pack of its
own and list it first: `--pack myart --pack my4x`. Copy the entry from the placeholder
pack's `pack.toml` into yours (the `[[tile]]` block with its `key`, `file`, `w` and `h`),
and put your picture at `file`.

## Using packs

| Option or key | What it does |
| --- | --- |
| `--pack NAME` | Load a pack. Repeat it to stack packs, highest priority first. `--pack none` loads none. |
| `--pack-dump NAME` | Dump new tiles and sprites into pack NAME while playing |
| F5 | Reload the packs (after editing their PNGs) |

The packs given with `--pack` are remembered in `settings.ini` (`packs=a,b`) and loaded
on the next start without the option. `--pack none` clears the list.

Packs are drawn by the GL renderer. With the HD-2D looks, the replacement goes through
the lighting, depth of field and grading like the original pixels. With the plain look
(no `--hd2d`), a loaded pack switches the window to the GL renderer with the `off`
preset. That shows the original look, at full resolution, with the replacements. Your
saved look is kept. `--no-hd2d` with packs loads none.

## How it works

1. **Provenance G-buffer** (`PpuSetProvBuffer`, `sched_set_prov`, `sched_prov`). The PPU
   records, per output pixel, where the main screen's winning pixel came from: the 8×8
   character's VRAM address and bit depth, the palette and the CGRAM index drawn, the
   texel within the character, and for a sprite the OAM entry, its name, size and the
   texel within the whole sprite (`PpuProv` in `third_party/snes/ppu.h`). With it,
   the scheduler keeps the VRAM, CGRAM and OAM as they were when the frame was copied
   (`sched_prov_mem`). It is off unless a pack is loaded or being dumped. Sprite
   evaluation is compiled in two versions, so the path with it off is unchanged. With
   it on, frame and state hashes are still identical, over all 9,200 frames of the Leene
   Square replay.
2. **Keys** (`packs_key`): a 64-bit FNV-1a hash.
   - A **BG tile key** hashes the character's decoded 8×8 color indices and its
     palette's colors (color 0 left out: it is transparent), plus the bit depth.
   - A **sprite piece key** hashes the same for one OAM entry's whole sprite, its
     characters gathered with the 16×16 name-page wrap, and its 16-color palette.
   - The same tile in two palettes (day and night, or a fade the game does by changing
     the palette) gets two keys, as in Mesen's HD packs. Fades done with the brightness
     register don't change any key.
   - **Whole-object keys** come later. A character's animation frame as one picture,
     instead of the OAM pieces it is drawn with, needs a decoder for the game's own
     sprite frame format (the `sprites` track). Until then, `[[object]]` groups are only
     a finding aid.
3. **Per frame**, `packs_map_frame` turns the provenance into a map: per console pixel,
   the atlas entry that replaces it, the texel it showed and the palette color it was
   drawn with. Keys are cached per frame by the provenance's raw identity (address,
   palette, size), so each distinct tile is hashed once a frame.
4. **The atlas**: every replacement PNG of the loaded packs, shelf-packed into pages of
   an array texture (256 to 4,096 texels square, as needed), mipmapped.
5. **Compositing** (`shaders/upscale.frag`, compiled into the passes that read the
   picture). For each output pixel, the replacement is sampled at the pixel's own
   position within the console texel it covers, flipped as the console flipped the
   original. Where a replacement texel is larger than an output pixel, it is drawn
   sharp, with only its edges smoothed (as sharp-bilinear does for console pixels).
   What the console and the lighting did to the original's palette color carries over
   to the replacement: a ratio (brightness, lighting) or, where color math changed the
   pixel, an offset. Fades, tints and the HD-2D lighting therefore apply to
   replacements too. Transparent texels show the behind G-buffer (the layer under the
   piece), which is turned on whenever a pack is loaded. Pixels with no replacement
   are drawn as before.

With no pack loaded none of this runs. HD-2D screenshots (presets off, hd2d and hd2d3d,
four frames of the Leene Square replay at 1920×1080) are byte-identical to the build
without packs.

### Cost

Measured on a ROG Ally (Radeon 780M, Mesa radeonsi) at 1706×1120 over 6,000 frames of
the Leene Square replay, with the 4× pack (4,474 pictures in one 4,096² page, 64 MB):

| | GPU per frame (median), no pack | with the pack |
| --- | --- | --- |
| Preset `off` | 0.24 ms | 0.36 ms |
| Preset `hd2d` | 1.00 ms | 1.28 ms |

On the CPU, building the map takes about 0.13 ms per frame, and the provenance
G-buffer about 0.4 ms in the PPU (`ct_boot --prov`: 39 s instead of 35 s for 9,200
frames). The atlas is held in RAM and on the GPU (64 MB for the 4× Leene pack).

## The format

A pack is a directory holding `pack.toml` and PNGs. `pack.toml` is a small TOML subset:
`[pack]`, then `[[tile]]`, `[[object]]` and `[[model]]` entries of `key = value` lines.
Values are strings, numbers or arrays of numbers. It is data only: nothing in a pack
can run code.

```toml
[pack]
version = 1          # this format
name = "my4x"
scale = 4            # informational: the factor its pictures were made at
upscaled_from = "mydump"

[[tile]]
key = "67570e230f375981"      # packs_key, 16 hex digits
kind = "sprite"               # "bg" or "sprite" (informational)
file = "sprites/67570e230f375981.png"
w = 8                         # the original piece's size in console pixels
h = 8
bpp = 4                       # tags, informational:
layer = "OBJ"                 #   BG1-BG4 or OBJ
palette = 0                   #   sprite palette 0-7
oam = 30                      #   the OAM entry it was first seen in
first_frame = 801             #   the frame it was first seen in

[[object]]                    # a finding aid: sprites drawn together
key = "27cd0626975fbc46"
file = "objects/27cd0626975fbc46.png"
w = 48
h = 96
first_frame = 801
pieces = [
  { key = "a8d096398c37d0df", x = 40, y = 56, hflip = 0, vflip = 0, oam = 0 },
]

[[model]]                     # a 3D model for a map cell (the 3D path, later)
map = 0x1F0                   # map id
cell = [3, 4]                 # the cell, x and y
file = "models/well.glb"      # glTF 2.0, .gltf or .glb
scale = 16.0                  # model units to map pixels
offset = [0.5, 0.0, 0.5]      # in cells
```

Only `key`, `file`, `w` and `h` are needed for a `[[tile]]`. A replacement picture can
be any size up to 2,048 pixels on a side. `[[model]]` entries are read and their files
are checked with cgltf (`third_party/cgltf`) when a pack loads (`packs_models`,
`packs_model_parse`), but nothing draws them yet.

### Compared with Mesen's HD packs and RT64's texture packs

The format follows the ideas of Mesen's `hires.txt` (GPL-3.0), the closest prior art for
tile-level replacement ([PRIOR-ART.md](PRIOR-ART.md)). There is no mainline SNES
dialect of `hires.txt`, so `pack.toml` is the definition here.

- **The same:** a pack is a folder of PNGs and a manifest. A tile is keyed by its pixels
  and its palette, so one tile in two palettes is two entries. There is a scale. A dump
  mode records what your own ROM displays, like Mesen's HD Pack Builder.
- **Different:**
  - Keys are hashes of the decoded tile and its palette's 15-bit colors, not the tile's
    raw bytes and palette bytes. They are the same for 2bpp, 4bpp and 8bpp tiles
    wherever they sit in VRAM.
  - SNES sprites are whole OAM entries (up to 64×64), not 8×8 tiles, plus the
    `[[object]]` grouping. NES packs don't need that.
  - Pictures can be any size per entry, not one scale per pack.
  - There are **no conditions yet**. Mesen can make a replacement depend on, for
    example, a memory value or a neighboring tile. The format has room for that: a
    `when` table in an entry is a planned addition and is ignored today.
  - No background images, sound or palette overrides.
- **From RT64** (MIT): a database keyed by content hash, PNG for authors, and later an
  optional packed container (pre-compressed textures in one file) for loading speed.
  For now a pack is only PNG plus the manifest, loaded in full at start and on F5.

## For the 3D path (scene3d)

The 2D compositing covers every HD-2D preset that draws the console's picture. The 3D
diorama (`hd2d3d`, `frontend/hd2d/scene3d.*`) draws tiles itself. It is not wired to
packs yet, and in that preset the 3D picture is drawn without them. The pieces it
needs exist:

- `packs_key(mem, prov)` and `packs_lookup_tile(key)`: from a tile to its replacement's
  atlas rectangle (`packs_rect`: page, x, y, w, h, and the original's size).
  `packs_atlas` and `packs_entries` give the atlas and the table that the 2D path
  uploads (`hd2d_pack_atlas`).
- Every 3D surface samples through `tileTexel(uint tile, vec2 uv)` in
  `shaders/scene3d.frag`, keyed by the packed tile value (VRAM address and palette).
  Bits 26-31 of that value are free for a pack flag (`scene3d.h`).
- The plan: per frame, for each distinct tile value in the scene's meshes, compute its
  key from the scene's VRAM and CGRAM (the decode `packs_key` does, given the address,
  palette and bit depth), and fill a small lookup texture from tile value to atlas
  entry. `tileTexel` then samples the atlas (`uPackAtlas`, as `upscale.frag` does) where
  there is an entry and the console's tile where there isn't.
- `[[model]]` entries go through `s3d_set_cell_hook`: for a cell with a model, the hook
  adds the model's triangles to the cell's mesh instead of the generated geometry.

## Limits

- Mode 7 isn't replaced: it has no tiles in the usual sense.
- Mid-frame palette or VRAM changes: keys use the VRAM and CGRAM at the end of the frame.
  A tile drawn with a palette that HDMA changes partway down the screen gets the key of
  the palette at the end.
- A replacement covers exactly the original piece's pixels. It can't draw outside them
  (a sprite can't get bigger than its original), and its transparent parts show the one
  layer under it (the behind G-buffer), not every layer.
- Each animation frame and each pair of characters standing together is a new
  `[[object]]`, so dumps have many of them.
- The whole-line PPU renderer (unused in the game) doesn't record provenance.
- The atlas holds every replacement at once. A full game's dump, upscaled 4×, could go
  past the 4,096² page and use several pages (each 64 MB). A packed, streamed container
  is the fix, later.
- The 3D preset doesn't use packs yet (above).

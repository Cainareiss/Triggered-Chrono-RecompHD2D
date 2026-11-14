<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Audit of the visual QA branches (`qa3d`, `qa2d`, `qaworld`)

Audited against [VISUAL-DESIGN.md](VISUAL-DESIGN.md) (check numbers like U1, S1 refer to
it). The audit read the branches' bug lists, their diffs against `hd2d` and their
screenshots. **Nothing was built or run here** (memory limit), so every statement about
behaviour is from reading code and pictures, not from a test run.

Status: **interim, 06:40.** At this point none of the three branches had a commit; the
review below is of their working trees. The final pass replaces this line.

## Verdicts

| Branch | Verdict | Condition |
| --- | --- | --- |
| `qa2d` | (interim) merge | both fixes are small and right; see notes |
| `qa3d` | (interim) merge after two small changes | bounds check on the tile index; early-out when no UI text is on screen |
| `qaworld` | (interim) nothing to merge yet | only a `--poke-wram` debug option so far, no bug list |

## Merge order

1. `qa2d` first. It holds the fix for the user's report (the bright grid), which
   `qa3d` deliberately left to it.
2. `qa3d` second. Both edit `frontend/hd2d/hd2d.c` (`qa2d` in `hd2d_render` at the
   `want3` test; `qa3d` adds `s3d_box_mask` above `s3d_render` and one call below the
   `want3` test) and `game/ct/CMakeLists.txt` (tests added at different places). The
   hunks don't overlap, so the merge should be clean; if it conflicts, keep both sides.
3. `qaworld` last (touches `frontend/main.c` only, so far).

After merging 1 and 2, run `sdl_hidden_map_3d`, `sdl_3d_box`, `sdl_side_fill_tall` and
`sdl_hdtext` together: the gate (`qa2d`) and the box mask (`qa3d`) both decide what the
3D path shows at frames 3560-4300 and were each tested without the other.

## `qa2d`

**Bug 1, the bright grid (the user's report): root cause real, fix right.** At frame
3800 the console's picture is black with a text box and the 3D path draws a lit 16x16
map of repeated tiles (`build/qa/cmp/a_03800.png` in the `qa3d` worktree shows both).
The module reports a map view because the map is loaded; the console has BG1 and BG2
off. The fix adds one term to `want3`: no 3D while neither screen enables BG1 or BG2.
It is game-agnostic (PPU state only) and costs nothing.

- Risk, low: a scene that draws its map on BG3 only, or turns BG1/BG2 off for a
  frame during an effect, drops to 2D for that time and crossfades back over 8 frames.
  That is the safe side (design rule 1).
- Gap: it is one case of the scene gate (VISUAL-DESIGN.md, Transitions), not the gate.
  A map that is loaded but not the one on screen with BG1/BG2 *on* still gets through.
  Follow-up, not a blocker: compare a few floor tiles against the console frame
  (`qa3d`'s `s3d_box_mask` already computes exactly that difference per tile; if most
  of the screen's BG1 tiles differ, the scene is not what the console shows).
- To check when it is built: the 8-frame fade when the gate trips *during* a 3D view
  keeps "the last 3D frame", which is right for a battle start; make sure a map whose
  layers go off on the same frame its tiles are replaced doesn't fade from a frame of
  garbage (shoot the 10 frames around 3560).

**Bug 2, bars above and below in a tall window: fix right.** `final.frag` mirrored and
faded only in x. The change mirrors and fades in y the same way. Portable (no new
uniforms or types). Corners take the larger of the two distances, which is fine.
Check `deck` at 16:10 (`side_fill_wide_only`) still gives black bars: that path
doesn't go through this branch, but the test should include it.

**Bug 3 (name entry blurred) and bug 4 (glyph marks under HD text): open, and bug 3
is wider than listed.** The pause menu has the same fault: `qaworld`'s
`build-qa/shots/tr_sheet.png` (frames 7748-7792) shows the status screen with its top
and bottom thirds blurred until the numbers can't be read. This fails U3 and design
rule 2, and it is in every flat look, so it outranks everything else still open.
Decision asked for in the bug list (design lead's answer): **don't use
`CT_SCENE_OTHER`** for it, battles need their depth of field. Add a generic mode to
`ct_scene` for "a full-screen UI" (e.g. `CT_SCENE_UI`: menus, name entry, save/load,
shops), set by the module where it knows, and have `hd2d_render` set the field amount
(DOF, tilt, bloom threshold, vignette) to 0 for it. That also fixes bug 4 (no tilt, so
no reveal of the behind buffer). Bug 4 needs one more guard regardless: no tilt reveal
inside the HD text overlay's rectangle.

**Bug 6 (soft picture at 185% desktop scale): agreed, not a renderer bug**, but it is
likely part of what the user saw ("visual bugs" at a 1038x556 drawable on a 1080p
panel means every preset was rendered at about half the panel's resolution and scaled
up by the compositor). Recommend preferring the Wayland video driver when the session
is Wayland, or at least logging the drawable size against the display's. Worth a line
to the user.

## `qa3d`

**Q2, text without its box: root cause real, fix works by detection, acceptable as a
first fix.** The window is in the console's BG1 tilemap only; the scene's BG1 is the
map from WRAM. `s3d_box_mask` compares the console's BG1 pixels with the scene's tile,
per 8x8 tile, and where runs of 16 or more tiles differ near UI text, `ui3d.frag` lays
the console's pixels flat. It is game-agnostic. Review notes, in order of importance:

1. **Out-of-bounds read (fix before merge).** `e = tx < tw && ty < th ? L->tiles[...]`
   has no lower bound. `camera_x` / `camera_y` are `int32_t`; if a module ever reports
   a negative camera (a map smaller than the screen, a shake), `tx` or `ty` is negative
   and the index is before the array. Add `tx >= 0 && ty >= 0`.
2. **Cost (fix before merge, small).** It walks all 57,344 console pixels and decodes
   their tiles on the CPU on every 3D frame, text box or not. Compute `text_row` first
   and return before the pixel loop when no row has UI text: boxes need text nearby by
   the function's own rule, so the result is the same, and the cost is paid only while
   a box is open. Report the CPU time with a box open (the 3D path's whole CPU side was
   0.18 ms).
3. **Boxes narrower than 16 tiles are missed** (choice windows, item pop-ups, shop
   prompts, name tags). And the opening animation (Q6). The run length is a guess
   against false positives; fine for now, but it means U2 still fails for small boxes.
4. **Translucent windows.** The laid-flat pixels are the console's composite, so a
   window the console blends over the scene would show the *2D* room inside the box,
   over the 3D room. Not the case for this box; a limit to document.
5. **Below brightness 4 nothing is detected**, so during a fade-in with a box open the
   text shows before its box. A few frames.
6. Assumes 4 bpp 8x8 BG1 tiles and that the scene's camera is BG1's scroll. True for
   this module; say so next to `ct_scene` in `runtime/module.h`, or skip the mask when
   the layer isn't 4 bpp.
7. GLES: `uBox` is a `usampler2D` like `uLayer` in the same shader, and the texture is
   `R8UI` / `RED_INTEGER`, both ES 3.0. No issue seen.

The durable fix is the one in VISUAL-DESIGN.md (P0 item 1): the module tells the engine
where UI is on BG1/BG2 (a rectangle list in `ct_scene`); then no detection, small boxes
and the opening animation work, and items 3 and 5 go away. Keep the mask as the
fallback for modules that don't say.

**Q1:** same cause as `qa2d` bug 1, left to `qa2d`. Good call; no duplicate fix.

**Q3 (sleeper stands on the bed; grey rectangle), Q4 (small rooms: void, other rooms
in view), Q5 (opening flyover shows another place): open, causes plausible.**

- Q4's cause is confirmed in `hd2d.c` (`s3d_render`, the camera stop): it clamps to
  `s3d_map_bounds`, the bounds of every cell with pixels on the whole map, and centers
  when the map is smaller than the view. With several rooms on one map that puts the
  room off-center and other rooms in the picture (`build/qa/cmp/a_07150.png`: the room
  is cut by the picture's left edge while a second room fills the right). This is the
  most visible fault left in interiors and probably what the user saw at frame 4690.
  Fix direction: bounds from the cells connected to the console's view (flood from the
  player's cell over cells with pixels), draw only those, zoom to fit (S6).
- Q5's cause is confirmed too (`at[]` is the player object on the world map). Fix: use
  the console's camera center whenever the player object is off the console's screen
  or the camera moves without it; belongs to `qaworld`.
- Q3: the grey rectangle is worth ten minutes: it sits exactly where the blanket is and
  is flat grey, which looks more like a sprite drawn with the wrong palette or a
  priority-masking sprite drawn opaque than like wall tiles. If it is a masking sprite,
  other scenes will have the same rectangles.

## `qaworld`

No bug list and no fixes at the interim pass. Working tree: `--poke-wram F:ADDR^M` in
`frontend/main.c`, a debug option to force a state. Harmless, generic, but it is a
cheat-like switch in the shipping frontend: keep it out of `--help`'s user section, or
behind the existing debug options.

## Bugs the QA lists miss

From the screenshots (paths relative to each worktree).

1. **Pause/status menu blurred by the tilt-shift** (`qaworld`
   `build-qa/shots/tr_sheet.png`). Same class as `qa2d` bug 3; neither list names the
   pause menu. Fails U3. Highest priority of the open items.
2. **Interiors have no walls in 3D.** In `qa3d` `build/qa/cmp/a_04690.png` and
   `a_07150.png` the rooms' back walls lie on the floor plane: the room is a tilted
   flat picture with a trapezoid outline, not a diorama. The outdoor scenes rise;
   these don't. Either the cells aren't classed solid there or the wall tiles are
   overhead-only. Not in QA-3D.md.
3. **Side fill shows blob shapes on dark scenes** (`qaworld`
   `build-qa/shots/b_sheet1.png`, both sides: lighter rounded patches with visible
   contours). The fill should be featureless. Probably the eighth-size soft copy's few
   bright texels with too little blur, plus banding in the dark; needs a wider blur or
   dither.
4. **The void takes the backdrop's color**: a dark red-brown field fills two thirds of
   every interior shot. In 2D it is the same color but the room is centered and larger;
   in 3D it dominates. Neutral near-black, and Q4's framing, fix it.
5. **Sprites on the world map are placed by screen position while the ground isn't**
   (`qa3d` `build/qa/m1.png`, third and fourth pairs: the balloons float over the fair
   on the console and over open sea in 3D). Related to Q5 but separate: even with the
   right camera, unowned sprites are laid flat at console coordinates over a
   perspective ground.
6. **A pale translucent rectangle on the world map** at the right edge
   (`qa3d` `build/qa/a_1038/02600.png`, over the mountain): a cloud tile block drawn
   with a straight edge, or the cloud plane's edge. Fails L15.
7. **The 3D room is smaller on screen than the 2D one** (zoom 0.75 plus the tilt): the
   user's first impression of 3D indoors is "everything shrank". With S6 (zoom to fit
   small maps) it goes away.
8. **HD text with no box in 3D** (`b_sheet1.png`, frames 11700-12150): fixed by Q2 if
   HD text and the mask agree; see below.

## Bugs that cross areas

- **Text box in 3D (`qa3d` Q2) x HD text (`qa2d`).** HD text removes the console's
  glyph pixels from the source picture and draws its own in the final pass. The box
  mask lays the *console's* pixels flat from the same source picture. To verify once
  both are merged, with a font set: the box shows, no console glyph is left inside it,
  and the mask's "UI text nearby" rule still finds the text rows (it reads the layer
  buffer, which should be unaffected by glyph removal). `sdl_3d_box` runs without a
  font; add one frame with `--hd-font`.
- **The gate (`qa2d` bug 1) x the box (`qa3d` Q2)** at 3560-4250: with the gate the
  narrator's screen is 2D and has its box. Check the first frames after the gate opens
  (4250-4300): the room fades in with a box open and the brightness-4 rule hides the
  box for a few frames.
- **Menus as UI (`qa2d` bug 3) x battles (`qaworld`).** See the decision above: a new
  generic scene mode for full-screen UI; battles keep their effects but their HUD and
  damage numbers must pass U1.
- **World camera (`qa3d` Q5) belongs to `qaworld`.**
- **Desktop scaling (`qa2d` bug 6) x everything:** screenshots at 1038x556 are what the
  user saw, scaled 1.85x by the compositor. Thin features (text, 1-px seams, FSR
  ringing) look worse there than in any `--shot`. QA at 1038x556 should view the shots
  scaled up 1.85x, bilinear.

## Notes for the lead to relay

- `qa3d`: add the lower bound on `tx`, `ty` and the early-out before committing; say in
  QA-3D.md that small boxes are not covered; look at the interior walls (missed bug 2).
- `qa2d`: add the pause menu to bug 3; the fix is a new generic scene mode, not
  `OTHER`. Commit what is there, it is good.
- `qaworld`: write the list even if nothing is fixed; Q5 and missed bugs 5 and 6 are
  yours.
- All three: `__pycache__` directories appeared under `tools/` and `game/ct/tools/` in
  the worktrees. Don't commit them (use explicit paths).

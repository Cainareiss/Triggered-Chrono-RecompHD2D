/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The 3D diorama's geometry (frontend/hd2d/scene3d.c) on a synthetic
 * scene: a floor of one tile with one column of blocked cells, which must
 * stand up as a wall of hmax cells with the rest of the run on top, side
 * walls where the box ends, and the ground behind it filled; and sprites
 * standing at their object's foot point. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hd2d/scene3d.h"

static int fails;
#define CHECK(c, ...)                                     \
    do {                                                  \
        if (!(c)) {                                       \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            fails++;                                      \
        }                                                 \
    } while (0)

static uint16_t vram[32768], cgram[256], oam[272];
static uint16_t tiles[CT_SCENE_LAYERS][CT_SCENE_STRIDE * 2 * CT_SCENE_MAX_CELLS];
static ct_scene_cell cells[CT_SCENE_MAX_CELLS * CT_SCENE_MAX_CELLS];

enum { MW = 6, MH = 8, WALL_X = 2, RUN_A = 1, RUN_B = 4 };

static void make_scene(ct_scene *s)
{
    memset(s, 0, sizeof *s);
    /* Tile 1 at character base $1000 has pixels; tile 0 is empty. */
    for (int k = 0; k < 16; k++)
        vram[0x1000 + 16 + k] = 0x8181;
    for (int y = 0; y < 2 * MH; y++)
        for (int x = 0; x < 2 * MW; x++)
            tiles[1][y * CT_SCENE_STRIDE + x] = 0x0001;   /* BG2: tile 1; BG1: empty */
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            int solid = x == WALL_X && y >= RUN_A && y <= RUN_B;
            cells[y * CT_SCENE_MAX_CELLS + x].cls = solid ? CT_CELL_SOLID : CT_CELL_FLOOR;
        }
    s->mode = CT_SCENE_FIELD;
    s->cell_px = 16;
    s->map_w = MW;
    s->map_h = MH;
    for (int l = 0; l < CT_SCENE_LAYERS; l++)
        s->layer[l] = (ct_scene_layer){l < 2, 4, 0x1000, 0, tiles[l]};
    s->cells = cells;
    s->vram = vram;
    s->cgram = cgram;
    s->oam = oam;
    s->obj_chr[0] = 0x1000;
    s->obj_chr[1] = 0x2000;
    s->obj_size[0] = 8;
    s->obj_size[1] = 16;
}

static int has_vertex(const s3d_mesh *m, float x, float y, float z)
{
    for (int k = 0; k < m->nv; k++)
        if (fabsf(m->v[k].x - x) < 0.2f && fabsf(m->v[k].y - y) < 0.2f &&
            fabsf(m->v[k].z - z) < 0.2f)
            return 1;
    return 0;
}

static void test_map(void)
{
    static ct_scene s;
    make_scene(&s);
    s3d_mesh m = {0};
    s3d_params p = {3, 1.0f};
    int quads = s3d_build_map(&s, &p, &m);
    /* Floors: 44 free cells, plus 3 run rows not under the box, filled
       from the ground north of the run; raised: the 4 run cells; sides: 3
       bands on both sides of the one-cell box. 4 quarters each. */
    CHECK(m.ni_floor / 6 == (44 + 3) * 4, "floor quads %d", m.ni_floor / 6);
    CHECK(quads == (44 + 3 + 4 + 2 * 3) * 4, "quads %d", quads);
    /* The wall stands at the run's south edge, 3 cells high. */
    CHECK(has_vertex(&m, WALL_X * 16, 48, (RUN_B + 1) * 16), "wall top corner");
    CHECK(has_vertex(&m, WALL_X * 16, 0, (RUN_B + 1) * 16), "wall foot");
    /* The rest of the run (row 1) lies on top, moved south by the wall's height. */
    CHECK(has_vertex(&m, WALL_X * 16, 48, (RUN_A + 3) * 16), "roof");
    CHECK(s3d_height_at(WALL_X, RUN_B) == 48 && s3d_height_at(WALL_X, RUN_A) == 0 &&
              s3d_height_at(WALL_X + 1, RUN_B) == 0,
          "box heights");
    /* Side walls on the box's east and west faces. */
    CHECK(has_vertex(&m, (WALL_X + 1) * 16, 48, RUN_B * 16), "east side");
    CHECK(has_vertex(&m, WALL_X * 16, 48, (RUN_B + 1) * 16), "west side");
    int opaque = 0, raised = 0;
    for (int k = 0; k < m.nv; k += 4) {
        opaque += (m.v[k].tile & S3D_OPAQUE) != 0;
        raised += (m.v[k].tile & S3D_RAISED) != 0;
        CHECK((m.v[k].tile & S3D_ADDR_MASK) == 0x1010, "tile address %x", m.v[k].tile);
    }
    CHECK(raised == (4 + 6) * 4, "raised %d", raised);
    /* The back layer is opaque (color 0: the backdrop) except where it
       rises as a cut-out: posts, trunks and canopies show what is behind. */
    CHECK(opaque == quads - 4 * 4, "opaque %d of %d", opaque, quads);
    s3d_mesh_free(&m);
}

static void test_sprites(void)
{
    static ct_scene s;
    make_scene(&s);
    s.camera_x = 100;
    s.camera_y = 200;
    for (int e = 0; e < 128; e++)
        oam[2 * e] = 0xF0 << 8;   /* hidden */
    /* Entry 3: 16x16 at screen (40, 60); its object's foot is at screen
       (48, 74): 2 px above the sprite's bottom, so it is lifted 2 px. */
    oam[2 * 3] = (uint16_t)(60 << 8 | 40);
    oam[2 * 3 + 1] = 0x0002;
    oam[0x100 + 0] = 2 << 6;      /* entry 3: large */
    /* Entry 9: 8x8 at (10, 20), owned by nobody. */
    oam[2 * 9] = (uint16_t)(20 << 8 | 10);
    s.n_objects = 1;
    s.object[0] = (ct_scene_object){.x = 148, .y = 274, .facing = 1, .n_pieces = 1, .piece = {3}};
    s3d_mesh m = {0};
    s3d_params p = {3, 1.0f};
    int quads = s3d_build_sprites(&s, &p, &m);
    CHECK(quads == 4 + 1, "sprite quads %d", quads);
    /* Its top-left tile: x = camera + 40, top at 74 + 2 - 60 = 16 px up, at the foot's z. */
    CHECK(has_vertex(&m, 140, 16, 274.0f + 124 * 0.004f), "billboard top-left");
    CHECK(has_vertex(&m, 156, 0, 274.0f + 124 * 0.004f), "billboard foot");
    /* Unowned: stands at its own bottom (screen row 28 -> z = 228). */
    CHECK(has_vertex(&m, 110, 8, 228.0f + 118 * 0.004f), "unowned piece");
    CHECK((m.v[0].tile & S3D_OBJ) && ((m.v[0].tile >> S3D_NORMAL_SHIFT) & 7) == S3D_N_SPRITE,
          "sprite flags");
    s3d_mesh_free(&m);
}

int main(void)
{
    test_map();
    test_sprites();
    if (fails)
        return 1;
    printf("scene3d: ok\n");
    return 0;
}

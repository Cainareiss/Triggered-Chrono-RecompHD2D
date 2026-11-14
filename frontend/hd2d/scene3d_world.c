/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The world map as a diorama landscape (scene3d.h, CT_SCENE_WORLD).
 *
 * The scene gives two tile layers over a grid of 8 px cells and a walk
 * class per cell. The class says floor, water or blocked, and "blocked"
 * lumps mountains, forest, the shore band and buildings together, so the
 * relief comes from what the cell's pixels look like (BG1 over BG2, as
 * the console composites them), unless the module named the kind itself:
 *
 * - water: a water cell whose pixels are mostly blue (blue clearly above
 *   red and at least green), or any cell that is almost all blue. A
 *   "water" cell that isn't blue is blocked ground (some games mark
 *   mountain tops unwalkable that way).
 * - forest: blocked, and the cell's mean color is green-dominant.
 * - shore: blocked, within two cells of water, and not a dark red-brown
 *   (a mountain that reaches the sea stays a mountain).
 * - mountain: blocked, and the mean color is red-dominant (browns,
 *   rock), in a patch at least five cells across (a thinner one is a
 *   brown wall or roof); else flat (buildings, walls, bridges: they lie
 *   as drawn).
 *
 * Heights, px: water -5, flat; the shore band -1.5; floor and flat 0;
 * forest a canopy, 4 at its edge and 8 inside, bumped by the cell's
 * brightness (tree tops light, gaps dark); mountains a dome over their
 * footprint, up to 48 px at six cells from the edge, with the cell's
 * brightness against the mountain's mean as ridges. A tile corner takes
 * the mean of its four cells, or the water level if one of them is
 * water, so the water stays flat and the land slopes into it. */
#include "scene3d.h"

#include <math.h>
#include <string.h>

#define WS CT_SCENE_WORLD_STRIDE

enum { DIST_CAP = 8 };
static const float WATER_Y = -5.0f, SHORE_Y = -1.5f;

static int gw, gh, gwrap;
static uint8_t wcls[WS][WS];
static float cellh[WS][WS];
static float vh[WS + 1][WS + 1];   /* corner (x, y): the top-left of cell (x, y) */
static uint8_t nrm[WS * WS * 4];
static float lum_[WS][WS];
/* Entrance marks: the middle of each group of touching event cells, px. */
enum { MAX_MARKS = 256 };
static float marks_xz[MAX_MARKS][2];
static int n_marks;

int s3d_world_class(int x, int y)
{
    return x < 0 || y < 0 || x >= gw || y >= gh ? S3D_W_FLOOR : wcls[y][x];
}

const uint8_t *s3d_world_normals(int *w, int *h)
{
    *w = gw;
    *h = gh;
    return nrm;
}

static int wrapi(int v, int n) { return ((v % n) + n) % n; }

static int cell_x(int x) { return gwrap ? wrapi(x, gw) : x < 0 ? 0 : x >= gw ? gw - 1 : x; }
static int cell_y(int y) { return gwrap ? wrapi(y, gh) : y < 0 ? 0 : y >= gh ? gh - 1 : y; }

static float corner(int x, int y)
{
    if (gwrap) {
        x = wrapi(x, gw);
        y = wrapi(y, gh);
    } else {
        x = x < 0 ? 0 : x > gw ? gw : x;
        y = y < 0 ? 0 : y > gh ? gh : y;
    }
    return vh[y][x];
}

float s3d_world_height(float x, float z)
{
    if (!gw || !gh)
        return 0.0f;
    float fx = x / 8.0f, fz = z / 8.0f;
    int ix = (int)floorf(fx), iz = (int)floorf(fz);
    float ax = fx - (float)ix, az = fz - (float)iz;
    float a = corner(ix, iz), b = corner(ix + 1, iz), c = corner(ix, iz + 1),
          d = corner(ix + 1, iz + 1);
    return (a * (1 - ax) + b * ax) * (1 - az) + (c * (1 - ax) + d * ax) * az;
}

/* ---- pixels ---- */

static const ct_scene_layer *layer_of(const ct_scene *s, int l)
{
    return s->layer[l].present && s->layer[l].tiles ? &s->layer[l] : NULL;
}

static uint16_t entry(const ct_scene_layer *L, int x, int y)
{
    return L->tiles[y * WS + x];
}

static uint32_t tile_addr(const ct_scene_layer *L, uint16_t e)
{
    return (L->chr_base + (e & 0x3FFu) * (L->bpp == 2 ? 8u : 16u)) & S3D_ADDR_MASK;
}

static int tile_empty(const ct_scene *s, const ct_scene_layer *L, uint16_t e)
{
    uint32_t a = tile_addr(L, e);
    int n = L->bpp == 2 ? 8 : 16;
    for (int k = 0; k < n; k++)
        if (s->vram[(a + (uint32_t)k) & S3D_ADDR_MASK])
            return 0;
    return 1;
}

/* Color index (0-15) of pixel (x, y) of the tile e names. */
static int pixel(const ct_scene *s, const ct_scene_layer *L, uint16_t e, int x, int y)
{
    if (e & 0x4000)
        x = 7 - x;
    if (e & 0x8000)
        y = 7 - y;
    uint32_t a = tile_addr(L, e);
    unsigned w0 = s->vram[(a + (uint32_t)y) & S3D_ADDR_MASK], sh = 7u - (unsigned)x;
    int c = (int)((w0 >> sh) & 1u) | (int)((w0 >> (sh + 8)) & 1u) << 1;
    if (L->bpp != 2) {
        unsigned w1 = s->vram[(a + 8 + (uint32_t)y) & S3D_ADDR_MASK];
        c |= (int)((w1 >> sh) & 1u) << 2 | (int)((w1 >> (sh + 8)) & 1u) << 3;
    }
    return c;
}

typedef struct {
    float r, g, b;   /* mean, 0-1 */
    float lum;
    float blue;      /* share of pixels that are blue */
} feat;

/* The cell as the console composites it: the front layer over the back
   one, color 0 of both the backdrop. */
static feat features(const ct_scene *s, int x, int y)
{
    const ct_scene_layer *F = layer_of(s, 0), *B = layer_of(s, 1);
    uint16_t ef = F ? entry(F, x, y) : 0, eb = B ? entry(B, x, y) : 0;
    feat f = {0};
    int nblue = 0;
    for (int py = 0; py < 8; py++)
        for (int px = 0; px < 8; px++) {
            int c = F ? pixel(s, F, ef, px, py) : 0, ci = 0;
            if (c)
                ci = F->pal_base + (int)((ef >> 10) & 7) * (F->bpp == 2 ? 4 : 16) + c;
            else if (B && (c = pixel(s, B, eb, px, py)) != 0)
                ci = B->pal_base + (int)((eb >> 10) & 7) * (B->bpp == 2 ? 4 : 16) + c;
            uint16_t col = s->cgram[ci & 255];
            float r = (float)(col & 31) / 31.0f, g = (float)((col >> 5) & 31) / 31.0f,
                  b = (float)((col >> 10) & 31) / 31.0f;
            f.r += r;
            f.g += g;
            f.b += b;
            if (b > r + 0.06f && b >= g)
                nblue++;
        }
    f.r /= 64.0f;
    f.g /= 64.0f;
    f.b /= 64.0f;
    f.lum = 0.3f * f.r + 0.6f * f.g + 0.1f * f.b;
    f.blue = (float)nblue / 64.0f;
    return f;
}

/* ---- relief ---- */

/* Chessboard distance (cells, capped) to the nearest cell of another class. */
static uint8_t dist[WS][WS];

static void distances(int cls)
{
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            dist[y][x] = wcls[y][x] == cls ? DIST_CAP : 0;
    for (int it = 0; it < DIST_CAP; it++)
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                if (!dist[y][x])
                    continue;
                int m = dist[y][x];
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (!gwrap && (nx < 0 || ny < 0 || nx >= gw || ny >= gh))
                            continue;   /* the map's edge doesn't end a mountain */
                        int v = dist[cell_y(ny)][cell_x(nx)] + 1;
                        m = v < m ? v : m;
                    }
                dist[y][x] = (uint8_t)m;
            }
}

static void classify(const ct_scene *s)
{
    static uint8_t water[WS][WS];
    static feat fs[WS][WS];
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            const ct_scene_cell *c = &s->cells[y * WS + x];
            feat f = features(s, x, y);
            fs[y][x] = f;
            lum_[y][x] = f.lum;
            water[y][x] = (c->cls == CT_CELL_WATER && f.blue >= 0.5f) || f.blue >= 0.9f;
        }
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            const ct_scene_cell *c = &s->cells[y * WS + x];
            const feat *f = &fs[y][x];
            int k;
            if (water[y][x])
                k = S3D_W_WATER;
            else if (c->cls == CT_CELL_MOUNTAIN)
                k = S3D_W_MOUNTAIN;
            else if (c->cls == CT_CELL_FOREST)
                k = S3D_W_FOREST;
            else if (c->cls == CT_CELL_SHORE)
                k = S3D_W_SHORE;
            else if (c->cls == CT_CELL_FLAT || c->cls == CT_CELL_FLOOR ||
                     c->cls == CT_CELL_STAIRS || c->cls == CT_CELL_UNKNOWN)
                k = c->cls == CT_CELL_FLAT ? S3D_W_FLAT : S3D_W_FLOOR;
            else {   /* blocked, kind unknown: from the pixels */
                int near = 0;
                for (int dy = -2; dy <= 2 && !near; dy++)
                    for (int dx = -2; dx <= 2 && !near; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (gwrap || (nx >= 0 && ny >= 0 && nx < gw && ny < gh))
                            near = water[cell_y(ny)][cell_x(nx)];
                    }
                int green = f->g > f->r + 0.016f && f->g > f->b + 0.016f;
                int red = f->r >= f->g && f->r > f->b + 0.05f;
                if (green)
                    k = S3D_W_FOREST;
                else if (near && !(red && f->lum < 0.42f))
                    k = S3D_W_SHORE;
                else if (red)
                    k = S3D_W_MOUNTAIN;
                else
                    k = S3D_W_FLAT;
            }
            wcls[y][x] = (uint8_t)k;
        }
}

static void heights(void)
{
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            cellh[y][x] = wcls[y][x] == S3D_W_WATER ? WATER_Y
                          : wcls[y][x] == S3D_W_SHORE ? SHORE_Y : 0.0f;

    distances(S3D_W_FOREST);
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            if (wcls[y][x] == S3D_W_FOREST) {
                float h = dist[y][x] <= 1 ? 4.0f : 8.0f, j = (lum_[y][x] - 0.3f) * 14.0f;
                cellh[y][x] = h + (j < -2 ? -2 : j > 2 ? 2 : j);
            }

    /* Mountains: a dome, and each mountain's own brightness as ridges.
       A "mountain" too thin to have a middle (two cells across at most:
       walls and roofs in brown) lies flat. */
    distances(S3D_W_MOUNTAIN);
    static int16_t stack[WS * WS][2];
    static uint8_t seen[WS][WS];
    memset(seen, 0, sizeof seen);
    for (int y0 = 0; y0 < gh; y0++)
        for (int x0 = 0; x0 < gw; x0++) {
            if (wcls[y0][x0] != S3D_W_MOUNTAIN || seen[y0][x0])
                continue;
            int n = 0, top = 0, deep = 0;
            stack[top][0] = (int16_t)x0;
            stack[top++][1] = (int16_t)y0;
            seen[y0][x0] = 1;
            while (n < top) {   /* the component, breadth first; stack keeps it all */
                int x = stack[n][0], y = stack[n][1];
                n++;
                deep |= dist[y][x] >= 3;
                for (int k = 0; k < 9; k++) {
                    int nx = x + k % 3 - 1, ny = y + k / 3 - 1;
                    if (!gwrap && (nx < 0 || ny < 0 || nx >= gw || ny >= gh))
                        continue;
                    nx = cell_x(nx);
                    ny = cell_y(ny);
                    if (wcls[ny][nx] == S3D_W_MOUNTAIN && !seen[ny][nx]) {
                        seen[ny][nx] = 1;
                        stack[top][0] = (int16_t)nx;
                        stack[top++][1] = (int16_t)ny;
                    }
                }
            }
            if (!deep)
                for (int k = 0; k < top; k++)
                    wcls[stack[k][1]][stack[k][0]] = S3D_W_FLAT;
        }
    float lsum = 0;
    int ln = 0;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            if (wcls[y][x] == S3D_W_MOUNTAIN) {
                lsum += lum_[y][x];
                ln++;
            }
    float lmean = ln ? lsum / (float)ln : 0.3f;
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            if (wcls[y][x] == S3D_W_MOUNTAIN) {
                float t = (float)dist[y][x] / 6.0f;
                t = t > 1 ? 1 : t;
                float h = 48.0f * (1.0f - (1.0f - t) * (1.0f - t));
                float r = (lum_[y][x] - lmean) * 30.0f * t;
                cellh[y][x] = h + (r < -5 ? -5 : r > 5 ? 5 : r);
            }

    /* Corners. */
    int cw = gwrap ? gw : gw + 1, ch = gwrap ? gh : gh + 1;
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            float sum = 0;
            int wet = 0, n = 0;
            for (int k = 0; k < 4; k++) {
                int cx = x - 1 + (k & 1), cy = y - 1 + (k >> 1);
                if (!gwrap && (cx < 0 || cy < 0 || cx >= gw || cy >= gh))
                    continue;
                cx = cell_x(cx);
                cy = cell_y(cy);
                wet |= wcls[cy][cx] == S3D_W_WATER;
                sum += cellh[cy][cx];
                n++;
            }
            vh[y][x] = wet ? WATER_Y : n ? sum / (float)n : 0.0f;
        }
    if (gwrap) {   /* the last row and column repeat the first */
        for (int y = 0; y < gh; y++)
            vh[y][gw] = vh[y][0];
        for (int x = 0; x <= gw; x++)
            vh[gh][x] = vh[0][x];
    }

    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            float dx = (corner(x + 1, y) - corner(x - 1, y)) / 16.0f;
            float dz = (corner(x, y + 1) - corner(x, y - 1)) / 16.0f;
            float l = sqrtf(dx * dx + 1.0f + dz * dz);
            uint8_t *o = &nrm[(y * gw + x) * 4];
            o[0] = (uint8_t)lrintf((-dx / l * 0.5f + 0.5f) * 255.0f);
            o[1] = (uint8_t)lrintf((1.0f / l * 0.5f + 0.5f) * 255.0f);
            o[2] = (uint8_t)lrintf((-dz / l * 0.5f + 0.5f) * 255.0f);
            o[3] = 255;
        }
}

/* ---- the mesh ---- */

static uint32_t pack(const ct_scene_layer *L, uint16_t e, int normal, uint32_t flags)
{
    return tile_addr(L, e) | (uint32_t)((e >> 10) & 7) << S3D_PAL_SHIFT |
           (L->bpp == 2 ? S3D_2BPP : 0) | (uint32_t)normal << S3D_NORMAL_SHIFT |
           (e & 0x2000 ? S3D_OVERHEAD : 0) | flags;
}

static int emit_cell(const ct_scene *s, s3d_mesh *m, int x, int y)
{
    float c[4][3];
    float hi = -1e9f, lo = 1e9f;
    for (int k = 0; k < 4; k++) {
        int cx = x + (k & 1), cy = y + (k >> 1);
        c[k][0] = (float)cx * 8;
        c[k][1] = corner(cx, cy);
        c[k][2] = (float)cy * 8;
        hi = c[k][1] > hi ? c[k][1] : hi;
        lo = c[k][1] < lo ? c[k][1] : lo;
    }
    int wet = wcls[y][x] == S3D_W_WATER;
    int normal = wet ? S3D_N_WATER : hi - lo < 0.01f ? S3D_N_UP : S3D_N_TERRAIN;
    uint32_t raised = hi > 0.75f ? S3D_RAISED : 0;
    const ct_scene_layer *B = layer_of(s, 1), *F = layer_of(s, 0);
    int bad = 0;
    if (B) {
        uint16_t e = entry(B, x, y);
        bad |= s3d_emit_corners(m, c, pack(B, e, normal, raised | S3D_OPAQUE), e & 0x4000,
                                e & 0x8000);
    }
    if (F) {
        uint16_t e = entry(F, x, y);
        /* On the same corners, right after the back layer's quad: the
           renderer draws the world map with a depth test that lets the
           later of two equal depths win (a lift would show slivers of the
           back layer on the slopes). */
        if (!tile_empty(s, F, e))
            bad |= s3d_emit_corners(m, c, pack(F, e, normal == S3D_N_WATER ? S3D_N_UP : normal,
                                               raised),
                                    e & 0x4000, e & 0x8000);
    }
    return bad;
}

/* One mark per group of touching event cells (an entrance several cells
   wide is one place). */
static void find_marks(const ct_scene *s)
{
    static uint8_t seen[WS][WS];
    static int16_t q[WS * WS][2];
    memset(seen, 0, sizeof seen);
    n_marks = 0;
    for (int y0 = 0; y0 < gh; y0++)
        for (int x0 = 0; x0 < gw; x0++) {
            if (seen[y0][x0] || !(s->cells[y0 * WS + x0].flags & CT_CELL_EVENT))
                continue;
            int n = 0, top = 1;
            long sx = 0, sy = 0;
            q[0][0] = (int16_t)x0;
            q[0][1] = (int16_t)y0;
            seen[y0][x0] = 1;
            while (n < top) {
                int x = q[n][0], y = q[n][1];
                n++;
                sx += x;
                sy += y;
                for (int k = 0; k < 9; k++) {
                    int nx = x + k % 3 - 1, ny = y + k / 3 - 1;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh || seen[ny][nx] ||
                        !(s->cells[ny * WS + nx].flags & CT_CELL_EVENT))
                        continue;
                    seen[ny][nx] = 1;
                    q[top][0] = (int16_t)nx;
                    q[top++][1] = (int16_t)ny;
                }
            }
            if (n_marks < MAX_MARKS) {
                marks_xz[n_marks][0] = (float)sx / (float)n * 8.0f + 4.0f;
                marks_xz[n_marks][1] = (float)sy / (float)n * 8.0f + 4.0f;
                n_marks++;
            }
        }
}

int s3d_build_world(const ct_scene *s, s3d_mesh *m)
{
    m->nv = m->ni = m->ni_floor = 0;
    if (s->mode != CT_SCENE_WORLD || !s->cells || !s->vram || !s->cgram || !s->map_w ||
        !s->map_h || s->map_w > WS || s->map_h > WS) {
        gw = gh = 0;
        return 0;
    }
    gw = s->map_w;
    gh = s->map_h;
    gwrap = s->wrap != 0;
    classify(s);
    heights();
    find_marks(s);
    int bad = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                float hi = corner(x, y);
                for (int k = 1; k < 4; k++) {
                    float v = corner(x + (k & 1), y + (k >> 1));
                    hi = v > hi ? v : hi;
                }
                if ((hi > 0.75f) == pass)
                    bad |= emit_cell(s, m, x, y);
            }
        if (pass == 0)
            m->ni_floor = m->ni;
    }
    return bad ? -1 : m->ni / 6;
}

/* ---- sprites ---- */

static int oam_entry(const ct_scene *s, int e, int *x, int *y, int *size)
{
    uint16_t w0 = s->oam[2 * e];
    unsigned hi = s->oam[0x100 + e / 8] >> (2 * (e % 8));
    *size = s->obj_size[(hi >> 1) & 1];
    *x = (w0 & 0xFF) | (int)(hi & 1) << 8;
    if (*x >= 256)
        *x -= 512;
    *y = w0 >> 8;
    if (*y >= 224 && *y + *size <= 256)
        return 0;
    if (*y + *size > 256)
        *y -= 256;
    return *x > -*size && *x < 256;
}

/* Target pixel (px, py) at view depth d, back in the world. */
static void unproject(const s3d_view *v, float px, float py, float d, float out[3])
{
    float a = (px - 0.5f * (float)v->w) / v->focal, b = (0.5f * (float)v->h - py) / v->focal;
    for (int k = 0; k < 3; k++)
        out[k] = v->eye[k] + d * (v->fwd[k] + a * v->right[k] + b * v->up[k]);
}

/* One OAM entry's 8x8 pieces as a camera-facing rectangle at depth d:
   console pixel (cx, cy) goes to target pixel (ox + cx * kx, oy + cy * ky). */
static void emit_entry(const ct_scene *s, s3d_mesh *m, const s3d_view *v, int e, float d,
                       float ox, float oy, float kx, float ky, uint32_t flags)
{
    int x, y, size;
    if (!oam_entry(s, e, &x, &y, &size))
        return;
    uint16_t w1 = s->oam[2 * e + 1];
    int hf = (w1 >> 14) & 1, vf = (w1 >> 15) & 1, n = size / 8;
    uint32_t base = s->obj_chr[(w1 >> 8) & 1], pal = (w1 >> 9) & 7;
    for (int ty = 0; ty < n; ty++)
        for (int tx = 0; tx < n; tx++) {
            int col = (hf ? n - 1 - tx : tx) * 8, row = (vf ? n - 1 - ty : ty) * 8;
            int used = ((((w1 & 0xFF) >> 4) + (row >> 3)) << 4) | (((w1 & 0xF) + (col >> 3)) & 0xF);
            uint32_t addr = (base + (uint32_t)used * 16) & S3D_ADDR_MASK;
            float x0 = floorf(ox + (float)(x + tx * 8) * kx + 0.5f),
                  x1 = floorf(ox + (float)(x + tx * 8 + 8) * kx + 0.5f),
                  y0 = floorf(oy + (float)(y + ty * 8) * ky + 0.5f),
                  y1 = floorf(oy + (float)(y + ty * 8 + 8) * ky + 0.5f);
            float c[4][3];
            unproject(v, x0, y0, d, c[0]);
            unproject(v, x1, y0, d, c[1]);
            unproject(v, x0, y1, d, c[2]);
            unproject(v, x1, y1, d, c[3]);
            s3d_emit_corners(m, c,
                             addr | pal << S3D_PAL_SHIFT | S3D_OBJ |
                                 (uint32_t)S3D_N_SPRITE << S3D_NORMAL_SHIFT | flags,
                             hf, vf);
        }
}

int s3d_build_world_sprites(const ct_scene *s, const s3d_view *v, int marks, s3d_mesh *m)
{
    m->nv = m->ni = m->ni_floor = 0;
    if (s->mode != CT_SCENE_WORLD || !s->oam || !s->vram || v->focal <= 0)
        return 0;
    float mw = (float)s->map_w * s->cell_px, mh = (float)s->map_h * s->cell_px;

    /* Entrance marks: a small diamond in the middle of each entrance,
       just above the ground (no sprite pixels: the shader draws it). */
    for (int k = 0; marks && k < n_marks; k++) {
        float cx = marks_xz[k][0], cz = marks_xz[k][1];
        if (s->wrap) {   /* the copy nearest the point looked at */
            cx += mw * roundf((v->at[0] - cx) / mw);
            cz += mh * roundf((v->at[2] - cz) / mh);
        }
        float h = s3d_world_height(cx, cz) + 0.6f;
        float c[4][3] = {{cx - 4, h, cz - 4}, {cx + 4, h, cz - 4}, {cx - 4, h, cz + 4},
                         {cx + 4, h, cz + 4}};
        s3d_emit_corners(m, c, S3D_OBJ | (uint32_t)S3D_N_WATER << S3D_NORMAL_SHIFT, 0, 0);
    }

    uint8_t owned[128] = {0};
    for (int k = 0; k < s->n_objects && k < CT_SCENE_MAX_OBJECTS; k++) {
        const ct_scene_object *o = &s->object[k];
        if (!o->n_pieces)
            continue;
        for (int j = 0; j < o->n_pieces; j++)
            owned[o->piece[j] & 127] = 1;
        float fx = (float)o->x, fz = (float)o->y;
        if (s->wrap) {   /* the copy nearest the point looked at */
            fx += mw * roundf((v->at[0] - fx) / mw);
            fz += mh * roundf((v->at[2] - fz) / mh);
        }
        float F[3] = {fx, s3d_world_height(fx, fz) - (float)o->z, fz};
        float rel[3] = {F[0] - v->eye[0], F[1] - v->eye[1], F[2] - v->eye[2]};
        float d = rel[0] * v->fwd[0] + rel[1] * v->fwd[1] + rel[2] * v->fwd[2];
        if (d <= 1.0f)
            continue;
        /* Where the foot lands, snapped, and a whole number of target
           pixels per sprite pixel, the ground's own scale there. */
        float sx = 0.5f * (float)v->w +
                   v->focal * (rel[0] * v->right[0] + rel[1] * v->right[1] + rel[2] * v->right[2]) / d;
        float sy = 0.5f * (float)v->h -
                   v->focal * (rel[0] * v->up[0] + rel[1] * v->up[1] + rel[2] * v->up[2]) / d;
        float kk = floorf(v->focal / d + 0.5f);
        kk = kk < 1 ? 1 : kk;
        /* The foot's console position: the pieces keep their layout around it. */
        float fcx = (float)o->x - (float)s->camera_x, fcy = (float)o->y - (float)s->camera_y;
        if (s->wrap) {
            fcx -= mw * roundf(fcx / mw);
            fcy -= mh * roundf(fcy / mh);
        }
        float ox = floorf(sx + 0.5f) - fcx * kk, oy = floorf(sy + 0.5f) - fcy * kk;
        for (int j = 0; j < o->n_pieces; j++) {
            int e = o->piece[j] & 127;
            /* Lower OAM entries draw in front: a hair toward the camera. */
            emit_entry(s, m, v, e, d - (float)(127 - e) * 0.01f, ox, oy, kk, kk, S3D_RAISED);
        }
    }
    /* The rest (captions, cursors): flat over the picture where the console
       drew them, close to the camera. */
    float near_d = v->near * 1.5f;
    float kx = (float)v->w / 256.0f, ky = (float)v->h / 224.0f;
    for (int e = 0; e < 128; e++) {
        int x, y, size;
        if (owned[e] || !oam_entry(s, e, &x, &y, &size))
            continue;
        emit_entry(s, m, v, e, near_d - (float)(127 - e) * 0.001f, 0.0f, 0.0f, kx, ky,
                   S3D_OVERHEAD);
    }
    return m->ni / 6;
}

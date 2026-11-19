/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: remastered menus (uipanel.h). See the header for what it finds;
 * how, per BG layer 0-2:
 *
 * 1. The layer's 8x8 cells on the screen, from the provenance G-buffer:
 *    each pixel names its character and where in it the texel is (u, v),
 *    so the cell is (x - u, y - v) / 8 whatever the scroll. A cell's key is
 *    its character, palette and flips.
 * 2. Connected groups of the layer's cells (4-neighbours), and each one's
 *    box. A window: at least 3 x 2 cells, filled to 85%, not the whole
 *    screen, with a frame: the top, bottom, left and right edges each
 *    mostly one key, none of them inside.
 * 3. Its background: the keys inside that repeat (at least twice, and a
 *    fifth as often as the commonest). Text the game renders into tiles of
 *    their own and icons appear once, so they stay; frame and background
 *    pixels are marked for the panel, and so are the console glyphs HD
 *    text took out (hdtext.h), which it filled with the box's colors.
 *
 * Map layers (a battle's field, a grass patch) rarely have a frame on all
 * four sides of one key each, and a box over most of the screen is never
 * taken. */
#include "uipanel.h"

#include <string.h>

enum { W = SCHED_WIDTH, H = SCHED_HEIGHT, CW = W / 8 + 2, CH = H / 8 + 2 };

static uint32_t cell_key[3][CH][CW];   /* 0: none; else key + 1 */
static uint8_t cell_on[3][CH][CW];
static int16_t comp[CH][CW];

static uint32_t key_of(const sched_prov_px *p)
{
    return ((uint32_t)p->chr | (uint32_t)p->pal << 16 |
            (uint32_t)(p->flags & 0x0C) << 22) + 1u;
}

/* The commonest key among n, and how often (0: none). */
static uint32_t mode_of(const uint32_t *k, int n, int *count)
{
    uint32_t best = 0;
    int bc = 0;
    for (int i = 0; i < n; i++) {
        if (!k[i])
            continue;
        int c = 0;
        for (int j = 0; j < n; j++)
            c += k[j] == k[i];
        if (c > bc) {
            bc = c;
            best = k[i];
        }
    }
    *count = bc;
    return best;
}

static int edge_ok(const uint32_t *k, int n, uint32_t *key)
{
    int c;
    *key = mode_of(k, n, &c);
    return n <= 0 || (*key && c * 10 >= n * 7);
}

int uipanel_frame(const sched_prov_px *prov, const uint8_t *text_mask, uint8_t *mask,
                  float rects[UIPANEL_MAX][4])
{
    memset(mask, 0, (size_t)W * H);
    if (!prov)
        return 0;
    memset(cell_key, 0, sizeof cell_key);
    memset(cell_on, 0, sizeof cell_on);
    /* The cells' origins, for the pixels' own cell: x = 8 * cx + ox. */
    int ox[3] = {0, 0, 0}, oy[3] = {0, 0, 0}, have[3] = {0, 0, 0};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const sched_prov_px *p = &prov[y * W + x];
            if ((p->flags & SCHED_PROV_OBJ) || !(p->flags & SCHED_PROV_BPP) || p->index > 2)
                continue;
            int l = p->index;
            if (!have[l]) {
                ox[l] = ((x - p->u) % 8 + 8) % 8;
                oy[l] = ((y - p->v) % 8 + 8) % 8;
                have[l] = 1;
            }
            int cx = (x - ox[l] + 8) / 8, cy = (y - oy[l] + 8) / 8;
            if (cx < 0 || cy < 0 || cx >= CW || cy >= CH)
                continue;
            cell_on[l][cy][cx] = 1;
            cell_key[l][cy][cx] = key_of(p);
        }

    int n = 0;
    for (int l = 0; l < 3 && n < UIPANEL_MAX; l++) {
        if (!have[l])
            continue;
        memset(comp, -1, sizeof comp);
        static int16_t stack[CW * CH][2];
        int nc = 0;
        for (int sy = 0; sy < CH && n < UIPANEL_MAX; sy++)
            for (int sx = 0; sx < CW && n < UIPANEL_MAX; sx++) {
                if (!cell_on[l][sy][sx] || comp[sy][sx] >= 0)
                    continue;
                /* flood the group, its box and size */
                int top = 0, cells = 0, x0 = sx, x1 = sx, y0 = sy, y1 = sy;
                stack[top][0] = (int16_t)sx;
                stack[top++][1] = (int16_t)sy;
                comp[sy][sx] = (int16_t)nc;
                while (top) {
                    int cx = stack[--top][0], cy = stack[top][1];
                    cells++;
                    x0 = cx < x0 ? cx : x0;
                    x1 = cx > x1 ? cx : x1;
                    y0 = cy < y0 ? cy : y0;
                    y1 = cy > y1 ? cy : y1;
                    static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (int k = 0; k < 4; k++) {
                        int nx = cx + d[k][0], ny = cy + d[k][1];
                        if (nx >= 0 && ny >= 0 && nx < CW && ny < CH && cell_on[l][ny][nx] &&
                            comp[ny][nx] < 0) {
                            comp[ny][nx] = (int16_t)nc;
                            stack[top][0] = (int16_t)nx;
                            stack[top++][1] = (int16_t)ny;
                        }
                    }
                }
                int id = nc++;
                int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
                if (bw < 3 || bh < 2 || cells * 100 < bw * bh * 85 || bw * bh > 26 * 24)
                    continue;
                /* the frame: each edge (corners aside) mostly one key */
                uint32_t e[4][CW + CH], ek[4];
                int en[4] = {0, 0, 0, 0};
                for (int cx = x0 + 1; cx < x1; cx++) {
                    e[0][en[0]++] = comp[y0][cx] == id ? cell_key[l][y0][cx] : 0;
                    e[1][en[1]++] = comp[y1][cx] == id ? cell_key[l][y1][cx] : 0;
                }
                for (int cy = y0 + 1; cy < y1; cy++) {
                    e[2][en[2]++] = comp[cy][x0] == id ? cell_key[l][cy][x0] : 0;
                    e[3][en[3]++] = comp[cy][x1] == id ? cell_key[l][cy][x1] : 0;
                }
                int framed = 1;
                for (int k = 0; k < 4; k++)
                    framed &= edge_ok(e[k], en[k], &ek[k]);
                if (!framed)
                    continue;
                /* the background: keys inside that repeat */
                static uint32_t in[CW * CH];
                int ni = 0;
                for (int cy = y0 + 1; cy < y1; cy++)
                    for (int cx = x0 + 1; cx < x1; cx++)
                        if (comp[cy][cx] == id)
                            in[ni++] = cell_key[l][cy][cx];
                int top_count;
                mode_of(in, ni, &top_count);
                int edge_inside = 0;
                for (int i = 0; i < ni; i++)
                    for (int k = 0; k < 4; k++)
                        edge_inside += in[i] == ek[k] && en[k] > 0;
                if (edge_inside * 4 > ni)   /* the "frame" is inside too: a map pattern */
                    continue;
                static uint8_t bg[CH][CW];
                memset(bg, 0, sizeof bg);
                int marked = 0;
                for (int cy = y0; cy <= y1; cy++)
                    for (int cx = x0; cx <= x1; cx++) {
                        if (comp[cy][cx] != id)
                            continue;
                        int ring = cy == y0 || cy == y1 || cx == x0 || cx == x1;
                        int c = 0;
                        if (!ring)
                            for (int i = 0; i < ni; i++)
                                c += in[i] == cell_key[l][cy][cx];
                        if (ring || (c >= 2 && c * 5 >= top_count)) {
                            bg[cy][cx] = 1;
                            marked++;
                        }
                    }
                if (marked * 10 < bw * bh * 3)
                    continue;
                /* its pixels: this layer's, in a marked cell */
                int px0 = x0 * 8 + ox[l] - 8, py0 = y0 * 8 + oy[l] - 8;
                for (int y = py0 < 0 ? 0 : py0; y < py0 + bh * 8 && y < H; y++)
                    for (int x = px0 < 0 ? 0 : px0; x < px0 + bw * 8 && x < W; x++) {
                        const sched_prov_px *p = &prov[y * W + x];
                        int cx = (x - ox[l] + 8) / 8, cy = (y - oy[l] + 8) / 8;
                        int mine = !(p->flags & SCHED_PROV_OBJ) && (p->flags & SCHED_PROV_BPP) &&
                                   p->index == l && bg[cy][cx];
                        if (mine || (text_mask && text_mask[y * W + x]))
                            mask[y * W + x] = 1;
                    }
                rects[n][0] = (float)px0;
                rects[n][1] = (float)py0;
                rects[n][2] = (float)(bw * 8);
                rects[n][3] = (float)(bh * 8);
                n++;
            }
    }
    return n;
}

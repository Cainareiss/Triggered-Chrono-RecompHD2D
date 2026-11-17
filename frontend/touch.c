/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On-screen touch controller (touch.h). */
#include "touch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hd2d/osd_font.h"
#include "input.h"
#include "settings.h"

#define PIC_W 256
#define PIC_H 224
#define FPS 60
#define LONG_PRESS_FRAMES 50
#define MAX_FINGERS 10
#define PI_F 3.14159265f
#define MOUSE_FINGER ((SDL_FingerID)-12345)

/* ---- the picture ---- */

touch_rect touch_picture_plain(int w, int h)
{
    int s = w / PIC_W < h / PIC_H ? w / PIC_W : h / PIC_H;
    if (s < 1)
        s = 1;
    touch_rect r = {(w - PIC_W * s) / 2, (h - PIC_H * s) / 2, PIC_W * s, PIC_H * s};
    return r;
}

touch_rect touch_picture_aspect(int w, int h, double aspect)
{
    touch_rect r = {0, 0, w, h};
    if (aspect <= 0)
        return r;
    r.w = (int)lround(h * aspect);
    if (r.w > w) {
        r.w = w;
        r.h = (int)lround(w / aspect);
    }
    r.x = (w - r.w) / 2;
    r.y = (h - r.h) / 2;
    return r;
}

/* ---- controls and layout ---- */

enum { C_DPAD, C_A, C_B, C_X, C_Y, C_L, C_R, C_SELECT, C_START, C_COUNT };
/* Groups move and resize together (edit mode): the face buttons are one. */
enum { G_DPAD, G_FACE, G_L, G_R, G_SELECT, G_START, G_COUNT };
static const int ctl_group[C_COUNT] = {G_DPAD, G_FACE, G_FACE, G_FACE, G_FACE,
                                       G_L,    G_R,    G_SELECT, G_START};
static const uint16_t ctl_bit[C_COUNT] = {0, PAD_A, PAD_B, PAD_X, PAD_Y,
                                          PAD_L, PAD_R, PAD_SELECT, PAD_START};
static const char *const ctl_label[C_COUNT] = {"", "A", "B", "X", "Y", "L", "R", "SELECT", "START"};
static const char *const group_key[G_COUNT] = {"dpad", "face", "l", "r", "select", "start"};

/* Nominal sizes, millimetres. */
#define DPAD_R 12.0f
#define FACE_R 4.6f
#define FACE_OFF 7.2f
#define SHOULDER_HW 8.0f
#define SHOULDER_HH 3.5f
#define SMALL_HW 6.0f
#define SMALL_HH 2.5f
#define MARGIN 4.0f

/* port: the touch options (touch.h): opacity and size in percent, hand
   0 = D-pad on the left (right-handed), 1 = mirrored. */
static int opt_opacity = TOUCH_OPACITY_DEFAULT, opt_size = 100, opt_hand;

typedef struct {
    float x, y, hw, hh, cr;   /* centre, half size, corner radius (px) */
} shape;

typedef struct {
    int w, h, wide;
    float k;                  /* px per mm */
    float gx[G_COUNT], gy[G_COUNT], gs[G_COUNT];   /* group centre and scale */
    shape c[C_COUNT];
} layout;

/* Edit-mode adjustments per layout kind (0 narrow, 1 wide): offset as
   fractions of the output, scale multiplier. */
static struct {
    float dx, dy, s;
} adjust[2][G_COUNT];

static float base_k(SDL_Window *win, int out_w, int out_h)
{
    float ddpi = 0;
    if (win) {
        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        int d = SDL_GetWindowDisplayIndex(win);
        if (d >= 0 && !SDL_GetDisplayDPI(d, &ddpi, NULL, NULL) && ddpi >= 50 && ddpi <= 1200 &&
            ww > 0)
            return ddpi / 25.4f * (float)out_w / (float)ww;
    }
    /* Unknown DPI: assume a phone, whose short side is about 68 mm. */
    return (float)(out_w < out_h ? out_w : out_h) / 68.0f;
}

static void layout_make(layout *L, int w, int h, touch_rect pic, float k)
{
    memset(L, 0, sizeof *L);
    L->w = w;
    L->h = h;
    /* Wide: a side area holds a 28 mm column (D-pad plus 2 mm each side)
       at half size or more. */
    float side = (float)pic.x, col = 2 * DPAD_R + 4.0f;
    L->wide = side >= 0.5f * col * k && pic.w < w;
    float kk = k;
    if (L->wide) {
        if (kk > side / col)
            kk = side / col;
        if (kk > h / 56.0f)
            kk = h / 56.0f;
    } else {
        if (kk > w / 66.0f)
            kk = w / 66.0f;
        if (kk > h / 60.0f)
            kk = h / 60.0f;
    }
    kk *= (float)opt_size / 100.0f;   /* port: touch_size */
    L->k = kk;
    float lx, rx, cy = h - (MARGIN + 2 * SMALL_HH + MARGIN + DPAD_R) * kk;
    if (L->wide) {
        lx = side / 2;
        rx = w - side / 2;
        L->gx[G_SELECT] = lx;
        L->gx[G_START] = rx;
        L->gy[G_SELECT] = L->gy[G_START] = h - (MARGIN + SMALL_HH) * kk;
        L->gy[G_L] = L->gy[G_R] = (MARGIN + SHOULDER_HH) * kk;
    } else {
        lx = (MARGIN + 2 + DPAD_R) * kk;
        rx = w - lx;
        L->gx[G_SELECT] = w / 2.0f - (SMALL_HW + 1.5f) * kk;
        L->gx[G_START] = w / 2.0f + (SMALL_HW + 1.5f) * kk;
        L->gy[G_SELECT] = L->gy[G_START] = h - (MARGIN + SMALL_HH) * kk;
        L->gy[G_L] = L->gy[G_R] = cy - (DPAD_R + MARGIN + SHOULDER_HH) * kk;
    }
    if (opt_hand) {   /* port: touch_hand: the sides trade places */
        float t = lx;
        lx = rx;
        rx = t;
    }
    L->gx[G_DPAD] = L->gx[G_L] = lx;
    L->gx[G_FACE] = L->gx[G_R] = rx;
    L->gy[G_DPAD] = L->gy[G_FACE] = cy;
    for (int g = 0; g < G_COUNT; g++) {
        L->gx[g] += adjust[L->wide][g].dx * w;
        L->gy[g] += adjust[L->wide][g].dy * h;
        L->gs[g] = adjust[L->wide][g].s > 0 ? adjust[L->wide][g].s : 1.0f;
    }
    for (int c = 0; c < C_COUNT; c++) {
        int g = ctl_group[c];
        float s = L->gs[g] * kk, x = L->gx[g], y = L->gy[g];
        shape *p = &L->c[c];
        p->x = x;
        p->y = y;
        switch (c) {
        case C_DPAD: p->hw = p->hh = p->cr = DPAD_R * s; break;
        case C_A: p->x += FACE_OFF * s; break;
        case C_B: p->y += FACE_OFF * s; break;
        case C_X: p->y -= FACE_OFF * s; break;
        case C_Y: p->x -= FACE_OFF * s; break;
        case C_L: case C_R:
            p->hw = SHOULDER_HW * s;
            p->hh = p->cr = SHOULDER_HH * s;
            break;
        default:
            p->hw = SMALL_HW * s;
            p->hh = p->cr = SMALL_HH * s;
        }
        if (c >= C_A && c <= C_Y)
            p->hw = p->hh = p->cr = FACE_R * s;
    }
}

/* A group's box (edit mode), px. */
static void group_box(const layout *L, int g, float *x0, float *y0, float *x1, float *y1)
{
    *x0 = *y0 = 1e9f;
    *x1 = *y1 = -1e9f;
    for (int c = 0; c < C_COUNT; c++)
        if (ctl_group[c] == g) {
            const shape *p = &L->c[c];
            *x0 = fminf(*x0, p->x - p->hw);
            *y0 = fminf(*y0, p->y - p->hh);
            *x1 = fmaxf(*x1, p->x + p->hw);
            *y1 = fmaxf(*y1, p->y + p->hh);
        }
}

/* Distance from the shape's edge, px (negative inside). */
static float shape_dist(const shape *p, float x, float y)
{
    float qx = fabsf(x - p->x) - (p->hw - p->cr), qy = fabsf(y - p->y) - (p->hh - p->cr);
    float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - p->cr;
}

/* ---- the quick menu handle ---- */

/* A small pill at the top edge's middle that opens the quick menu with a
   tap; it fades a few seconds after the last touch (any touch brings it
   back) and is left out where it would touch a control. Hit area bigger
   than the pill. */
#define HANDLE_FULL_FRAMES (3 * FPS)
#define HANDLE_FADE_FRAMES (FPS)

static int handle_rect(const layout *L, int hit, float *x0, float *y0, float *x1, float *y1)
{
    float hw = (hit ? 13.0f : 8.0f) * L->k, top = hit ? 0.0f : 1.6f * L->k;
    float hh = hit ? 9.0f * L->k : 3.2f * L->k;
    *x0 = L->w * 0.5f - hw;
    *x1 = L->w * 0.5f + hw;
    *y0 = top;
    *y1 = top + hh;
    for (int c = 0; c < C_COUNT; c++) {   /* never over the on-screen controller */
        const shape *p = &L->c[c];
        float m = 2.0f * L->k;
        if (p->x + p->hw + m > *x0 && p->x - p->hw - m < *x1 && p->y + p->hh + m > *y0 &&
            p->y - p->hh - m < *y1)
            return 0;
    }
    return 1;
}

/* 1 right after a touch, fading to 0 when idle. */
static float handle_alpha(long frame, long last_touch)
{
    long age = frame - last_touch;
    if (age <= HANDLE_FULL_FRAMES)
        return 1.0f;
    if (age >= HANDLE_FULL_FRAMES + HANDLE_FADE_FRAMES)
        return 0.0f;
    return 1.0f - (float)(age - HANDLE_FULL_FRAMES) / HANDLE_FADE_FRAMES;
}

/* ---- state ---- */

typedef struct {
    int used;
    SDL_FingerID id;
    float x, y, x0, y0;
    long t0;
    int on_dpad, moved, on_handle;
    uint16_t bits;
    int edit_group, edit_resize;
    float ga0, gb0, gs0, d0;
} finger;

static struct {
    touch_opts o;
    SDL_Window *win;
    layout L;
    int visible, edit, mouse_down;
    int qm_req;               /* the quick menu was asked for (handle, three fingers) */
    int tap3;                 /* three fingers down on empty spots, not moved */
    long tap3_t;
    long last_touch, frame;
    finger f[MAX_FINGERS];
} T;

static char *ini_path(void)
{
    char *dir = SDL_GetPrefPath("ct-recomp", "touch");
    if (!dir)
        return NULL;
    size_t n = strlen(dir) + sizeof "touch.ini";
    char *p = malloc(n);
    if (p)
        snprintf(p, n, "%stouch.ini", dir);
    SDL_free(dir);
    return p;
}

/* touch.ini: lines "wide.dpad=DX DY SCALE" / "narrow.face=...". */
static void layout_load(void)
{
    char *path = ini_path(), line[128], kind[16], key[16];
    FILE *f = path ? fopen(path, "r") : NULL;
    float dx, dy, s;
    while (f && fgets(line, sizeof line, f)) {
        if (sscanf(line, "%15[a-z].%15[a-z]=%f %f %f", kind, key, &dx, &dy, &s) != 5)
            continue;
        int w = !strcmp(kind, "wide") ? 1 : !strcmp(kind, "narrow") ? 0 : -1;
        for (int g = 0; g < G_COUNT && w >= 0; g++)
            if (!strcmp(key, group_key[g]) && s >= 0.4f && s <= 2.5f && fabsf(dx) < 1 &&
                fabsf(dy) < 1) {
                adjust[w][g].dx = dx;
                adjust[w][g].dy = dy;
                adjust[w][g].s = s;
            }
    }
    if (f)
        fclose(f);
    free(path);
}

static void layout_save(void)
{
    char *path = ini_path();
    FILE *f = path ? fopen(path, "w") : NULL;
    for (int w = 0; w < 2 && f; w++)
        for (int g = 0; g < G_COUNT; g++)
            fprintf(f, "%s.%s=%.4f %.4f %.3f\n", w ? "wide" : "narrow", group_key[g],
                    adjust[w][g].dx, adjust[w][g].dy, adjust[w][g].s > 0 ? adjust[w][g].s : 1.0);
    if (f) {
        fclose(f);
        fprintf(stderr, "ct_sdl: touch layout saved to %s\n", path);
    }
    free(path);
}

void touch_init(const touch_opts *o)
{
    memset(&T, 0, sizeof T);
    T.o = *o;
    for (int w = 0; w < 2; w++)
        for (int g = 0; g < G_COUNT; g++)
            adjust[w][g].s = 1.0f;
    layout_load();
#ifdef __ANDROID__
    T.visible = 1;
#endif
    if (o->force)
        T.visible = 1;
}

void touch_set_screen(SDL_Window *win, int out_w, int out_h, touch_rect picture)
{
    T.win = win;
    if (out_w < 16 || out_h < 16)
        return;
    layout_make(&T.L, out_w, out_h, picture, base_k(win, out_w, out_h));
}

int touch_visible(void)
{
    return T.visible;
}

/* ---- hit testing ---- */

static uint16_t dpad_bits(const layout *L, float x, float y)
{
    const shape *p = &L->c[C_DPAD];
    float dx = x - p->x, dy = p->y - y;   /* y up */
    if (dx * dx + dy * dy < 0.2f * p->hw * 0.2f * p->hw)
        return 0;
    float a = atan2f(dy, dx) * 57.29578f;   /* degrees, -180..180 */
    uint16_t b = 0;
    if (a > -67.5f && a < 67.5f)
        b |= PAD_RIGHT;
    if (a > 22.5f && a < 157.5f)
        b |= PAD_UP;
    if (a > 112.5f || a < -112.5f)
        b |= PAD_LEFT;
    if (a < -22.5f && a > -157.5f)
        b |= PAD_DOWN;
    return b;
}

/* Buttons under (x, y): the nearest face button within reach, and its
   neighbour too when the finger is about halfway between them. */
static uint16_t button_bits(const layout *L, float x, float y)
{
    float best = 1e9f, second = 1e9f;
    int bi = -1, si = -1;
    for (int c = C_A; c < C_COUNT; c++) {
        const shape *p = &L->c[c];
        float slack = (c <= C_Y ? 0.35f * p->hw : 2.0f * L->k * L->gs[ctl_group[c]]);
        float d = shape_dist(p, x, y);
        if (d > slack)
            continue;
        float dc = hypotf(x - p->x, y - p->y);
        if (dc < best) {
            second = best;
            si = bi;
            best = dc;
            bi = c;
        } else if (dc < second) {
            second = dc;
            si = c;
        }
    }
    uint16_t b = bi >= 0 ? ctl_bit[bi] : 0;
    if (bi >= C_A && bi <= C_Y && si >= C_A && si <= C_Y && second < best * 1.15f + 1.0f)
        b |= ctl_bit[si];
    return b;
}

static void finger_update(finger *f)
{
    if (T.edit) {
        f->bits = 0;
        return;
    }
    if (f->on_dpad)
        f->bits = dpad_bits(&T.L, f->x, f->y);
    else
        f->bits = button_bits(&T.L, f->x, f->y);
}

static finger *finger_find(SDL_FingerID id)
{
    for (int k = 0; k < MAX_FINGERS; k++)
        if (T.f[k].used && T.f[k].id == id)
            return &T.f[k];
    return NULL;
}

static int edit_hit(float x, float y, int *resize)
{
    float hr = 5.0f * T.L.k;
    for (int g = 0; g < G_COUNT; g++) {
        float x0, y0, x1, y1;
        group_box(&T.L, g, &x0, &y0, &x1, &y1);
        if (hypotf(x - x1, y - y1) < hr) {
            *resize = 1;
            return g;
        }
    }
    for (int g = 0; g < G_COUNT; g++) {
        float x0, y0, x1, y1;
        group_box(&T.L, g, &x0, &y0, &x1, &y1);
        if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
            *resize = 0;
            return g;
        }
    }
    return -1;
}

static void finger_down(SDL_FingerID id, float x, float y)
{
    finger *f = finger_find(id);
    for (int k = 0; !f && k < MAX_FINGERS; k++)
        if (!T.f[k].used)
            f = &T.f[k];
    if (!f)
        return;
    memset(f, 0, sizeof *f);
    f->used = 1;
    f->id = id;
    f->x = f->x0 = x;
    f->y = f->y0 = y;
    f->t0 = T.frame;
    f->edit_group = -1;
    float ha = handle_alpha(T.frame, T.last_touch);
    int was_visible = T.visible;
    T.visible = 1;
    T.last_touch = T.frame;
    if (!T.edit && was_visible && ha > 0.3f) {   /* the quick menu handle */
        float hx0, hy0, hx1, hy1;
        if (handle_rect(&T.L, 1, &hx0, &hy0, &hx1, &hy1) && x >= hx0 && x <= hx1 && y >= hy0 &&
            y <= hy1) {
            f->on_handle = 1;
            return;
        }
    }
    if (T.edit) {
        int g = edit_hit(x, y, &f->edit_resize);
        f->edit_group = g;
        if (g >= 0) {
            int w = T.L.wide;
            f->ga0 = adjust[w][g].dx;
            f->gb0 = adjust[w][g].dy;
            f->gs0 = adjust[w][g].s;
            f->d0 = hypotf(x - T.L.gx[g], y - T.L.gy[g]);
        }
        return;
    }
    f->on_dpad = shape_dist(&T.L.c[C_DPAD], x, y) < 0.3f * T.L.c[C_DPAD].hw;
    finger_update(f);
    /* three fingers down together, none on a control: a tap of them opens the quick menu */
    int n = 0, empty = 1;
    long first = T.frame;
    for (int k = 0; k < MAX_FINGERS; k++)
        if (T.f[k].used) {
            n++;
            empty &= !T.f[k].bits && !T.f[k].on_dpad && !T.f[k].on_handle;
            if (T.f[k].t0 < first)
                first = T.f[k].t0;
        }
    T.tap3 = n == 3 && empty && T.frame - first <= 24;
    T.tap3_t = T.frame;
}

static void finger_move(SDL_FingerID id, float x, float y)
{
    finger *f = finger_find(id);
    if (!f)
        return;
    f->x = x;
    f->y = y;
    T.last_touch = T.frame;
    if (hypotf(x - f->x0, y - f->y0) > 4.0f * T.L.k) {
        f->moved = 1;
        T.tap3 = 0;
    }
    if (T.edit && f->edit_group >= 0) {
        int g = f->edit_group, w = T.L.wide;
        if (f->edit_resize) {
            float d = hypotf(x - T.L.gx[g], y - T.L.gy[g]);
            float s = f->d0 > 1 ? f->gs0 * d / f->d0 : f->gs0;
            adjust[w][g].s = fminf(fmaxf(s, 0.5f), 2.0f);
        } else {
            adjust[w][g].dx = f->ga0 + (x - f->x0) / T.L.w;
            adjust[w][g].dy = f->gb0 + (y - f->y0) / T.L.h;
        }
        return;
    }
    finger_update(f);
}

static void finger_up(SDL_FingerID id)
{
    finger *f = finger_find(id);
    if (f && f->on_handle && !f->moved && !T.edit) {
        float hx0, hy0, hx1, hy1;
        if (handle_rect(&T.L, 1, &hx0, &hy0, &hx1, &hy1) && f->x >= hx0 && f->x <= hx1 &&
            f->y >= hy0 && f->y <= hy1)
            T.qm_req = 1;
    }
    if (f && T.tap3 && T.frame - T.tap3_t <= 30 && !T.edit) {
        T.qm_req = 1;
        T.tap3 = 0;
        for (int k = 0; k < MAX_FINGERS; k++)   /* the other fingers' lifts go to the menu */
            T.f[k].used = 0;
    }
    if (f)
        f->used = 0;
    T.last_touch = T.frame;
}

int touch_menu_requested(void)
{
    int r = T.qm_req;
    T.qm_req = 0;
    return r;
}

/* Long press on an empty spot: toggle edit mode. */
static void check_long_press(void)
{
    for (int k = 0; k < MAX_FINGERS; k++) {
        finger *f = &T.f[k];
        if (!f->used || f->moved || f->on_dpad || f->bits || f->edit_group >= 0 ||
            T.frame - f->t0 < LONG_PRESS_FRAMES)
            continue;
        if (!T.edit) {
            int r;
            if (edit_hit(f->x, f->y, &r) >= 0)
                continue;   /* near a control, if not on it: not "empty" */
        }
        T.edit = !T.edit;
        fprintf(stderr, "ct_sdl: touch edit mode %s\n", T.edit ? "on" : "off");
        if (!T.edit)
            layout_save();
        f->t0 = T.frame + 1000000;   /* once per press */
    }
}

int touch_event(const SDL_Event *ev, long frame)
{
    T.frame = frame;
    int ow = T.L.w, oh = T.L.h;
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERBUTTONDOWN:
        T.visible = 0;
        return 0;
    case SDL_CONTROLLERAXISMOTION:
        if (ev->caxis.value > 16000 || ev->caxis.value < -16000)
            T.visible = 0;
        return 0;
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
        if (T.o.force && ev->tfinger.touchId == SDL_MOUSE_TOUCHID)
            return 1;   /* SDL's own mouse-to-touch copy: the mouse is handled below */
        if (ev->type == SDL_FINGERDOWN)
            finger_down(ev->tfinger.fingerId, ev->tfinger.x * ow, ev->tfinger.y * oh);
        else if (ev->type == SDL_FINGERMOTION)
            finger_move(ev->tfinger.fingerId, ev->tfinger.x * ow, ev->tfinger.y * oh);
        else
            finger_up(ev->tfinger.fingerId);
        return 1;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION: {
        if (!T.o.force)
            return 0;
        if ((ev->type == SDL_MOUSEMOTION ? ev->motion.which : ev->button.which) ==
            SDL_TOUCH_MOUSEID)
            return 1;   /* SDL's mouse copy of a real touch */
        if (ev->type != SDL_MOUSEMOTION && ev->button.button != SDL_BUTTON_LEFT)
            return 0;
        int ww = ow, wh = oh;
        if (T.win)
            SDL_GetWindowSize(T.win, &ww, &wh);
        float sx = ww > 0 ? (float)ow / ww : 1, sy = wh > 0 ? (float)oh / wh : 1;
        if (ev->type == SDL_MOUSEBUTTONDOWN) {
            T.mouse_down = 1;
            finger_down(MOUSE_FINGER, ev->button.x * sx, ev->button.y * sy);
        } else if (ev->type == SDL_MOUSEBUTTONUP) {
            T.mouse_down = 0;
            finger_up(MOUSE_FINGER);
        } else if (T.mouse_down) {
            finger_move(MOUSE_FINGER, ev->motion.x * sx, ev->motion.y * sy);
        }
        return 1;
    }
    default:
        return 0;
    }
}

uint16_t touch_buttons(long frame)
{
    T.frame = frame;
    check_long_press();
    int down = 0;
    uint16_t b = 0;
    for (int k = 0; k < MAX_FINGERS; k++)
        if (T.f[k].used) {
            down = 1;
            b |= T.f[k].bits;
        }
    if (T.visible && !down && !T.edit && T.o.hide_seconds > 0 &&
        T.frame - T.last_touch > (long)T.o.hide_seconds * FPS)
        T.visible = 0;
    return b;
}

/* ---- scripted touches ---- */

typedef struct {
    long frame;
    int type;          /* SDL_FINGERDOWN / MOTION / UP */
    int target;        /* -1: x, y; else a control (or a D-pad direction, below) */
    float x, y;
    int finger;
} test_ev;

static test_ev *tests;
static int n_tests;

/* D-pad directions as targets: C_COUNT + bits. */
static const struct { const char *name; int target; } targets[] = {
    {"dpad", C_DPAD},
    {"up", C_COUNT + PAD_UP}, {"down", C_COUNT + PAD_DOWN},
    {"left", C_COUNT + PAD_LEFT}, {"right", C_COUNT + PAD_RIGHT},
    {"upleft", C_COUNT + (PAD_UP | PAD_LEFT)}, {"upright", C_COUNT + (PAD_UP | PAD_RIGHT)},
    {"downleft", C_COUNT + (PAD_DOWN | PAD_LEFT)}, {"downright", C_COUNT + (PAD_DOWN | PAD_RIGHT)},
    {"a", C_A}, {"b", C_B}, {"x", C_X}, {"y", C_Y}, {"l", C_L}, {"r", C_R},
    {"select", C_SELECT}, {"start", C_START},
};

int touch_test_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ct_sdl: cannot read %s\n", path);
        return -1;
    }
    char line[256];
    int ln = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char act[16] = "", tgt[64] = "";
        long fr;
        int finger = 0;
        int n = sscanf(line, "%ld %15s %63s %d", &fr, act, tgt, &finger);
        if (n <= 0) {
            char *s = line;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
                s++;
            if (!*s)
                continue;
        }
        test_ev e = {fr, 0, -1, 0, 0, finger};
        e.type = !strcmp(act, "down") ? SDL_FINGERDOWN : !strcmp(act, "move") ? SDL_FINGERMOTION
                 : !strcmp(act, "up") ? SDL_FINGERUP : 0;
        int ok = n >= 2 && e.type && fr >= 0;
        if (ok && n >= 3) {
            if (sscanf(tgt, "%f,%f", &e.x, &e.y) == 2) {
                e.target = -1;
            } else if (e.type == SDL_FINGERUP && sscanf(tgt, "%d", &e.finger) == 1 && n == 3) {
                e.target = -2;   /* "F up FINGER" */
            } else {
                ok = 0;
                for (size_t k = 0; k < sizeof targets / sizeof *targets; k++)
                    if (!SDL_strcasecmp(tgt, targets[k].name)) {
                        e.target = targets[k].target;
                        ok = 1;
                    }
            }
        } else if (ok) {
            ok = e.type == SDL_FINGERUP;
            e.target = -2;   /* up where it is */
        }
        if (!ok) {
            fprintf(stderr, "ct_sdl: %s:%d: bad line (FRAME down|move|up TARGET [FINGER])\n",
                    path, ln);
            fclose(f);
            return -1;
        }
        test_ev *t = realloc(tests, (size_t)(n_tests + 1) * sizeof *t);
        if (!t) {
            fclose(f);
            return -1;
        }
        tests = t;
        tests[n_tests++] = e;
    }
    fclose(f);
    fprintf(stderr, "ct_sdl: --touch-test %s: %d touch events\n", path, n_tests);
    return 0;
}

static void target_point(int target, float *x, float *y)
{
    const layout *L = &T.L;
    if (target >= C_COUNT) {
        uint16_t b = (uint16_t)(target - C_COUNT);
        const shape *p = &L->c[C_DPAD];
        float dx = (b & PAD_RIGHT ? 1.0f : 0) - (b & PAD_LEFT ? 1.0f : 0);
        float dy = (b & PAD_DOWN ? 1.0f : 0) - (b & PAD_UP ? 1.0f : 0);
        float n = hypotf(dx, dy);
        *x = p->x + (n > 0 ? dx / n : 0) * 0.65f * p->hw;
        *y = p->y + (n > 0 ? dy / n : 0) * 0.65f * p->hw;
    } else {
        *x = L->c[target].x;
        *y = L->c[target].y;
    }
}

int touch_target(const char *name, float *x, float *y)
{
    for (size_t k = 0; k < sizeof targets / sizeof *targets; k++)
        if (!SDL_strcasecmp(name, targets[k].name)) {
            target_point(targets[k].target, x, y);
            return 0;
        }
    return -1;
}

void touch_test_inject(long frame)
{
    static float last_x[64], last_y[64];
    static long done_frame = -1;
    if (n_tests && T.L.w > 0) {   /* once per frame: a menu's ticks poll again at the same frame */
        if (frame == done_frame)
            return;
        done_frame = frame;
    }
    for (int k = 0; k < n_tests; k++) {
        const test_ev *e = &tests[k];
        if (e->frame != frame || T.L.w <= 0)
            continue;
        int fi = e->finger & 63;
        float x, y;
        if (e->target == -1) {
            x = e->x * T.L.w;
            y = e->y * T.L.h;
        } else if (e->target == -2) {
            x = last_x[fi];
            y = last_y[fi];
        } else {
            target_point(e->target, &x, &y);
        }
        last_x[fi] = x;
        last_y[fi] = y;
        SDL_Event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = (Uint32)e->type;
        ev.tfinger.timestamp = SDL_GetTicks();
        ev.tfinger.touchId = 1;
        ev.tfinger.fingerId = 100 + e->finger;
        ev.tfinger.x = x / T.L.w;
        ev.tfinger.y = y / T.L.h;
        ev.tfinger.pressure = e->type == SDL_FINGERUP ? 0.0f : 1.0f;
        SDL_PushEvent(&ev);
    }
}

/* ---- geometry ---- */

#define MAX_VERTS 60000
static SDL_Vertex verts[MAX_VERTS];
static int n_verts;
static int alpha_pct = 100;   /* touch_opacity while building (edit mode: full) */

typedef struct {
    Uint8 r, g, b, a;
} rgba;

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, rgba c0, rgba c1,
                rgba c2)
{
    if (n_verts + 3 > MAX_VERTS)
        return;
    SDL_Vertex *v = &verts[n_verts];
    v[0].position.x = x0, v[0].position.y = y0;
    v[1].position.x = x1, v[1].position.y = y1;
    v[2].position.x = x2, v[2].position.y = y2;
    v[0].color = (SDL_Color){c0.r, c0.g, c0.b, (Uint8)(c0.a * alpha_pct / 100)};
    v[1].color = (SDL_Color){c1.r, c1.g, c1.b, (Uint8)(c1.a * alpha_pct / 100)};
    v[2].color = (SDL_Color){c2.r, c2.g, c2.b, (Uint8)(c2.a * alpha_pct / 100)};
    v[0].tex_coord.x = v[0].tex_coord.y = 0;
    v[1].tex_coord = v[2].tex_coord = v[0].tex_coord;
    n_verts += 3;
}

static void quad(float x0, float y0, float x1, float y1, rgba c)
{
    tri(x0, y0, x1, y0, x1, y1, c, c, c);
    tri(x0, y0, x1, y1, x0, y1, c, c, c);
}

#define MAX_CONTOUR 132

/* The outline of a rounded rectangle grown by d, clockwise on screen. */
static int contour(const shape *s, float d, float *px, float *py)
{
    float r = s->cr + d;
    if (r < 0)
        r = 0;
    int m = (int)(s->cr / 2.5f);
    m = m < 4 ? 4 : m > 32 ? 32 : m;
    float cx[4] = {s->x + s->hw - s->cr, s->x - s->hw + s->cr, s->x - s->hw + s->cr,
                   s->x + s->hw - s->cr};
    float cy[4] = {s->y + s->hh - s->cr, s->y + s->hh - s->cr, s->y - s->hh + s->cr,
                   s->y - s->hh + s->cr};
    int n = 0;
    for (int q = 0; q < 4; q++)
        for (int i = 0; i <= m; i++) {
            float a = q * PI_F / 2 + i * (PI_F / 2) / m;
            px[n] = cx[q] + r * cosf(a);
            py[n] = cy[q] + r * sinf(a);
            n++;
        }
    return n;
}

static void band(const float *ax, const float *ay, const float *bx, const float *by, int n,
                 rgba ca, rgba cb)
{
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        tri(ax[i], ay[i], bx[i], by[i], bx[j], by[j], ca, cb, cb);
        tri(ax[i], ay[i], bx[j], by[j], ax[j], ay[j], ca, cb, ca);
    }
}

/* A filled shape with a ring of width t, feathered by a pixel outside. */
static void shape_draw(const shape *s, float t, rgba fill, rgba ring)
{
    float ix[MAX_CONTOUR], iy[MAX_CONTOUR], ox[MAX_CONTOUR], oy[MAX_CONTOUR],
        fx[MAX_CONTOUR], fy[MAX_CONTOUR];
    int n = contour(s, -t, ix, iy);
    contour(s, 0, ox, oy);
    contour(s, 1.2f, fx, fy);
    for (int i = 0; i < n; i++)
        tri(s->x, s->y, ix[i], iy[i], ix[(i + 1) % n], iy[(i + 1) % n], fill, fill, fill);
    rgba clear = ring;
    clear.a = 0;
    if (t > 0)
        band(ix, iy, ox, oy, n, ring, ring);
    band(ox, oy, fx, fy, n, t > 0 ? ring : fill, clear);
}

static void circle(float x, float y, float r, float t, rgba fill, rgba ring)
{
    shape s = {x, y, r, r, r};
    shape_draw(&s, t, fill, ring);
}

/* Text from the 5x7 font, centred at (x, y), each font pixel px wide. */
static void text(const char *s, float x, float y, float px, rgba c)
{
    int n = (int)strlen(s);
    float w = (n * 6 - 1) * px, x0 = x - w / 2, y0 = y - 3.5f * px;
    for (int k = 0; k < n; k++) {
        const uint8_t *rows = osd_glyph(s[k]);
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (rows[r] & (0x10 >> b)) {
                    float gx = x0 + (k * 6 + b) * px, gy = y0 + r * px;
                    quad(gx, gy, gx + px, gy + px, c);
                }
    }
}

/* An arrow head pointing along (dx, dy), centred at (x, y). */
static void arrow(float x, float y, float dx, float dy, float size, rgba c)
{
    float nx = -dy, ny = dx;
    tri(x + dx * size, y + dy * size, x - dx * size * 0.6f + nx * size,
        y - dy * size * 0.6f + ny * size, x - dx * size * 0.6f - nx * size,
        y - dy * size * 0.6f - ny * size, c, c, c);
}

static const rgba FILL = {16, 18, 26, 84}, FILL_ON = {235, 238, 255, 120},
                  RING = {235, 238, 255, 120}, LABEL = {245, 246, 255, 190},
                  LABEL_ON = {20, 22, 30, 220};
/* Super Famicom face colors, for the rings. */
static const rgba FACE_RING[4] = {{220, 60, 60, 150}, {235, 195, 40, 150}, {60, 110, 230, 150},
                                  {40, 170, 90, 150}};   /* A B X Y */

static void build(const layout *L, uint16_t held, int edit)
{
    n_verts = 0;
    alpha_pct = edit ? 100 : opt_opacity;
    float k = L->k;
    /* D-pad: a disc, four arrows, a bright spot toward the held direction. */
    {
        const shape *p = &L->c[C_DPAD];
        float r = p->hw, t = fmaxf(1.5f, 0.45f * k);
        circle(p->x, p->y, r, t, FILL, RING);
        uint16_t d = held & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
        if (d) {
            float dx = (d & PAD_RIGHT ? 1.0f : 0) - (d & PAD_LEFT ? 1.0f : 0);
            float dy = (d & PAD_DOWN ? 1.0f : 0) - (d & PAD_UP ? 1.0f : 0);
            float n = hypotf(dx, dy);
            if (n > 0)
                circle(p->x + dx / n * 0.55f * r, p->y + dy / n * 0.55f * r, 0.36f * r, 0,
                       FILL_ON, FILL_ON);
        }
        static const struct { float dx, dy; uint16_t bit; } dirs[4] = {
            {0, -1, PAD_UP}, {0, 1, PAD_DOWN}, {-1, 0, PAD_LEFT}, {1, 0, PAD_RIGHT}};
        for (int i = 0; i < 4; i++)
            arrow(p->x + dirs[i].dx * 0.62f * r, p->y + dirs[i].dy * 0.62f * r, dirs[i].dx,
                  dirs[i].dy, 0.16f * r, held & dirs[i].bit ? LABEL_ON : LABEL);
        circle(p->x, p->y, 0.12f * r, 0, RING, RING);
    }
    for (int c = C_A; c < C_COUNT; c++) {
        const shape *p = &L->c[c];
        int on = (held & ctl_bit[c]) != 0;
        float t = fmaxf(1.5f, 0.45f * k * L->gs[ctl_group[c]]);
        shape_draw(p, t, on ? FILL_ON : FILL, c <= C_Y ? FACE_RING[c - C_A] : RING);
        /* Letters: about 45% of a face button, 55% of a pill's height. */
        float px = c <= C_Y ? p->hh * 0.9f / 7 : p->hh * 1.1f / 7;
        if (c >= C_SELECT)
            px = fminf(px, p->hw * 1.6f / (6.0f * (float)strlen(ctl_label[c])));
        px = fmaxf(px, 1.0f);
        text(ctl_label[c], p->x, p->y, px, on ? LABEL_ON : LABEL);
    }
    if (!edit) {   /* the quick menu handle, fading when idle */
        float a = handle_alpha(T.frame, T.last_touch), hx0, hy0, hx1, hy1;
        if (a > 0.01f && handle_rect(L, 0, &hx0, &hy0, &hx1, &hy1)) {
            rgba f = {16, 18, 26, (Uint8)(110 * a)}, r = {235, 238, 255, (Uint8)(190 * a)};
            shape p = {(hx0 + hx1) / 2, (hy0 + hy1) / 2, (hx1 - hx0) / 2, (hy1 - hy0) / 2,
                       (hy1 - hy0) / 2};
            shape_draw(&p, fmaxf(1.0f, 0.3f * k), f, r);
        }
    }
    if (edit) {
        static const rgba BOX = {255, 210, 80, 60}, HANDLE = {255, 210, 80, 200};
        for (int g = 0; g < G_COUNT; g++) {
            float x0, y0, x1, y1;
            group_box(L, g, &x0, &y0, &x1, &y1);
            shape b = {(x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2 + 2, (y1 - y0) / 2 + 2, 2};
            shape_draw(&b, 2, (rgba){255, 210, 80, 18}, BOX);
            circle(x1, y1, 2.5f * k, 0, HANDLE, HANDLE);
        }
        /* the longer line, 36 characters, fits the width */
        float px = fmaxf(1.0f, fminf(0.5f * k, L->w * 0.94f / (36 * 6)));
        text("EDIT: DRAG TO MOVE, CORNER TO RESIZE", L->w / 2.0f, 5 * k, px, HANDLE);
        text("HOLD AN EMPTY SPOT TO SAVE", L->w / 2.0f, 5 * k + 10 * px, px, HANDLE);
    }
}

static uint16_t held_now(void)
{
    uint16_t b = 0;
    for (int k = 0; k < MAX_FINGERS; k++)
        if (T.f[k].used)
            b |= T.f[k].bits;
    return b;
}

int touch_geometry(const SDL_Vertex **v)
{
    *v = verts;
    if (!T.visible || T.L.w <= 0)
        return 0;
    build(&T.L, held_now(), T.edit);
    return n_verts;
}

int touch_geometry_for(int out_w, int out_h, touch_rect picture, const SDL_Vertex **v)
{
    *v = verts;
    if (!T.visible)
        return 0;
    layout L;
    /* A screenshot stands for a phone screen of that size. */
    layout_make(&L, out_w, out_h, picture, base_k(NULL, out_w, out_h));
    build(&L, held_now(), T.edit);
    return n_verts;
}

void touch_draw_sdl(SDL_Renderer *ren)
{
    const SDL_Vertex *v;
    int n = touch_geometry(&v);
    if (!n)
        return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(ren, NULL, v, n, NULL, 0);
}

int touch_shot_sdl(SDL_Renderer *ren, SDL_Texture *frame, int w, int h, uint8_t *bgrx)
{
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                       w, h);
    if (!t)
        return -1;
    SDL_SetRenderTarget(ren, t);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    touch_rect p = touch_picture_plain(w, h);
    SDL_Rect dst = {p.x, p.y, p.w, p.h};
    SDL_RenderCopy(ren, frame, NULL, &dst);
    const SDL_Vertex *v;
    int n = touch_geometry_for(w, h, p, &v);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    if (n)
        SDL_RenderGeometry(ren, NULL, v, n, NULL, 0);
    int r = SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, bgrx, w * 4);
    SDL_SetRenderTarget(ren, NULL);
    SDL_DestroyTexture(t);
    return r ? -1 : 0;
}

/* ---- port: options (touch.h) ---- */

void touch_options_set(int opacity_pct, int size_pct, int hand)
{
    opt_opacity = opacity_pct < TOUCH_OPACITY_MIN ? TOUCH_OPACITY_MIN
                  : opacity_pct > 100 ? 100 : opacity_pct;
    opt_size = size_pct < TOUCH_SIZE_MIN ? TOUCH_SIZE_MIN
               : size_pct > TOUCH_SIZE_MAX ? TOUCH_SIZE_MAX : size_pct;
    opt_hand = hand != 0;
}

void touch_options_get(int *opacity_pct, int *size_pct, int *hand)
{
    if (opacity_pct)
        *opacity_pct = opt_opacity;
    if (size_pct)
        *size_pct = opt_size;
    if (hand)
        *hand = opt_hand;
}

static int get_opacity(void *u) { (void)u; return opt_opacity; }
static void set_opacity(void *u, int v) { (void)u; touch_options_set(v, opt_size, opt_hand); }
static int get_size(void *u) { (void)u; return opt_size; }
static void set_size(void *u, int v) { (void)u; touch_options_set(opt_opacity, v, opt_hand); }
static int get_hand(void *u) { (void)u; return opt_hand; }
static void set_hand(void *u, int v) { (void)u; touch_options_set(opt_opacity, opt_size, v); }

void touch_register_settings(void)
{
    static const char *const hands[2] = {"Right hand", "Left hand"},
                             *const hand_values[2] = {"right", "left"};
    settings_register(&(setting){.label = "Touch controls", .category = "Controls",
                                 .type = SETTING_INFO, .flags = SETTING_HEADING});
    settings_register(&(setting){
        .key = "touch_opacity", .label = "Touch opacity", .category = "Controls",
        .type = SETTING_INT, .min = TOUCH_OPACITY_MIN, .max = 100, .step = 5, .format = "%d%%",
        .get = get_opacity, .set = set_opacity,
        .help = "How see-through the on-screen buttons are."});
    settings_register(&(setting){
        .key = "touch_size", .label = "Touch size", .category = "Controls", .type = SETTING_INT,
        .min = TOUCH_SIZE_MIN, .max = TOUCH_SIZE_MAX, .step = 5, .format = "%d%%",
        .get = get_size, .set = set_size,
        .help = "All the on-screen buttons larger or smaller. (Long-press an empty spot to "
                "move and resize them one by one.)"});
    settings_register(&(setting){
        .key = "touch_hand", .label = "Touch layout", .category = "Controls",
        .type = SETTING_ENUM, .options = hands, .values = hand_values, .count = 2,
        .get = get_hand, .set = set_hand,
        .help = "Right hand: D-pad on the left. Left hand: the sides swapped."});
}

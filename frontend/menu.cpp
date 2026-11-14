/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the in-game settings menu (menu.h), drawn with Dear ImGui over
 * either picture. The rows come from the settings registry (settings.h);
 * navigation is ours (a cursor over the rows, held directions repeating),
 * ImGui only lays out and draws, and takes the mouse. */
#include "menu.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "glyphs.h"
#include "png.h"
#include "settings.h"
}

#include "imgui.h"
#include "imgui_internal.h"   /* RegisterUserTexture */
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#ifdef CT_MENU_FONT
#include "menu_font.h"   /* Roboto Medium (third_party/imgui/fonts), tools/embed_binary.cmake */
#endif

namespace {

constexpr int MAX_TABS = 16;
constexpr int MAX_ROWS = SETTINGS_MAX;
constexpr unsigned REPEAT_FIRST_MS = 380, REPEAT_MS = 85;
constexpr int STICK_ON = 16000, STICK_OFF = 9000;

/* A glyph's picture as an ImGui texture, per glyph set and token. */
struct GlyphTex {
    int set;
    char token[24];
    ImTextureData *tex;   /* NULL: no picture (label) */
    char label[32];
};

struct Dir {   /* one source of a direction: -1, 0, 1 per axis */
    int x, y;
};

struct Menu {
    menu_host host;
    int have_host, ready, open;
    long opened;
    int tab, row[MAX_TABS];
    int capture;         /* registry index waiting for a key, -1 */
    int confirm;         /* registry index of an action waiting for its second press, -1 */
    Dir pad, stick, keys;
    int cur_x, cur_y;    /* the combined direction being repeated */
    unsigned held_ms, next_ms;
    float scale_px;      /* the body font size the style was made for */
    ImFont *font;
    GlyphTex glyphs[64];
    int n_glyphs;
    ImTextureData *preview;
    const setting *preview_of;
    unsigned preview_gen;
    ImVector<ImTextureData *> retired;
    float scroll;        /* list scroll, kept by us */
    int ensure_visible;
} M = {};

/* ---- the rows of the current tab ---- */

int tab_names(const char **out)
{
    return settings_categories(out, MAX_TABS);
}

int rows_of(const char *cat, int *idx)
{
    int n = 0;
    for (int k = 0; k < settings_count() && n < MAX_ROWS; k++) {
        const setting *s = settings_at(k);
        if (s->category && !strcmp(s->category, cat))
            idx[n++] = k;
    }
    return n;
}

bool selectable(const setting *s)
{
    return !(s->type == SETTING_INFO && (s->flags & SETTING_HEADING));
}

/* The registry rows of the current tab; *sel the selected one, fixed up
   to a selectable row. */
int current_rows(int *idx, int *sel)
{
    const char *tabs[MAX_TABS];
    int nt = tab_names(tabs);
    if (!nt) {
        *sel = -1;
        return 0;
    }
    if (M.tab >= nt)
        M.tab = nt - 1;
    int n = rows_of(tabs[M.tab], idx);
    int r = M.row[M.tab];
    if (r >= n)
        r = n - 1;
    if (r < 0)
        r = 0;
    for (int k = 0; k < n && !selectable(settings_at(idx[r])); k++)
        r = (r + 1) % n;
    M.row[M.tab] = n ? r : -1;
    *sel = M.row[M.tab];
    return n;
}

void move_row(int dir)
{
    int idx[MAX_ROWS], sel;
    int n = current_rows(idx, &sel);
    if (n <= 0)
        return;
    int r = sel;
    for (int k = 0; k < n; k++) {
        r += dir;
        if (r < 0 || r >= n)
            return;   /* no wrap: stop at the ends */
        if (selectable(settings_at(idx[r])))
            break;
    }
    if (r >= 0 && r < n && selectable(settings_at(idx[r]))) {
        M.row[M.tab] = r;
        M.confirm = -1;
        M.ensure_visible = 1;
    }
}

void switch_tab(int dir)
{
    const char *tabs[MAX_TABS];
    int nt = tab_names(tabs);
    if (nt <= 0)
        return;
    M.tab = ((M.tab + dir) % nt + nt) % nt;
    M.confirm = -1;
    M.scroll = 0;
    M.ensure_visible = 1;
}

const setting *selected(int *index)
{
    int idx[MAX_ROWS], sel;
    int n = current_rows(idx, &sel);
    if (n <= 0 || sel < 0)
        return NULL;
    if (index)
        *index = idx[sel];
    return settings_at(idx[sel]);
}

/* A: toggle, step forward, run, or ask for a key. */
void activate(const setting *s, int index)
{
    if (!s || !settings_is_enabled(s))
        return;
    switch (s->type) {
    case SETTING_BOOL:
    case SETTING_ENUM:
        settings_step(s, 1);
        break;
    case SETTING_INT:
        break;   /* left / right */
    case SETTING_ACTION:
        if ((s->flags & SETTING_CONFIRM) && M.confirm != index) {
            M.confirm = index;
            return;
        }
        M.confirm = -1;
        if (s->run)
            s->run(s->user);
        if (s->flags & SETTING_CLOSE)
            menu_close();
        break;
    case SETTING_KEY:
        M.capture = index;
        break;
    default:
        break;
    }
}

void change(const setting *s, int dir)
{
    if (s && settings_is_enabled(s) &&
        (s->type == SETTING_BOOL || s->type == SETTING_ENUM || s->type == SETTING_INT))
        settings_step(s, dir);
}

void back(void)
{
    if (M.capture >= 0)
        M.capture = -1;
    else if (M.confirm >= 0)
        M.confirm = -1;
    else
        menu_close();
}

/* The direction held now, from the d-pad, then the keys, then the stick. */
void update_dir(void)
{
    int x = M.pad.x ? M.pad.x : M.keys.x ? M.keys.x : M.stick.x;
    int y = M.pad.y ? M.pad.y : M.keys.y ? M.keys.y : M.stick.y;
    if (x == M.cur_x && y == M.cur_y)
        return;
    M.cur_x = x, M.cur_y = y;
    M.held_ms = 0;
    M.next_ms = REPEAT_FIRST_MS;
    if (y)
        move_row(y);
    else if (x)
        change(selected(NULL), x);
}

int stick_dir(int v, int cur)
{
    if (v <= -STICK_ON)
        return -1;
    if (v >= STICK_ON)
        return 1;
    if (cur && (v * cur) > STICK_OFF)
        return cur;   /* hysteresis */
    return 0;
}

/* ---- textures: glyphs and the preview ---- */

ImTextureData *make_texture(int w, int h)
{
    ImTextureData *t = IM_NEW(ImTextureData)();
    t->Create(ImTextureFormat_RGBA32, w, h);
    ImGui::RegisterUserTexture(t);
    return t;
}

void retire_texture(ImTextureData *t)
{
    if (!t)
        return;
    t->WantDestroyNextFrame = true;
    M.retired.push_back(t);
}

void collect_retired(void)
{
    for (int k = 0; k < M.retired.Size;) {
        ImTextureData *t = M.retired[k];
        if (t->Status == ImTextureStatus_Destroyed) {
            ImGui::UnregisterUserTexture(t);
            IM_DELETE(t);
            M.retired.erase(M.retired.Data + k);
        } else {
            k++;
        }
    }
}

const GlyphTex *glyph(const char *token)
{
    int set = glyphs_in_use();
    for (int k = 0; k < M.n_glyphs; k++)
        if (M.glyphs[k].set == set && !strcmp(M.glyphs[k].token, token))
            return &M.glyphs[k];
    if (M.n_glyphs == (int)(sizeof M.glyphs / sizeof *M.glyphs))
        return NULL;
    GlyphTex *g = &M.glyphs[M.n_glyphs++];
    g->set = set;
    snprintf(g->token, sizeof g->token, "%s", token);
    const char *label = "";
    const uint8_t *px = glyphs_lookup_in(set, token, &label);
    snprintf(g->label, sizeof g->label, "%s", label ? label : "");
    g->tex = NULL;
    if (px) {
        g->tex = make_texture(GLYPH_PX, GLYPH_PX);
        unsigned char *p = (unsigned char *)g->tex->GetPixels();
        for (int k = 0; k < GLYPH_PX * GLYPH_PX; k++) {
            p[k * 4 + 0] = p[k * 4 + 1] = p[k * 4 + 2] = 255;
            p[k * 4 + 3] = px[k];
        }
    }
    return g;
}

void update_preview(const setting *s)
{
    int w = 0, h = 0;
    unsigned gen = 0;
    const uint8_t *px = s && s->preview ? s->preview(s->user, &w, &h, &gen) : NULL;
    if (!px || w <= 0 || h <= 0) {
        M.preview_of = NULL;
        return;
    }
    if (M.preview && M.preview_of == s && M.preview_gen == gen)
        return;
    bool same = M.preview && M.preview->Width == w && M.preview->Height == h &&
                M.preview->Status == ImTextureStatus_OK;
    if (!same) {
        retire_texture(M.preview);
        M.preview = make_texture(w, h);
    }
    unsigned char *p = (unsigned char *)M.preview->GetPixels();
    for (int k = 0; k < w * h; k++) {
        p[k * 4 + 0] = px[k * 4 + 2];
        p[k * 4 + 1] = px[k * 4 + 1];
        p[k * 4 + 2] = px[k * 4 + 0];
        p[k * 4 + 3] = 255;
    }
    if (same) {
        ImTextureRect r = {0, 0, (unsigned short)w, (unsigned short)h};
        M.preview->Updates.push_back(r);
        M.preview->UpdateRect = r;
        M.preview->UsedRect = r;
        M.preview->SetStatus(ImTextureStatus_WantUpdates);
    }
    M.preview_of = s;
    M.preview_gen = gen;
}

/* ---- look ---- */

const ImU32 C_TEXT = IM_COL32(236, 238, 244, 255);
const ImU32 C_DIM = IM_COL32(150, 158, 174, 255);
const ImU32 C_FAINT = IM_COL32(96, 104, 120, 255);
const ImU32 C_ACCENT = IM_COL32(242, 193, 78, 255);
const ImU32 C_ACCENT_BG = IM_COL32(242, 193, 78, 38);
const ImU32 C_PANEL = IM_COL32(16, 19, 27, 255);
const ImU32 C_LINE = IM_COL32(255, 255, 255, 22);
const ImU32 C_PILL_OFF = IM_COL32(70, 76, 92, 255);

/* The body text size for a display of w x h points (scale by size and,
   where SDL knows it, by how big the screen physically is). */
float body_px(float w, float h)
{
    float px = h * 0.040f;   /* a handheld's 800-line screen: 32 px */
    float ddpi = 0, hdpi = 0, vdpi = 0;
    int idx = M.host.win ? SDL_GetWindowDisplayIndex(M.host.win) : -1;
    if (idx >= 0 && !SDL_GetDisplayDPI(idx, &ddpi, &hdpi, &vdpi) && vdpi > 40.0f) {
        SDL_DisplayMode dm;
        if (!SDL_GetCurrentDisplayMode(idx, &dm) && dm.h / vdpi > 6.0f)
            px = h * 0.032f;   /* a desktop monitor: the menu needn't be as big */
    }
    if (w < h * 1.2f)
        px = fminf(px, w * 0.036f);   /* narrow windows */
    const setting *ms = settings_find("menu_scale");
    static const float scales[] = {0.85f, 1.0f, 1.2f};
    int v = ms ? settings_get_value(ms) : 1;
    px *= scales[v >= 0 && v < 3 ? v : 1];
    return px < 15.0f ? 15.0f : px > 72.0f ? 72.0f : px;
}

void make_style(float px)
{
    ImGuiStyle st;
    ImGui::StyleColorsDark(&st);
    float s = px / 16.0f;
    st.WindowPadding = ImVec2(0, 0);
    st.WindowBorderSize = 0;
    st.WindowRounding = 0;
    st.ChildBorderSize = 0;
    st.ScrollbarSize = 6;
    st.ScrollbarRounding = 3;
    st.ItemSpacing = ImVec2(0, 0);
    st.FramePadding = ImVec2(0, 0);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.18f);
    st.ScaleAllSizes(s);
    st.FontSizeBase = px;
    ImGui::GetStyle() = st;
    M.scale_px = px;
}

void text_at(ImDrawList *dl, float size, ImVec2 p, ImU32 col, const char *t)
{
    dl->AddText(M.font, size, p, col, t);
}

ImVec2 text_size(float size, const char *t)
{
    return M.font->CalcTextSizeA(size, 1e9f, 0.0f, t);
}

/* t, or as much of its end as fits in maxw after "..." (paths). */
const char *elide_left(const char *t, float size, float maxw, char *buf, size_t n)
{
    if (text_size(size, t).x <= maxw)
        return t;
    size_t len = strlen(t);
    for (size_t k = 1; k < len; k++) {
        snprintf(buf, n, "...%s", t + k);
        if (text_size(size, buf).x <= maxw)
            return buf;
    }
    return "";
}

/* A glyph (or its label in a key cap) of height hgt at p; returns its width. */
float draw_glyph(ImDrawList *dl, const char *token, ImVec2 p, float hgt, float px)
{
    const GlyphTex *g = glyph(token);
    if (g && g->tex) {
        dl->AddImage(g->tex->GetTexRef(), p, ImVec2(p.x + hgt, p.y + hgt), ImVec2(0, 0), ImVec2(1, 1),
                     C_TEXT);
        return hgt;
    }
    const char *label = g ? g->label : token;
    float fs = px * 0.72f;
    ImVec2 ts = text_size(fs, label);
    float w = ts.x + px * 0.6f;
    dl->AddRect(ImVec2(p.x, p.y + hgt * 0.1f), ImVec2(p.x + w, p.y + hgt * 0.9f), C_TEXT, px * 0.2f,
                fmaxf(1.0f, px * 0.06f));
    text_at(dl, fs, ImVec2(p.x + px * 0.3f, p.y + (hgt - ts.y) * 0.5f), C_TEXT, label);
    return w;
}

void triangle(ImDrawList *dl, ImVec2 c, float r, int dir, ImU32 col)
{
    if (dir < 0)
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y), ImVec2(c.x + r * 0.5f, c.y - r),
                              ImVec2(c.x + r * 0.5f, c.y + r), col);
    else
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.6f, c.y), ImVec2(c.x - r * 0.5f, c.y - r),
                              ImVec2(c.x - r * 0.5f, c.y + r), col);
}

/* One row; returns its height. Mouse: a click picks it, and on the value
   steps it (left / right half) or activates it. */
void draw_row(ImDrawList *dl, int r, int index, bool sel, ImVec2 p, float w, float rh, float px)
{
    const setting *s = settings_at(index);
    char buf[160];
    bool on = settings_is_enabled(s);
    float pad = px * 0.8f;
    if (s->type == SETTING_INFO && (s->flags & SETTING_HEADING)) {
        float fs = px * 0.72f;
        text_at(dl, fs, ImVec2(p.x + pad, p.y + rh - fs * 1.45f), C_ACCENT, s->label);
        dl->AddLine(ImVec2(p.x + pad, p.y + rh - 2), ImVec2(p.x + w - pad, p.y + rh - 2), C_LINE,
                    1.0f);
        return;
    }
    if (sel) {
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rh), C_ACCENT_BG, px * 0.25f);
        dl->AddRectFilled(p, ImVec2(p.x + px * 0.18f, p.y + rh), C_ACCENT, px * 0.09f);
    }
    ImU32 label_col = !on ? C_FAINT : sel ? C_TEXT : IM_COL32(214, 218, 228, 255);
    ImU32 value_col = !on ? C_FAINT : sel ? C_ACCENT : C_DIM;
    float ty = p.y + (rh - px) * 0.5f;
    text_at(dl, px, ImVec2(p.x + pad, ty), label_col, s->label);
    /* free text on the right (info, actions) stops short of the label */
    float text_room = w - 2.0f * pad - text_size(px, s->label).x - px;

    /* The value column: the right 42%. */
    float vx0 = p.x + w * 0.58f, vx1 = p.x + w - pad, vc = 0.5f * (vx0 + vx1);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
    ImGui::PushID(index);
    bool clicked = ImGui::InvisibleButton("row", ImVec2(w, rh));
    ImGui::PopID();
    float mx = ImGui::GetIO().MousePos.x;
    if (clicked) {
        M.row[M.tab] = r;
        if (mx >= vx0 && on) {
            if (s->type == SETTING_INT || s->type == SETTING_ENUM)
                change(s, mx < vc ? -1 : 1);
            else
                activate(s, index);
        }
    }
    switch (s->type) {
    case SETTING_BOOL: {
        bool v = settings_get_value(s) != 0;
        float ph = px * 1.05f, pw = px * 2.1f;
        ImVec2 a(vx1 - pw, p.y + (rh - ph) * 0.5f), b(vx1, a.y + ph);
        dl->AddRectFilled(a, b, v && on ? C_ACCENT : C_PILL_OFF, ph * 0.5f);
        float kr = ph * 0.38f;
        ImVec2 kc(v ? b.x - ph * 0.5f : a.x + ph * 0.5f, a.y + ph * 0.5f);
        dl->AddCircleFilled(kc, kr, on ? IM_COL32(250, 250, 252, 255) : C_DIM);
        const char *t = v ? "On" : "Off";
        ImVec2 ts = text_size(px, t);
        text_at(dl, px, ImVec2(a.x - px * 0.6f - ts.x, ty), value_col, t);
        break;
    }
    case SETTING_ENUM:
    case SETTING_INT: {
        const char *t = settings_value_text(s, buf, sizeof buf);
        ImVec2 ts = text_size(px, t);
        float ar = px * 0.28f;
        int v = settings_get_value(s);
        bool can_l = on && (s->type == SETTING_ENUM || v > s->min);
        bool can_r = on && (s->type == SETTING_ENUM || v < s->max);
        if (sel || on) {
            triangle(dl, ImVec2(vx0 + ar, p.y + rh * 0.5f), ar, -1,
                     can_l ? (sel ? C_ACCENT : C_DIM) : IM_COL32(255, 255, 255, 30));
            triangle(dl, ImVec2(vx1 - ar, p.y + rh * 0.5f), ar, 1,
                     can_r ? (sel ? C_ACCENT : C_DIM) : IM_COL32(255, 255, 255, 30));
        }
        float tx = vc - ts.x * 0.5f;
        if (s->type == SETTING_INT && s->max > s->min) {
            ty = p.y + (rh - px) * 0.5f - px * 0.12f;
            float bx0 = vx0 + ar * 3.0f, bx1 = vx1 - ar * 3.0f, by = p.y + rh * 0.5f + px * 0.58f;
            float f = (float)(v - s->min) / (float)(s->max - s->min);
            dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + px * 0.1f),
                              IM_COL32(255, 255, 255, 34), px * 0.05f);
            dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * f, by + px * 0.1f),
                              on ? (sel ? C_ACCENT : C_DIM) : C_FAINT, px * 0.05f);
        }
        text_at(dl, px, ImVec2(tx, ty), sel ? C_TEXT : value_col, t);
        break;
    }
    case SETTING_ACTION: {
        char el[200];
        float ar = px * 0.28f;
        const char *t = elide_left(M.confirm == index ? "Press again to confirm"
                                   : s->text ? s->text(s->user, buf, sizeof buf) : "",
                                   px, text_room - ar * 3.0f, el, sizeof el);
        ImVec2 ts = text_size(px, t);
        triangle(dl, ImVec2(vx1 - ar, p.y + rh * 0.5f), ar, 1, on ? (sel ? C_ACCENT : C_DIM) : C_FAINT);
        text_at(dl, px, ImVec2(vx1 - ar * 3.0f - ts.x, ty), M.confirm == index ? C_ACCENT : value_col,
                t);
        break;
    }
    case SETTING_KEY: {
        bool cap = M.capture == index;
        const char *t = cap ? "Press a key..." : settings_value_text(s, buf, sizeof buf);
        float fs = px * 0.86f;
        ImVec2 ts = text_size(fs, t);
        float bw = ts.x + px * 1.0f, bh = px * 1.35f;
        ImVec2 a(vx1 - bw, p.y + (rh - bh) * 0.5f), b(vx1, a.y + bh);
        if (cap)
            dl->AddRectFilled(a, b, C_ACCENT_BG, px * 0.2f);
        dl->AddRect(a, b, cap ? C_ACCENT : value_col, px * 0.2f, fmaxf(1.0f, px * 0.06f));
        text_at(dl, fs, ImVec2(a.x + px * 0.5f, a.y + (bh - fs) * 0.5f), cap ? C_ACCENT : value_col, t);
        break;
    }
    case SETTING_INFO: {
        char el[200];
        float fs = px * 0.9f;
        const char *t = elide_left(settings_value_text(s, buf, sizeof buf), fs, text_room, el,
                                   sizeof el);
        ImVec2 ts = text_size(fs, t);
        text_at(dl, fs, ImVec2(vx1 - ts.x, p.y + (rh - fs) * 0.5f), C_DIM, t);
        break;
    }
    }
}

/* A prompt in the footer: glyph and text, from x; returns the new x. */
float prompt(ImDrawList *dl, float x, float y, float px, const char *token, const char *token2,
             const char *text)
{
    float gh = px * 1.25f;
    x += draw_glyph(dl, token, ImVec2(x, y - gh * 0.5f), gh, px);
    if (token2) {
        x += px * 0.15f;
        x += draw_glyph(dl, token2, ImVec2(x, y - gh * 0.5f), gh, px);
    }
    x += px * 0.35f;
    text_at(dl, px * 0.9f, ImVec2(x, y - px * 0.45f), C_TEXT, text);
    return x + text_size(px * 0.9f, text).x + px * 1.4f;
}

void build_ui(float W, float H)
{
    float px = M.scale_px;
    ImDrawList *bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(4, 6, 10, 170));

    float pw = fminf(W - 2.0f * px * 0.6f, px * 36.0f), ph = H - 2.0f * px * 0.7f;
    ImVec2 p0((W - pw) * 0.5f, (H - ph) * 0.5f), p1(p0.x + pw, p0.y + ph);
    bg->AddRectFilled(p0, p1, C_PANEL, px * 0.5f);
    bg->AddRect(p0, p1, IM_COL32(255, 255, 255, 18), px * 0.5f, 1.0f);

    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(pw, ph));
    ImGui::Begin("##ct_menu", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float pad = px * 1.0f;
    float x0 = p0.x + pad, x1 = p1.x - pad, y = p0.y + px * 0.8f;

    /* Title: the app's name, the state. */
    float tsz = px * 1.25f;
    text_at(dl, tsz, ImVec2(x0, y), C_ACCENT, M.host.app ? M.host.app : "");
    float tw = text_size(tsz, M.host.app ? M.host.app : "").x;
    text_at(dl, tsz, ImVec2(x0 + tw + px * 0.5f, y), C_TEXT, "Settings");
    {
        const char *t = "Paused";
        ImVec2 ts = text_size(px * 0.8f, t);
        text_at(dl, px * 0.8f, ImVec2(x1 - ts.x, y + (tsz - px * 0.8f) * 0.6f), C_DIM, t);
    }
    y += tsz + px * 0.8f;

    /* Tabs: [L1] a b c [R1], the current one underlined. */
    const char *tabs[MAX_TABS];
    int nt = tab_names(tabs);
    {
        float gh = px * 1.25f, fs = px * 0.95f, gap = px * 1.1f;
        bool kb = glyphs_in_use() == GLYPHS_KEYBOARD;
        float lw = draw_glyph(dl, kb ? "key:Q" : "l1", ImVec2(x0, y), gh, px);
        float rx = x1 - gh;
        /* where the tab names go, and how far to shift them to show the current one */
        float tx0 = x0 + lw + gap * 0.6f, tx1 = rx - gap * 0.6f;
        float total = 0, cur0 = 0, cur1 = 0;
        for (int k = 0; k < nt; k++) {
            float tw2 = text_size(fs, tabs[k]).x;
            if (k == M.tab)
                cur0 = total, cur1 = total + tw2;
            total += tw2 + (k + 1 < nt ? gap : 0);
        }
        float avail = tx1 - tx0, shift = 0;
        if (total > avail) {
            shift = cur0 + (cur1 - cur0) * 0.5f - avail * 0.5f;
            shift = fmaxf(0.0f, fminf(shift, total - avail));
        } else {
            shift = -(avail - total) * 0.5f;   /* centered */
        }
        dl->PushClipRect(ImVec2(tx0, y - px), ImVec2(tx1, y + gh + px), true);
        float tx = tx0 - shift;
        for (int k = 0; k < nt; k++) {
            float tw2 = text_size(fs, tabs[k]).x;
            bool cur = k == M.tab;
            float ty = y + (gh - fs) * 0.5f;
            text_at(dl, fs, ImVec2(tx, ty), cur ? C_TEXT : C_DIM, tabs[k]);
            if (cur)
                dl->AddRectFilled(ImVec2(tx, ty + fs * 1.18f), ImVec2(tx + tw2, ty + fs * 1.18f + px * 0.12f),
                                  C_ACCENT, px * 0.06f);
            ImGui::SetCursorScreenPos(ImVec2(tx, y));
            ImGui::PushID(1000 + k);
            if (tx + tw2 > tx0 && tx < tx1 && ImGui::InvisibleButton("tab", ImVec2(tw2, gh)) &&
                k != M.tab) {
                M.tab = k;
                M.scroll = 0;
                M.confirm = -1;
            }
            ImGui::PopID();
            tx += tw2 + gap;
        }
        dl->PopClipRect();
        draw_glyph(dl, kb ? "key:W" : "r1", ImVec2(rx, y), gh, px);
        y += gh + px * 0.75f;
    }
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), C_LINE, 1.0f);
    y += px * 0.4f;

    /* The footer and help area take the bottom. */
    float footer_h = px * 2.2f, help_h = px * 2.5f;
    float list_y0 = y, list_y1 = p1.y - footer_h - help_h;
    int idx[MAX_ROWS], sel;
    int n = current_rows(idx, &sel);
    /* A tab with pictures (the save states) keeps a column on the right for them. */
    bool pic_col = false;
    for (int k = 0; k < n && !pic_col; k++)
        pic_col = settings_at(idx[k])->preview != NULL;
    float lx1 = pic_col ? x1 - (x1 - x0) * 0.34f : x1;
    float rh = px * 1.95f, head_h = px * 1.7f;
    /* row positions, for scrolling */
    float tops[MAX_ROWS + 1], acc = 0;
    for (int k = 0; k < n; k++) {
        tops[k] = acc;
        const setting *s = settings_at(idx[k]);
        acc += (s->type == SETTING_INFO && (s->flags & SETTING_HEADING)) ? (k ? head_h : head_h * 0.75f) : rh;
    }
    tops[n] = acc;
    float view = list_y1 - list_y0;
    if (M.ensure_visible && sel >= 0) {
        /* keep the row (and the heading above it) in view */
        float top = tops[sel], bot = tops[sel + 1];
        if (sel > 0 && settings_at(idx[sel - 1])->flags & SETTING_HEADING)
            top = tops[sel - 1];
        if (sel + 1 < n)   /* and a peek at the next, clear of the fade */
            bot = fminf(bot + rh * 0.7f, acc);
        if (top > 0)
            top = fmaxf(0.0f, top - px * 0.6f);
        if (top < M.scroll)
            M.scroll = top;
        if (bot > M.scroll + view)
            M.scroll = bot - view;
        M.ensure_visible = 0;
    }
    if (ImGui::IsMouseHoveringRect(ImVec2(x0, list_y0), ImVec2(lx1, list_y1)))
        M.scroll -= ImGui::GetIO().MouseWheel * rh;
    M.scroll = fmaxf(0.0f, fminf(M.scroll, acc - view));
    if (acc <= view)
        M.scroll = 0;
    dl->PushClipRect(ImVec2(x0 - px * 0.3f, list_y0), ImVec2(lx1 + px * 0.3f, list_y1), true);
    for (int k = 0; k < n; k++) {
        float ry = list_y0 + tops[k] - M.scroll;
        float h = tops[k + 1] - tops[k];
        if (ry + h < list_y0 || ry > list_y1)
            continue;
        draw_row(dl, k, idx[k], k == sel, ImVec2(x0 - px * 0.3f, ry), lx1 - x0 + px * 0.6f, h, px);
    }
    if (!n)
        text_at(dl, px, ImVec2(x0, list_y0 + px), C_DIM, "Nothing here yet.");
    dl->PopClipRect();
    if (acc > view) {   /* a scroll bar */
        float bx = lx1 + px * 0.45f, frac = view / acc;
        float by0 = list_y0 + (view - view * frac) * (M.scroll / (acc - view));
        dl->AddRectFilled(ImVec2(bx, list_y0), ImVec2(bx + px * 0.12f, list_y1), IM_COL32(255, 255, 255, 18),
                          px * 0.06f);
        dl->AddRectFilled(ImVec2(bx, by0), ImVec2(bx + px * 0.12f, by0 + view * frac),
                          IM_COL32(255, 255, 255, 80), px * 0.06f);
        if (M.scroll > 1.0f)
            dl->AddRectFilledMultiColor(ImVec2(x0, list_y0), ImVec2(lx1, list_y0 + px * 0.6f),
                                        IM_COL32(16, 19, 27, 230), IM_COL32(16, 19, 27, 230),
                                        IM_COL32(16, 19, 27, 0), IM_COL32(16, 19, 27, 0));
        if (M.scroll < acc - view - 1.0f)   /* more below: fade the cut-off row */
            dl->AddRectFilledMultiColor(ImVec2(x0 - px * 0.3f, list_y1 - px * 1.2f),
                                        ImVec2(lx1 + px * 0.3f, list_y1),
                                        IM_COL32(16, 19, 27, 0), IM_COL32(16, 19, 27, 0),
                                        IM_COL32(16, 19, 27, 250), IM_COL32(16, 19, 27, 250));
    }

    /* Help for the selected row, and its picture. */
    float hy = list_y1;
    dl->AddLine(ImVec2(x0, hy), ImVec2(x1, hy), C_LINE, 1.0f);
    const setting *cur = sel >= 0 ? settings_at(idx[sel]) : NULL;
    update_preview(cur);
    float text_x1 = x1;
    if (pic_col) {
        /* the selected row's picture, 8:7 as the console's, in its column */
        float cx0 = lx1 + px * 1.3f, cw = x1 - cx0, ch = cw * 7.0f / 8.0f;
        ImVec2 a(cx0, list_y0 + px * 0.5f), b(x1, a.y + ch);
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), px * 0.2f);
        if (cur && M.preview_of == cur && M.preview) {
            float iw = cw, ih = cw * M.preview->Height / M.preview->Width;
            if (ih > ch)
                ih = ch, iw = ch * M.preview->Width / M.preview->Height;
            ImVec2 ia(a.x + (cw - iw) * 0.5f, a.y + (ch - ih) * 0.5f);
            dl->AddImage(M.preview->GetTexRef(), ia, ImVec2(ia.x + iw, ia.y + ih));
        } else {
            const char *t = "No picture";
            ImVec2 ts = text_size(px * 0.8f, t);
            text_at(dl, px * 0.8f, ImVec2(a.x + (cw - ts.x) * 0.5f, a.y + (ch - ts.y) * 0.5f), C_FAINT,
                    t);
        }
        dl->AddRect(a, b, C_LINE, px * 0.2f, 1.0f);
    }
    if (cur) {
        char help[512];
        snprintf(help, sizeof help, "%s%s%s", cur->help ? cur->help : "",
                 cur->help && (cur->flags & SETTING_RESTART) ? " " : "",
                 cur->flags & SETTING_RESTART ? "(Takes effect on the next start.)" : "");
        if (M.capture >= 0)
            snprintf(help, sizeof help, "Press the key to use. Escape: cancel. Backspace: no key.");
        float fs = px * 0.8f;
        dl->AddText(M.font, fs, ImVec2(x0, hy + px * 0.45f), C_DIM, help, NULL, text_x1 - x0);
    }

    /* Footer: what the buttons do, with the glyphs of the device in use. */
    float fy = p1.y - footer_h * 0.5f;
    dl->AddLine(ImVec2(x0, p1.y - footer_h), ImVec2(x1, p1.y - footer_h), C_LINE, 1.0f);
    bool kb = glyphs_in_use() == GLYPHS_KEYBOARD;
    float fx = x0;
    /* A's word for the row; an int is adjusted with left / right instead */
    bool usable = cur && settings_is_enabled(cur);
    const char *what = !usable                                                   ? NULL
                       : cur->type == SETTING_BOOL || cur->type == SETTING_ENUM ? "Change"
                       : cur->type == SETTING_KEY                               ? "Set key"
                       : cur->type == SETTING_ACTION                            ? "Run"
                                                                                : NULL;
    bool adjust = usable && cur->type == SETTING_INT;
    fx = prompt(dl, fx, fy, px * 0.85f, kb ? "key:Arrows" : "dpad", NULL,
                adjust ? "Move, adjust" : "Move");
    if (what)
        fx = prompt(dl, fx, fy, px * 0.85f, kb ? "key:Return" : "a", NULL, what);
    fx = prompt(dl, fx, fy, px * 0.85f, kb ? "key:Escape" : "b", NULL,
                M.capture >= 0 || M.confirm >= 0 ? "Cancel" : "Resume");
    fx = prompt(dl, fx, fy, px * 0.85f, kb ? "key:Q" : "l1", kb ? "key:W" : "r1", "Tabs");
    (void)fx;
    ImGui::End();
}

void setup(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   /* main.c owns the cursor */
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
#ifdef CT_MENU_FONT
    M.font = io.Fonts->AddFontFromMemoryTTF((void *)menu_font, (int)menu_font_size, 20.0f, &cfg);
#endif
    if (!M.font)
        M.font = io.Fonts->AddFontDefaultVector();
    if (M.host.ren) {
        ImGui_ImplSDL2_InitForSDLRenderer(M.host.win, M.host.ren);
        ImGui_ImplSDLRenderer2_Init(M.host.ren);
    } else {
        ImGui_ImplSDL2_InitForOpenGL(M.host.win, SDL_GL_GetCurrentContext());
        ImGui_ImplOpenGL3_Init(M.host.gles ? "#version 300 es" : "#version 330 core");
    }
    /* Controllers are ours (menu_event): ImGui's backend opens none. */
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, NULL, 0);
    M.scale_px = 0;
    M.ready = 1;
    fprintf(stderr, "ct_sdl: menu: Dear ImGui %s, %s renderer\n", ImGui::GetVersion(),
            M.host.ren ? "SDL_Renderer" : M.host.gles ? "OpenGL ES 3" : "OpenGL 3");
}

}   // namespace

extern "C" {

void menu_init(const menu_host *h)
{
    M.host = *h;
    M.have_host = 1;
    M.capture = M.confirm = -1;
}

void menu_shutdown(void)
{
    if (!M.ready)
        return;
    if (M.host.ren)
        ImGui_ImplSDLRenderer2_Shutdown();
    else
        ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    for (int k = 0; k < M.n_glyphs; k++)
        if (M.glyphs[k].tex) {
            ImGui::UnregisterUserTexture(M.glyphs[k].tex);
            IM_DELETE(M.glyphs[k].tex);
        }
    for (ImTextureData *t : M.retired) {
        ImGui::UnregisterUserTexture(t);
        IM_DELETE(t);
    }
    if (M.preview) {
        ImGui::UnregisterUserTexture(M.preview);
        IM_DELETE(M.preview);
    }
    ImGui::DestroyContext();
    M.ready = 0;
    M.n_glyphs = 0;
    M.preview = NULL;
    M.retired.clear();
}

void menu_open(void)
{
    if (M.open || !M.have_host)
        return;
    settings_refresh();   /* lists that change (packs) */
    M.open = 1;
    M.opened++;
    M.capture = M.confirm = -1;
    M.pad = M.stick = M.keys = Dir{0, 0};
    M.cur_x = M.cur_y = 0;
    M.ensure_visible = 1;
}

void menu_close(void)
{
    M.open = 0;
    M.capture = M.confirm = -1;
}

int menu_is_open(void) { return M.open; }
long menu_opened_count(void) { return M.opened; }
int menu_capturing_key(void) { return M.open && M.capture >= 0; }

int menu_event(const SDL_Event *ev)
{
    if (!M.open)
        return 0;
    switch (ev->type) {
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int down = ev->type == SDL_CONTROLLERBUTTONDOWN;
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: M.pad.y = down ? -1 : M.pad.y == -1 ? 0 : M.pad.y; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: M.pad.y = down ? 1 : M.pad.y == 1 ? 0 : M.pad.y; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: M.pad.x = down ? -1 : M.pad.x == -1 ? 0 : M.pad.x; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: M.pad.x = down ? 1 : M.pad.x == 1 ? 0 : M.pad.x; break;
        default:
            if (!down)
                break;
            if (M.capture >= 0) {   /* a controller can't give a key: B / A leave */
                M.capture = -1;
                break;
            }
            switch (ev->cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: {
                int i;
                const setting *s = selected(&i);
                activate(s, i);
                break;
            }
            case SDL_CONTROLLER_BUTTON_B: back(); break;
            case SDL_CONTROLLER_BUTTON_START: menu_close(); break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: switch_tab(-1); break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: switch_tab(1); break;
            default: break;
            }
        }
        update_dir();
        return 1;
    }
    case SDL_CONTROLLERAXISMOTION:
        if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
            M.stick.x = stick_dir(ev->caxis.value, M.stick.x);
        else if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            M.stick.y = stick_dir(ev->caxis.value, M.stick.y);
        else
            return 1;
        update_dir();
        return 1;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        int down = ev->type == SDL_KEYDOWN;
        SDL_Scancode sc = ev->key.keysym.scancode;
        if (M.capture >= 0) {
            if (!down || ev->key.repeat)
                return 1;
            const setting *s = settings_at(M.capture);
            M.capture = -1;
            if (sc == SDL_SCANCODE_ESCAPE)
                return 1;
            if (s)
                settings_set_value(s, sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_DELETE ? 0 : sc);
            return 1;
        }
        switch (sc) {
        case SDL_SCANCODE_UP: M.keys.y = down ? -1 : M.keys.y == -1 ? 0 : M.keys.y; break;
        case SDL_SCANCODE_DOWN: M.keys.y = down ? 1 : M.keys.y == 1 ? 0 : M.keys.y; break;
        case SDL_SCANCODE_LEFT: M.keys.x = down ? -1 : M.keys.x == -1 ? 0 : M.keys.x; break;
        case SDL_SCANCODE_RIGHT: M.keys.x = down ? 1 : M.keys.x == 1 ? 0 : M.keys.x; break;
        default:
            if (!down || ev->key.repeat)
                break;
            switch (sc) {
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
            case SDL_SCANCODE_SPACE: {
                int i;
                const setting *s = selected(&i);
                activate(s, i);
                break;
            }
            case SDL_SCANCODE_ESCAPE:
            case SDL_SCANCODE_BACKSPACE: back(); break;
            case SDL_SCANCODE_Q:
            case SDL_SCANCODE_PAGEUP: switch_tab(-1); break;
            case SDL_SCANCODE_W:
            case SDL_SCANCODE_PAGEDOWN:
            case SDL_SCANCODE_TAB: switch_tab(1); break;
            case SDL_SCANCODE_HOME: M.row[M.tab] = 0; M.ensure_visible = 1; break;
            default: return 0;   /* not ours: the caller's (the menu key closes it) */
            }
        }
        update_dir();
        return 1;
    }
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        if (M.ready)
            ImGui_ImplSDL2_ProcessEvent(ev);
        return 1;
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
    case SDL_FINGERMOTION:
        return 1;   /* SDL turns touches into mouse events too: those work the menu */
    case SDL_WINDOWEVENT:
        if (M.ready)
            ImGui_ImplSDL2_ProcessEvent(ev);
        return 0;
    default:
        return 0;
    }
}

void menu_tick(unsigned ms)
{
    if (!M.open || (!M.cur_x && !M.cur_y))
        return;
    M.held_ms += ms;
    while (M.held_ms >= M.next_ms) {
        M.next_ms += REPEAT_MS;
        if (M.cur_y)
            move_row(M.cur_y);
        else
            change(selected(NULL), M.cur_x);
    }
}

void menu_draw(int w, int h)
{
    if (!M.open || !M.have_host)
        return;
    if (!M.ready)
        setup();
    collect_retired();
    if (M.host.ren)
        ImGui_ImplSDLRenderer2_NewFrame();
    else
        ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    /* The window size in ImGui's units; the backend took it from SDL,
       which the offscreen and dummy drivers may not keep current. */
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) {
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DisplayFramebufferScale = ImVec2(1, 1);
    }
    float px = body_px(io.DisplaySize.x, io.DisplaySize.y);
    if (fabsf(px - M.scale_px) > 0.5f)
        make_style(px);
    ImGui::NewFrame();
    ImGui::PushFont(M.font, M.scale_px);
    build_ui(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::PopFont();
    ImGui::Render();
    if (M.host.ren) {
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), M.host.ren);
    } else {
        typedef void (*bind_fn)(unsigned, unsigned);
        static bind_fn bind = (bind_fn)SDL_GL_GetProcAddress("glBindFramebuffer");
        if (bind)
            bind(0x8D40 /* GL_FRAMEBUFFER */, 0);   /* the window's */
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

int menu_shot(const char *path, int w, int h)
{
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px)
        return -1;
    int ok = 0;
    if (M.host.ren) {
        ok = !SDL_RenderReadPixels(M.host.ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4);
    } else {
        typedef void (*read_fn)(int, int, int, int, unsigned, unsigned, void *);
        typedef void (*pixel_fn)(unsigned, int);
        typedef void (*buf_fn)(unsigned);
        read_fn rd = (read_fn)SDL_GL_GetProcAddress("glReadPixels");
        pixel_fn ps = (pixel_fn)SDL_GL_GetProcAddress("glPixelStorei");
        buf_fn rb = (buf_fn)SDL_GL_GetProcAddress("glReadBuffer");
        if (rd && ps) {
            if (rb && !M.host.gles)
                rb(0x0405 /* GL_BACK */);
            ps(0x0D05 /* GL_PACK_ALIGNMENT */, 4);
            unsigned char *rgba = (unsigned char *)malloc((size_t)w * h * 4);
            if (rgba) {
                rd(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba);
                for (int y = 0; y < h; y++)   /* bottom-up RGBA -> top-down B G R x */
                    for (int x = 0; x < w; x++) {
                        const unsigned char *s = rgba + ((size_t)(h - 1 - y) * w + x) * 4;
                        unsigned char *d = px + ((size_t)y * w + x) * 4;
                        d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = 255;
                    }
                free(rgba);
                ok = 1;
            }
        }
    }
    ok = ok && !png_write_bgrx(path, px, w, h);
    free(px);
    return ok ? 0 : -1;
}

}   // extern "C"

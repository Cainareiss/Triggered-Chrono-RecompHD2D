/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the Dear ImGui backend both menus share (ui_backend.h). */
#include "ui_backend.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "glyphs.h"
#include "png.h"
#include "settings.h"
}

#include "imgui_internal.h"   /* RegisterUserTexture */
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#ifdef CT_MENU_FONT
#include "menu_font.h"   /* Roboto Medium (third_party/imgui/fonts), tools/embed_binary.cmake */
#endif

namespace {

/* A glyph's picture as an ImGui texture, per glyph set and token. */
struct GlyphTex {
    int set;
    char token[24];
    ImTextureData *tex;   /* NULL: no picture (label) */
    char label[32];
};

struct Ui {
    SDL_Window *win;
    SDL_Renderer *ren;
    int gles, have_host, ready;
    ImFont *font;
    GlyphTex glyphs[64];
    int n_glyphs;
    ImVector<ImTextureData *> retired;
} U = {};

const GlyphTex *glyph(const char *token)
{
    int set = glyphs_in_use();
    for (int k = 0; k < U.n_glyphs; k++)
        if (U.glyphs[k].set == set && !strcmp(U.glyphs[k].token, token))
            return &U.glyphs[k];
    if (U.n_glyphs == (int)(sizeof U.glyphs / sizeof *U.glyphs))
        return NULL;
    GlyphTex *g = &U.glyphs[U.n_glyphs++];
    g->set = set;
    snprintf(g->token, sizeof g->token, "%s", token);
    const char *label = "";
    const uint8_t *px = glyphs_lookup_in(set, token, &label);
    snprintf(g->label, sizeof g->label, "%s", label ? label : "");
    g->tex = NULL;
    if (px) {
        g->tex = ui_make_texture(GLYPH_PX, GLYPH_PX);
        unsigned char *p = (unsigned char *)g->tex->GetPixels();
        for (int k = 0; k < GLYPH_PX * GLYPH_PX; k++) {
            p[k * 4 + 0] = p[k * 4 + 1] = p[k * 4 + 2] = 255;
            p[k * 4 + 3] = px[k];
        }
    }
    return g;
}

}   // namespace

void ui_set_host(SDL_Window *win, SDL_Renderer *ren, int gles)
{
    U.win = win;
    U.ren = ren;
    U.gles = gles;
    U.have_host = 1;
}

int ui_have_host(void) { return U.have_host; }
SDL_Window *ui_window(void) { return U.win; }
int ui_ready(void) { return U.ready; }
ImFont *ui_font(void) { return U.font; }

int ui_ensure(void)
{
    if (U.ready)
        return 1;
    if (!U.have_host)
        return 0;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   /* main.c owns the cursor */
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
#ifdef CT_MENU_FONT
    U.font = io.Fonts->AddFontFromMemoryTTF((void *)menu_font, (int)menu_font_size, 20.0f, &cfg);
#endif
    if (!U.font)
        U.font = io.Fonts->AddFontDefaultVector();
    if (U.ren) {
        ImGui_ImplSDL2_InitForSDLRenderer(U.win, U.ren);
        ImGui_ImplSDLRenderer2_Init(U.ren);
    } else {
        ImGui_ImplSDL2_InitForOpenGL(U.win, SDL_GL_GetCurrentContext());
        ImGui_ImplOpenGL3_Init(U.gles ? "#version 300 es" : "#version 330 core");
    }
    /* Controllers are the menus' own: ImGui's backend opens none. */
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, NULL, 0);
    U.ready = 1;
    fprintf(stderr, "ct_sdl: menu: Dear ImGui %s, %s renderer\n", ImGui::GetVersion(),
            U.ren ? "SDL_Renderer" : U.gles ? "OpenGL ES 3" : "OpenGL 3");
    return 1;
}

void ui_shutdown(void)
{
    if (!U.ready)
        return;
    if (U.ren)
        ImGui_ImplSDLRenderer2_Shutdown();
    else
        ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    for (int k = 0; k < U.n_glyphs; k++)
        if (U.glyphs[k].tex) {
            ImGui::UnregisterUserTexture(U.glyphs[k].tex);
            IM_DELETE(U.glyphs[k].tex);
        }
    for (ImTextureData *t : U.retired) {
        ImGui::UnregisterUserTexture(t);
        IM_DELETE(t);
    }
    ImGui::DestroyContext();
    U.ready = 0;
    U.font = NULL;
    U.n_glyphs = 0;
    U.retired.clear();
}

void ui_process_event(const SDL_Event *ev)
{
    if (U.ready)
        ImGui_ImplSDL2_ProcessEvent(ev);
}

void ui_frame_begin(int w, int h)
{
    ui_collect_retired();
    if (U.ren)
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
}

void ui_frame_end(void)
{
    ImGui::Render();
    if (U.ren) {
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), U.ren);
    } else {
        typedef void (*bind_fn)(unsigned, unsigned);
        static bind_fn bind = (bind_fn)SDL_GL_GetProcAddress("glBindFramebuffer");
        if (bind)
            bind(0x8D40 /* GL_FRAMEBUFFER */, 0);   /* the window's */
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

float ui_menu_scale(void)
{
    const setting *ms = settings_find("menu_scale");
    static const float scales[] = {0.85f, 1.0f, 1.2f};
    int v = ms ? settings_get_value(ms) : 1;
    return scales[v >= 0 && v < 3 ? v : 1];
}

/* The body text size for a display of w x h points (scale by size and,
   where SDL knows it, by how big the screen physically is). */
float ui_body_px(float w, float h)
{
    float px = h * 0.040f;   /* a handheld's 800-line screen: 32 px */
    float ddpi = 0, hdpi = 0, vdpi = 0;
    int idx = U.win ? SDL_GetWindowDisplayIndex(U.win) : -1;
    if (idx >= 0 && !SDL_GetDisplayDPI(idx, &ddpi, &hdpi, &vdpi) && vdpi > 40.0f) {
        SDL_DisplayMode dm;
        if (!SDL_GetCurrentDisplayMode(idx, &dm) && dm.h / vdpi > 6.0f)
            px = h * 0.032f;   /* a desktop monitor: the menu needn't be as big */
    }
    if (w < h * 1.2f)
        px = fminf(px, w * 0.036f);   /* narrow windows */
    px *= ui_menu_scale();
    return px < 15.0f ? 15.0f : px > 72.0f ? 72.0f : px;
}

void ui_text(ImDrawList *dl, float size, ImVec2 p, ImU32 col, const char *t)
{
    dl->AddText(U.font, size, p, col, t);
}

ImVec2 ui_text_size(float size, const char *t)
{
    return U.font->CalcTextSizeA(size, 1e9f, 0.0f, t);
}

ImTextureData *ui_make_texture(int w, int h)
{
    ImTextureData *t = IM_NEW(ImTextureData)();
    t->Create(ImTextureFormat_RGBA32, w, h);
    ImGui::RegisterUserTexture(t);
    return t;
}

void ui_retire_texture(ImTextureData *t)
{
    if (!t)
        return;
    t->WantDestroyNextFrame = true;
    U.retired.push_back(t);
}

void ui_collect_retired(void)
{
    for (int k = 0; k < U.retired.Size;) {
        ImTextureData *t = U.retired[k];
        if (t->Status == ImTextureStatus_Destroyed) {
            ImGui::UnregisterUserTexture(t);
            IM_DELETE(t);
            U.retired.erase(U.retired.Data + k);
        } else {
            k++;
        }
    }
}

void ui_destroy_texture(ImTextureData *t)
{
    if (t) {
        ImGui::UnregisterUserTexture(t);
        IM_DELETE(t);
    }
}

float ui_draw_glyph(ImDrawList *dl, const char *token, ImVec2 p, float hgt, float px, ImU32 col)
{
    const GlyphTex *g = glyph(token);
    if (g && g->tex) {
        dl->AddImage(g->tex->GetTexRef(), p, ImVec2(p.x + hgt, p.y + hgt), ImVec2(0, 0), ImVec2(1, 1),
                     col);
        return hgt;
    }
    const char *label = g ? g->label : token;
    float fs = px * 0.72f;
    ImVec2 ts = ui_text_size(fs, label);
    float w = ts.x + px * 0.6f;
    dl->AddRect(ImVec2(p.x, p.y + hgt * 0.1f), ImVec2(p.x + w, p.y + hgt * 0.9f), col, px * 0.2f,
                fmaxf(1.0f, px * 0.06f));
    ui_text(dl, fs, ImVec2(p.x + px * 0.3f, p.y + (hgt - ts.y) * 0.5f), col, label);
    return w;
}

int ui_shot(const char *path, int w, int h)
{
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px)
        return -1;
    int ok = 0;
    if (U.ren) {
        ok = !SDL_RenderReadPixels(U.ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4);
    } else {
        typedef void (*read_fn)(int, int, int, int, unsigned, unsigned, void *);
        typedef void (*pixel_fn)(unsigned, int);
        typedef void (*buf_fn)(unsigned);
        read_fn rd = (read_fn)SDL_GL_GetProcAddress("glReadPixels");
        pixel_fn ps = (pixel_fn)SDL_GL_GetProcAddress("glPixelStorei");
        buf_fn rb = (buf_fn)SDL_GL_GetProcAddress("glReadBuffer");
        if (rd && ps) {
            if (rb && !U.gles)
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

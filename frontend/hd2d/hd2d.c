/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HD-2D renderer (hd2d.h). Passes, all full-screen triangles:
 *
 *   1 light     source size   classify pixels from the layer G-buffer,
 *                             relief lighting, actor shadows, contact
 *                             occlusion, rim light, haze  -> lit, mat
 *   2 upscale   picture size  sharp bilinear               -> scene
 *   3 coc/dof   half size     blur amount, then H and V variable blur
 *   4 bloom     1/4 and 1/8   threshold, Gaussian H and V; plus an
 *                             unthresholded 1/8 copy for the side fill
 *   5 final     output size   DOF mix, bloom, grade, vignette, grain, UI
 *
 * Every intermediate keeps row 0 at the top of the picture (the order the
 * frame is uploaded in); only drawing to the window flips. GL entry points
 * come from SDL_GL_GetProcAddress, so nothing links libGL directly. */
#include "hd2d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "hd2d_shaders.h"   /* generated: tools/embed_text.py */

/* ---- the GL subset used here (values from the Khronos registry) ---- */
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef char GLchar;

#define GL_NO_ERROR                 0
#define GL_TRIANGLES                0x0004
#define GL_DEPTH_TEST               0x0B71
#define GL_BLEND                    0x0BE2
#define GL_SCISSOR_TEST             0x0C11
#define GL_UNPACK_ALIGNMENT         0x0CF5
#define GL_PACK_ALIGNMENT           0x0D05
#define GL_TEXTURE_2D               0x0DE1
#define GL_UNSIGNED_BYTE            0x1401
#define GL_FLOAT                    0x1406
#define GL_HALF_FLOAT               0x140B
#define GL_RED                      0x1903
#define GL_RGBA                     0x1908
#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_NEAREST                  0x2600
#define GL_LINEAR                   0x2601
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_COLOR_BUFFER_BIT         0x00004000
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_TEXTURE0                 0x84C0
#define GL_RGBA8                    0x8058
#define GL_RGBA16F                  0x881A
#define GL_R8UI                     0x8232
#define GL_RED_INTEGER              0x8D94
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_VERTEX_SHADER            0x8B31
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_INFO_LOG_LENGTH          0x8B84
#define GL_FRAMEBUFFER              0x8D40
#define GL_FRAMEBUFFER_COMPLETE     0x8CD5
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_COLOR_ATTACHMENT1        0x8CE1
#define GL_READ_FRAMEBUFFER         0x8CA8
#define GL_DRAW_FRAMEBUFFER         0x8CA9

#define GL_FUNCS(X)                                                                           \
    X(const GLubyte *, glGetString, (GLenum name))                                            \
    X(GLenum, glGetError, (void))                                                             \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                             \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                       \
    X(void, glClear, (GLbitfield mask))                                                       \
    X(void, glDisable, (GLenum cap))                                                          \
    X(void, glFinish, (void))                                                                 \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                       \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p)) \
    X(void, glGenTextures, (GLsizei n, GLuint *t))                                            \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *t))                                   \
    X(void, glBindTexture, (GLenum target, GLuint t))                                         \
    X(void, glActiveTexture, (GLenum unit))                                                   \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                      \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,      \
                           GLint border, GLenum fmt, GLenum type, const void *data))          \
    X(void, glTexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w,        \
                              GLsizei h, GLenum fmt, GLenum type, const void *data))          \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *f))                                        \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *f))                               \
    X(void, glBindFramebuffer, (GLenum target, GLuint f))                                     \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum att, GLenum tt, GLuint t, GLint l)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                      \
    X(void, glDrawBuffers, (GLsizei n, const GLenum *bufs))                                   \
    X(GLuint, glCreateShader, (GLenum type))                                                  \
    X(void, glShaderSource, (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len)) \
    X(void, glCompileShader, (GLuint s))                                                      \
    X(void, glGetShaderiv, (GLuint s, GLenum pname, GLint *v))                                \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei max, GLsizei *len, GLchar *log))           \
    X(void, glDeleteShader, (GLuint s))                                                       \
    X(GLuint, glCreateProgram, (void))                                                        \
    X(void, glAttachShader, (GLuint p, GLuint s))                                             \
    X(void, glLinkProgram, (GLuint p))                                                        \
    X(void, glGetProgramiv, (GLuint p, GLenum pname, GLint *v))                               \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei max, GLsizei *len, GLchar *log))          \
    X(void, glDeleteProgram, (GLuint p))                                                      \
    X(void, glUseProgram, (GLuint p))                                                         \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar *name))                            \
    X(void, glUniform1i, (GLint loc, GLint v))                                                \
    X(void, glUniform1f, (GLint loc, GLfloat v))                                              \
    X(void, glUniform2f, (GLint loc, GLfloat a, GLfloat b))                                   \
    X(void, glUniform2i, (GLint loc, GLint a, GLint b))                                       \
    X(void, glUniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c))                        \
    X(void, glUniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d))             \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *v))                                        \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *v))                               \
    X(void, glBindVertexArray, (GLuint v))                                                    \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))

#define X(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p_##name;
GL_FUNCS(X)
#undef X

static int load_gl(void)
{
    int missing = 0;
#define X(ret, name, args)                                                     \
    {                                                                          \
        void *sym = SDL_GL_GetProcAddress(#name);                              \
        memcpy(&p_##name, &sym, sizeof sym); /* object -> function pointer */  \
    }                                                                          \
    if (!p_##name) {                                                           \
        fprintf(stderr, "hd2d: missing GL function %s\n", #name);             \
        missing = 1;                                                           \
    }
    GL_FUNCS(X)
#undef X
    return missing ? -1 : 0;
}

/* ---- settings ---- */

typedef struct {
    float relief, shadow, ao, rim, haze;
    float dof_tilt, dof_depth, focus_band, focus_ramp, dof_radius;
    float bloom, bloom_threshold;
    float exposure, saturation, contrast, vignette, grain, split;
    float side_dim;
    int effects;
} settings;

static const settings presets[HD2D_PRESETS] = {
    [HD2D_OFF] = {.exposure = 1.0f, .saturation = 1.0f, .contrast = 1.0f, .side_dim = 0.45f},
    [HD2D_SUBTLE] = {.relief = 0.3f, .shadow = 0.5f, .ao = 0.4f, .rim = 0.3f, .haze = 0.25f,
                     .dof_tilt = 0.45f, .dof_depth = 0.25f, .focus_band = 0.2f, .focus_ramp = 0.4f,
                     .dof_radius = 4.0f, .bloom = 0.25f, .bloom_threshold = 0.78f,
                     .exposure = 1.1f, .saturation = 1.06f, .contrast = 1.02f, .vignette = 0.2f,
                     .grain = 0.01f, .split = 0.2f, .side_dim = 0.42f, .effects = 1},
    [HD2D_FULL] = {.relief = 0.55f, .shadow = 0.85f, .ao = 0.7f, .rim = 0.6f, .haze = 0.5f,
                   .dof_tilt = 0.9f, .dof_depth = 0.5f, .focus_band = 0.18f, .focus_ramp = 0.34f,
                   .dof_radius = 6.0f, .bloom = 0.45f, .bloom_threshold = 0.7f,
                   .exposure = 1.25f, .saturation = 1.12f, .contrast = 1.04f, .vignette = 0.35f,
                   .grain = 0.018f, .split = 0.35f, .side_dim = 0.42f, .effects = 1},
};

static const char *const preset_names[HD2D_PRESETS] = {"off", "subtle", "hd2d"};

static int cur_preset = HD2D_FULL, debug_view, aspect_mode, timing;
static double last_ms;

void hd2d_set_preset(int p) { cur_preset = p < 0 || p >= HD2D_PRESETS ? HD2D_FULL : p; }
int hd2d_preset(void) { return cur_preset; }
const char *hd2d_preset_name(int p) { return p >= 0 && p < HD2D_PRESETS ? preset_names[p] : "?"; }
void hd2d_set_debug(int v) { debug_view = v < 0 || v >= HD2D_DEBUGS ? 0 : v; }
int hd2d_debug(void) { return debug_view; }
void hd2d_set_aspect(int a) { aspect_mode = a < 0 || a >= HD2D_ASPECTS ? 0 : a; }
int hd2d_aspect(void) { return aspect_mode; }
void hd2d_set_timing(int on) { timing = on; }
double hd2d_last_ms(void) { return last_ms; }

/* ---- GL objects ---- */

enum { P_LIGHT, P_UPSCALE, P_COC, P_DOFBLUR, P_BRIGHT, P_GAUSS, P_FINAL, P_COUNT };

typedef struct {
    GLuint tex, fbo;
    int w, h;
} target;

static struct {
    int gles, ready;
    GLenum float_fmt, float_type;   /* RGBA16F if renderable, else RGBA8 */
    GLuint prog[P_COUNT];
    GLuint vao;
    GLuint t_color, t_layer;        /* uploads, source size */
    GLuint t_lit, t_mat, fbo_light; /* pass 1 targets, source size */
    target scene, half[2], quarter[2], eighth[2], soft[2], out;
    int pic_w, pic_h, out_w, out_h;
    float focus_y;
    unsigned frame;
} g;

static GLuint compile(GLenum type, const char *body, const char *name)
{
    const char *prefix = g.gles ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                                  "precision highp usampler2D;\n"
                                : "#version 330 core\n";
    const char *src[2] = {prefix, body};
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 2, src, NULL);
    p_glCompileShader(s);
    GLint ok = 0;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "hd2d: %s: compile failed:\n%s\n", name, log);
        p_glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(const char *frag, const char *name)
{
    GLuint vs = compile(GL_VERTEX_SHADER, hd2d_src_fullscreen_vert, "fullscreen.vert");
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag, name);
    if (!vs || !fs)
        return 0;
    GLuint p = p_glCreateProgram();
    p_glAttachShader(p, vs);
    p_glAttachShader(p, fs);
    p_glLinkProgram(p);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    GLint ok = 0;
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "hd2d: %s: link failed:\n%s\n", name, log);
        p_glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint make_tex(int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filter)
{
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D, t);
    p_glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, NULL);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static int fbo_for(GLuint *fbo, const GLuint *tex, int n)
{
    static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    p_glGenFramebuffers(1, fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    for (int k = 0; k < n; k++)
        p_glFramebufferTexture2D(GL_FRAMEBUFFER, bufs[k], GL_TEXTURE_2D, tex[k], 0);
    p_glDrawBuffers(n, bufs);
    int ok = p_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok ? 0 : -1;
}

static void target_free(target *t)
{
    if (t->fbo)
        p_glDeleteFramebuffers(1, &t->fbo);
    if (t->tex)
        p_glDeleteTextures(1, &t->tex);
    memset(t, 0, sizeof *t);
}

static int target_make(target *t, int w, int h, int fp)
{
    target_free(t);
    t->w = w < 1 ? 1 : w;
    t->h = h < 1 ? 1 : h;
    t->tex = fp ? make_tex(t->w, t->h, (GLint)g.float_fmt, GL_RGBA, g.float_type, GL_LINEAR)
                : make_tex(t->w, t->h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    return fbo_for(&t->fbo, &t->tex, 1);
}

int hd2d_init(int gles)
{
    memset(&g, 0, sizeof g);
    g.gles = gles;
    g.focus_y = 0.55f;
    if (load_gl())
        return -1;
    fprintf(stderr, "hd2d: %s / %s / %s\n", p_glGetString(GL_VENDOR), p_glGetString(GL_RENDERER),
            p_glGetString(GL_VERSION));

    static const struct { const char *src, *name; } fs[P_COUNT] = {
        [P_LIGHT] = {hd2d_src_light_frag, "light.frag"},
        [P_UPSCALE] = {hd2d_src_upscale_frag, "upscale.frag"},
        [P_COC] = {hd2d_src_coc_frag, "coc.frag"},
        [P_DOFBLUR] = {hd2d_src_dofblur_frag, "dofblur.frag"},
        [P_BRIGHT] = {hd2d_src_bright_frag, "bright.frag"},
        [P_GAUSS] = {hd2d_src_gauss_frag, "gauss.frag"},
        [P_FINAL] = {hd2d_src_final_frag, "final.frag"},
    };
    for (int k = 0; k < P_COUNT; k++)
        if (!(g.prog[k] = link(fs[k].src, fs[k].name)))
            return -1;

    p_glGenVertexArrays(1, &g.vao);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1);

    g.t_color = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    g.t_layer = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                         GL_NEAREST);

    /* Half-float targets where they render (desktop GL 3.3 always; ES 3.0
       with EXT_color_buffer_float), else 8-bit. */
    g.float_fmt = GL_RGBA16F;
    g.float_type = GL_HALF_FLOAT;
    g.t_lit = make_tex(SCHED_WIDTH, SCHED_HEIGHT, (GLint)g.float_fmt, GL_RGBA, g.float_type,
                       GL_LINEAR);
    g.t_mat = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    GLuint lt[2] = {g.t_lit, g.t_mat};
    if (fbo_for(&g.fbo_light, lt, 2)) {
        fprintf(stderr, "hd2d: half-float targets not renderable: using RGBA8\n");
        p_glDeleteFramebuffers(1, &g.fbo_light);
        p_glDeleteTextures(1, &g.t_lit);
        g.float_fmt = GL_RGBA8;
        g.float_type = GL_UNSIGNED_BYTE;
        g.t_lit = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        lt[0] = g.t_lit;
        if (fbo_for(&g.fbo_light, lt, 2)) {
            fprintf(stderr, "hd2d: cannot make the lighting target\n");
            return -1;
        }
    }
    GLenum err = p_glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "hd2d: GL error 0x%x during setup\n", err);
        return -1;
    }
    g.ready = 1;
    return 0;
}

void hd2d_shutdown(void)
{
    if (!g.ready)
        return;
    target *ts[] = {&g.scene, &g.half[0], &g.half[1], &g.quarter[0], &g.quarter[1],
                    &g.eighth[0], &g.eighth[1], &g.soft[0], &g.soft[1], &g.out};
    for (size_t k = 0; k < sizeof ts / sizeof *ts; k++)
        target_free(ts[k]);
    GLuint tex[4] = {g.t_color, g.t_layer, g.t_lit, g.t_mat};
    p_glDeleteTextures(4, tex);
    p_glDeleteFramebuffers(1, &g.fbo_light);
    for (int k = 0; k < P_COUNT; k++)
        p_glDeleteProgram(g.prog[k]);
    p_glDeleteVertexArrays(1, &g.vao);
    g.ready = 0;
}

/* The picture's rectangle inside the output, top-left origin. */
static void picture_rect(int ow, int oh, int *x, int *y, int *w, int *h)
{
    double aspect = aspect_mode == HD2D_ASPECT_PIXEL ? (SCHED_WIDTH * 8.0 / 7.0) / SCHED_HEIGHT
                    : aspect_mode == HD2D_ASPECT_STRETCH ? (double)ow / oh
                                                         : 4.0 / 3.0;
    *h = oh;
    *w = (int)lround(oh * aspect);
    if (*w > ow) {
        *w = ow;
        *h = (int)lround(ow / aspect);
    }
    *x = (ow - *w) / 2;
    *y = (oh - *h) / 2;
}

static int resize(int ow, int oh, int pw, int ph)
{
    if (pw != g.pic_w || ph != g.pic_h) {
        g.pic_w = pw;
        g.pic_h = ph;
        int ok = !target_make(&g.scene, pw, ph, 1);
        for (int k = 0; k < 2; k++) {
            ok &= !target_make(&g.half[k], pw / 2, ph / 2, 1);
            ok &= !target_make(&g.quarter[k], pw / 4, ph / 4, 1);
            ok &= !target_make(&g.eighth[k], pw / 8, ph / 8, 1);
            ok &= !target_make(&g.soft[k], pw / 8, ph / 8, 1);
        }
        if (!ok) {
            fprintf(stderr, "hd2d: cannot make %dx%d targets\n", pw, ph);
            return -1;
        }
    }
    if (ow != g.out_w || oh != g.out_h) {
        g.out_w = ow;
        g.out_h = oh;
        if (target_make(&g.out, ow, oh, 0)) {
            fprintf(stderr, "hd2d: cannot make the %dx%d output target\n", ow, oh);
            return -1;
        }
    }
    return 0;
}

static GLint U(int prog, const char *name) { return p_glGetUniformLocation(g.prog[prog], name); }

static void bind_tex(int unit, GLuint tex)
{
    p_glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    p_glBindTexture(GL_TEXTURE_2D, tex);
}

static void draw_to(GLuint fbo, int w, int h)
{
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glViewport(0, 0, w, h);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
}

/* Separable Gaussian: src -> tmp (H) -> dst (V). */
static void gauss(const target *src, target *tmp, target *dst)
{
    p_glUseProgram(g.prog[P_GAUSS]);
    p_glUniform1i(U(P_GAUSS, "uSrc"), 0);
    bind_tex(0, src->tex);
    p_glUniform2f(U(P_GAUSS, "uDir"), 1.0f / src->w, 0.0f);
    draw_to(tmp->fbo, tmp->w, tmp->h);
    bind_tex(0, tmp->tex);
    p_glUniform2f(U(P_GAUSS, "uDir"), 0.0f, 1.0f / tmp->h);
    draw_to(dst->fbo, dst->w, dst->h);
}

static void bright(const target *src, target *dst, float threshold)
{
    p_glUseProgram(g.prog[P_BRIGHT]);
    p_glUniform1i(U(P_BRIGHT, "uSrc"), 0);
    p_glUniform1i(U(P_BRIGHT, "uMat"), 1);
    p_glUniform1f(U(P_BRIGHT, "uThreshold"), threshold);
    bind_tex(0, src->tex);
    bind_tex(1, g.t_mat);
    draw_to(dst->fbo, dst->w, dst->h);
}

/* Scene analysis on the CPU: which z level is UI, how much of the screen
   is UI (menus), and where the actors are (the focus line). */
static void analyze(const uint8_t *layers, const sched_ppu_state *st, int *ui_z, float *ui_frac,
                    float *field_frac, float *focus_target)
{
    *ui_z = st && st->mode == 1 && st->bg3_priority ? 15 : -1;
    long ui = 0, actors = 0, field = 0;
    double ysum = 0, wsum = 0;
    for (int y = 0; y < SCHED_HEIGHT; y++) {
        const uint8_t *row = layers + y * SCHED_WIDTH;
        for (int x = 0; x < SCHED_WIDTH; x++) {
            int l = row[x] & 7, z = row[x] >> 4;
            if (*ui_z >= 0 && z == *ui_z && l < 4)
                ui++;
            else if (l < 4 && z >= 4)
                field++;   /* ground and foreground planes */
            else if (l == 4 || l == 6) {
                /* Weight actors near the middle: the camera follows the lead. */
                double dx = (x - SCHED_WIDTH / 2) / (double)(SCHED_WIDTH / 2);
                double w = 1.0 / (1.0 + 4.0 * dx * dx);
                ysum += w * y;
                wsum += w;
                actors++;
            }
        }
    }
    *ui_frac = (float)ui / (SCHED_WIDTH * SCHED_HEIGHT);
    *field_frac = (float)field / (SCHED_WIDTH * SCHED_HEIGHT);
    *focus_target = actors > 24 ? (float)(ysum / wsum / SCHED_HEIGHT) : 0.55f;
}

void hd2d_render(const uint8_t *bgrx, const uint8_t *layers, const sched_ppu_state *st,
                 int ow, int oh, int offscreen)
{
    if (!g.ready || ow < 8 || oh < 8)
        return;
    uint64_t t0 = 0;
    if (timing) {
        p_glFinish();
        t0 = SDL_GetPerformanceCounter();
    }
    int px, py, pw, ph;
    picture_rect(ow, oh, &px, &py, &pw, &ph);
    if (resize(ow, oh, pw, ph))
        return;
    const settings *s = &presets[cur_preset];
    g.frame++;

    int ui_z;
    float ui_frac, field_frac, focus_target;
    analyze(layers, st, &ui_z, &ui_frac, &field_frac, &focus_target);
    g.focus_y += (focus_target - g.focus_y) * 0.08f;
    /* Menus and full-screen text: ease the scene effects off. */
    float scene_amt = ui_frac > 0.3f ? 0.0f : ui_frac > 0.15f ? (0.3f - ui_frac) / 0.15f : 1.0f;
    if (st && st->mode == 7)
        ui_z = -1;
    /* Tilt-shift only where there is a scene to look down on: none on
       title cards and black-backed screens, full from 45% ground up. */
    float field_target = field_frac < 0.2f ? 0.0f : field_frac > 0.45f ? 1.0f
                                                   : (field_frac - 0.2f) / 0.25f;
    static float field_amt = 1.0f;
    field_amt += (field_target - field_amt) * 0.1f;

    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_BLEND);
    p_glDisable(GL_SCISSOR_TEST);
    p_glBindVertexArray(g.vao);

    /* Uploads. */
    bind_tex(0, g.t_color);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                      bgrx);
    bind_tex(1, g.t_layer);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCHED_WIDTH, SCHED_HEIGHT, GL_RED_INTEGER,
                      GL_UNSIGNED_BYTE, layers);

    /* 1: light. */
    p_glUseProgram(g.prog[P_LIGHT]);
    p_glUniform1i(U(P_LIGHT, "uColor"), 0);
    p_glUniform1i(U(P_LIGHT, "uLayer"), 1);
    p_glUniform1i(U(P_LIGHT, "uUiZ"), ui_z);
    float a = s->effects ? scene_amt : 0.0f;
    p_glUniform4f(U(P_LIGHT, "uAmount"), s->relief * a, s->shadow * a, s->ao * a, s->rim * a);
    p_glUniform4f(U(P_LIGHT, "uAmount2"), s->haze * a, 0, 0, 0);
    p_glUniform3f(U(P_LIGHT, "uLightDir"), -0.5f, -0.65f, 0.58f);
    p_glUniform3f(U(P_LIGHT, "uKey"), 0.55f, 0.51f, 0.44f);
    p_glUniform3f(U(P_LIGHT, "uAmbient"), 0.47f, 0.5f, 0.58f);
    p_glUniform3f(U(P_LIGHT, "uHaze"), 0.75f, 0.82f, 0.95f);
    p_glUniform2i(U(P_LIGHT, "uShadowOffset"), 3, 4);
    draw_to(g.fbo_light, SCHED_WIDTH, SCHED_HEIGHT);

    /* 2: upscale. */
    p_glUseProgram(g.prog[P_UPSCALE]);
    p_glUniform1i(U(P_UPSCALE, "uSrc"), 0);
    p_glUniform2f(U(P_UPSCALE, "uScale"), (float)pw / SCHED_WIDTH, (float)ph / SCHED_HEIGHT);
    bind_tex(0, g.t_lit);
    draw_to(g.scene.fbo, pw, ph);

    if (s->effects) {
        /* 3: blur amount, then the variable blur, H and V. */
        p_glUseProgram(g.prog[P_COC]);
        p_glUniform1i(U(P_COC, "uScene"), 0);
        p_glUniform1i(U(P_COC, "uMat"), 1);
        p_glUniform4f(U(P_COC, "uFocus"), g.focus_y, s->focus_band, s->focus_ramp, 0.5f);
        p_glUniform2f(U(P_COC, "uDof"), s->dof_tilt * scene_amt * field_amt,
                      s->dof_depth * scene_amt * field_amt);
        bind_tex(0, g.scene.tex);
        bind_tex(1, g.t_mat);
        draw_to(g.half[0].fbo, g.half[0].w, g.half[0].h);

        p_glUseProgram(g.prog[P_DOFBLUR]);
        p_glUniform1i(U(P_DOFBLUR, "uSrc"), 0);
        float radius = s->dof_radius * (float)ph / 1080.0f * 2.0f;
        p_glUniform1f(U(P_DOFBLUR, "uRadius"), radius);
        bind_tex(0, g.half[0].tex);
        p_glUniform2f(U(P_DOFBLUR, "uDir"), 1.0f / g.half[0].w, 0.0f);
        draw_to(g.half[1].fbo, g.half[1].w, g.half[1].h);
        bind_tex(0, g.half[1].tex);
        p_glUniform2f(U(P_DOFBLUR, "uDir"), 0.0f, 1.0f / g.half[1].h);
        draw_to(g.half[0].fbo, g.half[0].w, g.half[0].h);

        /* 4: bloom, two levels. */
        bright(&g.scene, &g.quarter[0], s->bloom_threshold);
        gauss(&g.quarter[0], &g.quarter[1], &g.quarter[0]);
        bright(&g.quarter[0], &g.eighth[0], -1.0f);
        gauss(&g.eighth[0], &g.eighth[1], &g.eighth[0]);
    }
    /* Side fill: a soft copy of the scene. */
    bright(&g.scene, &g.quarter[1], -1.0f);
    bright(&g.quarter[1], &g.soft[0], -1.0f);
    gauss(&g.soft[0], &g.soft[1], &g.soft[0]);
    gauss(&g.soft[0], &g.soft[1], &g.soft[0]);
    gauss(&g.soft[0], &g.soft[1], &g.soft[0]);

    /* 5: final. */
    p_glUseProgram(g.prog[P_FINAL]);
    p_glUniform1i(U(P_FINAL, "uScene"), 0);
    p_glUniform1i(U(P_FINAL, "uDof"), 1);
    p_glUniform1i(U(P_FINAL, "uBloomA"), 2);
    p_glUniform1i(U(P_FINAL, "uBloomB"), 3);
    p_glUniform1i(U(P_FINAL, "uSoft"), 4);
    p_glUniform1i(U(P_FINAL, "uMat"), 5);
    p_glUniform1i(U(P_FINAL, "uOrig"), 6);
    bind_tex(0, g.scene.tex);
    bind_tex(1, g.half[0].tex);
    bind_tex(2, g.quarter[0].tex);
    bind_tex(3, g.eighth[0].tex);
    bind_tex(4, g.soft[0].tex);
    bind_tex(5, g.t_mat);
    bind_tex(6, g.t_color);
    p_glUniform4f(U(P_FINAL, "uRect"), (float)px, (float)py, (float)pw, (float)ph);
    p_glUniform2f(U(P_FINAL, "uOut"), (float)ow, (float)oh);
    p_glUniform1i(U(P_FINAL, "uFlip"), offscreen ? 0 : 1);
    p_glUniform4f(U(P_FINAL, "uGrade"), s->bloom, s->exposure, s->saturation, s->vignette);
    p_glUniform4f(U(P_FINAL, "uGrade2"), s->grain, s->split, s->contrast, (float)s->effects);
    p_glUniform3f(U(P_FINAL, "uShadowTint"), 0.92f, 0.98f, 1.08f);
    p_glUniform3f(U(P_FINAL, "uHighTint"), 1.06f, 1.0f, 0.9f);
    p_glUniform1f(U(P_FINAL, "uSideDim"), s->side_dim);
    p_glUniform1f(U(P_FINAL, "uTime"), (float)(g.frame % 997) / 997.0f);
    p_glUniform1i(U(P_FINAL, "uDebug"), debug_view);
    draw_to(offscreen ? g.out.fbo : 0, ow, oh);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (timing) {
        p_glFinish();
        last_ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 /
                  (double)SDL_GetPerformanceFrequency();
    }
}

int hd2d_read(uint8_t *bgrx, int ow, int oh)
{
    if (!g.ready || ow != g.out_w || oh != g.out_h)
        return -1;
    uint8_t *rgba = malloc((size_t)ow * oh * 4);
    if (!rgba)
        return -1;
    p_glBindFramebuffer(GL_FRAMEBUFFER, g.out.fbo);
    p_glReadPixels(0, 0, ow, oh, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* The offscreen target is in picture order already (row 0 = top). */
    for (size_t k = 0; k < (size_t)ow * oh; k++) {
        bgrx[k * 4 + 0] = rgba[k * 4 + 2];
        bgrx[k * 4 + 1] = rgba[k * 4 + 1];
        bgrx[k * 4 + 2] = rgba[k * 4 + 0];
        bgrx[k * 4 + 3] = 0;
    }
    free(rgba);
    return p_glGetError() == GL_NO_ERROR ? 0 : -1;
}

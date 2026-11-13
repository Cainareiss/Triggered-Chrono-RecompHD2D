/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HD-2D renderer (hd2d.h). Passes, all full-screen triangles:
 *
 *   1 light     source size   classify pixels from the layer G-buffer,
 *                             relief lighting, actor shadows, contact
 *                             occlusion, rim light, haze  -> lit, mat
 *   2 upscale   picture size  sharp bilinear, split into a foreground
 *                             and a background plane (the behind
 *                             G-buffer fills in under the foreground),
 *                             diorama tilt. Not a pass: upscale.frag is
 *                             compiled into the passes that read the
 *                             picture (3a, 4a, 5), which work it out from
 *                             the source-size targets as they go
 *   3 coc/dof   half size     blur amount, then H and V variable blur,
 *                             both planes at once
 *   4 bloom     1/4 and 1/8   threshold, Gaussian H and V; plus an
 *                             unthresholded 1/8 copy for the side fill,
 *                             made only when the picture leaves sides
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
#include "osd_font.h"

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
#define GL_R8                       0x8229
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
#define GL_COLOR_ATTACHMENT2        0x8CE2
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

/* ---- optional per-pass GPU timing ----
   CT_HD2D_PROFILE=FILE writes one line per frame: GPU time per pass from
   timestamp queries (GL 3.3 / ARB_timer_query, or ES with
   EXT_disjoint_timer_query), read back a few frames later so nothing
   stalls, plus the CPU time of the scene analysis and of the uploads. */
#define GL_TIMESTAMP                0x8E28
#define GL_QUERY_RESULT             0x8866
#define GL_EXTENSIONS               0x1F03
typedef unsigned long long GLuint64;
typedef void (*PFN_glGenQueries)(GLsizei n, GLuint *ids);
typedef void (*PFN_glDeleteQueries)(GLsizei n, const GLuint *ids);
typedef void (*PFN_glQueryCounter)(GLuint id, GLenum target);
typedef void (*PFN_glGetQueryObjectui64v)(GLuint id, GLenum pname, GLuint64 *v);

enum { PROF_MARKS = 16, PROF_RING = 4 };

static struct {
    FILE *f;
    PFN_glGenQueries gen;
    PFN_glDeleteQueries del;
    PFN_glQueryCounter counter;
    PFN_glGetQueryObjectui64v get;
    GLuint q[PROF_RING][PROF_MARKS];
    const char *name[PROF_RING][PROF_MARKS];
    int n[PROF_RING];
    unsigned frame[PROF_RING];
    double analyze_ms[PROF_RING], upload_ms[PROF_RING], submit_ms[PROF_RING],
        total_ms[PROF_RING];
    int slot;
} prof;

static void *gl_sym(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

static void prof_init(int gles)
{
    if (prof.f) {   /* a second init, after the first context failed */
        fclose(prof.f);
        memset(&prof, 0, sizeof prof);
    }
    const char *path = getenv("CT_HD2D_PROFILE");
    if (!path || !*path)
        return;
    void *gen = gl_sym("glGenQueries"), *del = gl_sym("glDeleteQueries");
    void *counter = gl_sym(gles ? "glQueryCounterEXT" : "glQueryCounter");
    void *get = gl_sym(gles ? "glGetQueryObjectui64vEXT" : "glGetQueryObjectui64v");
    /* (glGetString(GL_EXTENSIONS) is an error in a core profile.) */
    const char *ext = gles ? (const char *)p_glGetString(GL_EXTENSIONS) : NULL;
    if (gles && !(ext && strstr(ext, "GL_EXT_disjoint_timer_query")))
        counter = NULL;
    if (!gen || !del || !counter || !get) {
        fprintf(stderr, "hd2d: CT_HD2D_PROFILE: no timer queries here\n");
        return;
    }
    memcpy(&prof.gen, &gen, sizeof gen);
    memcpy(&prof.del, &del, sizeof del);
    memcpy(&prof.counter, &counter, sizeof counter);
    memcpy(&prof.get, &get, sizeof get);
    if (!(prof.f = fopen(path, "w"))) {
        fprintf(stderr, "hd2d: cannot write %s\n", path);
        return;
    }
    prof.gen(PROF_RING * PROF_MARKS, &prof.q[0][0]);
}

/* A timestamp at the end of the pass called name (the first one, "start",
   only opens the frame). */
static void prof_mark(const char *name)
{
    int s = prof.slot;
    if (prof.f && prof.n[s] < PROF_MARKS) {
        prof.counter(prof.q[s][prof.n[s]], GL_TIMESTAMP);
        prof.name[s][prof.n[s]++] = name;
    }
}

/* Write out the frame recorded in the ring slot about to be reused. */
static void prof_flush(int s)
{
    if (!prof.f || prof.n[s] < 2)
        return;
    GLuint64 t[PROF_MARKS];
    for (int k = 0; k < prof.n[s]; k++)
        prof.get(prof.q[s][k], GL_QUERY_RESULT, &t[k]);
    fprintf(prof.f, "frame=%u total=%.4f analyze=%.4f upload=%.4f submit=%.4f gpu=%.4f",
            prof.frame[s], prof.total_ms[s], prof.analyze_ms[s], prof.upload_ms[s],
            prof.submit_ms[s], (double)(t[prof.n[s] - 1] - t[0]) / 1e6);
    for (int k = 1; k < prof.n[s]; k++)
        fprintf(prof.f, " %s=%.4f", prof.name[s][k], (double)(t[k] - t[k - 1]) / 1e6);
    fputc('\n', prof.f);
    fflush(prof.f);
    prof.n[s] = 0;
}

static double ms_since(uint64_t t0)
{
    return (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 /
           (double)SDL_GetPerformanceFrequency();
}

/* ---- settings ---- */

typedef struct {
    float relief, shadow, ao, rim, haze, point_light;
    float dof_tilt, dof_depth, focus_band, focus_ramp, dof_radius;
    float bloom, bloom_threshold;
    float exposure, saturation, contrast, vignette, grain, split;
    float side_dim;
    float tilt;   /* diorama tilt: foreground enlargement about the focus */
    int effects;
    int bloom_quarter_only;   /* one bloom level, no 1/8 one */
    int side_fill_wide_only;  /* side fill only where the bars are over 20% of the width */
} settings;

static const settings presets[HD2D_PRESETS] = {
    [HD2D_OFF] = {.exposure = 1.0f, .saturation = 1.0f, .contrast = 1.0f, .side_dim = 0.45f},
    [HD2D_SUBTLE] = {.relief = 0.3f, .shadow = 0.5f, .ao = 0.4f, .rim = 0.3f, .haze = 0.25f, .point_light = 0.6f,
                     .dof_tilt = 0.45f, .dof_depth = 0.25f, .focus_band = 0.2f, .focus_ramp = 0.4f,
                     .dof_radius = 4.0f, .bloom = 0.25f, .bloom_threshold = 0.78f,
                     .exposure = 1.03f, .saturation = 1.06f, .contrast = 1.02f, .vignette = 0.2f,
                     .grain = 0.01f, .split = 0.2f, .side_dim = 0.42f, .tilt = 0.0f, .effects = 1},
    [HD2D_FULL] = {.relief = 0.55f, .shadow = 0.85f, .ao = 0.7f, .rim = 0.6f, .haze = 0.5f, .point_light = 1.2f,
                   .dof_tilt = 0.9f, .dof_depth = 0.5f, .focus_band = 0.18f, .focus_ramp = 0.34f,
                   .dof_radius = 6.0f, .bloom = 0.45f, .bloom_threshold = 0.7f,
                   .exposure = 1.06f, .saturation = 1.12f, .contrast = 1.04f, .vignette = 0.35f,
                   .grain = 0.018f, .split = 0.35f, .side_dim = 0.42f, .tilt = 0.022f,
                   .effects = 1},
    /* The Steam Deck: HD2D_FULL with a shorter blur, one bloom level (the
       1/4 one, read for both of the final pass's bloom taps) and, at
       16:10, black bars without the side fill. */
    [HD2D_DECK] = {.relief = 0.55f, .shadow = 0.85f, .ao = 0.7f, .rim = 0.6f, .haze = 0.5f, .point_light = 1.2f,
                   .dof_tilt = 0.9f, .dof_depth = 0.5f, .focus_band = 0.18f, .focus_ramp = 0.34f,
                   .dof_radius = 3.5f, .bloom = 0.4f, .bloom_threshold = 0.7f,
                   .exposure = 1.06f, .saturation = 1.12f, .contrast = 1.04f, .vignette = 0.35f,
                   .grain = 0.018f, .split = 0.35f, .side_dim = 0.42f, .tilt = 0.022f,
                   .effects = 1, .bloom_quarter_only = 1, .side_fill_wide_only = 1},
};

static const char *const preset_names[HD2D_PRESETS] = {"off", "subtle", "hd2d", "deck"};

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
int hd2d_wants_behind(void) { return presets[cur_preset].effects; }
double hd2d_last_ms(void) { return last_ms; }

/* ---- GL objects ---- */

enum { P_EMIT, P_LIGHT, P_COC, P_DOFBLUR, P_BRIGHT, P_GAUSS, P_FINAL, P_COUNT };

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
    GLuint t_behind, t_blayer;      /* the behind G-buffer, source size */
    GLuint t_lit, t_mat, t_back, fbo_light; /* pass 1 targets, source size */
    target half[2], fgh[2];         /* pass 3: background, foreground plane */
    GLuint fbo_dof[2];              /* half[k] and fgh[k] */
    target quarter[2], eighth[2], soft[2], out;
    GLuint fbo_quarter;             /* quarter[0] (bloom) and [1] (plain) */
    target emit[2];                 /* point-light map, source / 4 */
    int pic_w, pic_h, out_w, out_h;
    float focus_y;
    unsigned frame;
    /* On-screen message: text, frames left, and its mask texture. */
    char osd_text[48];
    int osd_frames, osd_dirty, osd_w, osd_h;
    GLuint t_osd;
    /* Uniform locations, looked up once (key: program, name literal). */
    struct { const char *name; int prog; GLint loc; } uloc[256];
} g;

static GLuint make_tex(int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filter);

#define OSD_FRAMES 130   /* about two seconds, the last 20 fading */
#define OSD_GLYPH_W 6    /* 5 pixels and a gap */
#define OSD_H 11         /* 7 rows and 2 above and below */

void hd2d_osd(const char *text)
{
    snprintf(g.osd_text, sizeof g.osd_text, "%s", text);
    g.osd_frames = OSD_FRAMES;
    g.osd_dirty = 1;
}

/* Rasterize the message into its mask texture (1 = text). */
static void osd_upload(void)
{
    int len = (int)strlen(g.osd_text);
    g.osd_w = len * OSD_GLYPH_W + 3;
    g.osd_h = OSD_H;
    uint8_t *mask = calloc((size_t)g.osd_w * g.osd_h, 1);
    if (!mask)
        return;
    for (int k = 0; k < len; k++) {
        const uint8_t *rows = osd_glyph(g.osd_text[k]);
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (rows[y] & (0x10 >> x))
                    mask[(y + 2) * g.osd_w + 2 + k * OSD_GLYPH_W + x] = 255;
    }
    if (g.t_osd)
        p_glDeleteTextures(1, &g.t_osd);
    g.t_osd = make_tex(g.osd_w, g.osd_h, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g.osd_w, g.osd_h, GL_RED, GL_UNSIGNED_BYTE, mask);
    free(mask);
    g.osd_dirty = 0;
}

/* lib: an optional source compiled in front of body (upscale.frag). */
static GLuint compile(GLenum type, const char *lib, const char *body, const char *name)
{
    const char *prefix = g.gles ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                                  "precision highp usampler2D;\n"
                                : "#version 330 core\n";
    const char *src[3] = {prefix, lib ? lib : "", body};
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 3, src, NULL);
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

static GLuint link(const char *lib, const char *frag, const char *name)
{
    GLuint vs = compile(GL_VERTEX_SHADER, NULL, hd2d_src_fullscreen_vert, "fullscreen.vert");
    GLuint fs = compile(GL_FRAGMENT_SHADER, lib, frag, name);
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
    static const GLenum bufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                   GL_COLOR_ATTACHMENT2};
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
    prof_init(gles);

    /* up: the pass reads the picture, so upscale.frag goes in front. */
    static const struct { const char *src, *name; int up; } fs[P_COUNT] = {
        [P_EMIT] = {hd2d_src_emit_frag, "emit.frag", 0},
        [P_LIGHT] = {hd2d_src_light_frag, "light.frag", 0},
        [P_COC] = {hd2d_src_coc_frag, "coc.frag", 1},
        [P_DOFBLUR] = {hd2d_src_dofblur_frag, "dofblur.frag", 0},
        [P_BRIGHT] = {hd2d_src_bright_frag, "bright.frag", 1},
        [P_GAUSS] = {hd2d_src_gauss_frag, "gauss.frag", 0},
        [P_FINAL] = {hd2d_src_final_frag, "final.frag", 1},
    };
    for (int k = 0; k < P_COUNT; k++)
        if (!(g.prog[k] = link(fs[k].up ? hd2d_src_upscale_frag : NULL, fs[k].src, fs[k].name)))
            return -1;

    p_glGenVertexArrays(1, &g.vao);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1);

    g.t_color = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    g.t_layer = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                         GL_NEAREST);
    g.t_behind = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    g.t_blayer = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                          GL_NEAREST);

    /* Half-float targets where they render (desktop GL 3.3 always; ES 3.0
       with EXT_color_buffer_float), else 8-bit. */
    g.float_fmt = GL_RGBA16F;
    g.float_type = GL_HALF_FLOAT;
    g.t_lit = make_tex(SCHED_WIDTH, SCHED_HEIGHT, (GLint)g.float_fmt, GL_RGBA, g.float_type,
                       GL_LINEAR);
    g.t_mat = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    g.t_back = make_tex(SCHED_WIDTH, SCHED_HEIGHT, (GLint)g.float_fmt, GL_RGBA, g.float_type,
                        GL_LINEAR);
    GLuint lt[3] = {g.t_lit, g.t_mat, g.t_back};
    if (fbo_for(&g.fbo_light, lt, 3)) {
        fprintf(stderr, "hd2d: half-float targets not renderable: using RGBA8\n");
        p_glDeleteFramebuffers(1, &g.fbo_light);
        p_glDeleteTextures(1, &g.t_lit);
        p_glDeleteTextures(1, &g.t_back);
        g.float_fmt = GL_RGBA8;
        g.float_type = GL_UNSIGNED_BYTE;
        g.t_lit = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        g.t_back = make_tex(SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        lt[0] = g.t_lit;
        lt[2] = g.t_back;
        if (fbo_for(&g.fbo_light, lt, 3)) {
            fprintf(stderr, "hd2d: cannot make the lighting target\n");
            return -1;
        }
    }
    for (int k = 0; k < 2; k++)
        if (target_make(&g.emit[k], SCHED_WIDTH / 4, SCHED_HEIGHT / 4, 1)) {
            fprintf(stderr, "hd2d: cannot make the light map\n");
            return -1;
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
    if (prof.f) {
        for (int k = 1; k <= PROF_RING; k++)
            prof_flush((prof.slot + k) % PROF_RING);
        prof.del(PROF_RING * PROF_MARKS, &prof.q[0][0]);
        fclose(prof.f);
        prof.f = NULL;
    }
    target *ts[] = {&g.half[0], &g.half[1], &g.fgh[0], &g.fgh[1],
                    &g.quarter[0], &g.quarter[1], &g.eighth[0], &g.eighth[1], &g.soft[0],
                    &g.soft[1], &g.out, &g.emit[0], &g.emit[1]};
    for (size_t k = 0; k < sizeof ts / sizeof *ts; k++)
        target_free(ts[k]);
    GLuint fbos[3] = {g.fbo_quarter, g.fbo_dof[0], g.fbo_dof[1]};
    p_glDeleteFramebuffers(3, fbos);
    GLuint tex[7] = {g.t_color, g.t_layer, g.t_behind, g.t_blayer, g.t_lit, g.t_mat, g.t_back};
    p_glDeleteTextures(7, tex);
    if (g.t_osd)
        p_glDeleteTextures(1, &g.t_osd);
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
        int ok = 1;
        for (int k = 0; k < 2; k++) {
            ok &= !target_make(&g.half[k], pw / 2, ph / 2, 1);
            ok &= !target_make(&g.fgh[k], pw / 2, ph / 2, 1);
            ok &= !target_make(&g.quarter[k], pw / 4, ph / 4, 1);
            ok &= !target_make(&g.eighth[k], pw / 8, ph / 8, 1);
            ok &= !target_make(&g.soft[k], pw / 8, ph / 8, 1);
        }
        /* Two-target framebuffers for passes 3 and 4a. */
        GLuint fbos[3] = {g.fbo_quarter, g.fbo_dof[0], g.fbo_dof[1]};
        if (g.fbo_quarter)
            p_glDeleteFramebuffers(3, fbos);
        GLuint two[2] = {g.quarter[0].tex, g.quarter[1].tex};
        ok &= !fbo_for(&g.fbo_quarter, two, 2);
        for (int k = 0; k < 2; k++) {
            two[0] = g.half[k].tex;
            two[1] = g.fgh[k].tex;
            ok &= !fbo_for(&g.fbo_dof[k], two, 2);
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

/* Uniform location, cached: names are string literals, so the pointer is
   the key. */
static GLint U(int prog, const char *name)
{
    enum { N = sizeof g.uloc / sizeof *g.uloc };
    unsigned h = (unsigned)(((uintptr_t)name >> 2) * 2654435761u + (unsigned)prog * 40503u) % N;
    for (int probe = 0; probe < N; probe++, h = (h + 1) % N) {
        if (g.uloc[h].name == name && g.uloc[h].prog == prog)
            return g.uloc[h].loc;
        if (!g.uloc[h].name) {
            g.uloc[h].name = name;
            g.uloc[h].prog = prog;
            return g.uloc[h].loc = p_glGetUniformLocation(g.prog[prog], name);
        }
    }
    return p_glGetUniformLocation(g.prog[prog], name);
}

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

/* The upscale's inputs (upscale.frag) for a pass that reads the picture:
   the source-size targets on units 0-2. */
static void bind_picture(int prog, int pw, int ph, float tilt)
{
    p_glUniform1i(U(prog, "uLit"), 0);
    p_glUniform1i(U(prog, "uLitBack"), 1);
    p_glUniform1i(U(prog, "uMat"), 2);
    p_glUniform2f(U(prog, "uScale"), (float)pw / SCHED_WIDTH, (float)ph / SCHED_HEIGHT);
    p_glUniform2f(U(prog, "uPic"), (float)pw, (float)ph);
    p_glUniform3f(U(prog, "uTilt"), 0.5f, g.focus_y, tilt);
    bind_tex(0, g.t_lit);
    bind_tex(1, g.t_back);
    bind_tex(2, g.t_mat);
}

/* Bloom source from a smaller target (threshold < 0: a plain copy). */
static void bright(const target *src, target *dst, float threshold)
{
    p_glUseProgram(g.prog[P_BRIGHT]);
    p_glUniform1i(U(P_BRIGHT, "uFromPicture"), 0);
    p_glUniform1i(U(P_BRIGHT, "uSrc"), 3);
    p_glUniform1i(U(P_BRIGHT, "uMat"), 2);
    p_glUniform1f(U(P_BRIGHT, "uThreshold"), threshold);
    bind_tex(3, src->tex);
    bind_tex(2, g.t_mat);
    draw_to(dst->fbo, dst->w, dst->h);
}

/* Scene analysis on the CPU: which z level is UI, how much of the screen
   is UI (menus), and where the actors are (the focus line). */
static void analyze(const uint8_t *bgrx, const uint8_t *layers, const sched_ppu_state *st,
                    int *ui_z, float *ui_frac, float *field_frac, float *focus_target,
                    float *mean_luma)
{
    /* Mean brightness of the picture, every 4th pixel: channel sums in
       integers, weighted once at the end. */
    uint32_t sb = 0, sg = 0, sr = 0;
    for (int k = 0; k < SCHED_WIDTH * SCHED_HEIGHT; k += 4) {
        sb += bgrx[k * 4];
        sg += bgrx[k * 4 + 1];
        sr += bgrx[k * 4 + 2];
    }
    double lsum = 0.114 * sb + 0.587 * sg + 0.299 * sr;
    *mean_luma = (float)(lsum / (SCHED_WIDTH * SCHED_HEIGHT / 4) / 255.0);

    /* Weight actors near the middle: the camera follows the lead. */
    static float colw[SCHED_WIDTH];
    if (colw[0] == 0.0f)
        for (int x = 0; x < SCHED_WIDTH; x++) {
            double dx = (x - SCHED_WIDTH / 2) / (double)(SCHED_WIDTH / 2);
            colw[x] = (float)(1.0 / (1.0 + 4.0 * dx * dx));
        }
    /* What each layer byte counts as, packed so one add per pixel counts
       all three: UI in bits 0-10, ground and foreground planes in 11-21,
       actors in 22-31 (a row has at most 256 of each); and the actor mask
       as a float, for the weighted sum. */
    *ui_z = st && st->mode == 1 && st->bg3_priority ? 15 : -1;
    uint32_t pack[256];
    float actor[256];
    for (int v = 0; v < 256; v++) {
        int l = v & 7, z = v >> 4;
        pack[v] = *ui_z >= 0 && z == *ui_z && l < 4 ? 1u : l < 4 && z >= 4 ? 1u << 11
                  : l == 4 || l == 6 ? 1u << 22 : 0u;
        actor[v] = pack[v] == 1u << 22 ? 1.0f : 0.0f;
    }
    long n[4] = {0};
    double ysum = 0, wsum = 0;
    for (int y = 0; y < SCHED_HEIGHT; y++) {
        const uint8_t *row = layers + y * SCHED_WIDTH;
        uint32_t acc = 0;
        float r0 = 0, r1 = 0;   /* two sums: half the chain of dependent adds */
        for (int x = 0; x < SCHED_WIDTH; x += 2) {
            acc += pack[row[x]] + pack[row[x + 1]];
            r0 += colw[x] * actor[row[x]];
            r1 += colw[x + 1] * actor[row[x + 1]];
        }
        float rw = r0 + r1;
        n[1] += acc & 0x7ff;
        n[2] += acc >> 11 & 0x7ff;
        n[3] += acc >> 22;
        ysum += (double)rw * y;
        wsum += rw;
    }
    *ui_frac = (float)n[1] / (SCHED_WIDTH * SCHED_HEIGHT);
    *field_frac = (float)n[2] / (SCHED_WIDTH * SCHED_HEIGHT);
    *focus_target = n[3] > 24 ? (float)(ysum / wsum / SCHED_HEIGHT) : 0.55f;
}

void hd2d_render(const uint8_t *bgrx, const uint8_t *layers, const uint8_t *behind,
                 const uint8_t *behind_layers, const sched_ppu_state *st, int ow, int oh,
                 int offscreen)
{
    if (!g.ready || ow < 8 || oh < 8)
        return;
    /* The profile's own readback goes before the clock starts. */
    prof.slot = (prof.slot + 1) % PROF_RING;
    prof_flush(prof.slot);
    uint64_t t0 = 0;
    if (timing) {
        p_glFinish();
        t0 = SDL_GetPerformanceCounter();
    }
    uint64_t tc = SDL_GetPerformanceCounter();
    int px, py, pw, ph;
    picture_rect(ow, oh, &px, &py, &pw, &ph);
    if (resize(ow, oh, pw, ph))
        return;
    const settings *s = &presets[cur_preset];
    g.frame++;

    int ui_z;
    float ui_frac, field_frac, focus_target, mean_luma;
    analyze(bgrx, layers, st, &ui_z, &ui_frac, &field_frac, &focus_target, &mean_luma);
    prof.analyze_ms[prof.slot] = ms_since(tc);
    /* Point lights matter in the dark: full below 0.18 mean brightness,
       gone above 0.42 (daylight). */
    float dark_target = mean_luma < 0.18f ? 1.0f : mean_luma > 0.42f ? 0.0f
                                                 : (0.42f - mean_luma) / 0.24f;
    static float dark_amt = 0.5f;
    dark_amt += (dark_target - dark_amt) * 0.1f;
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
    uint64_t tu = SDL_GetPerformanceCounter();
    prof_mark("start");
    bind_tex(0, g.t_color);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                      bgrx);
    bind_tex(1, g.t_layer);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCHED_WIDTH, SCHED_HEIGHT, GL_RED_INTEGER,
                      GL_UNSIGNED_BYTE, layers);
    /* Without the behind G-buffer the front layer stands in for it: the
       planes then match the old single-plane depth of field, and the
       tilt is off (it would have nothing to reveal). */
    int have_behind = behind && behind_layers;
    bind_tex(3, g.t_behind);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCHED_WIDTH, SCHED_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                      have_behind ? behind : bgrx);
    bind_tex(4, g.t_blayer);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCHED_WIDTH, SCHED_HEIGHT, GL_RED_INTEGER,
                      GL_UNSIGNED_BYTE, have_behind ? behind_layers : layers);
    prof.upload_ms[prof.slot] = ms_since(tu);
    prof_mark("texup");

    /* 0: light sources -> blurred light map. */
    p_glUseProgram(g.prog[P_EMIT]);
    p_glUniform1i(U(P_EMIT, "uColor"), 0);
    p_glUniform1i(U(P_EMIT, "uLayer"), 1);
    p_glUniform1i(U(P_EMIT, "uUiZ"), ui_z);
    p_glUniform1f(U(P_EMIT, "uThreshold"), 0.66f);
    draw_to(g.emit[0].fbo, g.emit[0].w, g.emit[0].h);
    gauss(&g.emit[0], &g.emit[1], &g.emit[0]);
    gauss(&g.emit[0], &g.emit[1], &g.emit[0]);
    prof_mark("emit");
    bind_tex(0, g.t_color);
    bind_tex(1, g.t_layer);

    /* 1: light. */
    p_glUseProgram(g.prog[P_LIGHT]);
    p_glUniform1i(U(P_LIGHT, "uColor"), 0);
    p_glUniform1i(U(P_LIGHT, "uLayer"), 1);
    p_glUniform1i(U(P_LIGHT, "uUiZ"), ui_z);
    float a = s->effects ? scene_amt : 0.0f;
    p_glUniform4f(U(P_LIGHT, "uAmount"), s->relief * a, s->shadow * a, s->ao * a, s->rim * a);
    p_glUniform4f(U(P_LIGHT, "uAmount2"), s->haze * a, s->point_light * a * (0.25f + 0.75f * dark_amt),
                  0, 0);
    p_glUniform1i(U(P_LIGHT, "uLightMap"), 2);
    p_glUniform1i(U(P_LIGHT, "uBehind"), 3);
    p_glUniform1i(U(P_LIGHT, "uBLayer"), 4);
    bind_tex(2, g.emit[0].tex);
    p_glUniform3f(U(P_LIGHT, "uLightDir"), -0.5f, -0.65f, 0.58f);
    p_glUniform3f(U(P_LIGHT, "uKey"), 0.55f, 0.51f, 0.44f);
    p_glUniform3f(U(P_LIGHT, "uAmbient"), 0.47f, 0.5f, 0.58f);
    p_glUniform3f(U(P_LIGHT, "uHaze"), 0.75f, 0.82f, 0.95f);
    p_glUniform2i(U(P_LIGHT, "uShadowOffset"), 3, 4);
    draw_to(g.fbo_light, SCHED_WIDTH, SCHED_HEIGHT);
    prof_mark("light");

    /* 2: the upscale into two planes, with the tilt: worked out in the
       passes that read the picture (bind_picture). */
    float tilt = have_behind ? s->tilt * scene_amt * field_amt : 0.0f;

    /* Depth of field settings, shared by passes 3 and 5. */
    float dof_tilt = s->dof_tilt * scene_amt * field_amt;
    float dof_depth = s->dof_depth * scene_amt * field_amt;
    const float fore_depth = 0.22f;   /* light.frag's depthOf(C_FORE) */
    if (s->effects) {
        /* 3: blur amount, then the variable blur, H and V, both planes. */
        p_glUseProgram(g.prog[P_COC]);
        bind_picture(P_COC, pw, ph, tilt);
        p_glUniform4f(U(P_COC, "uFocus"), g.focus_y, s->focus_band, s->focus_ramp, 0.5f);
        p_glUniform3f(U(P_COC, "uDof"), dof_tilt, dof_depth, fore_depth);
        draw_to(g.fbo_dof[0], g.half[0].w, g.half[0].h);
        prof_mark("coc");

        p_glUseProgram(g.prog[P_DOFBLUR]);
        p_glUniform1i(U(P_DOFBLUR, "uSrc"), 0);
        p_glUniform1i(U(P_DOFBLUR, "uFg"), 1);
        p_glUniform4f(U(P_DOFBLUR, "uFocus"), g.focus_y, s->focus_band, s->focus_ramp, 0.5f);
        p_glUniform3f(U(P_DOFBLUR, "uDof"), dof_tilt, dof_depth, fore_depth);
        float radius = s->dof_radius * (float)ph / 1080.0f * 2.0f;
        p_glUniform1f(U(P_DOFBLUR, "uRadius"), radius);
        bind_tex(0, g.half[0].tex);
        bind_tex(1, g.fgh[0].tex);
        p_glUniform2f(U(P_DOFBLUR, "uDir"), 1.0f / g.half[0].w, 0.0f);
        draw_to(g.fbo_dof[1], g.half[1].w, g.half[1].h);
        bind_tex(0, g.half[1].tex);
        bind_tex(1, g.fgh[1].tex);
        p_glUniform2f(U(P_DOFBLUR, "uDir"), 0.0f, 1.0f / g.half[1].h);
        draw_to(g.fbo_dof[0], g.half[0].w, g.half[0].h);
        prof_mark("dof");
    }
    /* 4: bloom, two levels, and the side fill: a soft copy of the
       picture, only made when the picture leaves part of the output
       uncovered. One pass reads the picture for both: quarter[0] gets the
       bloom source, quarter[1] the plain copy. */
    int sides = (px > 0 || py > 0) &&
                /* the deck look: only where the bars are over 20% of the width */
                (!s->side_fill_wide_only || (ow - pw) * 5 > ow);
    if (s->effects || sides) {
        p_glUseProgram(g.prog[P_BRIGHT]);
        bind_picture(P_BRIGHT, pw, ph, tilt);
        p_glUniform1i(U(P_BRIGHT, "uFromPicture"), 1);
        p_glUniform1i(U(P_BRIGHT, "uSrc"), 3);
        bind_tex(3, g.t_mat);   /* unused here, but a valid texture */
        p_glUniform1f(U(P_BRIGHT, "uThreshold"), s->effects ? s->bloom_threshold : -1.0f);
        draw_to(g.fbo_quarter, g.quarter[0].w, g.quarter[0].h);
    }
    if (s->effects) {
        /* quarter[1] is the blur's scratch target, so the soft copy is
           taken from it first. */
        if (sides)
            bright(&g.quarter[1], &g.soft[0], -1.0f);
        gauss(&g.quarter[0], &g.quarter[1], &g.quarter[0]);
        if (!s->bloom_quarter_only) {
            bright(&g.quarter[0], &g.eighth[0], -1.0f);
            gauss(&g.eighth[0], &g.eighth[1], &g.eighth[0]);
        }
        prof_mark("bloom");
    } else if (sides) {
        bright(&g.quarter[1], &g.soft[0], -1.0f);
    }
    if (sides) {
        gauss(&g.soft[0], &g.soft[1], &g.soft[0]);
        gauss(&g.soft[0], &g.soft[1], &g.soft[0]);
        gauss(&g.soft[0], &g.soft[1], &g.soft[0]);
        prof_mark("side");
    }
    int side_fill = sides;

    /* 5: final, with the on-screen message if any. */
    float osd_alpha = 0.0f;
    if (g.osd_frames > 0) {
        if (g.osd_dirty)
            osd_upload();
        osd_alpha = g.osd_frames < 20 ? g.osd_frames / 20.0f : 1.0f;
        if (!offscreen)
            g.osd_frames--;   /* screenshots don't use up the message's time */
    }
    p_glUseProgram(g.prog[P_FINAL]);
    bind_picture(P_FINAL, pw, ph, tilt);
    {
        float scale = (float)(ph / 180 > 2 ? ph / 180 : 2);   /* 6x at 1080p */
        p_glUniform1i(U(P_FINAL, "uOsd"), 7);
        bind_tex(7, g.t_osd ? g.t_osd : g.t_mat);
        p_glUniform4f(U(P_FINAL, "uOsdRect"), px + scale * 4, py + scale * 4, g.osd_w * scale,
                      g.osd_h * scale);
        p_glUniform1f(U(P_FINAL, "uOsdAlpha"), g.t_osd ? osd_alpha : 0.0f);
    }
    p_glUniform1i(U(P_FINAL, "uDof"), 3);
    p_glUniform1i(U(P_FINAL, "uBloomA"), 4);
    p_glUniform1i(U(P_FINAL, "uBloomB"), 5);
    p_glUniform1i(U(P_FINAL, "uSoft"), 6);
    p_glUniform1i(U(P_FINAL, "uOrig"), 8);
    p_glUniform1i(U(P_FINAL, "uFgDof"), 9);
    bind_tex(3, g.half[0].tex);
    bind_tex(4, g.quarter[0].tex);
    bind_tex(5, s->bloom_quarter_only ? g.quarter[0].tex : g.eighth[0].tex);
    bind_tex(6, g.soft[0].tex);
    bind_tex(8, g.t_color);
    bind_tex(9, g.fgh[0].tex);
    p_glUniform4f(U(P_FINAL, "uFocus"), g.focus_y, s->focus_band, s->focus_ramp, 0.5f);
    p_glUniform3f(U(P_FINAL, "uDof3"), dof_tilt, dof_depth, fore_depth);
    p_glUniform4f(U(P_FINAL, "uRect"), (float)px, (float)py, (float)pw, (float)ph);
    p_glUniform2f(U(P_FINAL, "uOut"), (float)ow, (float)oh);
    p_glUniform1i(U(P_FINAL, "uFlip"), offscreen ? 0 : 1);
    p_glUniform4f(U(P_FINAL, "uGrade"), s->bloom, s->exposure, s->saturation, s->vignette);
    p_glUniform4f(U(P_FINAL, "uGrade2"), s->grain, s->split, s->contrast, (float)s->effects);
    p_glUniform3f(U(P_FINAL, "uShadowTint"), 0.92f, 0.98f, 1.08f);
    p_glUniform3f(U(P_FINAL, "uHighTint"), 1.06f, 1.0f, 0.9f);
    p_glUniform1f(U(P_FINAL, "uSideDim"), side_fill ? s->side_dim : 0.0f);
    p_glUniform1f(U(P_FINAL, "uTime"), (float)(g.frame % 997) / 997.0f);
    p_glUniform1i(U(P_FINAL, "uDebug"), debug_view);
    draw_to(offscreen ? g.out.fbo : 0, ow, oh);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    prof_mark("final");
    prof.frame[prof.slot] = g.frame;
    prof.submit_ms[prof.slot] = ms_since(tc);

    if (timing) {
        p_glFinish();
        last_ms = ms_since(t0);
    }
    prof.total_ms[prof.slot] = timing ? last_ms : 0.0;
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

/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GL drawer for the touch overlay (touch_gl.h). The overlay arrives as
 * SDL_Vertex triangles in output pixels (touch_geometry); one program
 * maps them to clip space and blends them over the frame. The loader
 * follows hd2d.c's pattern, with its own pointers. */
#include "touch_gl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

/* ---- the GL subset used here (values from the Khronos registry) ---- */
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_NO_ERROR             0
#define GL_ZERO                 0
#define GL_ONE                  1
#define GL_TRIANGLES            0x0004
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_DEPTH_TEST           0x0B71
#define GL_BLEND                0x0BE2
#define GL_SCISSOR_TEST         0x0C11
#define GL_UNPACK_ALIGNMENT     0x0CF5
#define GL_PACK_ALIGNMENT       0x0D05
#define GL_TEXTURE_2D           0x0DE1
#define GL_UNSIGNED_BYTE        0x1401
#define GL_FLOAT                0x1406
#define GL_RGBA                 0x1908
#define GL_RGBA8                0x8058
#define GL_NEAREST              0x2600
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_ARRAY_BUFFER         0x8892
#define GL_STREAM_DRAW          0x88E0
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_FRAMEBUFFER          0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0    0x8CE0

#define GL_FUNCS(X)                                                                           \
    X(GLenum, glGetError, (void))                                                             \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                             \
    X(void, glEnable, (GLenum cap))                                                           \
    X(void, glDisable, (GLenum cap))                                                          \
    X(void, glBlendFuncSeparate, (GLenum sc, GLenum dc, GLenum sa, GLenum da))                \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                       \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p)) \
    X(void, glGenTextures, (GLsizei n, GLuint *t))                                            \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *t))                                   \
    X(void, glBindTexture, (GLenum target, GLuint t))                                         \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                      \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,      \
                           GLint border, GLenum fmt, GLenum type, const void *data))          \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *f))                                        \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *f))                               \
    X(void, glBindFramebuffer, (GLenum target, GLuint f))                                     \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum att, GLenum tt, GLuint t, GLint l)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                      \
    X(void, glGenBuffers, (GLsizei n, GLuint *b))                                             \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *b))                                    \
    X(void, glBindBuffer, (GLenum target, GLuint b))                                          \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))   \
    X(void, glVertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm,        \
                                    GLsizei stride, const void *ptr))                         \
    X(void, glEnableVertexAttribArray, (GLuint i))                                            \
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
    X(void, glUniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c))                        \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *v))                                        \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *v))                               \
    X(void, glBindVertexArray, (GLuint v))                                                    \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))

#define X(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name q_##name;
GL_FUNCS(X)
#undef X

static int load_gl(void)
{
    int missing = 0;
#define X(ret, name, args)                                                     \
    {                                                                          \
        void *sym = SDL_GL_GetProcAddress(#name);                              \
        memcpy(&q_##name, &sym, sizeof sym); /* object -> function pointer */  \
    }                                                                          \
    if (!q_##name) {                                                           \
        fprintf(stderr, "touch_gl: missing GL function %s\n", #name);         \
        missing = 1;                                                           \
    }
    GL_FUNCS(X)
#undef X
    return missing ? -1 : 0;
}

/* Pixels (top-left origin) in, clip space out. uXform = (2/w, 2/h, s):
   s = -1 for the window (GL's origin is bottom-left), +1 for a texture
   kept in picture order (row 0 at the top). */
static const char *const vs_body =
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec4 aCol;\n"
    "uniform vec3 uXform;\n"
    "out vec4 vCol;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos.x * uXform.x - 1.0, uXform.z * (aPos.y * uXform.y - 1.0),\n"
    "                       0.0, 1.0);\n"
    "    vCol = aCol;\n"
    "}\n";
static const char *const fs_body =
    "in vec4 vCol;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vCol; }\n";

static struct {
    int ready;
    GLuint prog, vao, vbo;
    GLint u_xform;
} G;

static GLuint compile(GLenum type, int gles, const char *body)
{
    const char *src[2] = {gles ? "#version 300 es\nprecision mediump float;\n"
                               : "#version 330 core\n",
                          body};
    GLuint s = q_glCreateShader(type);
    q_glShaderSource(s, 2, src, NULL);
    q_glCompileShader(s);
    GLint ok = 0;
    q_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        q_glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "touch_gl: shader: %s\n", log);
        q_glDeleteShader(s);
        return 0;
    }
    return s;
}

int touch_gl_init(int gles)
{
    if (load_gl())
        return -1;
    GLuint vs = compile(GL_VERTEX_SHADER, gles, vs_body);
    GLuint fs = compile(GL_FRAGMENT_SHADER, gles, fs_body);
    if (!vs || !fs)
        return -1;
    G.prog = q_glCreateProgram();
    q_glAttachShader(G.prog, vs);
    q_glAttachShader(G.prog, fs);
    q_glLinkProgram(G.prog);
    q_glDeleteShader(vs);
    q_glDeleteShader(fs);
    GLint ok = 0;
    q_glGetProgramiv(G.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        q_glGetProgramInfoLog(G.prog, sizeof log, NULL, log);
        fprintf(stderr, "touch_gl: link: %s\n", log);
        return -1;
    }
    G.u_xform = q_glGetUniformLocation(G.prog, "uXform");
    q_glGenVertexArrays(1, &G.vao);
    q_glGenBuffers(1, &G.vbo);
    q_glBindVertexArray(G.vao);
    q_glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
    q_glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(SDL_Vertex),
                            (const void *)offsetof(SDL_Vertex, position));
    q_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, 1, sizeof(SDL_Vertex),
                            (const void *)offsetof(SDL_Vertex, color));
    q_glEnableVertexAttribArray(0);
    q_glEnableVertexAttribArray(1);
    q_glBindVertexArray(0);
    q_glBindBuffer(GL_ARRAY_BUFFER, 0);
    G.ready = q_glGetError() == GL_NO_ERROR;
    return G.ready ? 0 : -1;
}

void touch_gl_shutdown(void)
{
    if (!G.ready)
        return;
    q_glDeleteProgram(G.prog);
    q_glDeleteBuffers(1, &G.vbo);
    q_glDeleteVertexArrays(1, &G.vao);
    G.ready = 0;
}

/* Triangles to the bound framebuffer, w x h; window: flip for GL's
   bottom-left origin. Leaves blending off, as hd2d_render expects. */
static void draw(const SDL_Vertex *v, int n, int w, int h, int window)
{
    q_glViewport(0, 0, w, h);
    q_glDisable(GL_DEPTH_TEST);
    q_glDisable(GL_SCISSOR_TEST);
    q_glEnable(GL_BLEND);
    q_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    q_glUseProgram(G.prog);
    q_glUniform3f(G.u_xform, 2.0f / w, 2.0f / h, window ? -1.0f : 1.0f);
    q_glBindVertexArray(G.vao);
    q_glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
    q_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n * (GLsizeiptr)sizeof *v, v, GL_STREAM_DRAW);
    q_glDrawArrays(GL_TRIANGLES, 0, n);
    q_glBindBuffer(GL_ARRAY_BUFFER, 0);
    q_glBindVertexArray(0);
    q_glUseProgram(0);
    q_glDisable(GL_BLEND);
}

void touch_gl_draw(int w, int h)
{
    const SDL_Vertex *v;
    int n = G.ready && w > 0 && h > 0 ? touch_geometry(&v) : 0;
    if (n)
        draw(v, n, w, h, 1);
}

int touch_gl_shot(uint8_t *bgrx, int w, int h, touch_rect picture)
{
    const SDL_Vertex *v;
    if (!G.ready)
        return -1;
    int n = touch_geometry_for(w, h, picture, &v);
    if (!n)
        return 0;
    size_t px = (size_t)w * h;
    uint8_t *rgba = malloc(px * 4);
    if (!rgba)
        return -1;
    for (size_t k = 0; k < px; k++) {
        rgba[k * 4 + 0] = bgrx[k * 4 + 2];
        rgba[k * 4 + 1] = bgrx[k * 4 + 1];
        rgba[k * 4 + 2] = bgrx[k * 4 + 0];
        rgba[k * 4 + 3] = 255;
    }
    GLuint tex, fbo;
    q_glGenTextures(1, &tex);
    q_glBindTexture(GL_TEXTURE_2D, tex);
    q_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    q_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    q_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    q_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    q_glGenFramebuffers(1, &fbo);
    q_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    q_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    int ok = q_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        draw(v, n, w, h, 0);
        q_glPixelStorei(GL_PACK_ALIGNMENT, 4);
        q_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        for (size_t k = 0; k < px; k++) {
            bgrx[k * 4 + 0] = rgba[k * 4 + 2];
            bgrx[k * 4 + 1] = rgba[k * 4 + 1];
            bgrx[k * 4 + 2] = rgba[k * 4 + 0];
            bgrx[k * 4 + 3] = 0;
        }
    }
    q_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    q_glDeleteFramebuffers(1, &fbo);
    q_glBindTexture(GL_TEXTURE_2D, 0);
    q_glDeleteTextures(1, &tex);
    free(rgba);
    return ok && q_glGetError() == GL_NO_ERROR ? 0 : -1;
}

// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D diorama at a render scale below 1: FSR 1's EASU (edge-adaptive
// upscaling, third_party/fidelityfx/ffx_fsr1_cas.glsl, compiled in front)
// from the render-size target to the picture size, before the UI goes on
// top and before the depth of field, bloom and grade read it. Its 12 taps
// are texelFetches (ES 3.0 has no textureGather), clamped to the edge as a
// gather would be. Alpha (the depth of field's depth) is bilinear, the
// material nearest: what the passes after it read at the picture's size.
uniform highp sampler2D uSrc;   // render size: rgb, a = depth of field depth
uniform sampler2D uSrcMat;      // render size, nearest: the material
uniform vec4 uCon0;             // FsrEasuCon's con0: input / output size, and the offset
in vec2 vUV;
layout(location = 0) out vec4 oLit;
layout(location = 1) out vec4 oMat;

ivec2 lim;

vec3 tap(ivec2 p) { return texelFetch(uSrc, clamp(p, ivec2(0), lim), 0).rgb; }

void main()
{
    lim = textureSize(uSrc, 0) - 1;
    vec2 pp = floor(gl_FragCoord.xy) * uCon0.xy + uCon0.zw;
    vec2 fp = floor(pp);
    ivec2 f = ivec2(fp);
    vec3 c = FsrEasuF(pp - fp,
                      tap(f + ivec2(0, -1)), tap(f + ivec2(1, -1)),
                      tap(f + ivec2(-1, 0)), tap(f), tap(f + ivec2(1, 0)), tap(f + ivec2(2, 0)),
                      tap(f + ivec2(-1, 1)), tap(f + ivec2(0, 1)), tap(f + ivec2(1, 1)),
                      tap(f + ivec2(2, 1)),
                      tap(f + ivec2(0, 2)), tap(f + ivec2(1, 2)));
    oLit = vec4(c, texture(uSrc, vUV).a);
    oMat = texture(uSrcMat, vUV);
}

// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ambient occlusion's input, at its own size (half or a quarter of
// the render size): what ssao.frag samples, packed into one small RG16F
// texture, so each of its samples is one fetch that stays in the cache
// instead of two (depth, material) from the render-size targets.
//   r = view depth (map px) less the focus distance: the smaller numbers
//       keep a half float's precision where the picture is (a quarter
//       of a map pixel or better)
//   g = normal index + 1 (scene3d.h's S3D_N_*), 0: gets no occlusion
//       (UI, the void around the map); + 8 for sprites, which get none
//       and don't occlude either
uniform highp sampler2D uDepth;   // render size: the 3D depth buffer
uniform sampler2D uMat;           // render size: g = sprite, a = normal index + 1, over 8
uniform vec4 uProj;               // P[0][0], P[1][1], near, far
uniform float uFocus;             // the depth of field's focus distance
in vec2 vUV;
out vec2 oPrep;

void main()
{
    float d = texture(uDepth, vUV).r;
    vec4 m = texture(uMat, vUV);
    float z = d * 2.0 - 1.0, n = uProj.z, f = uProj.w;
    float w = 2.0 * n * f / (f + n - z * (f - n));
    int ni = int(m.a * 8.0 + 0.5);
    oPrep = vec2(w - uFocus, (d >= 1.0 || ni > 6 ? 0.0 : float(ni)) + (m.g > 0.5 ? 8.0 : 0.0));
}

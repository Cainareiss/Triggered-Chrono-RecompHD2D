// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ambient occlusion's input, at its own size (half or a quarter of
// the picture's): what ssao.frag samples, packed into one small texture,
// so each of its samples is one fetch that stays in the cache instead of
// two (depth, material) from the render-size targets.
//   r = view depth (map px) less the focus distance: the smaller numbers
//       keep a half float's precision where the picture is (a quarter
//       of a map pixel or better)
//   g = normal index + 1 (scene3d.h's S3D_N_*), 0: gets no occlusion
//       (sprites, UI, the void around the map)
//   b = 1 for sprites, which don't occlude
//   a = the depth of field's depth (scene3d.frag's), which the blur and
//       the upsample compare (the picture carries it in alpha)
uniform highp sampler2D uDepth;   // render size: the 3D depth buffer
uniform sampler2D uMat;           // render size: g = sprite, a = normal index + 1, over 8
uniform vec4 uProj;               // P[0][0], P[1][1], near, far
uniform vec2 uFocus;              // the depth of field's focus distance and range
in vec2 vUV;
out vec4 oPrep;

void main()
{
    float d = texture(uDepth, vUV).r;
    vec4 m = texture(uMat, vUV);
    float z = d * 2.0 - 1.0, n = uProj.z, f = uProj.w;
    float w = 2.0 * n * f / (f + n - z * (f - n));
    vec2 ndc = vUV * 2.0 - 1.0;
    float dist = length(vec3(ndc.x * w / uProj.x, ndc.y * w / uProj.y, w));
    int ni = int(m.a * 8.0 + 0.5);
    oPrep = vec4(w - uFocus.x, d >= 1.0 || ni > 6 ? 0.0 : float(ni), m.g > 0.5 ? 1.0 : 0.0,
                 clamp(0.5 + (dist - uFocus.x) / uFocus.y, 0.0, 1.0));
}

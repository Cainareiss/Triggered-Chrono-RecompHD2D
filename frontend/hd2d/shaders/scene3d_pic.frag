// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// upscale.frag's interface for the 3D diorama: compiled in front of the
// passes that read the picture (coc.frag, bright.frag, final.frag) in
// place of the upscale, so the depth of field, bloom and grade run on the
// 3D render unchanged. The picture is already at its size (rendered so,
// or brought to it by FSR's EASU, fsr_easu.frag), and in one plane: the
// background plane is the whole picture (with real depth in alpha) and
// the foreground plane is empty.
//
// Two things happen here, where the passes read the picture:
// - Ambient occlusion (ssao.frag, half or quarter size) darkens the lit
//   surfaces (not sprites or UI: the material's alpha), brought up to the
//   picture's size with a joint bilateral upsample against the 3D depth
//   buffer, so it stays on its side of every edge.
// - Sharpening, in the final pass only: CAS at the native render scale,
//   or FSR's RCAS after EASU (third_party/fidelityfx/ffx_fsr1_cas.glsl is
//   compiled in front of this). It sharpens the in-focus picture only:
//   the depth of field mixes its own (unsharpened) blur in where things
//   are out of focus, and bloom reads the picture unsharpened. Next to
//   UI it is skipped, so text boxes keep clean edges.
uniform sampler2D uLit;      // picture size, linear: the 3D render (rgb, depth)
uniform sampler2D uLitBack;  // the same
uniform sampler2D uMat;      // picture size, nearest: r = UI, g = actor, b = class / 8,
                             // a = normal index + 1, over 8 (0: no occlusion)
uniform vec2 uScale;         // (unused here)
uniform vec2 uPic;           // picture size in output pixels
uniform vec3 uTilt;          // (unused here)
uniform sampler2D uAoTex;            // ssao.frag's, blurred: r = openness, g = view depth, b = surface
uniform highp sampler2D uDepthTex;   // render size: the 3D depth buffer
uniform vec4 uAo;            // strength (0: off), near, far, -
uniform vec2 uSharp;         // mode (0: none, 1: CAS, 2: RCAS), CAS peak or RCAS strength

const int C_FORE = 3;

int classOf(vec4 m) { return int(m.b * 8.0 + 0.5); }

float aoViewDepth(float d)
{
    float z = d * 2.0 - 1.0, n = uAo.y, f = uAo.z;
    return 2.0 * n * f / (f + n - z * (f - n));
}

// Openness at uv: the 4 nearest AO texels, bilinear weights times how
// close their depth is to this pixel's.
float aoAt(vec2 uv)
{
    float w = aoViewDepth(texture(uDepthTex, uv).r);
    ivec2 size = textureSize(uAoTex, 0);
    vec2 t = uv * vec2(size) - 0.5;
    vec2 fr = fract(t);
    ivec2 b = ivec2(floor(t));
    float sum = 0.0, wsum = 0.0;
    for (int k = 0; k < 4; k++) {
        ivec2 o = ivec2(k & 1, k >> 1);
        vec4 s = texelFetch(uAoTex, clamp(b + o, ivec2(0), size - 1), 0);
        float bil = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y);
        float dz = (s.g - w) / (0.02 * w);
        float wt = (bil + 1e-3) * s.b / (1.0 + dz * dz);
        sum += s.r * wt;
        wsum += wt;
    }
    return wsum > 1e-4 ? sum / wsum : 1.0;
}

vec3 sharpened(vec2 uv, vec3 e)
{
    ivec2 p = ivec2(uv * uPic), lim = ivec2(uPic) - 1;
    ivec2 pb = clamp(p + ivec2(0, -1), ivec2(0), lim), pd = clamp(p + ivec2(-1, 0), ivec2(0), lim);
    ivec2 pf = clamp(p + ivec2(1, 0), ivec2(0), lim), ph = clamp(p + ivec2(0, 1), ivec2(0), lim);
    float ui = max(max(texelFetch(uMat, pb, 0).r, texelFetch(uMat, pd, 0).r),
                   max(texelFetch(uMat, pf, 0).r, texelFetch(uMat, ph, 0).r));
    if (ui > 0.5)
        return e;
    // Both want 0..1: FSR's reversible tonemap takes highlights over 1 in and out.
    vec3 b = FsrSrtmF(texelFetch(uLit, pb, 0).rgb), d = FsrSrtmF(texelFetch(uLit, pd, 0).rgb);
    vec3 f = FsrSrtmF(texelFetch(uLit, pf, 0).rgb), h = FsrSrtmF(texelFetch(uLit, ph, 0).rgb);
    vec3 m = FsrSrtmF(e);
    vec3 r = uSharp.x > 1.5 ? FsrRcasF(b, d, m, f, h, uSharp.y) : CasFilterF(b, d, m, f, h, uSharp.y);
    return FsrSrtmInvF(min(r, vec3(1.0 - 1.0 / 32768.0)));
}

void planes(vec2 uv, out vec4 scene, out vec4 back, out vec4 here)
{
    here = texture(uMat, uv);
    scene = texture(uLit, uv);
    if (uSharp.x > 0.5 && here.r < 0.5)
        scene.rgb = sharpened(uv, scene.rgb);
    if (uAo.x > 0.0 && here.a > 0.06 && here.a < 0.8)
        scene.rgb *= mix(1.0, aoAt(uv), uAo.x);
    back = vec4(scene.rgb, 0.0);
}

void planes(vec2 uv, out vec4 scene, out vec4 back)
{
    vec4 here;
    planes(uv, scene, back, here);
}

void planesLinear(vec2 uv, out vec4 scene, out vec4 back)
{
    planes(uv, scene, back);
}

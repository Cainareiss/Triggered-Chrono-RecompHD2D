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
// - Ambient occlusion (ssao.frag, at half or a quarter of the render
//   size) darkens the lit surfaces (not sprites or UI: the material's
//   alpha), brought up to the
//   picture's size with a joint bilateral upsample against the picture's
//   depth (alpha), so it stays on its side of every edge. Only at the
//   native render scale: below it, fsr_easu.frag has applied it already.
// - Sharpening, in the final pass only: CAS at the native render scale,
//   or FSR's RCAS after EASU (third_party/fidelityfx/ffx_fsr1_cas.glsl is
//   compiled in front of this). It sharpens the in-focus picture only:
//   the depth of field mixes its own (unsharpened) blur in where things
//   are out of focus, and bloom reads the picture unsharpened. Next to
//   UI it is skipped, so text boxes keep clean edges.
uniform sampler2D uLit;      // the 3D render (rgb, depth), picture size, linear; with uUp, EASU's
                             // picture (fsr_easu.frag: encoded rgb, a = UI)
uniform sampler2D uLitBack;  // the 3D render, linear (with uUp: render size, a = depth)
uniform sampler2D uMat;      // its material, nearest: r = UI, g = actor, b = class / 8,
                             // a = normal index + 1, over 8 (0: no occlusion)
uniform vec2 uScale;         // (unused here)
uniform vec2 uPic;           // picture size in output pixels
uniform vec3 uTilt;          // (unused here)
uniform int uUp;             // 1: below a render scale of 1 (EASU's picture)
uniform int uPicDepth;       // with uUp: planes() fills in scene.a (the depth debug view)
uniform sampler2D uAoTex;    // ssao.frag's, blurred: r = openness (-1: none), g = depth
uniform float uAo;           // strength (0: off)
uniform vec2 uSharp;         // mode (0: none, 1: CAS, 2: RCAS), CAS peak or RCAS strength

const int C_FORE = 3;

int classOf(vec4 m) { return int(m.b * 8.0 + 0.5); }

// The material at uv (coc.frag and bright.frag read it too). Below a
// render scale of 1 it comes from the render-size target, and the UI,
// drawn at the picture's size, from EASU's picture.
vec4 matAt(vec2 uv)
{
    vec4 m = texture(uMat, uv);
    if (uUp == 1)
        m.r = step(0.5, texture(uLit, uv).a);
    return m;
}

const float SRTM_MAX = 1.0 - 1.0 / 32768.0;

// Openness at uv, whose depth of field depth is w: the 4 nearest AO
// texels, bilinear weights times how close their depth is to this pixel's.
float aoAt(vec2 uv, float w)
{
    ivec2 size = textureSize(uAoTex, 0);
    vec2 t = uv * vec2(size) - 0.5;
    vec2 fr = fract(t);
    ivec2 b = ivec2(floor(t));
    float sum = 0.0, wsum = 0.0;
    for (int k = 0; k < 4; k++) {
        ivec2 o = ivec2(k & 1, k >> 1);
        vec4 s = texelFetch(uAoTex, clamp(b + o, ivec2(0), size - 1), 0);
        float bil = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y);
        float dz = (s.g - w) * 80.0;   // weight halved 2% of the view distance apart
        float wt = s.r < 0.0 ? 0.0 : (bil + 1e-3) / (1.0 + dz * dz);
        sum += s.r * wt;
        wsum += wt;
    }
    return wsum > 1e-4 ? sum / wsum : 1.0;
}

// The cross of 4 neighbours around picture pixel p, in 0..1 (FSR's
// reversible tonemap takes highlights over 1 in and out), or false next
// to UI.
bool cross4(ivec2 p, out vec3 b, out vec3 d, out vec3 f, out vec3 h)
{
    ivec2 lim = ivec2(uPic) - 1;
    ivec2 pb = clamp(p + ivec2(0, -1), ivec2(0), lim), pd = clamp(p + ivec2(-1, 0), ivec2(0), lim);
    ivec2 pf = clamp(p + ivec2(1, 0), ivec2(0), lim), ph = clamp(p + ivec2(0, 1), ivec2(0), lim);
    vec4 tb = texelFetch(uLit, pb, 0), td = texelFetch(uLit, pd, 0);
    vec4 tf = texelFetch(uLit, pf, 0), th = texelFetch(uLit, ph, 0);
    float ui;
    if (uUp == 1) {   // already encoded, a = UI
        ui = max(max(tb.a, td.a), max(tf.a, th.a));
        b = tb.rgb; d = td.rgb; f = tf.rgb; h = th.rgb;
    } else {
        ui = max(max(texelFetch(uMat, pb, 0).r, texelFetch(uMat, pd, 0).r),
                 max(texelFetch(uMat, pf, 0).r, texelFetch(uMat, ph, 0).r));
        b = FsrSrtmF(tb.rgb); d = FsrSrtmF(td.rgb); f = FsrSrtmF(tf.rgb); h = FsrSrtmF(th.rgb);
    }
    return ui < 0.5;
}

void planes(vec2 uv, out vec4 scene, out vec4 back, out vec4 here)
{
    here = matAt(uv);
    scene = texture(uLit, uv);
    vec3 m = uUp == 1 ? scene.rgb : FsrSrtmF(scene.rgb);   // 0..1, for sharpening
    bool sharp = false;
    if (uSharp.x > 0.5 && here.r < 0.5) {
        vec3 b, d, f, h;
        if (cross4(ivec2(uv * uPic), b, d, f, h)) {
            m = uSharp.x > 1.5 ? FsrRcasF(b, d, m, f, h, uSharp.y) : CasFilterF(b, d, m, f, h, uSharp.y);
            sharp = true;
        }
    }
    if (uUp == 1 || sharp)
        scene.rgb = FsrSrtmInvF(min(m, vec3(SRTM_MAX)));
    if (uUp == 1)
        scene.a = uPicDepth == 1 ? texture(uLitBack, uv).a : 0.5;
    else if (uAo > 0.0 && here.a > 0.06 && here.a < 0.8)   // (below 1, EASU applied it)
        scene.rgb *= mix(1.0, aoAt(uv, scene.a), uAo);
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

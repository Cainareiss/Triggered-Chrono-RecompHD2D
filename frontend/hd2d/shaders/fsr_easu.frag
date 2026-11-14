// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D diorama at a render scale below 1: FSR 1's EASU (edge-adaptive
// upscaling, third_party/fidelityfx/ffx_fsr1_cas.glsl, compiled in front)
// from the render-size target to the picture size, before the UI goes on
// top and before the depth of field, bloom and grade read it. Its 12 taps
// are texelFetches (ES 3.0 has no textureGather), clamped to the edge as a
// gather would be.
//
// Its output is small: RGB10_A2, the color through FSR's reversible
// tonemap (FsrSrtmF: 0..1, highlights over 1 kept), alpha 0 (the UI pass
// writes 1 over it). The passes after it read the depth of field's depth
// and the material from the render-size targets, which hold them at that
// size anyway, bilinear and nearest (scene3d_pic.frag).
//
// The ambient occlusion goes on here too, once per picture pixel, so the
// passes after it read it with the picture (scene3d_pic.frag does the same
// at the native render scale, where there is no pass like this one).
uniform highp sampler2D uSrc;   // render size: rgb, a = depth of field depth
uniform sampler2D uSrcMat;      // render size, nearest: the material (a: normal, for the occlusion)
uniform vec4 uCon0;             // FsrEasuCon's con0: input / output size, and the offset
uniform sampler2D uAoTex;       // ssao.frag's, blurred: r = openness (-1: none), g = depth
uniform float uAo;              // strength (0: off)
in vec2 vUV;
out vec4 oLit;

ivec2 lim;

vec4 tap(ivec2 p) { return texelFetch(uSrc, clamp(p, ivec2(0), lim), 0); }

// As scene3d_pic.frag's aoAt.
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
        float dz = (s.g - w) * 80.0;
        float wt = s.r < 0.0 ? 0.0 : (bil + 1e-3) / (1.0 + dz * dz);
        sum += s.r * wt;
        wsum += wt;
    }
    return wsum > 1e-4 ? sum / wsum : 1.0;
}

void main()
{
    lim = textureSize(uSrc, 0) - 1;
    vec2 pp = floor(gl_FragCoord.xy) * uCon0.xy + uCon0.zw;
    vec2 fp = floor(pp);
    ivec2 f = ivec2(fp);
    vec2 fr = pp - fp;
    vec4 tf = tap(f), tg = tap(f + ivec2(1, 0)), tj = tap(f + ivec2(0, 1)), tk = tap(f + ivec2(1, 1));
    vec3 c = FsrEasuF(fr,
                      tap(f + ivec2(0, -1)).rgb, tap(f + ivec2(1, -1)).rgb,
                      tap(f + ivec2(-1, 0)).rgb, tf.rgb, tg.rgb, tap(f + ivec2(2, 0)).rgb,
                      tap(f + ivec2(-1, 1)).rgb, tj.rgb, tk.rgb, tap(f + ivec2(2, 1)).rgb,
                      tap(f + ivec2(0, 2)).rgb, tap(f + ivec2(1, 2)).rgb);
    if (uAo > 0.0) {
        float na = texture(uSrcMat, vUV).a;
        // The depth, bilinear from the four centre taps (no fetch of its own).
        float depth = mix(mix(tf.a, tg.a, fr.x), mix(tj.a, tk.a, fr.x), fr.y);
        if (na > 0.06 && na < 0.8)
            c *= mix(1.0, aoAt(vUV, depth), uAo);
    }
    oLit = vec4(FsrSrtmF(c), 0.0);
}

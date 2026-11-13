// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 5, at the output size: depth of field, bloom, color grade, vignette
// and grain, with the UI put back sharp and ungraded on top, and the
// screen's sides (outside the picture's 4:3 rectangle) filled with a soft,
// dimmed extension of the picture.
uniform sampler2D uScene;    // picture size: rgb, a = depth
uniform sampler2D uDof;      // half size: rgb blurred, a = coc
uniform sampler2D uBloomA;   // quarter size
uniform sampler2D uBloomB;   // eighth size
uniform sampler2D uSoft;     // eighth size, unthresholded: side fill
uniform sampler2D uMat;      // source size, nearest: r = UI, b = class / 8
uniform sampler2D uOrig;     // source size, nearest: the console's frame (.bgr)
uniform vec4 uRect;          // picture rectangle in output pixels: x, y, w, h (top-left origin)
uniform vec2 uOut;           // output size
uniform int uFlip;           // 1: drawing to the window (bottom-left origin)
uniform vec4 uGrade;         // bloom, exposure, saturation, vignette
uniform vec4 uGrade2;        // grain, split-tone amount, contrast, effects on (0/1)
uniform vec3 uShadowTint;
uniform vec3 uHighTint;
uniform float uSideDim;
uniform float uTime;
uniform int uDebug;          // 0 off, 1 classes, 2 depth, 3 blur amount, 4 split compare
out vec4 oColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 classColor(float c)
{
    int k = int(c * 8.0 + 0.5);
    if (k == 0) return vec3(0.1, 0.1, 0.3);   // backdrop
    if (k == 1) return vec3(0.2, 0.4, 0.8);   // far
    if (k == 2) return vec3(0.2, 0.7, 0.3);   // ground
    if (k == 3) return vec3(0.9, 0.6, 0.1);   // foreground
    if (k == 4) return vec3(1.0, 0.2, 0.6);   // actor
    return vec3(1.0, 1.0, 1.0);               // UI
}

vec3 grade(vec3 c, vec2 uv)
{
    // Gentle filmic shoulder, normalized so 1.0 stays near 1.0.
    float e = uGrade.y;
    c = (1.0 - exp(-c * e)) / (1.0 - exp(-e));
    // Contrast around mid grey.
    c = mix(vec3(0.5), c, uGrade2.z);
    // Saturation.
    float l = luma(c);
    c = mix(vec3(l), c, uGrade.z);
    // Split toning: tint the shadows and the highlights.
    float t = smoothstep(0.0, 1.0, l);
    c *= mix(uShadowTint, uHighTint, t) * uGrade2.y + vec3(1.0 - uGrade2.y);
    // Vignette.
    vec2 d = (uv - 0.5) * vec2(1.15, 1.0);
    c *= 1.0 - uGrade.w * smoothstep(0.35, 0.95, length(d));
    return c;
}

void main()
{
    vec2 fc = vec2(gl_FragCoord.x, uFlip == 1 ? uOut.y - gl_FragCoord.y : gl_FragCoord.y);
    vec2 uv = (fc - uRect.xy) / uRect.zw;   // picture coordinates, 0..1 inside
    bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)));

    if (!inside) {
        // Side fill: the soft copy mirrored at the picture's edge (a blurred
        // reflection, not stretched edge columns), dimmed with distance.
        vec2 q = uv;
        float out_ = max(-uv.x, uv.x - 1.0);
        q.x = uv.x < 0.0 ? -uv.x * 0.7 : uv.x > 1.0 ? 1.0 - (uv.x - 1.0) * 0.7 : uv.x;
        q = clamp(q, vec2(0.002), vec2(0.998));
        // A 9-tap disc on top of the soft copy's blur: no visible blobs.
        vec2 r = 2.5 / vec2(textureSize(uSoft, 0));
        vec3 c = texture(uSoft, q).rgb * 0.2;
        for (int k = 0; k < 8; k++) {
            float a = float(k) * 0.785398;
            c += texture(uSoft, clamp(q + vec2(cos(a), sin(a)) * r, vec2(0.002), vec2(0.998))).rgb * 0.1;
        }
        c *= uSideDim;
        float fade = 1.0 - smoothstep(0.0, 0.35, out_);
        oColor = vec4(c * (0.35 + 0.65 * fade), 1.0);
        return;
    }

    vec4 scene = texture(uScene, uv);
    vec4 mat = texture(uMat, uv);

    if (uDebug == 1) { oColor = vec4(mix(classColor(mat.b), scene.rgb, 0.35), 1.0); return; }
    if (uDebug == 2) { oColor = vec4(vec3(scene.a), 1.0); return; }
    if (uDebug == 3) { oColor = vec4(vec3(texture(uDof, uv).a), 1.0); return; }
    if (uDebug == 4 && uv.x < 0.5) {
        // Split compare: the console's own pixels left of center.
        float line_ = 1.0 - step(1.5 / uRect.z, abs(uv.x - 0.5));
        oColor = vec4(mix(texture(uOrig, uv).bgr, vec3(1.0), line_), 1.0);
        return;
    }

    if (uGrade2.w < 0.5 || mat.r > 0.5) {
        // Effects off, or UI: the plain upscaled picture.
        oColor = vec4(scene.rgb, 1.0);
        return;
    }

    vec4 dof = texture(uDof, uv);
    vec3 c = mix(scene.rgb, dof.rgb, smoothstep(0.0, 0.6, dof.a));
    c += uGrade.x * (texture(uBloomA, uv).rgb * 0.6 + texture(uBloomB, uv).rgb * 0.8);
    c = grade(c, uv);
    c += (hash(fc + fract(uTime) * 61.0) - 0.5) * uGrade2.x;
    oColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}

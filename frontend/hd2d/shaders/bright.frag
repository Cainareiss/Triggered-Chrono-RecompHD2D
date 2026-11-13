// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 4a: the bloom source. Pixels above a brightness threshold (soft
// knee), with UI masked out. uThreshold < 0 copies the source unchanged.
//
// Compiled after upscale.frag. With uFromPicture set, the source is the
// picture itself (planes), and a second output gets the plain copy
// too: the start of the soft copy that fills the screen's sides. Else the
// source is uSrc, a smaller target.
uniform sampler2D uSrc;
uniform int uFromPicture;
uniform float uThreshold;
in vec2 vUV;
layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oPlain;

vec3 at(vec2 uv)
{
    if (uFromPicture == 0)
        return texture(uSrc, uv).rgb;
    // The nearest picture pixel: the taps fall on or near pixel centers
    // (exactly when the picture's size divides by 4), and what follows
    // blurs this heavily anyway.
    vec4 s, b;
    planes((floor(uv * uPic) + 0.5) / uPic, s, b);
    return s.rgb;
}

void main()
{
    // 4-tap box: this pass also halves the resolution.
    vec2 t = 0.5 / (uFromPicture == 1 ? uPic : vec2(textureSize(uSrc, 0)));
    vec3 c = 0.25 * (at(vUV + vec2(-t.x, -t.y)) + at(vUV + vec2(t.x, -t.y)) +
                     at(vUV + vec2(-t.x, t.y)) + at(vUV + vec2(t.x, t.y)));
    oPlain = vec4(c, 1.0);
    if (uThreshold < 0.0) {
        oColor = vec4(c, 1.0);
        return;
    }
    float ui = texture(uMat, vUV).r;
    float l = max(c.r, max(c.g, c.b));
    float knee = 0.2;
    float soft = clamp(l - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float w = max(soft, l - uThreshold) / max(l, 1e-4);
    oColor = vec4(c * w * (1.0 - ui), 1.0);
}

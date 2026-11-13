// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 4a: the bloom source. Pixels above a brightness threshold (soft
// knee), with UI masked out. uThreshold < 0 copies the scene unchanged
// (the soft copy used to fill the screen's sides).
uniform sampler2D uSrc;
uniform sampler2D uMat;
uniform float uThreshold;
in vec2 vUV;
out vec4 oColor;

void main()
{
    // 4-tap box: this pass also halves the resolution.
    vec2 t = 0.5 / vec2(textureSize(uSrc, 0));
    vec3 c = 0.25 * (texture(uSrc, vUV + vec2(-t.x, -t.y)).rgb + texture(uSrc, vUV + vec2(t.x, -t.y)).rgb +
                     texture(uSrc, vUV + vec2(-t.x, t.y)).rgb + texture(uSrc, vUV + vec2(t.x, t.y)).rgb);
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

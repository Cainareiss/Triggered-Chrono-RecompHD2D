// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 3b: one direction of the separable depth-of-field blur. The radius
// follows the center pixel's blur amount; each sample is weighted by its
// own, so sharp (in-focus) pixels don't bleed halos into blurred ones.
// Alpha (the center's blur amount) is passed through for the next pass.
uniform sampler2D uSrc;    // rgb, a = coc
uniform vec2 uDir;         // one texel step along the blur direction
uniform float uRadius;     // max radius in texels
in vec2 vUV;
out vec4 oColor;

void main()
{
    vec4 c = texture(uSrc, vUV);
    float r = c.a * uRadius;
    if (r < 0.25) {
        oColor = c;
        return;
    }
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    const int N = 7;
    for (int k = -N; k <= N; k++) {
        float t = float(k) / float(N);
        vec4 s = texture(uSrc, vUV + uDir * (t * r));
        float g = exp(-2.5 * t * t);
        float w = g * (0.08 + s.a);
        sum += s.rgb * w;
        wsum += w;
    }
    oColor = vec4(sum / wsum, c.a);
}

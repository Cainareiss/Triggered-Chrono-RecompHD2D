// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 0, at a quarter of the console's resolution: find light sources.
// A pixel emits when it is much brighter than the frame around it and
// reasonably saturated or near white (flames, lamps, windows, glowing
// magic); UI never does. The result, blurred, is the point-light map the
// lighting pass adds onto nearby surfaces.
uniform sampler2D uColor;     // .bgr
uniform usampler2D uLayer;
uniform int uUiZ;
uniform float uThreshold;     // brightness above which a pixel emits
out vec4 oColor;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    ivec2 size = textureSize(uColor, 0);
    ivec2 base = ivec2(gl_FragCoord.xy) * 4;
    // Brightness of the surroundings (a 24x24 window, every 3rd pixel):
    // a light source stands out from it; a sunlit field doesn't.
    float local = 0.0;
    for (int dy = -10; dy <= 11; dy += 3)
        for (int dx = -10; dx <= 11; dx += 3)
            local += luma(texelFetch(uColor, clamp(base + ivec2(dx, dy), ivec2(0), size - 1), 0).bgr);
    local /= 64.0;
    vec3 sum = vec3(0.0);
    for (int dy = 0; dy < 4; dy++)
        for (int dx = 0; dx < 4; dx++) {
            ivec2 p = min(base + ivec2(dx, dy), size - 1);
            uint l = texelFetch(uLayer, p, 0).r;
            if (int(l >> 4) == uUiZ && (l & 7u) < 4u)
                continue;
            vec3 c = texelFetch(uColor, p, 0).bgr;
            float y = luma(c);
            float sat = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
            float w = smoothstep(uThreshold, uThreshold + 0.15, y) * (0.1 + 1.5 * sat) *
                      smoothstep(0.18, 0.4, y - local);
            sum += c * w;
        }
    oColor = vec4(sum / 16.0, 1.0);
}

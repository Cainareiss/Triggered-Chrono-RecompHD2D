// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One direction of the ambient occlusion's bilateral blur (ssao.frag's
// output: r = openness, g = view depth, b = 1 on a surface that gets
// occlusion). 9 taps, Gaussian, each weighted down by how far its depth is
// from the center's, so occlusion doesn't bleed across a wall's edge onto
// the floor far behind it; taps that get no occlusion (sprites, the void)
// don't count. Depth and the surface flag pass through.
uniform sampler2D uSrc;
uniform vec2 uDir;   // one texel along the blur
in vec2 vUV;
out vec4 oAo;

void main()
{
    vec4 c = texture(uSrc, vUV);
    if (c.b < 0.5) {
        oAo = c;
        return;
    }
    float sum = c.r, wsum = 1.0;
    for (int k = 1; k <= 4; k++) {
        float g = exp(-0.18 * float(k * k));
        for (int s = -1; s <= 1; s += 2) {
            vec4 t = texture(uSrc, vUV + uDir * float(k * s));
            float dz = (t.g - c.g) / (0.015 * c.g);
            float w = g * t.b / (1.0 + dz * dz);
            sum += t.r * w;
            wsum += w;
        }
    }
    oAo = vec4(sum / wsum, c.g, 1.0, 1.0);
}

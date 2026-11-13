// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 2: the lit picture up to the output size with sharp bilinear
// filtering (crisp pixels, smooth only on the edge between two of them, so
// non-integer scales don't shimmer). Alpha (depth) is carried along.
uniform sampler2D uSrc;   // linear filtering
uniform vec2 uScale;      // output pixels per source pixel
in vec2 vUV;
out vec4 oColor;

void main()
{
    vec2 size = vec2(textureSize(uSrc, 0));
    vec2 texel = vUV * size;
    vec2 base = floor(texel);
    vec2 centerDist = fract(texel) - 0.5;
    vec2 range = 0.5 - 0.5 / uScale;
    vec2 f = (centerDist - clamp(centerDist, -range, range)) * uScale + 0.5;
    oColor = texture(uSrc, (base + f) / size);
}

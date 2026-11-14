// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Over the 3D diorama: the console frame's UI (text boxes, menus, HUD:
// the layer G-buffer's top BG level, as light.frag classifies it), flat and
// sharp, as the 2D path keeps it. Everything else is discarded.
uniform sampler2D uColor;    // the console's frame (.bgr), nearest
uniform usampler2D uLayer;   // its layer G-buffer
uniform int uUiZ;            // z level that is UI, -1: none
in vec2 vUV;
layout(location = 0) out vec4 oLit;
layout(location = 1) out vec4 oMat;

void main()
{
    vec2 uv = vUV;   // texture order: 0 is the picture's top row
    ivec2 size = textureSize(uLayer, 0);
    ivec2 p = clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1);
    uint l = texelFetch(uLayer, p, 0).r;
    if (uUiZ < 0 || int(l >> 4u) != uUiZ || int(l & 7u) >= 4)
        discard;
    oLit = vec4(texelFetch(uColor, p, 0).bgr, 0.5);
    oMat = vec4(1.0, 0.0, 5.0 / 8.0, 0.0);
}

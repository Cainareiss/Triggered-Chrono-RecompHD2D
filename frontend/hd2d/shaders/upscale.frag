// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 2: the lit picture up to the output size with sharp bilinear
// filtering (crisp pixels, smooth only on the edge between two of them, so
// non-integer scales don't shimmer), split into two planes for the layered
// depth of field, with the optional diorama tilt.
//
// Tilt: the foreground (high-priority BG tiles) is drawn slightly enlarged
// about the focus point, so it shifts away from the focus by a few pixels
// in proportion to its distance, as tall things near the camera do in a
// tilted view. Where it moves off, the behind G-buffer's pixels (the
// background plane) show; it never covers UI or what was in front of it.
//
// Outputs: oScene = the composite (rgb, depth)
//          oBack  = the background plane (rgb), a = foreground coverage
uniform sampler2D uSrc;    // lit picture (rgb, depth), linear
uniform sampler2D uBack;   // background plane (rgb, depth), linear
uniform sampler2D uMat;    // nearest: r = UI, b = class / 8, a = (z level + 16 if
                           // something real is behind a foreground pixel) / 31
uniform vec2 uScale;       // output pixels per source pixel
uniform vec3 uTilt;        // focus x, y (texture order, 0..1), enlargement (0: off)
in vec2 vUV;
layout(location = 0) out vec4 oScene;
layout(location = 1) out vec4 oBack;

const int C_FORE = 3;

vec2 sharpUV(vec2 uv, vec2 size)
{
    vec2 texel = uv * size;
    vec2 base = floor(texel);
    vec2 centerDist = fract(texel) - 0.5;
    vec2 range = 0.5 - 0.5 / uScale;
    vec2 f = (centerDist - clamp(centerDist, -range, range)) * uScale + 0.5;
    return (base + f) / size;
}

int classOf(vec4 m) { return int(m.b * 8.0 + 0.5); }
int zOf(vec4 m) { return int(m.a * 31.0 + 0.5) & 15; }
bool behindValid(vec4 m) { return int(m.a * 31.0 + 0.5) >= 16; }

void main()
{
    vec2 size = vec2(textureSize(uSrc, 0));
    vec2 uv = vUV;
    vec4 here = texture(uMat, uv);
    vec2 suv = sharpUV(uv, size);
    vec4 front = texture(uSrc, suv);
    vec4 back = texture(uBack, suv);
    bool hereFore = classOf(here) == C_FORE;
    if (uTilt.z <= 0.0) {
        oScene = front;
        oBack = vec4(back.rgb, hereFore ? 1.0 : 0.0);
        return;
    }
    // The enlargement maps the picture into itself, so uvF never leaves
    // it: no edge to fill.
    vec2 uvF = uTilt.xy + (uv - uTilt.xy) / (1.0 + uTilt.z);
    vec4 there = texture(uMat, uvF);
    bool blocked = here.r > 0.5 || (!hereFore && zOf(here) > zOf(there));
    if (classOf(there) == C_FORE && !blocked) {
        oScene = texture(uSrc, sharpUV(uvF, size));
        oBack = vec4(back.rgb, 1.0);
    } else {
        // Revealed: what the foreground hid, if anything was drawn there;
        // else the foreground stays (no hole).
        bool stays = hereFore && !behindValid(here);
        oScene = hereFore && !stays ? back : front;
        oBack = vec4(back.rgb, stays ? 1.0 : 0.0);
    }
}

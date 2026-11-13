// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 2, the upscale: the lit picture up to the output size with sharp
// bilinear filtering (crisp pixels, smooth only on the edge between two of
// them, so non-integer scales don't shimmer), split into two planes for the
// layered depth of field, with the optional diorama tilt.
//
// It has no pass of its own: writing and reading back two picture-size
// half-float planes cost more than working them out again from the small
// source-size targets, so this file is compiled in front of the passes
// that read the picture (coc.frag, bright.frag, final.frag), which call
// planes() where they used to sample the stored planes.
//
// Tilt: the foreground (high-priority BG tiles) is drawn slightly enlarged
// about the focus point, so it shifts away from the focus by a few pixels
// in proportion to its distance, as tall things near the camera do in a
// tilted view. Where it moves off, the behind G-buffer's pixels (the
// background plane) show; it never covers UI or what was in front of it.
uniform sampler2D uLit;      // source size, linear: lit picture (rgb, depth)
uniform sampler2D uLitBack;  // source size, linear: background plane (rgb, depth)
uniform sampler2D uMat;      // source size, nearest: r = UI, g = actor, b = class / 8,
                             // a = (z level + 16 if something real is behind a
                             // foreground pixel) / 31
uniform vec2 uScale;         // output pixels per source pixel
uniform vec2 uPic;           // picture size in output pixels
uniform vec3 uTilt;          // focus x, y (texture order, 0..1), enlargement (0: off)

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

// The picture pixel whose center is uv (0..1, texture order):
// scene = the composite (rgb, depth), back = the background plane (rgb),
// a = foreground coverage; here = uMat there.
void planes(vec2 uv, out vec4 scene, out vec4 back, out vec4 here)
{
    vec2 size = vec2(textureSize(uLit, 0));
    here = texture(uMat, uv);
    vec2 suv = sharpUV(uv, size);
    vec4 front = texture(uLit, suv);
    vec4 b = texture(uLitBack, suv);
    bool hereFore = classOf(here) == C_FORE;
    if (uTilt.z <= 0.0) {
        scene = front;
        back = vec4(b.rgb, hereFore ? 1.0 : 0.0);
        return;
    }
    // The enlargement maps the picture into itself, so uvF never leaves
    // it: no edge to fill.
    vec2 uvF = uTilt.xy + (uv - uTilt.xy) / (1.0 + uTilt.z);
    vec4 there = texture(uMat, uvF);
    bool blocked = here.r > 0.5 || (!hereFore && zOf(here) > zOf(there));
    if (classOf(there) == C_FORE && !blocked) {
        scene = texture(uLit, sharpUV(uvF, size));
        back = vec4(b.rgb, 1.0);
    } else {
        // Revealed: what the foreground hid, if anything was drawn there;
        // else the foreground stays (no hole).
        bool stays = hereFore && !behindValid(here);
        scene = hereFore && !stays ? b : front;
        back = vec4(b.rgb, stays ? 1.0 : 0.0);
    }
}

void planes(vec2 uv, out vec4 scene, out vec4 back)
{
    vec4 here;
    planes(uv, scene, back, here);
}

// The planes as a bilinear read of picture-size targets holding them would
// give them at uv (what a pass at a lower resolution sampled).
void planesLinear(vec2 uv, out vec4 scene, out vec4 back)
{
    vec2 x = uv * uPic - 0.5;
    vec2 i0 = floor(x);
    vec2 f = x - i0;
    vec2 lo = (clamp(i0, vec2(0.0), uPic - 1.0) + 0.5) / uPic;
    vec2 hi = (clamp(i0 + 1.0, vec2(0.0), uPic - 1.0) + 0.5) / uPic;
    vec4 s00, s10, s01, s11, b00, b10, b01, b11;
    planes(lo, s00, b00);
    planes(vec2(hi.x, lo.y), s10, b10);
    planes(vec2(lo.x, hi.y), s01, b01);
    planes(hi, s11, b11);
    scene = mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
    back = mix(mix(b00, b10, f.x), mix(b01, b11, f.x), f.y);
}

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

// Asset packs (docs/ASSET-PACKS.md): replacement art at high resolution.
// The map says, per console pixel, which atlas entry replaces it (x: entry
// + 1, 0 none), the texel of the original piece it showed (y: u | v << 8),
// its provenance flags (z: 4 H flip, 8 V flip, 32 color math, 64 a pose
// beyond the original's pixels: no behind G-buffer under it) and the
// palette color it was drawn with (w: 15-bit). The replacement is sampled
// at the output pixel's own position within that texel, and given the
// change the console's pipeline (brightness, color math) and the lighting
// made to the original color, so fades and effects carry over. With
// uPack 0 nothing here runs.
uniform int uPack;
uniform usampler2D uPackMap;
uniform usampler2D uPackRects;    // per entry: (page, x, y, w), (h, src w, src h, 0)
uniform highp sampler2DArray uPackAtlas;
uniform sampler2D uPackBehind;    // behind G-buffer colors (.bgr): under transparent texels
uniform vec2 uPackParams;         // -, behind G-buffer valid (0/1)

// The picture's color at uv (0..1) with the replacement, if any, in place
// of orig (the composite's color there).
vec3 packColor(vec2 uv, vec3 orig)
{
    vec2 size = vec2(textureSize(uLit, 0));
    vec2 s = uv * size;
    ivec2 p = clamp(ivec2(floor(s)), ivec2(0), ivec2(size) - 1);
    vec2 f = clamp(s - vec2(p), 0.0, 1.0);
    uvec4 m = texelFetch(uPackMap, p, 0);
    if (m.x == 0u)
        return orig;
    int e = int(m.x) - 1;
    ivec2 rc = ivec2((e % 1024) * 2, e / 1024);
    uvec4 r0 = texelFetch(uPackRects, rc, 0);
    uvec4 r1 = texelFetch(uPackRects, rc + ivec2(1, 0), 0);
    vec2 rect = vec2(float(r0.w), float(r1.x));
    vec2 src = vec2(float(r1.y), float(r1.z));
    vec2 t = vec2(float(m.y & 255u) + ((m.z & 4u) != 0u ? 1.0 - f.x : f.x),
                  float(m.y >> 8) + ((m.z & 8u) != 0u ? 1.0 - f.y : f.y));
    t = t / src * rect;
    // Sharp where magnified (each replacement texel a crisp block, smooth
    // only at its edges), as the upscale does with console pixels.
    vec2 perTexel = uScale * src / rect;   // output pixels per replacement texel
    if (perTexel.x > 1.0) {
        vec2 base = floor(t), cd = t - base - 0.5;
        vec2 range = 0.5 - 0.5 / perTexel;
        t = base + 0.5 + (cd - clamp(cd, -range, range)) * perTexel;
    }
    t = clamp(t, vec2(0.5), rect - 0.5);
    float asize = float(textureSize(uPackAtlas, 0).x);
    // Texels per output pixel: the mip level, so a big replacement drawn
    // small doesn't sparkle.
    float lod = max(log2(rect.x / src.x / uScale.x), 0.0);
    vec4 rep = textureLod(uPackAtlas, vec3((vec2(float(r0.y), float(r0.z)) + t) / asize, float(r0.x)), lod);
    // What the console did to this pixel's palette color, done to the
    // replacement: a ratio (brightness, which can change line by line,
    // and the lighting), or an offset where color math added or
    // subtracted another layer.
    uint c = m.w;
    vec3 pal = vec3(float(c & 31u), float((c >> 5) & 31u), float((c >> 10) & 31u)) / 31.0;
    vec3 lit = texelFetch(uLit, p, 0).rgb;
    vec3 col = (m.z & 32u) != 0u ? rep.rgb + (lit - pal)
                                  : (rep.rgb + 0.03) * ((lit + 0.03) / (pal + 0.03)) - 0.03;
    // (64: a pose's picture beyond the original's pixels: what is there shows under it)
    vec3 under = uPackParams.y > 0.5 && (m.z & 64u) == 0u ? texelFetch(uPackBehind, p, 0).bgr : orig;
    return mix(under, max(col, 0.0), rep.a);
}

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
// The material at uv, for the passes that read it directly (coc.frag,
// bright.frag); scene3d_pic.frag has its own.
vec4 matAt(vec2 uv) { return texture(uMat, uv); }
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
    if (uPack != 0)
        front.rgb = packColor(uv, front.rgb);
    vec4 b = texture(uLitBack, suv);
    bool hereFore = classOf(here) == C_FORE;
    // Outside the foreground the background plane is the pixel itself
    // (light.frag's oBack), so it takes the replacement too: the final
    // pass draws in-focus pixels from it.
    if (uPack != 0 && !hereFore)
        b.rgb = front.rgb;
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
        if (uPack != 0)
            scene.rgb = packColor(uvF, scene.rgb);
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

// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D diorama's surfaces. Each quad names an 8x8 tile in VRAM (address,
// palette, depth: scene3d.h's packing), decoded here from the console's
// own VRAM and CGRAM, so animated tiles and palette changes show as they
// happen. Color 0 is transparent (cut-outs, sprites) except on the back
// layer's floor, where it is the backdrop color, as on the console.
//
// Lighting keeps the original's colors where the sun falls on the floor:
// everything is lit relative to that (a south wall a little darker, the
// shaded side of a box more), with the key light's shadow map, the 2D
// path's point-light map looked up where the console drew the pixel, and
// haze with distance. Outputs the lit color with the depth of field's
// depth in alpha (0.5 at the focus), and the material (as light.frag's).
//
// With SHADOW_PASS defined: only the cut-out test, for the shadow map.
#ifdef GL_ES
precision highp sampler2DShadow;
#endif
uniform usampler2D uVram;        // 256 x 128 words
uniform sampler2D uCgram;        // 256 x 1, RGB
in vec2 vUV;
in vec3 vWorld;
in vec4 vShadow;
flat in uint vTile;

uint vramAt(uint a) { return texelFetch(uVram, ivec2(int(a & 255u), int(a >> 8u)), 0).r; }

// The tile's color index (0-15) at pixel p of it.
int colorIndex(uint tile, ivec2 p)
{
    uint addr = tile & 0x7FFFu;
    uint w0 = vramAt((addr + uint(p.y)) & 0x7FFFu);
    int sh = 7 - p.x;
    int c = int((w0 >> uint(sh)) & 1u) | int((w0 >> uint(sh + 8)) & 1u) << 1;
    if ((tile & (1u << 18)) == 0u) {   // 4 bpp: planes 2 and 3 eight words on
        uint w1 = vramAt((addr + 8u + uint(p.y)) & 0x7FFFu);
        c |= int((w1 >> uint(sh)) & 1u) << 2 | int((w1 >> uint(sh + 8)) & 1u) << 3;
    }
    return c;
}

// THE TILE LOOKUP: what a quad's tile looks like at uv (0-8 px, flips
// applied): rgb, and a = 0 where it is transparent. Everything 3D samples
// its texture through here, keyed by the tile (VRAM address, palette,
// depth, BG or sprite: scene3d.h's packing). An asset pack replaces this
// function (or branches in it on its own flag) to return its own texture
// for a (tile, palette); the console's pixels are the default.
vec4 tileTexel(uint tile, vec2 uv)
{
    ivec2 p = clamp(ivec2(floor(uv)), ivec2(0), ivec2(7));
    int c = colorIndex(tile, p);
    bool opaque = (tile & (1u << 23)) != 0u;   // the back floor: color 0 is the backdrop
    if (c == 0 && !opaque)
        return vec4(0.0);
#ifdef SHADOW_PASS
    return vec4(1.0);
#else
    bool obj = (tile & (1u << 19)) != 0u;
    int pal = int((tile >> 15u) & 7u);
    int idx = c == 0 ? 0 : (obj ? 128 : 0) + pal * ((tile & (1u << 18)) != 0u ? 4 : 16) + c;
    return vec4(texelFetch(uCgram, ivec2(idx, 0), 0).rgb, 1.0);
#endif
}

#ifdef SHADOW_PASS
void main()
{
    if (tileTexel(vTile, vUV).a < 0.5)
        discard;
}
#else
uniform sampler2DShadow uShadowMap;
uniform sampler2D uLightMap;     // point lights, in the console's screen space
uniform vec3 uLightDir;          // towards the key light
uniform vec3 uKey;
uniform vec3 uAmbient;
uniform vec3 uEye;
uniform vec2 uCam;               // the game's camera: map px at the screen's top-left
uniform vec4 uFog;               // rgb, amount
uniform vec4 uDepth;             // focus distance, depth range, brightness, shadow amount
uniform vec4 uMisc;              // point lights, haze start, haze end, shadow texel
layout(location = 0) out vec4 oLit;
layout(location = 1) out vec4 oMat;

const vec3 NORMALS[6] = vec3[6](vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                                vec3(-1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0),
                                vec3(0.0, 0.5, 0.866));

float shadowAt(vec3 n, vec3 L)
{
    vec3 s = vShadow.xyz / vShadow.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0))))
        return 1.0;
    float bias = 0.0015 + 0.003 * (1.0 - max(dot(n, L), 0.0));
    float t = uMisc.w, v = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 o = vec2(float(k & 1) - 0.5, float(k >> 1) - 0.5) * 1.5 * t;
        v += texture(uShadowMap, vec3(s.xy + o, s.z - bias));
    }
    return v * 0.25;
}

void main()
{
    vec4 tex = tileTexel(vTile, vUV);
    if (tex.a < 0.5)
        discard;
    vec3 col = tex.rgb;
    bool obj = (vTile & (1u << 19)) != 0u;

    int ni = int((vTile >> 20u) & 7u);
    vec3 n = NORMALS[min(ni, 5)];
    vec3 L = normalize(uLightDir);
    float vis = shadowAt(n, L);
    float shade = 1.0 - uDepth.w * (1.0 - vis);
    // Relative to the sunlit floor, which keeps its colors.
    vec3 flat_ = uAmbient + uKey * max(L.y, 0.05);
    vec3 light = uAmbient * (ni == 0 ? 1.0 : 0.85) + uKey * max(dot(n, L), 0.0) * shade;
    vec3 rel = light / flat_;
    if (obj)
        rel = mix(vec3(shade), rel, 0.4);   // sprites: mostly their own shading
    vec3 lit = col * rel;

    // Point lights where the console drew this pixel (screen row = z - height).
    vec2 sp = vec2(vWorld.x - uCam.x, vWorld.z - vWorld.y - uCam.y) / vec2(256.0, 224.0);
    vec3 pl = texture(uLightMap, sp).rgb;
    lit += col * pl * uMisc.x;

    float dist = length(vWorld - uEye);
    lit = mix(lit, uFog.rgb, uFog.a * smoothstep(uMisc.y, uMisc.z, dist));
    lit *= uDepth.z;   // the console's brightness (fades)

    float depth = clamp(0.5 + (dist - uDepth.x) / uDepth.y, 0.0, 1.0);
    bool raised = (vTile & (1u << 24)) != 0u;
    float cls = obj ? 4.0 : raised ? 3.0 : 2.0;
    oLit = vec4(lit, depth);
    oMat = vec4(0.0, obj ? 1.0 : 0.0, cls / 8.0, 0.0);
}
#endif

// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 1, at the console's resolution: classify every pixel from the PPU's
// layer G-buffer, then light it as if the scene were a diorama.
//
// Layer byte: z level << 4 | math << 3 | layer, where layer is 0-3 BG1-4,
// 4/6 OBJ, 5 backdrop and z is the PPU's front-to-back priority level
// (0 backdrop .. 15 front). All targets keep row 0 at the top of the
// picture (texture order); only the final pass flips for the screen.
//
// Outputs: oLit  = lit color (linear-ish RGB), depth (0 near .. 1 far)
//          oMat  = UI mask, actor mask, class / 8, (z level + 16 if a
//                  foreground pixel has a real pixel behind it) / 31
//          oBack = the background plane: oLit, except under foreground
//                  pixels, where it is the behind G-buffer's pixel (the
//                  layer the foreground hides), lit the same way but
//                  without relief; a = that pixel's depth. Where only the
//                  backdrop is behind (nothing was drawn there, so its
//                  color means nothing) the foreground stands in.
uniform sampler2D uColor;     // bytes B G R x uploaded as RGBA: use .bgr
uniform usampler2D uLayer;
uniform sampler2D uBehind;    // behind G-buffer: the runner-up's color (.bgr)
uniform usampler2D uBLayer;   // and its layer byte
uniform int uUiZ;             // z level treated as UI (HUD, text), -1: none
uniform vec4 uAmount;         // relief, shadow, ao, rim
uniform vec4 uAmount2;        // haze, point lights, -, -
uniform sampler2D uLightMap;  // blurred light sources (emit.frag), linear
uniform vec3 uLightDir;       // towards the light, x right, y down, z viewer
uniform vec3 uKey;            // key light color
uniform vec3 uAmbient;        // ambient color
uniform vec3 uHaze;           // distance haze color
uniform ivec2 uShadowOffset;  // where an actor's shadow falls, in pixels

layout(location = 0) out vec4 oLit;
layout(location = 1) out vec4 oMat;
layout(location = 2) out vec4 oBack;

const int C_BACKDROP = 0, C_FAR = 1, C_GROUND = 2, C_FORE = 3, C_ACTOR = 4, C_UI = 5;

ivec2 gSize;

ivec2 clampP(ivec2 p) { return clamp(p, ivec2(0), gSize - 1); }

vec3 colorAt(ivec2 p) { return texelFetch(uColor, clampP(p), 0).bgr; }

int classOf(uint l)
{
    int layer = int(l & 7u);
    int z = int(l >> 4);
    if (layer == 5)
        return C_BACKDROP;
    if (z == uUiZ && layer < 4)
        return C_UI;
    if (layer == 4 || layer == 6)
        return C_ACTOR;
    if (z >= 11)
        return C_FORE;     // high-priority BG tiles: in front of actors
    if (z >= 4)
        return C_GROUND;
    return C_FAR;          // low BG3 / BG4 planes, behind everything
}

int classAt(ivec2 p) { return classOf(texelFetch(uLayer, clampP(p), 0).r); }

float depthOf(int c)
{
    if (c == C_BACKDROP) return 1.0;
    if (c == C_FAR)      return 0.85;
    if (c == C_GROUND)   return 0.55;
    if (c == C_FORE)     return 0.22;
    return 0.5;            // actors and UI sit in the focal plane
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// Height of a pixel for relief lighting: its brightness, but only within
// its own class, so layer edges don't become cliffs.
float heightAt(ivec2 p, int c, float h0)
{
    return classAt(p) == c ? luma(colorAt(p)) : h0;
}

float isActor(ivec2 p) { return classAt(p) == C_ACTOR ? 1.0 : 0.0; }

void main()
{
    gSize = textureSize(uColor, 0);
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 col = colorAt(p);
    uint lb = texelFetch(uLayer, p, 0).r;
    int c = classOf(lb);
    float depth = depthOf(c);
    uint bl = texelFetch(uBLayer, p, 0).r;
    int bc = classOf(bl);
    bool bvalid = c == C_FORE && bc != C_BACKDROP;
    float zl = float(int(lb >> 4) + (bvalid ? 16 : 0)) / 31.0;

    if (c == C_UI) {
        oLit = vec4(col, depth);
        oMat = vec4(1.0, 0.0, float(c) / 8.0, zl);
        oBack = oLit;
        return;
    }

    vec3 lit = col;

    // Relief: normals from the brightness field, Lambert from the key
    // light, normalized so a flat surface keeps its color.
    if (c == C_GROUND || c == C_FORE || c == C_FAR || c == C_ACTOR) {
        float h0 = luma(col);
        float hx = heightAt(p + ivec2(1, 0), c, h0) - heightAt(p - ivec2(1, 0), c, h0);
        float hy = heightAt(p + ivec2(0, 1), c, h0) - heightAt(p - ivec2(0, 1), c, h0);
        float k = c == C_ACTOR ? 1.2 : 2.2;
        vec3 n = normalize(vec3(-hx * k, -hy * k, 1.0));
        vec3 L = normalize(uLightDir);
        float flat_ = max(L.z, 0.05);
        float d = max(dot(n, L), 0.0) / flat_;
        vec3 light = uAmbient + uKey * d;
        vec3 lightFlat = uAmbient + uKey;
        lit = col * mix(vec3(1.0), light / lightFlat, uAmount.x);
    }

    // Actor shadows on everything behind the actors: their silhouette,
    // offset away from the light and softened over a 3x3 block.
    float s = 0.0;
    if (c != C_ACTOR) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                s += isActor(p - uShadowOffset + ivec2(dx, dy));
        s /= 9.0;
        float receive = c == C_FORE ? 0.3 : 1.0;   // tall foreground is mostly above
        lit *= 1.0 - uAmount.y * 0.7 * s * receive;
    }

    // Contact occlusion: ground darkens next to foreground and actors.
    if (c == C_GROUND || c == C_FAR || c == C_BACKDROP) {
        float o = 0.0;
        for (int k = 0; k < 8; k++) {
            float a = float(k) * 0.785398;
            ivec2 q = p + ivec2(round(vec2(cos(a), sin(a)) * 2.0));
            int cq = classAt(q);
            o += (cq == C_FORE || cq == C_ACTOR) ? 1.0 : 0.0;
        }
        lit *= 1.0 - uAmount.z * 0.35 * (o / 8.0);
    }

    // Rim light on actor edges that face the light.
    if (c == C_ACTOR) {
        ivec2 toward = ivec2(sign(uLightDir.x), sign(uLightDir.y));
        float edge = 1.0 - isActor(p + toward);
        lit += uAmount.w * 0.35 * edge * uKey * (0.4 + luma(col));
    }

    // Point lights: light sources found in the frame, blurred, light
    // the surfaces around them (the sources themselves gain little).
    vec3 pl = texture(uLightMap, (vec2(p) + 0.5) / vec2(gSize)).rgb;
    lit += col * pl * uAmount2.y * (1.0 - 0.6 * smoothstep(0.6, 0.9, luma(col)));

    // Distance haze on the far planes.
    // (Not on black: a void or letterbox stays black.)
    if (c == C_FAR || c == C_BACKDROP)
        lit = mix(lit, uHaze, uAmount2.x * (c == C_BACKDROP ? 0.35 : 0.2) *
                                  smoothstep(0.03, 0.18, luma(col)));

    oLit = vec4(lit, depth);
    oMat = vec4(0.0, c == C_ACTOR ? 1.0 : 0.0, float(c) / 8.0, zl);

    // The background plane under foreground: what the foreground hides,
    // with the same shadows, point lights and haze (no relief, which
    // would need its neighbors).
    oBack = oLit;
    if (bvalid) {
        vec3 bcol = texelFetch(uBehind, p, 0).bgr;
        vec3 b = bcol;
        if (bc != C_ACTOR)
            b *= 1.0 - uAmount.y * 0.7 * s;
        b += bcol * pl * uAmount2.y * (1.0 - 0.6 * smoothstep(0.6, 0.9, luma(bcol)));
        if (bc == C_FAR || bc == C_BACKDROP)
            b = mix(b, uHaze, uAmount2.x * (bc == C_BACKDROP ? 0.35 : 0.2) *
                                  smoothstep(0.03, 0.18, luma(bcol)));
        oBack = vec4(b, depthOf(bc));
    }
}

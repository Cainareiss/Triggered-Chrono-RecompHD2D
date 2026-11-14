// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D diorama's ambient occlusion, at half (or a quarter of) the
// picture's size: a normal-oriented hemisphere of samples around each
// surface point, in view space, tested against the 3D depth buffer. A
// sample the depth buffer has something in front of counts as occluded,
// faded out (range check) when that something is much nearer than the
// radius, so a wall in front of a far floor casts no halo on it. That
// gives the soft contact shadows where walls meet floors, in corners and
// under eaves.
//
// The normals aren't reconstructed from depth: every quad has one of six
// (scene3d.h's S3D_N_*), which scene3d.frag writes to the material's
// alpha, (index + 1) / 8, and 0 for what gets no occlusion (sprites, UI,
// the void around the map). Sprites aren't occluders either: the shadow
// map gives them their own, and their flat billboards would darken the
// floor behind them.
//
// Output: r = openness (1: unoccluded), g = view depth (map px), b = 1
// where this is an occluded-or-not surface, 0 where it gets none (the
// blur and the upsample skip those).
uniform highp sampler2D uDepth;   // render size: the 3D depth buffer
uniform sampler2D uMat;           // render size: a = normal index + 1, over 8; g = sprite
uniform mat4 uView;
uniform vec4 uProj;               // P[0][0], P[1][1], near, far
uniform vec2 uRadius;             // radius (map px), samples
in vec2 vUV;
out vec4 oAo;

const vec3 NORMALS[6] = vec3[6](vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                                vec3(-1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0),
                                vec3(0.0, 0.5, 0.866));

float viewDepth(float d)
{
    float z = d * 2.0 - 1.0, n = uProj.z, f = uProj.w;
    return 2.0 * n * f / (f + n - z * (f - n));
}

vec3 viewPos(vec2 uv, float w)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * w / uProj.x, ndc.y * w / uProj.y, -w);
}

// A 4x4 ordered pattern: the blur after this pass is as wide, so it
// averages the 16 rotations out.
float pattern(ivec2 p)
{
    int k = (p.x & 3) * 4 + (p.y & 3);
    const float B[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0,
                                  9.0, 15.0, 7.0, 13.0, 5.0);
    return (B[k] + 0.5) / 16.0;
}

void main()
{
    float d = texture(uDepth, vUV).r;
    vec4 m = texture(uMat, vUV);
    int ni = int(m.a * 8.0 + 0.5) - 1;
    float w = viewDepth(d);
    if (d >= 1.0 || ni < 0 || ni > 5) {
        oAo = vec4(1.0, w, 0.0, 1.0);
        return;
    }
    vec3 P = viewPos(vUV, w);
    vec3 n = normalize(mat3(uView) * NORMALS[ni]);
    // Lift the origin off the surface a little: the depth buffer's own
    // quantization then doesn't read as occlusion.
    float radius = uRadius.x;
    P += n * (0.04 * radius);

    float a = pattern(ivec2(gl_FragCoord.xy)) * 6.2831853;
    vec3 r = vec3(cos(a), sin(a), 0.0);
    vec3 t = r - n * dot(r, n);
    t = dot(t, t) < 1e-4 ? normalize(cross(n, vec3(0.0, 0.0, 1.0))) : normalize(t);
    vec3 b = cross(n, t);

    int count = int(uRadius.y);
    float occ = 0.0;
    for (int k = 0; k < 16; k++) {
        if (k >= count)
            break;
        // Golden-angle spiral over the disc, lifted to a cosine-weighted
        // hemisphere; nearer samples are denser (they matter more).
        float q = (float(k) + 0.5) / float(count);
        float phi = float(k) * 2.3999632;
        float rad = sqrt(q);
        vec3 h = vec3(cos(phi) * rad, sin(phi) * rad, sqrt(max(1.0 - q, 0.0)));
        float len = mix(0.2, 1.0, q * q) * radius;
        vec3 s = P + (t * h.x + b * h.y + n * h.z) * len;
        vec2 uv = vec2(s.x * uProj.x, s.y * uProj.y) / -s.z * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            continue;
        float sw = viewDepth(texture(uDepth, uv).r);
        // Sprites stand on the floor but don't occlude it.
        if (texture(uMat, uv).g > 0.5)
            continue;
        float in_front = -s.z - sw;   // > 0: the scene is nearer than the sample
        float range = smoothstep(0.0, 1.0, radius / max(abs(w - sw), 1e-3));
        occ += step(0.02 * radius, in_front) * range;
    }
    oAo = vec4(1.0 - occ / float(count), w, 1.0, 1.0);
}

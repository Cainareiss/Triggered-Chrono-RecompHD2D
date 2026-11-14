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
// alpha, and 0 for what gets no occlusion (sprites, UI, the void around
// the map). Sprites aren't occluders either: the shadow map gives them
// their own, and their flat billboards would darken the floor behind them.
// All of it comes from aoprep.frag's texture, at this pass's size.
//
// Output: r = openness (1: unoccluded), g = the depth of field's depth
// (what the blur and the upsample compare), b = 1 where this is a surface
// that gets occlusion, 0 where it gets none (they skip those).
uniform highp sampler2D uPrep;    // aoprep.frag's: view depth - focus, normal + 1, sprite, depth
uniform mat4 uView;
uniform vec4 uProj;               // P[0][0], P[1][1], near, far
uniform vec3 uRadius;             // radius (map px), samples, the focus distance
uniform vec3 uKernel[16];         // the samples, in radii (tangent, bitangent, normal)
in vec2 vUV;
out vec4 oAo;

const vec3 NORMALS[6] = vec3[6](vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0),
                                vec3(-1.0, 0.0, 0.0), vec3(0.0, 0.0, -1.0),
                                vec3(0.0, 0.5, 0.866));

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
    vec4 c = texture(uPrep, vUV);
    int ni = int(c.g + 0.5) - 1;
    float w = c.r + uRadius.z;
    if (ni < 0 || ni > 5) {
        oAo = vec4(1.0, c.a, 0.0, 1.0);
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

    // The kernel (hd2d.c s3d_ao_kernel): a golden-angle spiral over the
    // disc lifted to a cosine-weighted hemisphere, z along the normal, in
    // units of the radius; nearer samples are denser (they matter more).
    int count = int(uRadius.y);
    float occ = 0.0;
    for (int k = 0; k < 16; k++) {
        if (k >= count)
            break;
        vec3 h = uKernel[k] * radius;
        vec3 s = P + t * h.x + b * h.y + n * h.z;
        vec2 uv = vec2(s.x * uProj.x, s.y * uProj.y) / -s.z * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
            continue;
        vec4 at = texture(uPrep, uv);
        if (at.b > 0.5)   // sprites stand on the floor but don't occlude it
            continue;
        float sw = at.r + uRadius.z;
        float in_front = -s.z - sw;   // > 0: the scene is nearer than the sample
        float range = smoothstep(0.0, 1.0, radius / max(abs(w - sw), 1e-3));
        occ += step(0.02 * radius, in_front) * range;
    }
    oAo = vec4(1.0 - occ / float(count), c.a, 1.0, 1.0);
}

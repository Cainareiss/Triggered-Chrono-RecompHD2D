// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 3a, at half the output size: downsample the two planes and work out
// the background plane's blur amount (circle of confusion, 0..1) for the
// tilt-shift depth of field: a sharp band around the focus line, blur
// growing above and below it, plus blur for planes away from the focal
// depth. The foreground plane's blur amount depends only on the row
// (fgCoc), so it isn't stored.
//
// The background plane has the behind G-buffer's pixels under the
// foreground, so blurring it never smears the foreground into it, and the
// foreground, kept apart with its coverage, blurs out over what is behind
// it instead of ending in a hard edge.
//
// Compiled after upscale.frag: planesLinear() gives the two planes
// (composite = foreground color; background rgb, a = foreground coverage)
// as a bilinear read of picture-size copies would; uLitBack.a is the
// background plane's depth and matAt() (r = UI, g = actor) is nearest.
uniform vec4 uFocus;        // focus y (0 top .. 1), half band, ramp, focal depth
uniform vec3 uDof;          // tilt amount, depth amount, foreground depth
in vec2 vUV;
layout(location = 0) out vec4 oBg;   // rgb, a = blur amount
layout(location = 1) out vec4 oFg;   // premultiplied rgb, a = coverage

float cocAt(float y, float depth)
{
    float tilt = smoothstep(uFocus.y, uFocus.y + uFocus.z, abs(y - uFocus.x));
    return max(tilt * uDof.x, smoothstep(0.08, 0.45, abs(depth - uFocus.w)) * uDof.y);
}

void main()
{
    vec4 s, b;
    planesLinear(vUV, s, b);
    vec4 m = matAt(vUV);
    float coc = cocAt(vUV.y, texture(uLitBack, vUV).a);   // texture order: 0 is the top
    // Actors stay mostly readable wherever they stand.
    coc *= mix(1.0, 0.3, m.g);
    coc *= 1.0 - m.r;
    oBg = vec4(b.rgb, clamp(coc, 0.0, 1.0));
    oFg = vec4(s.rgb * b.a, b.a);
}

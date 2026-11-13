// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 3b: one direction of the separable depth-of-field blur, for both
// planes. Background: the radius follows the center pixel's blur amount;
// each sample is weighted by its own, so sharp (in-focus) pixels don't
// bleed halos into blurred ones. Alpha (the center's blur amount) is
// passed through for the next pass. Foreground: premultiplied color and
// coverage, blurred by the row's foreground blur amount, so its edge
// spreads over what is behind it.
uniform sampler2D uSrc;    // background plane: rgb, a = coc
uniform sampler2D uFg;     // foreground plane: premultiplied rgb, a = coverage
uniform vec2 uDir;         // one texel step along the blur direction
uniform float uRadius;     // max radius in texels
uniform vec4 uFocus;       // as coc.frag
uniform vec3 uDof;
in vec2 vUV;
layout(location = 0) out vec4 oBg;
layout(location = 1) out vec4 oFg;

const int N = 7;
// Taps per side for a blur of radius r texels: up to N, but only enough to
// keep them TAP_GAP texels apart (closer adds little, as the samples
// are bilinear), so small blurs cost less.
const float TAP_GAP = 1.75;
int tapsFor(float r) { return clamp(int(ceil(r / TAP_GAP)), 2, N); }

float fgCoc(float y)
{
    float tilt = smoothstep(uFocus.y, uFocus.y + uFocus.z, abs(y - uFocus.x));
    return max(tilt * uDof.x, smoothstep(0.08, 0.45, abs(uDof.z - uFocus.w)) * uDof.y);
}

void main()
{
    vec4 c = texture(uSrc, vUV);
    float r = c.a * uRadius;
    if (r < 0.25) {
        oBg = c;
    } else {
        vec3 sum = vec3(0.0);
        float wsum = 0.0;
        int n = tapsFor(r);
        for (int k = -n; k <= n; k++) {
            float t = float(k) / float(n);
            vec4 s = texture(uSrc, vUV + uDir * (t * r));
            float g = exp(-2.5 * t * t);
            float w = g * (0.08 + s.a);
            sum += s.rgb * w;
            wsum += w;
        }
        oBg = vec4(sum / wsum, c.a);
    }

    float rf = fgCoc(vUV.y) * uRadius;
    if (rf < 0.25) {
        oFg = texture(uFg, vUV);
    } else {
        vec4 sum = vec4(0.0);
        float wsum = 0.0;
        int n = tapsFor(rf);
        for (int k = -n; k <= n; k++) {
            float t = float(k) / float(n);
            float g = exp(-2.5 * t * t);
            sum += texture(uFg, vUV + uDir * (t * rf)) * g;
            wsum += g;
        }
        oFg = sum / wsum;
    }
}

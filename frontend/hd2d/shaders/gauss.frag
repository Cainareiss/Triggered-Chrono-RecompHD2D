// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 4b: one direction of a 9-tap Gaussian (linear-sampling weights),
// for the bloom chain and the soft side fill.
uniform sampler2D uSrc;
uniform vec2 uDir;   // one texel step
in vec2 vUV;
out vec4 oColor;

void main()
{
    const float o1 = 1.3846153846, o2 = 3.2307692308;
    const float w0 = 0.2270270270, w1 = 0.3162162162, w2 = 0.0702702703;
    vec3 c = texture(uSrc, vUV).rgb * w0;
    c += texture(uSrc, vUV + uDir * o1).rgb * w1;
    c += texture(uSrc, vUV - uDir * o1).rgb * w1;
    c += texture(uSrc, vUV + uDir * o2).rgb * w2;
    c += texture(uSrc, vUV - uDir * o2).rgb * w2;
    oColor = vec4(c, 1.0);
}

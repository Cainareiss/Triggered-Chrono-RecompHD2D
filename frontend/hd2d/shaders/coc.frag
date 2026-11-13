// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pass 3a, at half the output size: downsample the scene and work out each
// pixel's blur amount (circle of confusion, 0..1) for the tilt-shift depth
// of field: a sharp band around the focus line, blur growing above and
// below it, plus blur for planes away from the focal depth.
uniform sampler2D uScene;   // rgb, a = depth
uniform sampler2D uMat;     // source resolution, nearest: r = UI
uniform vec4 uFocus;        // focus y (0 top .. 1), half band, ramp, focal depth
uniform vec2 uDof;          // tilt amount, depth amount
in vec2 vUV;
out vec4 oColor;

void main()
{
    vec4 s = texture(uScene, vUV);
    float ui = texture(uMat, vUV).r;
    float y = vUV.y;   // texture order: 0 is the top
    float tilt = smoothstep(uFocus.y, uFocus.y + uFocus.z, abs(y - uFocus.x));
    float dz = abs(s.a - uFocus.w);
    float coc = max(tilt * uDof.x, smoothstep(0.08, 0.45, dz) * uDof.y);
    // Actors stay mostly readable wherever they stand.
    coc *= mix(1.0, 0.3, texture(uMat, vUV).g);
    coc *= 1.0 - ui;
    oColor = vec4(s.rgb, clamp(coc, 0.0, 1.0));
}

// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One triangle covering the viewport; no vertex buffers (gl_VertexID).
// vUV is 0..1 across the viewport, origin at the bottom-left as GL has it.
out vec2 vUV;

void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

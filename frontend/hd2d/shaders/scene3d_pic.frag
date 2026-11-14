// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// upscale.frag's interface for the 3D diorama: compiled in front of the
// passes that read the picture (coc.frag, bright.frag, final.frag) in
// place of the upscale, so the depth of field, bloom and grade run on the
// 3D render unchanged. The picture is already at its size, and in one
// plane: the background plane is the whole picture (with real depth in
// alpha) and the foreground plane is empty.
uniform sampler2D uLit;      // picture size, linear: the 3D render (rgb, depth)
uniform sampler2D uLitBack;  // the same
uniform sampler2D uMat;      // picture size, nearest: r = UI, g = actor, b = class / 8
uniform vec2 uScale;         // (unused here)
uniform vec2 uPic;           // picture size in output pixels
uniform vec3 uTilt;          // (unused here)

const int C_FORE = 3;

int classOf(vec4 m) { return int(m.b * 8.0 + 0.5); }

void planes(vec2 uv, out vec4 scene, out vec4 back, out vec4 here)
{
    here = texture(uMat, uv);
    scene = texture(uLit, uv);
    back = vec4(scene.rgb, 0.0);
}

void planes(vec2 uv, out vec4 scene, out vec4 back)
{
    vec4 here;
    planes(uv, scene, back, here);
}

void planesLinear(vec2 uv, out vec4 scene, out vec4 back)
{
    planes(uv, scene, back);
}

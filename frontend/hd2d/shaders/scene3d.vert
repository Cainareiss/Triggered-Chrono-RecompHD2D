// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D diorama (scene3d.h): one 8x8 tile per quad, in world space (map
// pixels, y up). uMVP is the camera's, or the key light's for the shadow
// map. The tile it shows is decoded from VRAM per pixel (scene3d.frag).
// With WORLD_MAP, uOffset moves the whole mesh (a wrapping world map's
// neighbouring copies).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in uint aTile;
uniform mat4 uMVP;
uniform mat4 uLightMVP;
#ifdef WORLD_MAP
uniform vec3 uOffset;
#define POS (aPos + uOffset)
#else
#define POS aPos
#endif
out vec2 vUV;
out vec3 vWorld;
out vec4 vShadow;
flat out uint vTile;

void main()
{
    vUV = aUV;
    vTile = aTile;
    vWorld = POS;
    vShadow = uLightMVP * vec4(POS, 1.0);
    gl_Position = uMVP * vec4(POS, 1.0);
}

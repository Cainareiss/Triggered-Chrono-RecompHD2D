// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 3D diorama (scene3d.h): one 8x8 tile per quad, in world space (map
// pixels, y up). uMVP is the camera's, or the key light's for the shadow
// map. The tile it shows is decoded from VRAM per pixel (scene3d.frag).
// With WORLD_MAP, uOffset moves the whole mesh (a wrapping world map's
// neighbouring copies).
//
// With MODEL, an asset pack's model (hd2d.h hd2d_models): its own normal
// and vertex color instead of a tile, always raised (it casts shadows).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
#ifdef MODEL
layout(location = 3) in vec3 aNrm;
layout(location = 4) in vec4 aCol;
out vec3 vNrm;
out vec4 vCol;
#else
layout(location = 2) in uint aTile;
#endif
uniform mat4 uMVP;
uniform mat4 uLightMVP;
#if defined(WORLD_MAP) || defined(MODEL)
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
#ifdef MODEL
    vTile = 1u << 24u;   // S3D_RAISED
    vNrm = aNrm;
    vCol = aCol;
#else
    vTile = aTile;
#endif
    vWorld = POS;
    vShadow = uLightMVP * vec4(POS, 1.0);
    gl_Position = uMVP * vec4(POS, 1.0);
}

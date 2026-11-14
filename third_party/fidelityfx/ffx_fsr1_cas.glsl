// SPDX-FileCopyrightText: 2017-2021 Advanced Micro Devices, Inc.
// SPDX-FileCopyrightText: 2026 jerbanpls
// SPDX-License-Identifier: MIT
//
// AMD FidelityFX Super Resolution 1 (EASU upscaling, RCAS sharpening) and
// Contrast Adaptive Sharpening (CAS), ported to GLSL 3.30 core / GLSL ES
// 3.00 at 32-bit float, for the HD-2D renderer's 3D diorama
// (frontend/hd2d/hd2d.c). Derived from, in this directory:
//   ffx_fsr1.h  FSR 1 v1.20210629 (FidelityFX-FSR v1.0.2, a21ffb8f): FsrEasuF, FsrRcasF
//   ffx_cas.h   CAS 1.20190610 (FidelityFX-CAS v1.0.2, 9fabcc9a): CasFilter, no scaling
//   ffx_a.h     the helpers those use (A_GPU, A_GLSL, 32-bit)
// ffx_a.h's GLSL section can't be included as it is: it needs
// bitfieldExtract and the like (GLSL 4.00 / ES 3.10). The math below is
// the headers' non-packed 32-bit paths, unchanged, with these differences:
//   - The taps are passed in as values; the caller fetches them (EASU's
//     gather4 callbacks, FsrRcasLoadF, CasLoad), so ES 3.00, which has
//     no textureGather, can use plain texelFetch.
//   - Per-channel scalar code is written on vec3s (the same operations).
//   - RCAS: its limiter's 1/(4*max) is guarded against 0/0 on black.
//   - RCAS without FSR_RCAS_DENOISE or FSR_RCAS_PASSTHROUGH_ALPHA; CAS
//     without CAS_BETTER_DIAGONALS, CAS_GO_SLOWER or CAS_SLOW (defaults).
// The approximations (APrx*) are ffx_a.h's, after Michal Drobot's
// "float approximations" (2014).
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// Copyright (c) 2026 jerbanpls (this port)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// ---- ffx_a.h helpers ----
float AMin3F1(float x, float y, float z) { return min(x, min(y, z)); }
float AMax3F1(float x, float y, float z) { return max(x, max(y, z)); }
vec3 AMin3F3(vec3 x, vec3 y, vec3 z) { return min(x, min(y, z)); }
vec3 AMax3F3(vec3 x, vec3 y, vec3 z) { return max(x, max(y, z)); }
float APrxLoRcpF1(float a) { return uintBitsToFloat(0x7ef07ebbu - floatBitsToUint(a)); }
vec3 APrxLoRcpF3(vec3 a) { return uintBitsToFloat(uvec3(0x7ef07ebbu) - floatBitsToUint(a)); }
float APrxMedRcpF1(float a)
{
    float b = uintBitsToFloat(0x7ef19fffu - floatBitsToUint(a));
    return b * (-b * a + 2.0);
}
float APrxLoRsqF1(float a) { return uintBitsToFloat(0x5f347d74u - (floatBitsToUint(a) >> 1u)); }
vec3 APrxLoSqrtF3(vec3 a) { return uintBitsToFloat((floatBitsToUint(a) >> 1u) + uvec3(0x1fbc4639u)); }

// ---- FSR 1: EASU (ffx_fsr1.h FsrEasuTapF, FsrEasuSetF, FsrEasuF) ----
void FsrEasuTapF(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob,
                 float clp, vec3 c)
{
    vec2 v;
    v.x = (off.x * (dir.x)) + (off.y * dir.y);
    v.y = (off.x * (-dir.y)) + (off.y * dir.x);
    v *= len;
    float d2 = v.x * v.x + v.y * v.y;
    d2 = min(d2, clp);
    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

// w: the bilinear weight of this 2x2 quad's corner (FsrEasuSetF's biS..biV).
void FsrEasuSetF(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD,
                 float lE)
{
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = APrxLoRcpF1(lenX);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = APrxLoRcpF1(lenY);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

float FsrEasuLuma(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }   // luma times 2

// pp: the output pixel's position in the input, minus floor() of it (the
// 'f' tap's integer position). The 12 taps around f:
//      b c
//    e f g h
//    i j k l
//      n o
vec3 FsrEasuF(vec2 pp, vec3 b, vec3 c, vec3 e, vec3 f, vec3 g, vec3 h, vec3 i, vec3 j, vec3 k,
              vec3 l, vec3 n, vec3 o)
{
    float bL = FsrEasuLuma(b), cL = FsrEasuLuma(c), eL = FsrEasuLuma(e), fL = FsrEasuLuma(f);
    float gL = FsrEasuLuma(g), hL = FsrEasuLuma(h), iL = FsrEasuLuma(i), jL = FsrEasuLuma(j);
    float kL = FsrEasuLuma(k), lL = FsrEasuLuma(l), nL = FsrEasuLuma(n), oL = FsrEasuLuma(o);
    vec2 dir = vec2(0.0);
    float len = 0.0;
    FsrEasuSetF(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    FsrEasuSetF(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    FsrEasuSetF(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    FsrEasuSetF(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < (1.0 / 32768.0);
    dirR = APrxLoRsqF1(dirR);
    dirR = zro ? 1.0 : dirR;
    dir.x = zro ? 1.0 : dir.x;
    dir *= vec2(dirR);
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = APrxLoRcpF1(lob);
    vec3 min4 = min(AMin3F3(f, g, j), k);
    vec3 max4 = max(AMax3F3(f, g, j), k);
    vec3 aC = vec3(0.0);
    float aW = 0.0;
    FsrEasuTapF(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    FsrEasuTapF(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    FsrEasuTapF(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    FsrEasuTapF(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    FsrEasuTapF(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    FsrEasuTapF(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    FsrEasuTapF(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    FsrEasuTapF(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    FsrEasuTapF(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    FsrEasuTapF(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    FsrEasuTapF(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    FsrEasuTapF(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    return min(max4, max(min4, aC * vec3(1.0 / aW)));
}

// ---- FSR 1: RCAS (ffx_fsr1.h FsrRcasF) ----
// Taps in a cross around e:  b / d e f / h. Inputs in 0..1. con: the
// sharpness as FsrRcasCon gives it, exp2(-stops) (1: the most).
#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))
vec3 FsrRcasF(vec3 b, vec3 d, vec3 e, vec3 f, vec3 h, float con)
{
    vec3 mn4 = min(AMin3F3(b, d, f), h);
    vec3 mx4 = max(AMax3F3(b, d, f), h);
    vec2 peakC = vec2(1.0, -1.0 * 4.0);
    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1.0 / 65536.0));
    vec3 hitMax = (peakC.x - max(mx4, e)) / (4.0 * mn4 + peakC.y);
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT, min(AMax3F1(lobeRGB.r, lobeRGB.g, lobeRGB.b), 0.0)) * con;
    float rcpL = APrxMedRcpF1(4.0 * lobe + 1.0);
    return (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
}

// ---- FSR 1: the reversible tonemap for inputs over 1 (FsrSrtmF, FsrSrtmInvF) ----
vec3 FsrSrtmF(vec3 c) { return c * (1.0 / (AMax3F1(c.r, c.g, c.b) + 1.0)); }
vec3 FsrSrtmInvF(vec3 c)
{
    return c * (1.0 / max(1.0 / 32768.0, 1.0 - AMax3F1(c.r, c.g, c.b)));
}

// ---- CAS (ffx_cas.h CasFilter, noScaling) ----
// Taps in a cross around e (the corners are dead code in the default
// path). Inputs in 0..1. peak: CasSetup's const1.x, -1 / mix(8, 5, sharpness).
vec3 CasFilterF(vec3 b, vec3 d, vec3 e, vec3 f, vec3 h, float peak)
{
    vec3 mn = AMin3F3(AMin3F3(d, e, f), b, h);
    vec3 mx = AMax3F3(AMax3F3(d, e, f), b, h);
    vec3 rcpM = APrxLoRcpF3(mx);
    vec3 amp = clamp(min(mn, vec3(1.0) - mx) * rcpM, 0.0, 1.0);
    amp = APrxLoSqrtF3(amp);
    float wG = amp.g * peak;   // the green weight for all three, as the default path
    float rcpWeight = APrxMedRcpF1(1.0 + 4.0 * wG);
    return clamp((b * wG + d * wG + f * wG + h * wG + e) * rcpWeight, 0.0, 1.0);
}

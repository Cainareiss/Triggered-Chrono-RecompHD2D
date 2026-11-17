/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the overlays through an ImDrawList (overlays.h). */
#include "overlays.h"

#include "imgui.h"

void overlays_draw(ImDrawList *dl, int w, int h)
{
    char lines[OVERLAY_COUNT][32];
    int n = overlays_lines(lines, OVERLAY_COUNT);
    if (!dl || n <= 0 || w < 64 || h < 64)
        return;
    float px = (float)h / 36.0f;   /* 22 px at 800 high */
    if (px < 12.0f)
        px = 12.0f;
    float pad = px * 0.35f, margin = px * 0.6f;
    ImFont *font = ImGui::GetFont();
    float tw = 0;
    for (int k = 0; k < n; k++) {
        ImVec2 s = font->CalcTextSizeA(px, 1e9f, 0.0f, lines[k]);
        if (s.x > tw)
            tw = s.x;
    }
    float bw = tw + 2 * pad, bh = n * px * 1.15f + 2 * pad;
    int c = overlays_get_corner();
    float x = (c == OVERLAY_TL || c == OVERLAY_BL) ? margin : (float)w - margin - bw;
    float y = (c == OVERLAY_TL || c == OVERLAY_TR) ? margin : (float)h - margin - bh;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(0, 0, 0, 150), pad);
    for (int k = 0; k < n; k++)
        dl->AddText(font, px, ImVec2(x + pad, y + pad + k * px * 1.15f),
                    IM_COL32(255, 255, 255, 230), lines[k]);
}

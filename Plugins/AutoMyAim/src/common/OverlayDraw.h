// OverlayDraw.h - in-game drawing shared by Auto My Aim and Pick My Loot (the
// two copies are identical): the corner status chip and the ground ring. The
// POEFixer plugin kit (PluginUi.h) supplies the panel, the colours of the
// settings tab and outlined text; this file adds only what the kit has no
// helper for. Both plugins stack their chips at one corner through `slot`, so
// the chip geometry must stay the same in both copies.
#pragma once

#include "../../../../POEFixer/plugin_sdk/PluginUi.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace PfOverlay {

// Where a status chip sits on the screen (saved as an int: keep the order).
enum class Anchor : int { TopLeft = 0, TopCenter, TopRight, BottomLeft, BottomCenter, BottomRight, Count };

inline ImU32 ToU32(const float c[4]) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
}

// A status chip on the kit's in-game panel: coloured dot, title, state text.
// `slot` stacks chips of different plugins under each other at one anchor
// (Pick My Loot uses 0, Auto My Aim 1). `opacity` 0.3-1.0 scales the panel and
// the text together.
inline void StatusChip(ImDrawList* dl, ImVec2 screen, Anchor anchor, int slot, float scale,
                       const char* title, const char* state, ImU32 stateColor, float opacity) {
    if (!dl || !title || !state) return;
    ImFont* font = ImGui::GetFont();
    const float size = std::floor(ImGui::GetFontSize() * scale);
    const float pad = std::floor(size * 0.45f);
    const float dot = std::floor(size * 0.45f);
    const float gap = std::floor(size * 0.40f);
    const ImVec2 t1 = font->CalcTextSizeA(size, FLT_MAX, 0.0f, title);
    const ImVec2 t2 = font->CalcTextSizeA(size, FLT_MAX, 0.0f, state);
    const float w = std::ceil(pad * 2.0f + dot + gap + t1.x + gap + t2.x);
    const float h = std::floor(size + pad * 1.2f);
    const float margin = std::floor(size * 0.8f);
    const float step = h + std::floor(size * 0.35f);
    const float stack = step * static_cast<float>(slot);
    float x = margin, y = margin + stack;
    switch (anchor) {
        case Anchor::TopCenter:    x = (screen.x - w) * 0.5f; break;
        case Anchor::TopRight:     x = screen.x - w - margin; break;
        case Anchor::BottomLeft:   y = screen.y - margin - h - stack; break;
        case Anchor::BottomCenter: x = (screen.x - w) * 0.5f; y = screen.y - margin - h - stack; break;
        case Anchor::BottomRight:  x = screen.x - w - margin; y = screen.y - margin - h - stack; break;
        default: break;
    }
    x = PluginUi::SnapPx(x);
    y = PluginUi::SnapPx(y);
    const float alpha = std::clamp(opacity, 0.16f, 1.0f);
    PluginUi::Panel(dl, ImVec2(x, y), ImVec2(x + w, y + h), alpha * 0.85f);

    const int a = static_cast<int>(alpha * 255.0f);
    const ImU32 dotCol = (stateColor & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
    dl->AddCircleFilled(ImVec2(x + pad + dot * 0.5f, y + std::floor(h * 0.5f)), dot * 0.5f, dotCol, 16);
    const float ty = PluginUi::SnapPx(y + (h - t1.y) * 0.5f);
    float tx = x + pad + dot + gap;
    dl->AddText(font, size, ImVec2(tx, ty), IM_COL32(235, 235, 235, a), title);
    tx = PluginUi::SnapPx(tx + t1.x + gap);
    dl->AddText(font, size, ImVec2(tx, ty), dotCol, state);
}

// A ground ring: a dark underlay 2 px wider than the coloured line on top, so
// it reads on bright and dark ground. Paths stroke on their centre line.
inline void GroundRing(ImDrawList* dl, const ImVec2* pts, int n, ImU32 col, float thickness) {
    if (!dl || n < 3) return;
    const int a = static_cast<int>((col >> IM_COL32_A_SHIFT) & 0xFF);
    // ImGui 1.93 order: thickness, then flags.
    dl->AddPolyline(pts, n, IM_COL32(0, 0, 0, a * 3 / 5), thickness + 2.0f, ImDrawFlags_Closed);
    dl->AddPolyline(pts, n, col, thickness, ImDrawFlags_Closed);
}

}  // namespace PfOverlay

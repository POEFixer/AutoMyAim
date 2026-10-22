// Overlay.cpp - Auto My Aim in-game drawing: the status chip, the target
// marker, the aim-range ring and the optional diagnostics readout.
#include "../AutoMyAimPlugin.h"

#include "../common/GameGates.h"
#include "../common/OverlayDraw.h"
#include "../common/ImGuiText.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

void AutoMyAimPlugin::DrawUI() {
    if (!m_enabled || !ctx()->ImGuiContext) return;
    if (!m_settings.showOverlay && !m_settings.showRangeCircle && !m_settings.debugShowTarget) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
    PfImGui::EnsureTextTables();
    if (!ctx()->Game.IsInGame() || !ctx()->Game.IsForeground()) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float scale = m_settings.overlayScale;

    if (m_settings.showRangeCircle) DrawRangeCircle(dl, scale);

    if (m_settings.showOverlay) {
        if (m_hasTarget && m_state == State::Aiming) DrawTargetMarker(dl, scale);
        const PluginSDK::ScreenSize disp = ctx()->Game.GetScreenSize();
        ImU32 stateCol = IM_COL32(170, 170, 170, 255);
        if (m_state == State::Aiming) stateCol = IM_COL32(240, 110, 100, 255);
        else if (m_state == State::NoTarget) stateCol = IM_COL32(230, 190, 90, 255);
        else if (StateIsActive(m_state)) stateCol = IM_COL32(120, 170, 240, 255);
        const char* state = StateText(m_state);
        PfOverlay::StatusChip(dl, ImVec2(disp.Width, disp.Height), static_cast<PfOverlay::Anchor>(m_settings.statusAnchor),
                         1, scale, GetName(), state, stateCol, m_settings.overlayOpacity);
    }

    if (m_settings.debugShowTarget && m_hasTarget && !m_targetDebug.empty()) {
        // Below the target marker, inside a dark panel, wrapped at 34 em.
        ImFont* font = ImGui::GetFont();
        const float size = std::floor(ImGui::GetFontSize() * scale);
        const float wrap = size * 34.0f;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, wrap, m_targetDebug.c_str());
        const ImVec2 padv = PluginUi::PanelPadding();
        const float pad = std::max(padv.x, std::floor(size * 0.4f));
        const PluginSDK::ScreenSize disp = ctx()->Game.GetScreenSize();
        float x = std::floor(m_curTarget.screenX - ts.x * 0.5f);
        float y = std::floor(m_curTarget.screenY + size * 2.0f);
        x = std::clamp(x, pad, std::max(pad, disp.Width - ts.x - pad * 2.0f));
        y = std::clamp(y, pad, std::max(pad, disp.Height - ts.y - pad * 2.0f));
        PluginUi::Panel(dl, ImVec2(x - pad, y - pad), ImVec2(x + ts.x + pad, y + ts.y + pad), 0.85f);
        dl->AddText(font, size, ImVec2(x, y), IM_COL32(150, 225, 150, 255), m_targetDebug.c_str(), nullptr, wrap);
    }
}

void AutoMyAimPlugin::DrawTargetMarker(ImDrawList* dl, float scale) {
    const ImU32 col = PfOverlay::ToU32(m_settings.targetColor);
    const int a = static_cast<int>((col >> IM_COL32_A_SHIFT) & 0xFF);
    // Whole-pixel centre: ImGui 1.93 draws lines without the old +0.5 px offset.
    const ImVec2 c(std::floor(m_curTarget.screenX + 0.5f), std::floor(m_curTarget.screenY + 0.5f));
    const float r = std::floor(ImGui::GetFontSize() * 0.85f * scale);
    const float t = std::max(1.5f, 2.0f * scale);
    const ImU32 under = IM_COL32(0, 0, 0, a * 3 / 5);
    // The dark underlay and the coloured ring share one centre line (ImGui 1.93
    // strokes circles inside the radius unless told otherwise).
    dl->AddCircle(c, r, under, 32, t + 2.0f, ImDrawFlags_StrokeCenter);
    dl->AddCircle(c, r, col, 32, t, ImDrawFlags_StrokeCenter);
    // Four short ticks outside the ring and a centre dot: reads as a crosshair
    // without covering the monster. Axis-aligned, so AddLineH / AddLineV,
    // centred on the crosshair's axes.
    const float in = r + 2.0f, out = r + std::floor(r * 0.45f);
    for (int pass = 0; pass < 2; ++pass) {
        const ImU32 lc = pass == 0 ? under : col;
        const float lt = pass == 0 ? t + 2.0f : t;
        dl->AddLineH(c.x + in, c.x + out, c.y, lc, lt, ImDrawFlags_StrokeCenter);
        dl->AddLineH(c.x - out, c.x - in, c.y, lc, lt, ImDrawFlags_StrokeCenter);
        dl->AddLineV(c.x, c.y + in, c.y + out, lc, lt, ImDrawFlags_StrokeCenter);
        dl->AddLineV(c.x, c.y - out, c.y - in, lc, lt, ImDrawFlags_StrokeCenter);
    }
    dl->AddCircleFilled(c, std::max(2.0f, r * 0.18f), col, 12);
}

void AutoMyAimPlugin::DrawRangeCircle(ImDrawList* dl, float scale) {
    // The player position from the worker's last pass when aiming; otherwise a
    // cheap player read (no entity enumeration).
    float wx = 0.f, wy = 0.f, wz = 0.f;
    const AutoMyAim::AimFrame* f = FreshFrame();
    if (f && f->havePlayer && StateIsActive(m_state)) {
        wx = f->playerWX;
        wy = f->playerWY;
        wz = f->playerWZ;
    } else {
        const PluginSDK::Entity p = ctx()->Entities.GetPlayer();
        if (!p.IsValid) return;
        wx = p.WorldX;
        wy = p.WorldY;
        wz = p.WorldZ;
    }
    const float R = static_cast<float>(m_settings.aimRange) * PfGates::kGridToWorld;
    constexpr int N = 64;
    ImVec2 pts[N];
    int n = 0;
    for (int i = 0; i < N; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(N);
        float sx = 0.f, sy = 0.f;
        if (!ctx()->Render.WorldToScreen(wx + R * std::cos(a), wy + R * std::sin(a), wz, sx, sy)) {
            n = 0;  // a point behind the camera: do not draw a broken ring
            break;
        }
        pts[n++] = ImVec2(sx, sy);
    }
    PfOverlay::GroundRing(dl, pts, n, PfOverlay::ToU32(m_settings.rangeColor), std::max(1.0f, 1.5f * scale));
}

// SettingsUi.cpp - Auto My Aim settings tab (drawn below the host's plugin
// header, which owns the Enabled checkbox and the version), built with the
// POEFixer plugin kit (PluginUi.h): cards per topic, one measured label column
// per card, wrapped tooltips, dimmed dependents.
#include "../AutoMyAimPlugin.h"

#include "../../../../POEFixer/imgui/IconsFontAwesome6.h"
#include "../../../../POEFixer/plugin_sdk/PluginUi.h"
#include "../common/ImGuiText.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

using PluginUi::Check;
using PluginUi::Heading;
using PluginUi::HelpMarker;
using PluginUi::Hint;
using PluginUi::LabelColumn;
using PluginUi::Tip;

namespace {
ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// The kit's warning amber is made for dark themes; on a light window it is
// too faint for text, so it is darkened there.
ImVec4 ReadableWarning(const PluginUi::Palette& pal) {
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const float luma = 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z;
    return luma > 0.5f ? ImVec4(0.62f, 0.38f, 0.0f, 1.0f) : pal.Warning;
}
}  // namespace

void AutoMyAimPlugin::DrawSettings() {
    if (!ctx()->ImGuiContext) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
    PfImGui::EnsureTextTables();
    EnsureLoaded();

    ImGui::PushID("AutoMyAimSettings");
    DrawHeader();
    ImGui::Spacing();

    if (PluginUi::WideEnoughForTwoColumns() &&
        ImGui::BeginTable("##cols", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextColumn();
        DrawSectionKeys();
        ImGui::Spacing();
        DrawSectionTargeting();
        ImGui::Spacing();
        DrawSectionPriority();
        ImGui::TableNextColumn();
        DrawSectionCursor();
        ImGui::Spacing();
        DrawSectionSafety();
        ImGui::Spacing();
        DrawSectionDisplay();
        ImGui::EndTable();
    } else {
        DrawSectionKeys();
        ImGui::Spacing();
        DrawSectionTargeting();
        ImGui::Spacing();
        DrawSectionPriority();
        ImGui::Spacing();
        DrawSectionCursor();
        ImGui::Spacing();
        DrawSectionSafety();
        ImGui::Spacing();
        DrawSectionDisplay();
    }
    ImGui::Spacing();
    DrawSectionDiagnostics();
    ImGui::PopID();

    // Save shortly after an edit completes (not every frame of a drag), so a
    // crash or a killed process does not lose the change.
    if (m_dirty && !ImGui::IsAnyItemActive() && ::GetTickCount64() - m_dirtyAt > 600) SaveSettings();
}

void AutoMyAimPlugin::DrawHeader() {
    const PluginUi::Palette pal = PluginUi::Colors();
    PluginUi::BeginCard("##header");
    ImVec4 col = pal.TextSecondary;
    if (m_state == State::Aiming) col = pal.Error;
    else if (m_state == State::NoTarget) col = ReadableWarning(pal);
    else if (StateIsActive(m_state)) col = pal.Accent;
    if (!m_enabled) PluginUi::StatusPill("Plugin disabled", U32(pal.TextSecondary));
    else PluginUi::StatusPill(StateText(m_state), U32(col));
    if (m_enabled && m_toggled) {
        ImGui::SameLine();
        PluginUi::StatusPill("Always-on aim: ON", U32(pal.Success));
        Tip("Press the toggle key in game again to turn always-on aiming off.");
    }
    ImGui::Spacing();
    Hint("Moves the cursor onto the best monster near you while you hold the aim key, or all the time after "
         "you press the toggle key. Your skills then cast at that monster. It only moves the cursor: you still "
         "press your skills.");
    if (m_saveGuard.Holding()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ReadableWarning(pal));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(m_saveGuard.Reason() == PfIo::LoadStatus::Corrupt
                                   ? "Your settings file could not be read (it is not valid JSON). A copy was kept "
                                     "as config/settings.json.corrupt. The defaults are in use, and nothing is saved "
                                     "until you change a setting here."
                                   : "Your settings file exists but could not be opened (another program may be "
                                     "using it). The defaults are in use for now; the file is not overwritten and is "
                                     "read again shortly.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionKeys() {
    PluginUi::BeginCard("##keys");
    Heading(ICON_FA_KEYBOARD, "Keys");
    HelpMarker("Click a key button, then press the key or mouse button to use. Esc cancels, Clear removes the "
               "key. A click cannot bind the left mouse button (clicking is how you use the button), so it has "
               "its own button.\n\n"
               "To aim while you use a skill on a mouse button, give Aim (hold) that same button: holding it "
               "casts and aims; releasing it stops. Keep always-on aim off with that setup.");
    AutoMyAimConfig::Settings& s = m_settings;
    const float x0 = ImGui::GetCursorPosX();
    const LabelColumn col({"Aim (hold)", "Toggle always-on"});

    col.RowNoWidth("Aim (hold)");
    if (PluginUi::HotkeyCapture("##aimKey", &s.aimKey,
                                "Hold this key or mouse button in game to aim.\n"
                                "Click, then press the key or mouse button to use."))
        OnKeyBound(s.aimKey);
    const char* lmb = "Left mouse###aimLmb";
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < PluginUi::ButtonWidth(lmb)) {
        ImGui::NewLine();
        ImGui::SetCursorPosX(x0 + col.Width());
    }
    ImGui::BeginDisabled(s.aimKey == VK_LBUTTON);
    if (PluginUi::SubtleButton(lmb)) OnKeyBound(s.aimKey = VK_LBUTTON);
    ImGui::EndDisabled();
    Tip("Aims while you hold the left mouse button.\n"
        "Useful when your main skill is on the left button; it also redirects left-click movement to monsters.");

    col.RowNoWidth("Toggle always-on");
    if (PluginUi::HotkeyCapture("##toggleKey", &s.toggleKey,
                                "Press this key in game to switch always-on aiming on or off.\n"
                                "Click, then press the key or mouse button to use."))
        OnKeyBound(s.toggleKey);
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionTargeting() {
    PluginUi::BeginCard("##targeting");
    Heading(ICON_FA_CROSSHAIRS, "Targeting");
    AutoMyAimConfig::Settings& s = m_settings;
    const LabelColumn col({"Aim range", "Update every", "Wall threshold"});

    if (PluginUi::SliderIntRow(col, "Aim range", "##range", &s.aimRange, AutoMyAimConfig::kAimRangeMin,
                               AutoMyAimConfig::kAimRangeMax, "%d cells",
                               "How far from your character a monster may be to be aimed at, in map cells.\n"
                               "Turn on \"Show aim range ring\" under Display to see it on the ground while you "
                               "adjust it."))
        MarkDirty();

    if (PluginUi::SliderIntRow(col, "Update every", "##interval", &s.actionIntervalMs,
                               AutoMyAimConfig::kActionIntervalMinMs, AutoMyAimConfig::kActionIntervalMaxMs,
                               "%d ms",
                               "How often the target is re-chosen and the cursor moved.\n"
                               "16 ms follows moving monsters as closely as the game updates them;\n"
                               "a few tens of ms is calmer."))
        MarkDirty();

    if (Check("Only monsters in line of sight", "los", &s.useLineOfSight,
              "Skips monsters behind walls, using the map's walkable terrain.")) MarkDirty();
    ImGui::BeginDisabled(!s.useLineOfSight);
    if (PluginUi::SliderIntRow(col, "Wall threshold", "##los", &s.losBlockBelow, 0, AutoMyAimConfig::kLosBlockBelowMax,
                               "%d",
                               "Terrain below this walkable level blocks the line of sight (0 = nothing blocks).\n"
                               "Raise it if monsters behind low walls are still aimed at; lower it if open ground "
                               "blocks aiming."))
        MarkDirty();
    ImGui::EndDisabled();

    if (Check("Skip monsters with these buffs", "skipBuffs", &s.skipBlacklistedBuffs,
              "Skips monsters that carry any buff in the list below. Meant for monsters your skills cannot hit, "
              "such as Delirium phantoms or monsters frozen in time.")) MarkDirty();
    HelpMarker("Buff names, separated by commas; case and spaces are ignored.\n\n"
               "If the cursor sticks to a monster that cannot be hit: open Diagnostics, turn on the target "
               "readout, aim at the monster and add the buff name you see there.");
    ImGui::BeginDisabled(!s.skipBlacklistedBuffs);
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 6.0f, ImGui::GetContentRegionAvail().x));
    if (PluginUi::InputString("##buffs", "buff_name, other_buff", s.buffBlacklist)) {
        if (s.buffBlacklist.size() > AutoMyAimConfig::kBuffBlacklistMax)
            s.buffBlacklist.resize(AutoMyAimConfig::kBuffBlacklistMax);
        MarkDirty();
    }
    ImGui::EndDisabled();
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionPriority() {
    PluginUi::BeginCard("##priority");
    Heading(ICON_FA_SCALE_BALANCED, "Which monster first");
    HelpMarker("Every monster in range gets a score; the highest score is aimed at. Closeness always counts; "
               "rarity and life add to it, fading with distance, so a far rare monster does not pull the "
               "cursor away from one next to you unless you give rarity a large weight.");
    AutoMyAimConfig::Settings& s = m_settings;
    const LabelColumn col({"Closeness", "Magic", "Rare", "Unique", "Life weight"});

    if (PluginUi::SliderFloatRow(col, "Closeness", "##dist", &s.distanceWeight, 0.f,
                                 AutoMyAimConfig::kDistanceWeightMax, "%.1f",
                                 "How much being close to you counts.\n"
                                 "Higher = the nearest monster wins more often."))
        MarkDirty();

    if (Check("Prefer rarer monsters", "rarity", &s.rarityWeighting,
              "Adds a bonus for magic, rare and unique monsters.")) MarkDirty();
    ImGui::BeginDisabled(!s.rarityWeighting);
    if (PluginUi::SliderFloatRow(col, "Magic", "##wMagic", &s.wMagic, 0.f, AutoMyAimConfig::kRarityWeightMax, "%.0f",
                                 "Bonus for magic monsters.")) MarkDirty();
    if (PluginUi::SliderFloatRow(col, "Rare", "##wRare", &s.wRare, 0.f, AutoMyAimConfig::kRarityWeightMax, "%.0f",
                                 "Bonus for rare monsters.")) MarkDirty();
    if (PluginUi::SliderFloatRow(col, "Unique", "##wUnique", &s.wUnique, 0.f, AutoMyAimConfig::kRarityWeightMax,
                                 "%.0f", "Bonus for unique monsters.")) MarkDirty();
    ImGui::EndDisabled();

    if (Check("Weigh by remaining life", "hp", &s.hpWeighting,
              "Adds a bonus by the monster's remaining life and energy shield.")) MarkDirty();
    ImGui::BeginDisabled(!s.hpWeighting);
    ImGui::Indent();
    if (Check("Prefer healthier monsters", "hpHigh", &s.preferHigherHp,
              "Off: finish wounded monsters first.\nOn: go for the healthiest monster first.")) MarkDirty();
    ImGui::Unindent();
    if (PluginUi::SliderFloatRow(col, "Life weight", "##hpW", &s.hpWeight, 0.f, AutoMyAimConfig::kHpWeightMax, "%.0f",
                                 "How much the remaining life counts.")) MarkDirty();
    ImGui::EndDisabled();
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionCursor() {
    PluginUi::BeginCard("##cursor");
    Heading(ICON_FA_ARROW_POINTER, "Cursor");
    AutoMyAimConfig::Settings& s = m_settings;
    const LabelColumn col({"Max distance", "Offset radius"});

    if (Check("Keep the cursor near your character", "confine", &s.confineToCircle,
              "Moves the cursor towards the target but never further from your character than the distance "
              "below. Useful for skills that should land close to you.")) MarkDirty();
    ImGui::BeginDisabled(!s.confineToCircle);
    if (PluginUi::SliderIntRow(col, "Max distance", "##radius", &s.circleRadius, AutoMyAimConfig::kCircleRadiusMin,
                               AutoMyAimConfig::kCircleRadiusMax, "%d px",
                               "The farthest the cursor goes from your character, in screen pixels."))
        MarkDirty();
    ImGui::EndDisabled();

    if (Check("Add a small random offset", "randomize", &s.randomize,
              "Places the cursor a little off the monster's centre. The offset changes when the target "
              "changes and a few times a second, so the cursor does not shake.")) MarkDirty();
    ImGui::BeginDisabled(!s.randomize);
    if (PluginUi::SliderIntRow(col, "Offset radius", "##rand", &s.randomizeRadius, 0,
                               AutoMyAimConfig::kRandomizeRadiusMax, "%d px",
                               "The largest offset from the monster's centre, in screen pixels."))
        MarkDirty();
    ImGui::EndDisabled();
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionSafety() {
    const PluginUi::Palette pal = PluginUi::Colors();
    PluginUi::BeginCard("##safety");
    Heading(ICON_FA_SHIELD_HALVED, "Safety");
    AutoMyAimConfig::Settings& s = m_settings;
    if (Check("Pause while an item panel is open", "panel", &s.gatePanelOpen,
              "Stops moving the cursor while your inventory, a stash or a vendor window is open.")) MarkDirty();
    if (Check("Pause while Pick My Loot picks up items", "pick", &s.pauseWhilePicking,
              "Pick My Loot tells this plugin when it moves the cursor to pick something up, so the two never "
              "fight over the cursor.")) MarkDirty();
    // The pill goes on the checkbox's line when it fits, else under it.
    const char* pill = m_pickBusy.Valid() ? "Linked" : "Unavailable";
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < PluginUi::ButtonWidth(pill)) ImGui::NewLine();
    PluginUi::StatusPill(pill, U32(m_pickBusy.Valid() ? pal.Success : ReadableWarning(pal)));
    Tip(m_pickBusy.Valid() ? "The shared channel with Pick My Loot is ready.\n"
                             "It works whether or not Pick My Loot is installed."
                           : "The shared channel could not be opened, so pickups cannot pause aiming.");
    Hint("Aiming also always pauses while the POEFixer menu is open, during loading screens, while the game "
         "window is not in front, and while you are dead.");
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionDisplay() {
    PluginUi::BeginCard("##display");
    Heading(ICON_FA_EYE, "Display");
    AutoMyAimConfig::Settings& s = m_settings;
    const LabelColumn col({"Marker colour", "Status position", "Size", "Opacity", "Ring colour"});

    if (Check("Show status and target marker", "overlay", &s.showOverlay,
              "A small status chip on the screen and a marker on the monster being aimed at.")) MarkDirty();
    ImGui::BeginDisabled(!s.showOverlay);
    col.RowNoWidth("Marker colour");
    if (PluginUi::Swatch("##targetColor", s.targetColor)) MarkDirty();
    Tip("Colour of the marker on the monster being aimed at.");
    col.Row("Status position");
    if (PluginUi::Combo("##anchor", s.statusAnchor,
                        {"Top left", "Top centre", "Top right", "Bottom left", "Bottom centre", "Bottom right"}))
        MarkDirty();
    Tip("Where the status chip sits.\nIt stays below Pick My Loot's chip when both use the same corner.");
    ImGui::EndDisabled();

    int pct = static_cast<int>(s.overlayScale * 100.0f + 0.5f);
    if (PluginUi::SliderIntRow(col, "Size", "##scale", &pct, 70, 160, "%d%%",
                               "Size of the status chip, the marker and the ring line, relative to the POEFixer "
                               "font.")) {
        s.overlayScale = static_cast<float>(pct) / 100.0f;
        MarkDirty();
    }
    int op = static_cast<int>(s.overlayOpacity * 100.0f + 0.5f);
    if (PluginUi::SliderIntRow(col, "Opacity", "##opacity", &op, 30, 100, "%d%%", "Opacity of the status chip.")) {
        s.overlayOpacity = static_cast<float>(op) / 100.0f;
        MarkDirty();
    }

    if (Check("Show aim range ring", "ring", &s.showRangeCircle,
              "Draws the aim range on the ground around your character.")) MarkDirty();
    ImGui::BeginDisabled(!s.showRangeCircle);
    col.RowNoWidth("Ring colour");
    if (PluginUi::Swatch("##ringColor", s.rangeColor)) MarkDirty();
    Tip("Colour of the aim range ring.");
    ImGui::EndDisabled();

    ImGui::Spacing();
    const char* reset = ICON_FA_ROTATE_LEFT "  Reset display###resetDisplay";
    PluginUi::AlignRight(PluginUi::ButtonWidth(reset));
    if (PluginUi::SubtleButton(reset)) {
        const AutoMyAimConfig::Settings d;
        s.showOverlay = d.showOverlay;
        s.showRangeCircle = d.showRangeCircle;
        s.statusAnchor = d.statusAnchor;
        s.overlayScale = d.overlayScale;
        s.overlayOpacity = d.overlayOpacity;
        std::copy(std::begin(d.targetColor), std::end(d.targetColor), s.targetColor);
        std::copy(std::begin(d.rangeColor), std::end(d.rangeColor), s.rangeColor);
        MarkDirty();
    }
    Tip("Restores the display settings above to their defaults.");
    PluginUi::EndCard();
}

void AutoMyAimPlugin::DrawSectionDiagnostics() {
    const PluginUi::Palette pal = PluginUi::Colors();
    PluginUi::BeginCard("##diagnostics");
    ImGui::PushStyleColor(ImGuiCol_Text, pal.Accent);
    const bool open = ImGui::TreeNodeEx("##diagTree", ImGuiTreeNodeFlags_SpanAvailWidth, ICON_FA_BUG "  Diagnostics");
    ImGui::PopStyleColor();
    if (open) {
        if (Check("Show the target's flags and buffs in game", "debug", &m_settings.debugShowTarget,
                  "Prints the aimed monster's rarity, distance, targetable flags and buff names under it.\n"
                  "Use it to find the buff name of a monster that cannot be hit.")) MarkDirty();
        ImGui::BeginDisabled(!m_settings.debugShowTarget);
        ImGui::PushStyleColor(ImGuiCol_Text, pal.Success);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(m_targetDebug.empty() ? "(no target yet)" : m_targetDebug.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::Spacing();
        const AutoMyAim::AimFrame* f = FreshFrame();
        const unsigned long long nowMs = AutoMyAim::SteadyMs();
        const char* panel = !m_settings.gatePanelOpen ? "not checked"
                            : m_worker.PanelOpen(nowMs) ? "open or not read yet" : "closed";
        char line[200];
        std::snprintf(line, sizeof(line),
                      "Candidates in range: %d   |   Terrain for line of sight: %s   |   item panel: %s",
                      f ? f->candidates : 0, f && f->terrainLoaded ? "loaded" : "not loaded", panel);
        Hint(line);
        if (m_worker.Faults() > 0) {
            std::snprintf(line, sizeof(line), "Scan errors caught: %llu (each one skips a pass).",
                          static_cast<unsigned long long>(m_worker.Faults()));
            Hint(line);
        }
        ImGui::TreePop();
    }
    PluginUi::EndCard();
}

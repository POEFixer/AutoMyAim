// Settings.h - Auto My Aim configuration: defaults, ranges, load and save.
// File: <plugin folder>/config/settings.json, the same file and keys the
// original plugin used, so existing settings carry over. Unknown or malformed
// values fall back to the default one field at a time; saving is atomic. A file
// that cannot be read or parsed is never overwritten behind the user's back.
#pragma once

#include "../common/FileUtil.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace AutoMyAimConfig {

inline constexpr int kAimRangeMin = 20;
inline constexpr int kAimRangeMax = 200;
inline constexpr int kCircleRadiusMin = 50;
inline constexpr int kCircleRadiusMax = 1200;
inline constexpr int kRandomizeRadiusMax = 60;
inline constexpr int kLosBlockBelowMax = 5;
// Target updates faster than the game's own data (about 60 per second) gain
// nothing; an old saved 0 ("every frame") loads as this minimum.
inline constexpr int kActionIntervalMinMs = 16;
inline constexpr int kActionIntervalMaxMs = 200;
inline constexpr float kDistanceWeightMax = 50.0f;
inline constexpr float kRarityWeightMax = 100.0f;
inline constexpr float kHpWeightMax = 100.0f;
inline constexpr size_t kBuffBlacklistMax = 512;

struct Settings {
    // Keys (virtual-key codes; 0 = not set).
    int  aimKey = 0x05;      // Mouse 4
    int  toggleKey = 0x75;   // F6
    int  actionIntervalMs = kActionIntervalMinMs;

    // Targeting.
    int  aimRange = 100;
    bool useLineOfSight = true;
    int  losBlockBelow = 1;
    bool skipBlacklistedBuffs = false;
    std::string buffBlacklist = "hidden_monster, frozen_in_time";

    // Priority.
    float distanceWeight = 1.0f;
    bool  rarityWeighting = true;
    float wNormal = 0.0f, wMagic = 5.0f, wRare = 20.0f, wUnique = 40.0f;
    bool  hpWeighting = false;
    bool  preferHigherHp = false;
    float hpWeight = 10.0f;

    // Cursor.
    bool confineToCircle = false;
    int  circleRadius = 400;
    bool randomize = false;
    int  randomizeRadius = 8;

    // Safety.
    bool gatePanelOpen = true;
    bool pauseWhilePicking = true;

    // Display.
    bool  showOverlay = true;        // status chip + target marker
    bool  showRangeCircle = false;
    int   statusAnchor = 0;          // PfOverlay::Anchor
    float overlayScale = 1.0f;       // 0.7 .. 1.6
    float overlayOpacity = 0.9f;     // 0.3 .. 1.0
    float targetColor[4] = {0.95f, 0.25f, 0.25f, 1.0f};
    float rangeColor[4]  = {0.30f, 0.60f, 1.00f, 1.0f};

    // Diagnostics.
    bool debugShowTarget = false;

    static std::filesystem::path SettingsPath(const std::filesystem::path& dir) {
        return dir / "config" / "settings.json";
    }

    void Clamp() {
        aimKey = std::clamp(aimKey, 0, 254);
        toggleKey = std::clamp(toggleKey, 0, 254);
        actionIntervalMs = std::clamp(actionIntervalMs, kActionIntervalMinMs, kActionIntervalMaxMs);
        aimRange = std::clamp(aimRange, kAimRangeMin, kAimRangeMax);
        losBlockBelow = std::clamp(losBlockBelow, 0, kLosBlockBelowMax);
        if (buffBlacklist.size() > kBuffBlacklistMax) buffBlacklist.resize(kBuffBlacklistMax);
        distanceWeight = std::clamp(distanceWeight, 0.0f, kDistanceWeightMax);
        wNormal = std::clamp(wNormal, 0.0f, kRarityWeightMax);
        wMagic = std::clamp(wMagic, 0.0f, kRarityWeightMax);
        wRare = std::clamp(wRare, 0.0f, kRarityWeightMax);
        wUnique = std::clamp(wUnique, 0.0f, kRarityWeightMax);
        hpWeight = std::clamp(hpWeight, 0.0f, kHpWeightMax);
        circleRadius = std::clamp(circleRadius, kCircleRadiusMin, kCircleRadiusMax);
        randomizeRadius = std::clamp(randomizeRadius, 0, kRandomizeRadiusMax);
        statusAnchor = std::clamp(statusAnchor, 0, 5);
        overlayScale = std::clamp(overlayScale, 0.7f, 1.6f);
        overlayOpacity = std::clamp(overlayOpacity, 0.3f, 1.0f);
    }

    // A file that exists but cannot be read or parsed leaves the defaults in
    // place and is reported (Unreadable / Corrupt, the latter also copied to
    // settings.json.corrupt) so the caller can hold its writes (PfIo::SaveGuard).
    PfIo::LoadStatus Load(const std::filesystem::path& dir) {
        const std::filesystem::path path = SettingsPath(dir);
        std::string text;
        const PfIo::ReadStatus rs = PfIo::ReadTextFileChecked(path, text);
        if (rs == PfIo::ReadStatus::Missing) return PfIo::LoadStatus::Missing;
        if (rs == PfIo::ReadStatus::Unreadable) return PfIo::LoadStatus::Unreadable;
        const nlohmann::json j = PfIo::ParseJson(text);
        if (!j.is_object()) {
            PfIo::KeepCorruptCopy(path);
            return PfIo::LoadStatus::Corrupt;
        }
        using PfIo::JsonGet;
        aimKey = JsonGet(j, "aim_key", aimKey);
        toggleKey = JsonGet(j, "toggle_key", toggleKey);
        actionIntervalMs = JsonGet(j, "action_interval_ms", actionIntervalMs);
        aimRange = JsonGet(j, "aim_range", aimRange);
        debugShowTarget = JsonGet(j, "debug_show_target", debugShowTarget);
        skipBlacklistedBuffs = JsonGet(j, "skip_blacklisted_buffs", skipBlacklistedBuffs);
        buffBlacklist = JsonGet(j, "buff_blacklist", buffBlacklist);
        distanceWeight = JsonGet(j, "distance_weight", distanceWeight);
        rarityWeighting = JsonGet(j, "rarity_weighting", rarityWeighting);
        wNormal = JsonGet(j, "w_normal", wNormal);
        wMagic = JsonGet(j, "w_magic", wMagic);
        wRare = JsonGet(j, "w_rare", wRare);
        wUnique = JsonGet(j, "w_unique", wUnique);
        hpWeighting = JsonGet(j, "hp_weighting", hpWeighting);
        preferHigherHp = JsonGet(j, "prefer_higher_hp", preferHigherHp);
        hpWeight = JsonGet(j, "hp_weight", hpWeight);
        useLineOfSight = JsonGet(j, "use_line_of_sight", useLineOfSight);
        losBlockBelow = JsonGet(j, "los_block_below", losBlockBelow);
        confineToCircle = JsonGet(j, "confine_to_circle", confineToCircle);
        circleRadius = JsonGet(j, "circle_radius", circleRadius);
        randomize = JsonGet(j, "randomize", randomize);
        randomizeRadius = JsonGet(j, "randomize_radius", randomizeRadius);
        gatePanelOpen = JsonGet(j, "gate_panel_open", gatePanelOpen);
        pauseWhilePicking = JsonGet(j, "pause_while_picking", pauseWhilePicking);
        showOverlay = JsonGet(j, "show_overlay", showOverlay);
        showRangeCircle = JsonGet(j, "show_range_circle", showRangeCircle);
        statusAnchor = JsonGet(j, "status_anchor", statusAnchor);
        overlayScale = JsonGet(j, "overlay_scale", overlayScale);
        overlayOpacity = JsonGet(j, "overlay_opacity", overlayOpacity);
        const auto colors = j.find("colors");
        if (colors != j.end() && colors->is_object()) {
            if (colors->contains("target")) PfIo::ColorFromJson((*colors)["target"], targetColor);
            if (colors->contains("range")) PfIo::ColorFromJson((*colors)["range"], rangeColor);
        }
        Clamp();
        return PfIo::LoadStatus::Loaded;
    }

    bool Save(const std::filesystem::path& dir) const {
        if (dir.empty()) return false;
        nlohmann::json j;
        j["aim_key"] = aimKey;
        j["toggle_key"] = toggleKey;
        j["action_interval_ms"] = actionIntervalMs;
        j["aim_range"] = aimRange;
        j["debug_show_target"] = debugShowTarget;
        j["skip_blacklisted_buffs"] = skipBlacklistedBuffs;
        j["buff_blacklist"] = buffBlacklist;
        j["distance_weight"] = distanceWeight;
        j["rarity_weighting"] = rarityWeighting;
        j["w_normal"] = wNormal;
        j["w_magic"] = wMagic;
        j["w_rare"] = wRare;
        j["w_unique"] = wUnique;
        j["hp_weighting"] = hpWeighting;
        j["prefer_higher_hp"] = preferHigherHp;
        j["hp_weight"] = hpWeight;
        j["use_line_of_sight"] = useLineOfSight;
        j["los_block_below"] = losBlockBelow;
        j["confine_to_circle"] = confineToCircle;
        j["circle_radius"] = circleRadius;
        j["randomize"] = randomize;
        j["randomize_radius"] = randomizeRadius;
        j["gate_panel_open"] = gatePanelOpen;
        j["pause_while_picking"] = pauseWhilePicking;
        j["show_overlay"] = showOverlay;
        j["show_range_circle"] = showRangeCircle;
        j["status_anchor"] = statusAnchor;
        j["overlay_scale"] = overlayScale;
        j["overlay_opacity"] = overlayOpacity;
        j["colors"] = {{"target", PfIo::ColorToJson(targetColor)}, {"range", PfIo::ColorToJson(rangeColor)}};
        return PfIo::WriteTextFileAtomic(SettingsPath(dir), PfIo::DumpJson(j));
    }
};

}  // namespace AutoMyAimConfig

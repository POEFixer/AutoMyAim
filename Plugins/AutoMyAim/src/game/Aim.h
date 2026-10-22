// Aim.h - target selection. Works on a monster-only snapshot (the host filters
// entities before copying their strings), weights every hostile monster in
// range, and returns the best one that is on screen, in line of sight and not
// carrying a blacklisted buff.
#pragma once

#include "../../../../POEFixer/plugin_sdk/PluginSDK.h"

#include "../common/GameGates.h"
#include "../config/Settings.h"
#include "AimTypes.h"
#include "RayCaster.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace AutoMyAim {

// Buff names compare with case and whitespace ignored ("Frozen_In_Time " ==
// "frozen_in_time"); underscores stay significant.
inline std::string NormalizeToken(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (unsigned char c : raw) {
        if (std::isspace(c)) continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

// "a, b ,c" -> {"a","b","c"} (normalized, empties dropped, duplicates kept once).
inline std::vector<std::string> ParseBlacklist(const std::string& src) {
    std::vector<std::string> out;
    std::string token;
    auto flush = [&] {
        std::string t = NormalizeToken(token);
        token.clear();
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(std::move(t));
    };
    for (char c : src) {
        if (c == ',' || c == ';' || c == '\n') flush();
        else token.push_back(c);
    }
    flush();
    return out;
}

// Weight of one monster: closeness (0..1 from the edge of the range to the
// player) squared times the distance weight, plus rarity and life bonuses that
// also fade with distance, so a far rare never beats a monster in your face by
// much unless you ask for it.
inline float TargetWeight(const AutoMyAimConfig::Settings& s, float dist, float maxDist, int rarity,
                          int hp, int maxHp, int es, int maxEs) {
    const float raw = maxDist > 0.f ? 1.f - dist / maxDist : 0.f;
    const float df = raw < 0.f ? 0.f : raw;
    float w = df * df * s.distanceWeight;
    if (s.rarityWeighting) {
        const float rw = rarity == 1 ? s.wMagic : rarity == 2 ? s.wRare : rarity == 3 ? s.wUnique : s.wNormal;
        w += rw * df;
    }
    if (s.hpWeighting) {
        const float total = static_cast<float>(maxHp) + static_cast<float>(maxEs);
        if (total > 0.f) {
            const float cur = std::clamp((static_cast<float>(hp) + static_cast<float>(es)) / total, 0.f, 1.f);
            w += (s.preferHigherHp ? cur : 1.f - cur) * s.hpWeight * df;
        }
    }
    return w;
}

class AimEngine {
public:
    bool  HavePlayer() const { return m_havePlayer; }
    float PlayerWorldX() const { return m_pwx; }
    float PlayerWorldY() const { return m_pwy; }
    float PlayerWorldZ() const { return m_pwz; }
    int   Candidates() const { return static_cast<int>(m_targets.size()); }

    void ClearCaches() {
        m_buffMemo.clear();
        m_targets.clear();
    }

    // snap must hold the player and the monsters (GetSnapshot(EntityType::Monster)).
    const Target* Compute(const PluginSDK::Context* ctx, const PluginSDK::Snapshot& snap,
                          const AutoMyAimConfig::Settings& s, const RayCaster& rc,
                          float dispW, float dispH, unsigned long long nowMs) {
        m_targets.clear();
        m_havePlayer = false;
        if (!ctx || !snap.Player.IsValid) return nullptr;
        m_havePlayer = true;
        m_pwx = snap.Player.WorldX;
        m_pwy = snap.Player.WorldY;
        m_pwz = snap.Player.WorldZ;

        const float px = snap.Player.GridPositionX;
        const float py = snap.Player.GridPositionY;
        const int pxi = static_cast<int>(px);
        const int pyi = static_cast<int>(py);
        const float range = std::max(1.f, static_cast<float>(s.aimRange));
        const float range2 = range * range;

        for (const PluginSDK::Entity& e : snap.Entities) {
            if (!PfGates::IsHostileMonster(e)) continue;
            const float dx = e.GridPositionX - px;
            const float dy = e.GridPositionY - py;
            const float d2 = dx * dx + dy * dy;
            if (d2 > range2) continue;
            const float dist = std::sqrt(d2);

            float sx = 0.f, sy = 0.f;
            if (!ctx->Render.WorldToScreen(e.WorldX, e.WorldY, e.WorldZ, sx, sy)) continue;
            if (!(sx >= 0.f && sy >= 0.f && sx < dispW && sy < dispH)) continue;

            Target t;
            t.id = e.Id;
            t.gridX = e.GridPositionX;
            t.gridY = e.GridPositionY;
            t.worldX = e.WorldX;
            t.worldY = e.WorldY;
            t.worldZ = e.WorldZ;
            t.screenX = sx;
            t.screenY = sy;
            t.onScreen = true;
            t.distance = dist;
            t.rarity = e.Rarity;
            t.buffsAddr = e.Components.Buffs;
            t.targetableAddr = e.Components.Targetable;
            t.weight = TargetWeight(s, dist, range, e.Rarity, e.CurrentHP, e.MaxHP, e.CurrentES, e.MaxES);
            m_targets.push_back(t);
        }
        if (m_targets.empty()) return nullptr;
        std::sort(m_targets.begin(), m_targets.end(),
                  [](const Target& a, const Target& b) { return a.weight > b.weight; });

        if (s.skipBlacklistedBuffs && s.buffBlacklist != m_blacklistSrc) {
            m_blacklistSrc = s.buffBlacklist;
            m_blacklist = ParseBlacklist(m_blacklistSrc);
            m_buffMemo.clear();
        }
        const bool useBlacklist = s.skipBlacklistedBuffs && !m_blacklist.empty();
        if (m_buffMemo.size() > 512) m_buffMemo.clear();

        // Line of sight and buffs are checked best-first and only until a
        // target passes, so a big pack costs a handful of checks, not one each.
        int checks = 0;
        for (const Target& t : m_targets) {
            if (++checks > kMaxChecksPerPass) break;  // a pathological pack: try again next tick
            if (s.useLineOfSight &&
                !rc.HasLineOfSight(pxi, pyi, static_cast<int>(t.gridX), static_cast<int>(t.gridY), s.losBlockBelow))
                continue;
            if (useBlacklist && HasBlacklistedBuff(ctx, t, nowMs)) continue;
            return &t;
        }
        return nullptr;
    }

private:
    static constexpr int kMaxChecksPerPass = 64;
    static constexpr unsigned long long kBuffMemoMs = 400;

    struct BuffMemo {
        uintptr_t buffsAddr = 0;
        unsigned long long at = 0;
        bool blocked = false;
    };

    bool HasBlacklistedBuff(const PluginSDK::Context* ctx, const Target& t, unsigned long long nowMs) {
        if (!t.buffsAddr) return false;
        BuffMemo& m = m_buffMemo[t.id];
        if (m.buffsAddr == t.buffsAddr && m.at != 0 && nowMs - m.at < kBuffMemoMs) return m.blocked;
        m.buffsAddr = t.buffsAddr;
        m.at = nowMs;
        m.blocked = false;
        for (const auto& b : ctx->Components.EnumerateBuffs(t.buffsAddr)) {
            if (b.Name.empty()) continue;
            const std::string name = NormalizeToken(b.Name);
            if (std::find(m_blacklist.begin(), m_blacklist.end(), name) != m_blacklist.end()) {
                m.blocked = true;
                break;
            }
        }
        return m.blocked;
    }

    std::vector<Target> m_targets;
    std::string m_blacklistSrc;
    std::vector<std::string> m_blacklist;
    std::unordered_map<uint32_t, BuffMemo> m_buffMemo;
    bool  m_havePlayer = false;
    float m_pwx = 0.f, m_pwy = 0.f, m_pwz = 0.f;
};

}  // namespace AutoMyAim

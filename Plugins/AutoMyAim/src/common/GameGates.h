// GameGates.h - game-state checks shared by Auto My Aim and Pick My Loot (the
// two copies are identical).
#pragma once

#include "../../../../POEFixer/plugin_sdk/PluginSDK.h"

#include <string>

namespace PfGates {

// Grid cells -> world units (TileToWorld 250 / TileToGrid 23).
inline constexpr float kGridToWorld = 250.0f / 23.0f;

inline bool IsServerModEntity(const std::wstring& path) {
    static constexpr wchar_t kPrefix[] = L"Metadata/Monsters/MonsterMods/";
    constexpr size_t n = sizeof(kPrefix) / sizeof(kPrefix[0]) - 1;
    return path.size() >= n && path.compare(0, n, kPrefix) == 0;
}

// A living hostile monster that can actually be fought: not friendly, not a
// hidden pinnacle boss, not a "useless" helper entity and not a server-side
// monster-mod carrier.
inline bool IsHostileMonster(const PluginSDK::Entity& e) {
    if (!e.IsValid) return false;
    if (e.EntityType != PluginSDK::EntityType::Monster) return false;
    if (e.CurrentHP <= 0) return false;
    if (e.EntityState == PluginSDK::EntityState::MonsterFriendly) return false;
    if (e.EntityState == PluginSDK::EntityState::PinnacleBossHidden) return false;
    if (e.EntityState == PluginSDK::EntityState::Useless) return false;
    if (IsServerModEntity(e.Path)) return false;
    return true;
}

inline bool PlayerDead(const PluginSDK::Snapshot& snap) {
    if (snap.Player.IsValid && snap.Player.MaxHP > 0 && snap.Player.CurrentHP <= 0) return true;
    return snap.Vitals.IsValid && snap.Vitals.MaxHP > 0 && snap.Vitals.CurrentHP <= 0;
}

// An item panel (inventory, stash, vendor, ...) is open: a visible item grid of
// at least 40 cells. Enumerates the host's inventories (with their items: not
// for the render thread). The host refreshes that listing only when a scan is
// requested (Inventory.Scan), so call it through PanelGate.h, which requests
// the scans and treats a listing that is not fresh as open.
inline bool AnyItemPanelOpen(const PluginSDK::Context* ctx) {
    if (!ctx) return false;
    for (const auto& inv : ctx->Inventory.GetAll()) {
        if (!inv.Grid.Valid) continue;
        if (inv.TotalBoxesX <= 0 || inv.TotalBoxesY <= 0) continue;
        if (inv.TotalBoxesX * inv.TotalBoxesY < 40) continue;
        return true;
    }
    return false;
}

}  // namespace PfGates

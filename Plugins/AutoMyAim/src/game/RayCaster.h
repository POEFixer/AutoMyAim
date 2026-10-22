// RayCaster.h - line of sight over the host's packed walkable grid
// (two cells per byte, even x = low nibble, row = width / 2 bytes).
#pragma once

#include "../../../../POEFixer/plugin_sdk/PluginSDK.h"

#include <cstdint>
#include <cstdlib>

namespace AutoMyAim {

class RayCaster {
public:
    // Picks up a new grid when the host swapped it. The host parses the new
    // area's grid some time after the area-change event, so the handle is
    // polled (a cheap pointer compare) instead of trusted at the event.
    void Poll(const PluginSDK::Context* ctx) {
        if (!ctx) return;
        PluginSDK::WalkableGridHandle cur = ctx->Terrain.GetWalkableGrid();
        if (cur.Data() == m_grid.Data() && cur.Width() == m_w && cur.Height() == m_h) return;
        m_grid = std::move(cur);
        m_w = m_grid.Width();
        m_h = m_grid.Height();
    }

    void Reset() {
        m_grid.Reset();
        m_w = m_h = 0;
    }

    bool Valid() const { return m_grid.Valid() && m_w > 0 && m_h > 0; }

    int Cell(int x, int y) const {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h) return 0;
        const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_w) + static_cast<size_t>(x);
        const uint8_t b = m_grid.Data()[idx >> 1];
        return (idx & 1) ? ((b >> 4) & 0x0F) : (b & 0x0F);
    }

    // Bresenham walk between the two cells (end points excluded): a cell whose
    // walkable level is below blockBelow blocks the line.
    bool HasLineOfSight(int x0, int y0, int x1, int y1, int blockBelow) const {
        if (!Valid() || blockBelow <= 0) return true;
        const int dx = std::abs(x1 - x0);
        const int dy = -std::abs(y1 - y0);
        const int sx = x0 < x1 ? 1 : -1;
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        int x = x0, y = y0;
        for (int guard = 0; guard < 4096; ++guard) {
            if (x == x1 && y == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x += sx; }
            if (e2 <= dx) { err += dx; y += sy; }
            if (x == x1 && y == y1) break;
            if (Cell(x, y) < blockBelow) return false;
        }
        return true;
    }

private:
    PluginSDK::WalkableGridHandle m_grid;
    int m_w = 0;
    int m_h = 0;
};

}  // namespace AutoMyAim

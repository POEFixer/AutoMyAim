#pragma once

#include <cstdint>

namespace AutoMyAim {

struct Target {
    uint32_t  id = 0;
    float     gridX = 0.f, gridY = 0.f;
    float     worldX = 0.f, worldY = 0.f, worldZ = 0.f;
    float     screenX = 0.f, screenY = 0.f;
    float     distance = 0.f;
    float     weight = 0.f;
    int       rarity = 0;
    bool      onScreen = false;
    uintptr_t buffsAddr = 0;
    uintptr_t targetableAddr = 0;
};

}  // namespace AutoMyAim

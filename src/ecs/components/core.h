#pragma once

#include "core/tile_map.h"
#include "core/types.h"
#include <cstdint>
#include <type_traits>

namespace ecs {

struct Position {
    TilePosition tile{INVALID_POS};
};

struct PreviousPosition {
    TilePosition tile{INVALID_POS};
};

struct VisualPos {
    float x = 0.0f;
    float y = 0.0f;
};

struct Health {
    std::int32_t current = 100;
    std::int32_t max = 100;
    
    [[nodiscard]] bool is_alive() const noexcept { return current > 0; }
    [[nodiscard]] float ratio() const noexcept { 
        return max > 0 ? static_cast<float>(current) / static_cast<float>(max) : 0.0f; 
    }
};

struct Speed {
    double base = 1.0;
    double progress = 0.0;
};

struct Active {};

struct Dead {};

struct FactionMember {
    FactionID faction = FactionID::Neutral;
};

// Target: components < 64 bytes for cache line efficiency
static_assert(sizeof(Position) <= 8, "Position too large");
static_assert(sizeof(PreviousPosition) <= 8, "PreviousPosition too large");
static_assert(sizeof(VisualPos) <= 8, "VisualPos too large");
static_assert(sizeof(Health) <= 8, "Health too large");
static_assert(sizeof(Speed) <= 16, "Speed too large");
static_assert(sizeof(FactionMember) <= 4, "FactionMember too large");

// EnTT optimizes empty types to use zero storage - verify tags are truly empty
static_assert(std::is_empty_v<Active>, "Active should be empty tag");
static_assert(std::is_empty_v<Dead>, "Dead should be empty tag");

} // namespace ecs

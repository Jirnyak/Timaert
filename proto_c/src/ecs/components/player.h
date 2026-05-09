#pragma once

#include "core/types.h"
#include "core/tile_map.h"
#include <cstdint>

namespace ecs {

struct PlayerTag {};

struct PlayerStateComponent {
    PlayerState state = PlayerState::Normal;
};

struct PlayerAim {
    TilePosition target = INVALID_POS;
    
    [[nodiscard]] bool has_aim() const noexcept { return is_valid(target); }
    void clear() noexcept { target = INVALID_POS; }
};

struct Reputation {
    static constexpr std::size_t FACTION_COUNT = static_cast<std::size_t>(FactionID::Count);
    std::int32_t values[FACTION_COUNT]{};
    
    [[nodiscard]] std::int32_t get(FactionID faction) const noexcept {
        return values[static_cast<std::size_t>(faction)];
    }
    
    void modify(FactionID faction, std::int32_t delta) noexcept {
        values[static_cast<std::size_t>(faction)] += delta;
    }
};

} // namespace ecs

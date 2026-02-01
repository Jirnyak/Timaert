#pragma once

#include "core/types.h"

namespace ecs {

struct ObjectSprite {
    ObjectType type = ObjectType::Tree;
};

// Large 256x256 landmark sprite - treats as 4x4 tiles (64px each)
// Inner 2x2 tiles (128x128) are the "inside" of the landmark
// Outer border is 64px wide on all sides
struct LandmarkSprite {
    ObjectType type = ObjectType::City;
};

struct EntityStateComponent {
    EntityState state = EntityState::Default;
};

struct AimTarget {
    std::int32_t target_id = -1;
};

}  // namespace ecs

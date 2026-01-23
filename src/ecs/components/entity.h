#pragma once

#include "core/types.h"

namespace ecs {

struct ObjectSprite {
    ObjectType type = ObjectType::Tree;
};

struct EntityStateComponent {
    EntityState state = EntityState::Default;
};

struct AimTarget {
    std::int32_t target_id = -1;
};

} // namespace ecs

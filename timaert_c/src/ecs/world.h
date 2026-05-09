// EnTT registry wrapper — single source of truth for runtime entities.
#pragma once
#include <entt/entt.hpp>
#include "ecs/components.h"

namespace sm::ecs {

struct World {
    entt::registry reg;

    entt::entity create() { return reg.create(); }
    void destroy(entt::entity e) { reg.destroy(e); }
};

} // namespace sm::ecs

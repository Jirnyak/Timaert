// EnTT registry wrapper — single source of truth for runtime entities.
#pragma once
#include <entt/entt.hpp>
#include <utility>
#include "ecs/components.h"

namespace sm {
struct MacroStore;
}

namespace sm::ecs {

struct World {
    entt::registry reg;

    entt::entity create() { return reg.create(); }
    void destroy(entt::entity e) { reg.destroy(e); }
};

} // namespace sm::ecs

namespace sm {

// store_attach/store_of — ctx-мост store для СУБМИРА (вердикт 4а): живёт до
// M-171 (фрейм) и умирает вместе с App::ecs (M-150). Переехал сюда из
// store.h (M-150 шаг 0): store.h включает 71 файл, и мост был единственным,
// ради чего все они тянули entt. MacroStore здесь — forward-объявление:
// мост хранит и отдаёт только указатель, полный тип ему не нужен.
inline void store_attach(ecs::World& w, MacroStore* st) {
    w.reg.ctx().insert_or_assign(std::move(st));
}
inline MacroStore& store_of(ecs::World& w) {
    return *w.reg.ctx().get<MacroStore*>();
}
inline const MacroStore& store_of(const ecs::World& w) {
    return *w.reg.ctx().get<MacroStore*>();
}
inline MacroStore& store_of(entt::registry& reg) {
    return *reg.ctx().get<MacroStore*>();
}

} // namespace sm

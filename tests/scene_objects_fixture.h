// Арена объектов сцены ДЛЯ ФИКСТУР (M-150 ломоть 1а): свидетель обязан САМ
// рожать своё предусловие (§8 п.11) — телу, рождённому мимо двери спавна
// (голым reg.create в фикстуре), слот единого массива выдаёт эта обвязка.
// Чтение колонок — те же выражения, что у продакшена, никакой второй копии
// закона: helpers лишь избавляют тесты от повторения try_get-цепочки.
#pragma once

#include <memory>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components.h"
#include "sub/record.h"   // objects_attach / objects_of / objects_find

namespace sm::test {

// Носители живут до конца процесса: реестров у тестов много, арена — на
// каждый, по требованию.
inline std::vector<std::unique_ptr<sm::sub::SubObjects>>& scene_arenas() {
    static std::vector<std::unique_ptr<sm::sub::SubObjects>> arenas;
    return arenas;
}

inline sm::sub::SubObjects& arena_of(entt::registry& reg) {
    if (sm::sub::SubObjects* o = sm::sub::objects_find(reg)) return *o;
    scene_arenas().push_back(std::make_unique<sm::sub::SubObjects>());
    sm::sub::objects_attach(reg, scene_arenas().back().get());
    return *scene_arenas().back();
}

// Слот телу фикстуры — как выдала бы дверь спавна.
inline void give_slot(entt::registry& reg, entt::entity e) {
    const int slot = arena_of(reg).alloc();
    if (slot >= 0) reg.emplace<sm::ecs::ObjectSlot>(e, std::uint16_t(slot));
}

inline std::uint8_t fx_of(entt::registry& reg, entt::entity e) {
    const auto* os = reg.try_get<sm::ecs::ObjectSlot>(e);
    return os ? arena_of(reg).damageFx[os->slot] : std::uint8_t{0};
}
inline std::uint32_t last_hit_of(entt::registry& reg, entt::entity e) {
    const auto* os = reg.try_get<sm::ecs::ObjectSlot>(e);
    return os ? arena_of(reg).lastHitBy[os->slot] : sm::sub::kObjNoAttacker;
}

} // namespace sm::test

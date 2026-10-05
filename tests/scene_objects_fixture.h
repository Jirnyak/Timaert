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

// Бит маски телу фикстуры — как поставила бы боевая дверь; слот рожается
// по надобности (свидетель сам рожает предусловие, §8 п.11).
inline void give_flag(entt::registry& reg, entt::entity e,
                      std::uint16_t bit) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::object_flag_set(reg, e, bit);
}
inline bool flag_of(entt::registry& reg, entt::entity e, std::uint16_t bit) {
    return sm::sub::object_flag(reg, e, bit);
}
// Активное тело фикстуре — как поставила бы дверь входа (ссылка сцены,
// вердикт 2026-10-05): слот по надобности + перезапись ссылки.
inline void make_avatar(entt::registry& reg, entt::entity e) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_avatar(reg, e);
}

// Род/уровень телу фикстуры — как записала бы дверь рождения (кусок 1);
// слот рожается по надобности (§8 п.11): set_body_kind на бесслотном теле
// ушёл бы в no-op МОЛЧА — ровно шрам ломтя 1б.
inline void give_kind(entt::registry& reg, entt::entity e,
                      sm::ecs::NPCKind k) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_kind(reg, e, k);
}
inline void give_level(entt::registry& reg, entt::entity e, std::int16_t v) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_level(reg, e, v);
}
// Бары/лист/снарядные — колонки арены (кусок 2), слот по надобности.
inline void give_pools(entt::registry& reg, entt::entity e, sm::ecs::Pools p) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_pools(reg, e, p);
}
inline void give_combat(entt::registry& reg, entt::entity e,
                        const sm::ecs::Combat& c) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_combat(reg, e, c);
}
inline void give_missile(entt::registry& reg, entt::entity e,
                         sm::ecs::MissileAttack m) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_missile(reg, e, m);
}
inline void give_ai(entt::registry& reg, entt::entity e,
                    const sm::ecs::SubworldAi& a) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_ai(reg, e, a);
}
inline void give_visual(entt::registry& reg, entt::entity e,
                        sm::ecs::VisualPos v) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_visual(reg, e, v);
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

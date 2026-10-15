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

// ШИРОКАЯ ФАЗА ДЛЯ СВИДЕТЕЛЕЙ (§8 п.11: предусловие рождает сам свидетель).
// Двери удара и спелла требуют широкую фазу БЕЗУСЛОВНО — ветки «сетки не
// дали» у них больше нет (M-261), потому что полный проход капа в симуляции
// запрещён границей O(N) (§6). Свидетелю, который проверяет урон, а не
// сетку, настоящая `UnitGrid` не нужна: контракт двери — СУПЕРСЕТ, а самый
// тривиальный суперсет есть «все живые слоты». Точные гейты (род, фракция,
// 3D-дистанция) стоят у потребителя и работают те же, что в игре.
//
// Да, обход здесь по всему капу — и это законно именно потому, что он
// ПЕРЕЕХАЛ ИЗ СИМУЛЯЦИИ В ПРИБОР: тик игры его больше не содержит, а
// свидетель тиком не является (§8 п.8 — приоритет игра, тест прибор).
// Настоящую `UnitGrid` держит и сверяет с этим перечислителем
// `spell_broadphase_parity_test` — там она и есть предмет проверки.
// `user` — АРЕНА (`&arena_of(reg)`), подпись годится обеим дверям разом.
inline std::uint32_t all_live_slots(void* user, float, float, float,
                                    std::uint16_t (&out)[sm::kMaxSubObjects]) {
    const auto* objs = static_cast<const sm::sub::SubObjects*>(user);
    std::uint32_t n = 0;
    for (std::size_t s = 0; s < sm::kMaxSubObjects; ++s) {
        if (!sm::sub::slot_flag(*objs, int(s), sm::sub::kObjAlive)) continue;
        out[n++] = std::uint16_t(s);
    }
    return n;
}

// Слот телу фикстуры — как выдала бы дверь спавна.
inline void give_slot(entt::registry& reg, entt::entity e) {
    const int slot = arena_of(reg).alloc(sm::ecs::Position{});
    if (slot >= 0) reg.emplace<sm::ecs::ObjectSlot>(e, std::uint16_t(slot));
}

// Позиция телу фикстуры (ломоть 5) — как записало бы рождение: слот
// рожается по надобности (§8 п.11), колонка пишется дверью body_pos.
inline void give_pos(entt::registry& reg, entt::entity e,
                     float x, float y, float z = 0.0f) {
    if (!reg.all_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    if (sm::ecs::Position* p = sm::sub::body_pos(reg, e)) {
        *p = sm::ecs::Position{x, y, z};
    }
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
// Бэклинк телу фикстуры — как записала бы дверь tracked-рождения (ломоть 4),
// слот по надобности. Снятие — give_origin с MacroHandle{} (бита нет,
// «записи нет» = невалидный хэндл в самой колонке).
inline void give_origin(entt::registry& reg, entt::entity e,
                        sm::MacroHandle h) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_origin(reg, e, h);
}
// Вид телу фикстуры — как записала бы дверь рождения (ломоть 3), слот по
// надобности.
inline void give_sprite(entt::registry& reg, entt::entity e,
                        const sm::ecs::Sprite& sp) {
    if (!reg.any_of<sm::ecs::ObjectSlot>(e)) give_slot(reg, e);
    sm::sub::set_body_sprite(reg, e, sp);
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
// Снаряд-фикстура (ступень (v) ломтя 7): ЧИСТЫЙ ЖИЛЕЦ АРЕНЫ — слот без
// сущности, ровно той же дверью, что стреляет продакшен (birth_projectile).
// Арена рожается по надобности — закон «свидетель рожает своё
// предусловие» (§8 п.11). Возврат — слот (-1 = кап).
inline int give_projectile(entt::registry& reg,
                           const sm::ecs::Projectile& p,
                           const sm::ecs::Position& at) {
    return sm::sub::birth_projectile(arena_of(reg), p, at);
}

inline std::uint8_t fx_of(entt::registry& reg, entt::entity e) {
    const auto* os = reg.try_get<sm::ecs::ObjectSlot>(e);
    return os ? arena_of(reg).damageFx[os->slot] : std::uint8_t{0};
}
inline sm::sub::ObjRef last_hit_of(entt::registry& reg, entt::entity e) {
    const auto* os = reg.try_get<sm::ecs::ObjectSlot>(e);
    return os ? arena_of(reg).lastHitBy[os->slot] : sm::sub::ObjRef{};
}

} // namespace sm::test

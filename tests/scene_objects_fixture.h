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

// Тело фикстуры — СЛОТ арены (ломоть D: сущностей у тел больше нет).
// Рождение — как выдала бы дверь спавна; -1 = кап. Все give_* ниже берут
// СЛОТ и пишут колонку той же слот-дверью, что продакшен.
inline int give_body(entt::registry& reg) {
    return arena_of(reg).alloc(sm::ecs::Position{});
}

// Позиция телу фикстуры (ломоть 5) — как записало бы рождение.
inline void give_pos(entt::registry& reg, int s,
                     float x, float y, float z = 0.0f) {
    if (s >= 0) {
        arena_of(reg).pos[std::size_t(s)] = sm::ecs::Position{x, y, z};
    }
}

// Бит маски телу фикстуры — как поставила бы боевая дверь.
inline void give_flag(entt::registry& reg, int s, std::uint16_t bit) {
    if (s >= 0) sm::sub::slot_flag_set(arena_of(reg), s, bit);
}
inline bool flag_of(entt::registry& reg, int s, std::uint16_t bit) {
    return s >= 0 && sm::sub::slot_flag(arena_of(reg), s, bit);
}
// Активное тело фикстуре — как поставила бы дверь входа (ссылка сцены,
// вердикт 2026-10-05): перезапись ссылки той же прод-дверью.
inline void make_avatar(entt::registry& reg, int s) {
    sm::sub::set_avatar_slot(arena_of(reg), s);
}

// Род/уровень телу фикстуры — как записала бы дверь рождения (кусок 1).
inline void give_kind(entt::registry& reg, int s, sm::ecs::NPCKind k) {
    if (s >= 0) sm::sub::slot_set_kind(arena_of(reg), s, k);
}
inline void give_level(entt::registry& reg, int s, std::int16_t v) {
    if (s >= 0) sm::sub::slot_set_level(arena_of(reg), s, v);
}
// Бэклинк телу фикстуры — как записала бы дверь tracked-рождения (ломоть 4).
// Снятие — give_origin с MacroHandle{} (бита нет, «записи нет» = невалидный
// хэндл в самой колонке).
inline void give_origin(entt::registry& reg, int s, sm::MacroHandle h) {
    if (s >= 0) sm::sub::slot_set_origin(arena_of(reg), s, h);
}
// Вид телу фикстуры — как записала бы дверь рождения (ломоть 3).
inline void give_sprite(entt::registry& reg, int s,
                        const sm::ecs::Sprite& sp) {
    if (s >= 0) sm::sub::slot_set_sprite(arena_of(reg), s, sp);
}
// Бары/лист/снарядные — колонки арены (кусок 2).
inline void give_pools(entt::registry& reg, int s, sm::ecs::Pools p) {
    if (s >= 0) sm::sub::slot_set_pools(arena_of(reg), s, p);
}
inline void give_combat(entt::registry& reg, int s,
                        const sm::ecs::Combat& c) {
    if (s >= 0) sm::sub::slot_set_combat(arena_of(reg), s, c);
}
inline void give_missile(entt::registry& reg, int s,
                         sm::ecs::MissileAttack m) {
    if (s >= 0) sm::sub::slot_set_missile(arena_of(reg), s, m);
}
inline void give_ai(entt::registry& reg, int s,
                    const sm::ecs::SubworldAi& a) {
    if (s >= 0) sm::sub::slot_set_ai(arena_of(reg), s, a);
}
inline void give_visual(entt::registry& reg, int s, sm::ecs::VisualPos v) {
    if (s >= 0) sm::sub::slot_set_visual(arena_of(reg), s, v);
}
// Снаряд-фикстура (ступень (v) ломтя 7): такой же жилец арены — слот, ровно
// той же дверью, что стреляет продакшен (birth_projectile). Арена рожается
// по надобности — закон «свидетель рожает своё предусловие» (§8 п.11).
// Возврат — слот (-1 = кап).
inline int give_projectile(entt::registry& reg,
                           const sm::ecs::Projectile& p,
                           const sm::ecs::Position& at) {
    return sm::sub::birth_projectile(arena_of(reg), p, at);
}

inline std::uint8_t fx_of(entt::registry& reg, int s) {
    return s >= 0 ? arena_of(reg).damageFx[std::size_t(s)] : std::uint8_t{0};
}
inline sm::sub::ObjRef last_hit_of(entt::registry& reg, int s) {
    return s >= 0 ? arena_of(reg).lastHitBy[std::size_t(s)]
                  : sm::sub::ObjRef{};
}

} // namespace sm::test

// Арена объектов сцены ДЛЯ ФИКСТУР (M-150 ломоть 1а): свидетель обязан САМ
// рожать своё предусловие (§8 п.11) — телу, рождённому мимо двери спавна,
// слот единого массива выдаёт эта обвязка. Чтение колонок — те же выражения,
// что у продакшена, никакой второй копии закона: helpers лишь избавляют
// тесты от повторения слот-цепочки.
//
// АРЕНА — НА ТЕСТ, НЕ НА МИР (ломоть E). Прежняя форма вешала арену на
// entt-реестр ctx-мостом и копила все арены процесса в статическом векторе
// «до конца процесса»: SubObjects = 38.3 МиБ, и паритетные тесты с
// несколькими мирами держали ~0.5 ГиБ резидента мёртвым грузом. Реестр умер
// (ломоть E), носитель арены — uptr В КАДРЕ ТЕСТА: умирает со своим блоком,
// как в игре арена умирает со своим миром. SubObjects в стек не влезает —
// потому фабрика, не значение (та же причина, что у `make_body_crowd`).
#pragma once

#include <memory>

#include "ecs/components.h"
#include "sub/record.h"   // слот-двери арены (slot_set_* / birth_projectile)

namespace sm::test {

inline std::unique_ptr<sm::sub::SubObjects> make_scene_arena() {
    return std::make_unique<sm::sub::SubObjects>();
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
// `user` — АРЕНА (`&objs`), подпись годится обеим дверям разом.
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
// АРЕНУ и СЛОТ и пишут колонку той же слот-дверью, что продакшен (ломоть E:
// реестра-посредника больше не существует).
inline int give_body(sm::sub::SubObjects& objs) {
    return objs.alloc(sm::ecs::Position{});
}

// Позиция телу фикстуры (ломоть 5) — как записало бы рождение.
inline void give_pos(sm::sub::SubObjects& objs, int s,
                     float x, float y, float z = 0.0f) {
    if (s >= 0) {
        objs.pos[std::size_t(s)] = sm::ecs::Position{x, y, z};
    }
}

// Бит маски телу фикстуры — как поставила бы боевая дверь.
inline void give_flag(sm::sub::SubObjects& objs, int s, std::uint16_t bit) {
    if (s >= 0) sm::sub::slot_flag_set(objs, s, bit);
}
inline bool flag_of(const sm::sub::SubObjects& objs, int s,
                    std::uint16_t bit) {
    return s >= 0 && sm::sub::slot_flag(objs, s, bit);
}
// Активное тело фикстуре — как поставила бы дверь входа (ссылка сцены,
// вердикт 2026-10-05): перезапись ссылки той же прод-дверью.
inline void make_avatar(sm::sub::SubObjects& objs, int s) {
    sm::sub::set_avatar_slot(objs, s);
}

// Род/уровень телу фикстуры — как записала бы дверь рождения (кусок 1).
inline void give_kind(sm::sub::SubObjects& objs, int s, sm::ecs::NPCKind k) {
    if (s >= 0) sm::sub::slot_set_kind(objs, s, k);
}
inline void give_level(sm::sub::SubObjects& objs, int s, std::int16_t v) {
    if (s >= 0) sm::sub::slot_set_level(objs, s, v);
}
// Бэклинк телу фикстуры — как записала бы дверь tracked-рождения (ломоть 4).
// Снятие — give_origin с MacroHandle{} (бита нет, «записи нет» = невалидный
// хэндл в самой колонке).
inline void give_origin(sm::sub::SubObjects& objs, int s, sm::MacroHandle h) {
    if (s >= 0) sm::sub::slot_set_origin(objs, s, h);
}
// Вид телу фикстуры — как записала бы дверь рождения (ломоть 3).
inline void give_sprite(sm::sub::SubObjects& objs, int s,
                        const sm::ecs::Sprite& sp) {
    if (s >= 0) sm::sub::slot_set_sprite(objs, s, sp);
}
// Бары/лист/снарядные — колонки арены (кусок 2).
inline void give_pools(sm::sub::SubObjects& objs, int s, sm::ecs::Pools p) {
    if (s >= 0) sm::sub::slot_set_pools(objs, s, p);
}
inline void give_combat(sm::sub::SubObjects& objs, int s,
                        const sm::ecs::Combat& c) {
    if (s >= 0) sm::sub::slot_set_combat(objs, s, c);
}
inline void give_missile(sm::sub::SubObjects& objs, int s,
                         sm::ecs::MissileAttack m) {
    if (s >= 0) sm::sub::slot_set_missile(objs, s, m);
}
inline void give_ai(sm::sub::SubObjects& objs, int s,
                    const sm::ecs::SubworldAi& a) {
    if (s >= 0) sm::sub::slot_set_ai(objs, s, a);
}
inline void give_visual(sm::sub::SubObjects& objs, int s,
                        sm::ecs::VisualPos v) {
    if (s >= 0) sm::sub::slot_set_visual(objs, s, v);
}
// Снаряд-фикстура (ступень (v) ломтя 7): такой же жилец арены — слот, ровно
// той же дверью, что стреляет продакшен (birth_projectile). Возврат — слот
// (-1 = кап).
inline int give_projectile(sm::sub::SubObjects& objs,
                           const sm::ecs::Projectile& p,
                           const sm::ecs::Position& at) {
    return sm::sub::birth_projectile(objs, p, at);
}

inline std::uint8_t fx_of(const sm::sub::SubObjects& objs, int s) {
    return s >= 0 ? objs.damageFx[std::size_t(s)] : std::uint8_t{0};
}
inline sm::sub::ObjRef last_hit_of(const sm::sub::SubObjects& objs, int s) {
    return s >= 0 ? objs.lastHitBy[std::size_t(s)] : sm::sub::ObjRef{};
}

} // namespace sm::test

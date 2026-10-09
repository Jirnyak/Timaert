// ДВЕРИ АРЕНЫ ПО СЛОТУ — ломоть 7 M-150 (ИДЕНТИЧНОСТЬ).
//
// Каждая дверь берёт АРЕНУ и СЛОТ — ни реестра, ни сущности, ни entt в этом
// файле нет и не будет: это конечная форма доступа к телу сцены, та, что
// переживёт смерть реестра (ломти 7-9). Валидность слота — дело ЗВОНЯЩЕГО:
// слотовый цикл гейтится kObjAlive, вьюха по ObjectSlot несёт живой слот по
// построению (биекция — свидетель в body_contract_test), а КРОСС-ТИКОВАЯ
// ссылка обязана прийти ObjRef и свериться ID до входа (objects.h).
//
// Семантика «нет» у каждой двери — ровно та, что была у entt-формы record.h
// (суд контрактов ломтей 2-4): бит маски, сентинел в значении или
// безусловная колонка. Entity-формы record.h — ТОНКИЕ АДАПТЕРЫ над этими
// телами (слот из ObjectSlot → сюда): закон один, тел два не бывает.
//
// МАКРО ЗДЕСЬ НЕТ ВООБЩЕ: двери, которым нужна ЗАПИСЬ (slot_state,
// slot_pools_of, slot_macro_record), — двери ШВА, не арены, и живут в
// record.h, где включение macro/store.h уже оплачено списком канала.
#pragma once

#include "ecs/components.h" // типы колонок арены
#include "sub/objects.h"    // SubObjects/ObjRef/биты маски

namespace sm::sub {

// ── Разыменование ссылки: ЕДИНСТВЕННАЯ дверь из ObjRef в слот ────────────
// -1 = ссылка протухла (слот перерождён или мёртв) или её нет (id 0).
inline int ref_slot(const SubObjects& o, ObjRef r) {
    if (r.id == 0u) return -1;
    const std::size_t s = std::size_t(r.slot);
    if (o.id[s] != r.id) return -1;
    if ((o.flags[s] & kObjAlive) == 0u) return -1;
    return int(r.slot);
}
// Ссылка НА слот — для полей и событий, переживающих тик.
inline ObjRef ref_of(const SubObjects& o, int slot) {
    if (slot < 0 || slot >= int(kMaxSubObjects)) return ObjRef{};
    return ObjRef{std::uint16_t(slot), o.id[std::size_t(slot)]};
}

// ── Биты маски ───────────────────────────────────────────────────────────
inline bool slot_flag(const SubObjects& o, int s, std::uint16_t bit) {
    return (o.flags[std::size_t(s)] & bit) != 0u;
}
inline void slot_flag_set(SubObjects& o, int s, std::uint16_t bit) {
    o.flags[std::size_t(s)] |= bit;
}
inline void slot_flag_clear(SubObjects& o, int s, std::uint16_t bit) {
    o.flags[std::size_t(s)] &= std::uint16_t(~bit);
}

// ── Род и уровень (кусок 1) ─────────────────────────────────────────────
// «Рода нет» = kObjNoKind (игрок, голая фикстура) — сентинел в значении.
inline const ecs::NPCKind* slot_kind(const SubObjects& o, int s) {
    const ecs::NPCKind& k = o.kind[std::size_t(s)];
    return k.type != kObjNoKind ? &k : nullptr;
}
inline void slot_set_kind(SubObjects& o, int s, const ecs::NPCKind& k) {
    o.kind[std::size_t(s)] = k;
}
inline std::int16_t slot_level(const SubObjects& o, int s) {
    return o.level[std::size_t(s)];
}
inline void slot_set_level(SubObjects& o, int s, std::int16_t lv) {
    o.level[std::size_t(s)] = lv;
}

// ── Бары, лист боя, снарядные параметры (кусок 2) ───────────────────────
// «Баров нет» = maxHp 0 — тел с барами и нулевым максимумом не бывает.
inline ecs::Pools* slot_pools(SubObjects& o, int s) {
    ecs::Pools& p = o.pools[std::size_t(s)];
    return p.maxHp != 0 ? &p : nullptr;
}
inline const ecs::Pools* slot_pools(const SubObjects& o, int s) {
    const ecs::Pools& p = o.pools[std::size_t(s)];
    return p.maxHp != 0 ? &p : nullptr;
}
inline void slot_set_pools(SubObjects& o, int s, const ecs::Pools& p) {
    o.pools[std::size_t(s)] = p;
}
// Лист — бит kObjHasCombat (пустой лист законен, сентинела нет).
inline ecs::Combat* slot_combat(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasCombat)) return nullptr;
    return &o.combat[std::size_t(s)];
}
inline const ecs::Combat* slot_combat(const SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasCombat)) return nullptr;
    return &o.combat[std::size_t(s)];
}
inline void slot_set_combat(SubObjects& o, int s, const ecs::Combat& c) {
    o.combat[std::size_t(s)] = c;
    slot_flag_set(o, s, kObjHasCombat);
}
inline void slot_clear_combat(SubObjects& o, int s) {
    o.combat[std::size_t(s)] = ecs::Combat{};
    slot_flag_clear(o, s, kObjHasCombat);
}
// «Снарядных нет» = speed 0 (дверь рождения коэрсит авторский ноль).
inline const ecs::MissileAttack* slot_missile(const SubObjects& o, int s) {
    const ecs::MissileAttack& m = o.missile[std::size_t(s)];
    return m.speed > 0.0f ? &m : nullptr;
}
inline void slot_set_missile(SubObjects& o, int s, ecs::MissileAttack m) {
    o.missile[std::size_t(s)] = m;
}

// ── Листья (ломоть 2): лист безусловный, зеркало — бит ──────────────────
inline CharacterSheet* slot_sheet(SubObjects& o, int s) {
    return &o.sheet[std::size_t(s)];
}
inline const CharacterSheet* slot_sheet(const SubObjects& o, int s) {
    return &o.sheet[std::size_t(s)];
}
inline void slot_set_sheet(SubObjects& o, int s, const CharacterSheet& sh) {
    o.sheet[std::size_t(s)] = sh;
}
inline const BonusTotals* slot_standing(const SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjStandingMirror)) return nullptr;
    return &o.standing[std::size_t(s)];
}
inline BonusTotals* slot_standing(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjStandingMirror)) return nullptr;
    return &o.standing[std::size_t(s)];
}
inline void slot_set_standing(SubObjects& o, int s, const BonusTotals& b) {
    o.standing[std::size_t(s)] = b;
    slot_flag_set(o, s, kObjStandingMirror);
}

// ── Виды (ломоть 3): оба с битом ────────────────────────────────────────
inline const ecs::Sprite* slot_sprite(const SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasSprite)) return nullptr;
    return &o.sprite[std::size_t(s)];
}
inline ecs::Sprite* slot_sprite(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasSprite)) return nullptr;
    return &o.sprite[std::size_t(s)];
}
inline void slot_set_sprite(SubObjects& o, int s, const ecs::Sprite& sp) {
    o.sprite[std::size_t(s)] = sp;
    slot_flag_set(o, s, kObjHasSprite);
}
inline const ecs::LightEmitter* slot_light(const SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasLight)) return nullptr;
    return &o.light[std::size_t(s)];
}
inline ecs::LightEmitter* slot_light(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasLight)) return nullptr;
    return &o.light[std::size_t(s)];
}
inline void slot_set_light(SubObjects& o, int s, const ecs::LightEmitter& le) {
    o.light[std::size_t(s)] = le;
    slot_flag_set(o, s, kObjHasLight);
}
inline void slot_clear_light(SubObjects& o, int s) {
    o.light[std::size_t(s)] = ecs::LightEmitter{};
    slot_flag_clear(o, s, kObjHasLight);
}

// ── Шов и квитанция (ломоть 4): сентинелы в значении ────────────────────
inline MacroHandle slot_macro_origin(const SubObjects& o, int s) {
    return o.origin[std::size_t(s)].macro;
}
inline void slot_set_origin(SubObjects& o, int s, MacroHandle h) {
    o.origin[std::size_t(s)] = ecs::MacroOrigin{h};
}
inline const ecs::MacroDebt* slot_debt(const SubObjects& o, int s) {
    const ecs::MacroDebt& d = o.debt[std::size_t(s)];
    return d.amount != 0 ? &d : nullptr;
}
inline void slot_set_debt(SubObjects& o, int s, const ecs::MacroDebt& d) {
    o.debt[std::size_t(s)] = d;
}

// ── Движение и думка (кусок 3) ──────────────────────────────────────────
inline ecs::SubworldAi* slot_ai(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasAi)) return nullptr;
    return &o.ai[std::size_t(s)];
}
inline const ecs::SubworldAi* slot_ai(const SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjHasAi)) return nullptr;
    return &o.ai[std::size_t(s)];
}
inline void slot_set_ai(SubObjects& o, int s, const ecs::SubworldAi& a) {
    o.ai[std::size_t(s)] = a;
    slot_flag_set(o, s, kObjHasAi);
}
inline ecs::VisualPos* slot_visual(SubObjects& o, int s) {
    return &o.visual[std::size_t(s)];
}
inline void slot_set_visual(SubObjects& o, int s, ecs::VisualPos v) {
    o.visual[std::size_t(s)] = v;
}
inline ecs::Position* slot_pos(SubObjects& o, int s) {
    return &o.pos[std::size_t(s)];
}
inline const ecs::Position* slot_pos(const SubObjects& o, int s) {
    return &o.pos[std::size_t(s)];
}
inline const ecs::GoingHome* slot_going_home(const SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjGoingHome)) return nullptr;
    return &o.goHome[std::size_t(s)];
}
inline ecs::GoingHome* slot_going_home(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjGoingHome)) return nullptr;
    return &o.goHome[std::size_t(s)];
}
inline void slot_set_going_home(SubObjects& o, int s, ecs::GoingHome g) {
    o.goHome[std::size_t(s)] = g;
    slot_flag_set(o, s, kObjGoingHome);
}
inline void slot_clear_going_home(SubObjects& o, int s) {
    slot_flag_clear(o, s, kObjGoingHome);
}
inline float* slot_airborne_vz(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjAirborne)) return nullptr;
    return &o.airborneVz[std::size_t(s)];
}
inline float* slot_set_airborne(SubObjects& o, int s, float vz) {
    o.airborneVz[std::size_t(s)] = vz;
    slot_flag_set(o, s, kObjAirborne);
    return &o.airborneVz[std::size_t(s)];
}
inline void slot_clear_airborne(SubObjects& o, int s) {
    o.airborneVz[std::size_t(s)] = 0.0f;
    slot_flag_clear(o, s, kObjAirborne);
}

// ── Снаряд (ломоть 4): роль — бит ───────────────────────────────────────
inline bool slot_is_projectile(const SubObjects& o, int s) {
    return slot_flag(o, s, kObjProjectile);
}
inline ecs::Projectile* slot_projectile(SubObjects& o, int s) {
    if (!slot_flag(o, s, kObjProjectile)) return nullptr;
    return &o.projectile[std::size_t(s)];
}
inline void slot_set_projectile(SubObjects& o, int s,
                                const ecs::Projectile& p) {
    o.projectile[std::size_t(s)] = p;
    slot_flag_set(o, s, kObjProjectile);
}

} // namespace sm::sub

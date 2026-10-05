// THE door of the seam: WHOSE RECORD IS THIS BODY?
//
// Owner's form, 2026-09-12 («ЗЕРКАЛО ДЛЯ ВСЕХ», CANON.md): a body standing in
// the subworld OWNS NOTHING. Its sheet, its bars, its bag, what it wears and
// what it knows all belong to the macro record it is a projection of, and every
// reader and every writer goes through here to find them. The player's device
// became the law — he has worked this way since landing 4 (his bars are the
// ordinary Pools on his squad entity) — so the hero husk is not the exception
// that proves the rule, he is the ordinary case of it.
//
// WHY THIS EXISTS AT ALL. Before it, the seam carried COPIES down and folded a
// single number — an hp FRACTION — back up. Two consequences, both shipped:
//   * a lord you stripped, looted and levelled underground climbed out whole,
//     because his belongings were a copy nobody read back (the fold-up knew
//     about hp and nothing else);
//   * the two layers had to agree on conversions (a wound as a fraction, bars
//     built from two different sheets) precisely because there were two
//     memories of one thing. Every such pair drifts — problems.md §43, §44.
// With one memory there is no fold, no conversion, and nothing to forget: the
// sword you pick up is in his bag the instant you pick it up, because his bag
// is the only bag there ever was.
//
// THE ONE RULE, and it has no player branch: a body is a projection of the
// record its `MacroOrigin` names, and a body with no backlink is its own record.
// That second half is not a fallback, it is the other honest kind of birth — a
// citizen in a crowd, a wolf, a bandit rolled from a cell seed is ONE OF MANY
// made visible (sub/spawn.h, the derived form). Nothing above remembers him, so
// there is nothing above to write to; he answers for himself and his death
// settles a STOCK instead (ecs::MacroDebt). Two forms of birth, two honest
// answers, one question.
#pragma once

#include "ecs/components.h"
#include "ecs/world.h"      // store_of(reg) — ctx-мост (переехал из store.h, M-150 шаг 0)
#include "macro/store.h"
#include "macro/anketa.h"   // BonusTotals — «что на нём стоит», and its ==
#include "sub/objects.h"    // SubObjects — единый массив объектов сцены (M-150)

#include <entt/entt.hpp>

namespace sm::sub {

// The RECORD of this body — a store handle (эпик 2 шаг 2: MacroOrigin несёт
// MacroHandle, entt в адресе записи не участвует). Валидный хэндл = тело есть
// проекция макро-записи; невалидный = тело САМО СЕБЕ запись — либо честное
// derived-рождение (гражданин, волк, консольный спавн), либо протухший
// бэклинк (запись пожата store_death, пока тело стояло). Оба деградируют в
// тело, а не в ничто: a body without bars would be an invulnerable ghost —
// the one failure mode worse than losing the write-back.
//
// ctx().find, а не store_of: у фикстур с голым registry store нет, и «нет
// store» отвечает тем же честным «записи нет», что и протухший хэндл.
inline MacroHandle macro_record_of(const entt::registry& reg,
                                   entt::entity body) {
    if (body == entt::null || !reg.valid(body)) return MacroHandle{};
    const auto* origin = reg.try_get<ecs::MacroOrigin>(body);
    if (!origin) return MacroHandle{};
    MacroStore* const* st = reg.ctx().find<MacroStore*>();
    if (!st || !(*st)->valid(origin->macro)) return MacroHandle{};
    return origin->macro;
}

// THE accessor every typed door below is made of. One template, deliberately:
// the alternative is a hand-written run of near-identical `pools_of` /
// `bag_of` / `worn_of` functions, and a field that falls out of a hand-written
// run is this project's oldest bug shape (the fold-up that dropped a component
// and froze the world's AI — memory: handwritten-foldup-drops-fields). Adding
// a kind of owned state means adding one line, not a fifth twin.
//
// The self-fallback is the same guard as above, one level down: a record that
// does not keep this kind of state at all leaves the body answering for itself.
template <class C>
inline C* state_of(entt::registry& reg, entt::entity body) {
    // Запись-макро отвечает КОЛОНКОЙ store по хэндлу (шаг 2 1е); тело без
    // записи — своей компонентой (self-fallback — вторая честная форма).
    const MacroHandle rec = macro_record_of(reg, body);
    if (rec.slot != kMacroNoSlot) {
        if (C* owned = body_state<C>(store_of(reg), rec)) return owned;
    }
    // Тело без записи — «само себе запись»: своя компонента (кластер 7).
    return reg.try_get<C>(body);
}

template <class C>
inline const C* state_of(const entt::registry& reg, entt::entity body) {
    return state_of<C>(const_cast<entt::registry&>(reg), body);
}

// WHAT STOOD ON THE RECORD when this body's derived numbers were last built.
//
// The cost of the mirror was measured before it was built (owner's note in
// CANON, worst case — all 42 anatomy slots worn): assembling «what stands on
// him» costs 0.00041 ms per body, which is 0.8 % of a frame at 300 bodies and
// was accepted; RE-ROLLING the sheet and the strike off it costs 0.00196 ms,
// five times more, and at a thousand bodies that is 12.5 % of the frame — not
// acceptable, and not necessary, because it almost never changes.
//
// So the assembly runs and the RESULT is compared: `BonusTotals::operator==`
// exists for exactly this question, and the expensive half runs only when the
// answer is no. A lord who levels from a kill mid-fight swings harder on the
// next tick; a lord nobody touched pays one comparison.
struct StandingMirror { BonusTotals totals{}; };

// (pools_of — ниже моста арены: его фолбэк с куска 2 — КОЛОНКА, не
// компонента, и ему нужны objects_find/body_pools.)

// ── МОСТ ЕДИНОГО МАССИВА ОБЪЕКТОВ (M-150, транзит миграции) ─────────────
// Тот же ctx-приём, что у MacroStore: указатель живёт в реестре и умирает
// вместе с ним (ломоть 7). on_destroy-хук — ЕДИНСТВЕННАЯ точка
// освобождения слота на весь период миграции: любой путь смерти сущности
// (жнец, уход домой на рассвете, clear сцены или мира) проходит через
// него, и забытый путь невыразим.
inline SubObjects& objects_of(entt::registry& reg) {
    return *reg.ctx().get<SubObjects*>();
}
inline SubObjects* objects_find(entt::registry& reg) {
    auto* p = reg.ctx().find<SubObjects*>();
    return p != nullptr ? *p : nullptr;
}
// Арена приходит ПЭЙЛОАДОМ соединения, а не из ctx: деструктор реестра
// стреляет on_destroy, когда ctx уже мёртв (vars объявлены после пулов и
// умирают первыми) — хук, читавший ctx, ловил ноль (куплено SIGSEGV
// damage_door_test 2026-10-05).
inline void on_object_slot_destroy(SubObjects& objs, entt::registry& reg,
                                   entt::entity e) {
    objs.free(int(reg.get<ecs::ObjectSlot>(e).slot));
}
inline void objects_attach(entt::registry& reg, SubObjects* o) {
    reg.ctx().insert_or_assign(o);
    reg.on_destroy<ecs::ObjectSlot>().connect<&on_object_slot_destroy>(*o);
}

// ── Биты маски через сущность (ломоть 1б, транзит миграции) ─────────────
// Бывшие entt-теги читаются/пишутся ТОЛЬКО этими тремя дверями, пока жив
// реестр; ломоть 7 заменит сущность слотом и двери схлопнутся в прямой
// доступ к колонке. Тело без слота (фикстура без арены) честно отвечает
// «бита нет», запись — no-op: та же ветка транзита, что у emplace_body.
inline bool object_flag(const entt::registry& reg, entt::entity e,
                        std::uint16_t bit) {
    if (e == entt::null || !reg.valid(e)) return false;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return false;
    SubObjects* const* objs = reg.ctx().find<SubObjects*>();
    return objs != nullptr
        && ((*objs)->flags[std::size_t(os->slot)] & bit) != 0u;
}
inline void object_flag_set(entt::registry& reg, entt::entity e,
                            std::uint16_t bit) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->flags[std::size_t(os->slot)] |= bit;
    }
}
inline void object_flag_clear(entt::registry& reg, entt::entity e,
                              std::uint16_t bit) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->flags[std::size_t(os->slot)] =
            std::uint16_t(objs->flags[std::size_t(os->slot)] & ~bit);
    }
}

// ── РОД И УРОВЕНЬ ТЕЛА — КОЛОНКИ АРЕНЫ (M-150 ломоть 2 кусок 1) ─────────
// Бывшие компоненты ecs::NPCKind / ecs::NpcLevel. nullptr у body_kind —
// ровно прежняя семантика «компоненты нет»: у сущности нет слота
// (бесслотные снаряды/свет/труп-контейнер до ломтей 4-5) ИЛИ род не
// назначен (kObjNoKind — игрок, голая фикстура). Уровень безуровневого
// тела — 0: прежние читатели try_get сами подставляли свой дефолт, и
// каждый сохранил его на своём месте.
inline const ecs::NPCKind* body_kind(const entt::registry& reg,
                                     entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* const* objs = reg.ctx().find<SubObjects*>();
    if (objs == nullptr) return nullptr;
    const ecs::NPCKind& k = (*objs)->kind[std::size_t(os->slot)];
    return k.type == kObjNoKind ? nullptr : &k;
}
inline void set_body_kind(entt::registry& reg, entt::entity e,
                          ecs::NPCKind k) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->kind[std::size_t(os->slot)] = k;
    }
}
inline std::int16_t body_level(const entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return 0;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return 0;
    SubObjects* const* objs = reg.ctx().find<SubObjects*>();
    if (objs == nullptr) return 0;
    return (*objs)->level[std::size_t(os->slot)];
}
inline void set_body_level(entt::registry& reg, entt::entity e,
                           std::int16_t v) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->level[std::size_t(os->slot)] = v;
    }
}

// ── БОЕВАЯ ПАРА ТЕЛА — КОЛОНКИ АРЕНЫ (M-150 ломоть 2 кусок 2) ───────────
// Бывшие компоненты ecs::Pools / ecs::Combat / ecs::MissileAttack.
// «Баров нет» = maxHp 0 (даже мёртвый хранит максимум); «листа нет» —
// бит kObjHasCombat, его ставит ТОЛЬКО set_body_combat (у листа
// естественного нуля нет: пустой лист — законное значение, ЗАКОН АНКЕТЫ
// п.4); «снарядных нет» = speed 0 (дверь рождения коэрсит авторский ноль
// в 200). nullptr каждой двери — ровно прежняя семантика «компоненты
// нет». У зеркальных тел pools-колонка — КОПИЯ записи (mirror-проход);
// рана ложится на ЗАПИСЬ дверью pools_of ниже, store-первой.
inline ecs::Pools* body_pools(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    ecs::Pools& p = objs->pools[std::size_t(os->slot)];
    return p.maxHp != 0 ? &p : nullptr;
}
inline const ecs::Pools* body_pools(const entt::registry& reg,
                                    entt::entity e) {
    return body_pools(const_cast<entt::registry&>(reg), e);
}
inline void set_body_pools(entt::registry& reg, entt::entity e,
                           ecs::Pools p) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->pools[std::size_t(os->slot)] = p;
    }
}
inline ecs::Combat* body_combat(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    if ((objs->flags[std::size_t(os->slot)] & kObjHasCombat) == 0u)
        return nullptr;
    return &objs->combat[std::size_t(os->slot)];
}
inline const ecs::Combat* body_combat(const entt::registry& reg,
                                      entt::entity e) {
    return body_combat(const_cast<entt::registry&>(reg), e);
}
inline void set_body_combat(entt::registry& reg, entt::entity e,
                            const ecs::Combat& c) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->combat[std::size_t(os->slot)] = c;
        objs->flags[std::size_t(os->slot)] |= kObjHasCombat;
    }
}
// Разоружить тело (бывший remove<Combat> — смоук-нейтрализация): лист
// гаснет битом, колонка зануляется, чтобы протухшие числа не пережили слот.
inline void clear_body_combat(entt::registry& reg, entt::entity e) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->combat[std::size_t(os->slot)] = ecs::Combat{};
        objs->flags[std::size_t(os->slot)] =
            std::uint16_t(objs->flags[std::size_t(os->slot)] & ~kObjHasCombat);
    }
}
// ── ДВИЖЕНИЕ/ДУМКА — КОЛОНКИ АРЕНЫ (M-150 ломоть 2 кусок 3) ─────────────
// Мозг: бит kObjHasAi + колонка (Wander = 0 законен — сентинела нет).
// Визуальная позиция — безусловная колонка слота (нулевая скорость =
// интерполятор стоит). «Домой»/«в воздухе» — ленивые состояния: прежнее
// «наличие компоненты» стало битом, числа — колонкой.
inline ecs::SubworldAi* body_ai(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    if ((objs->flags[std::size_t(os->slot)] & kObjHasAi) == 0u)
        return nullptr;
    return &objs->ai[std::size_t(os->slot)];
}
inline const ecs::SubworldAi* body_ai(const entt::registry& reg,
                                      entt::entity e) {
    return body_ai(const_cast<entt::registry&>(reg), e);
}
inline void set_body_ai(entt::registry& reg, entt::entity e,
                        const ecs::SubworldAi& a) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->ai[std::size_t(os->slot)] = a;
        objs->flags[std::size_t(os->slot)] |= kObjHasAi;
    }
}
inline ecs::VisualPos* body_visual(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    return &objs->visual[std::size_t(os->slot)];
}
inline const ecs::VisualPos* body_visual(const entt::registry& reg,
                                         entt::entity e) {
    return body_visual(const_cast<entt::registry&>(reg), e);
}
inline void set_body_visual(entt::registry& reg, entt::entity e,
                            ecs::VisualPos v) {
    if (ecs::VisualPos* col = body_visual(reg, e)) *col = v;
}
inline ecs::GoingHome* going_home(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    if ((objs->flags[std::size_t(os->slot)] & kObjGoingHome) == 0u)
        return nullptr;
    return &objs->goHome[std::size_t(os->slot)];
}
inline const ecs::GoingHome* going_home(const entt::registry& reg,
                                        entt::entity e) {
    return going_home(const_cast<entt::registry&>(reg), e);
}
inline void set_going_home(entt::registry& reg, entt::entity e,
                           ecs::GoingHome g) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->goHome[std::size_t(os->slot)] = g;
        objs->flags[std::size_t(os->slot)] |= kObjGoingHome;
    }
}
inline void clear_going_home(entt::registry& reg, entt::entity e) {
    object_flag_clear(reg, e, kObjGoingHome);
}
inline float* airborne_vz(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    if ((objs->flags[std::size_t(os->slot)] & kObjAirborne) == 0u)
        return nullptr;
    return &objs->airborneVz[std::size_t(os->slot)];
}
inline float* set_airborne(entt::registry& reg, entt::entity e, float vz) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    if (SubObjects* objs = objects_find(reg)) {
        objs->airborneVz[std::size_t(os->slot)] = vz;
        objs->flags[std::size_t(os->slot)] |= kObjAirborne;
        return &objs->airborneVz[std::size_t(os->slot)];
    }
    return nullptr;
}
inline void clear_airborne(entt::registry& reg, entt::entity e) {
    object_flag_clear(reg, e, kObjAirborne);
}

// ── СНАРЯД — КОЛОНКА АРЕНЫ (M-150 ломоть 4) ─────────────────────────────
// Бывшая компонента ecs::Projectile. Роль — бит kObjProjectile, его ставит
// ТОЛЬКО set_projectile (у снаряда нет запретного нуля: нулевая скорость
// законна у метеоров, Bolt = 0, жизнь — шкала с достижимым нулём, так что
// сентинел попал бы внутрь области значений — ровно случай боевого листа).
// nullptr — прежняя семантика «компоненты нет»: слота нет ИЛИ слот не
// снаряд. Снимать бит некому: снаряд умирает слотом целиком (queue_reap →
// destroy → on_destroy-хук), а слот зануляет колонку при следующей выдаче.
inline ecs::Projectile* projectile_of(entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return nullptr;
    if ((objs->flags[std::size_t(os->slot)] & kObjProjectile) == 0u)
        return nullptr;
    return &objs->projectile[std::size_t(os->slot)];
}
inline const ecs::Projectile* projectile_of(const entt::registry& reg,
                                            entt::entity e) {
    return projectile_of(const_cast<entt::registry&>(reg), e);
}
inline bool is_projectile(const entt::registry& reg, entt::entity e) {
    return object_flag(reg, e, kObjProjectile);
}
inline void set_projectile(entt::registry& reg, entt::entity e,
                           const ecs::Projectile& p) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->projectile[std::size_t(os->slot)] = p;
        objs->flags[std::size_t(os->slot)] |= kObjProjectile;
    }
}
// РОДИТЬ СНАРЯД ЖИЛЬЦОМ АРЕНЫ — одна дверь на все четыре места выстрела.
// `false` значит КАП: слота нет, и снаряда быть не должно вовсе — звонящий
// убирает сущность и отказывается честно, ровно как `BodyCrowd::add` (до
// ломтя 4 у снарядов капа не было ВООБЩЕ, слот и есть первый). Каст при
// этом уже оплачен маной и восстановлением — как промах мечом (вердикт
// владельца 2026-09-17 о касте, который «фыркнул и ничего не нашёл»).
// Фикстура без арены рождает снаряд бесслотным и получает `true`: правду
// несёт компонента, пока она жива, — та же ветка ТРАНЗИТА, что у тел
// (`spawn.cpp`), и умирает она вместе с реестром (ломоть 7).
inline bool birth_projectile(entt::registry& reg, entt::entity e,
                             const ecs::Projectile& p) {
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return true;
    const int slot = objs->alloc();
    if (slot < 0) return false;
    reg.emplace<ecs::ObjectSlot>(e, std::uint16_t(slot));
    set_projectile(reg, e, p);
    return true;
}

inline const ecs::MissileAttack* body_missile(const entt::registry& reg,
                                              entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return nullptr;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return nullptr;
    SubObjects* const* objs = reg.ctx().find<SubObjects*>();
    if (objs == nullptr) return nullptr;
    const ecs::MissileAttack& m = (*objs)->missile[std::size_t(os->slot)];
    return m.speed > 0.0f ? &m : nullptr;
}
inline void set_body_missile(entt::registry& reg, entt::entity e,
                             ecs::MissileAttack m) {
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;
    if (SubObjects* objs = objects_find(reg)) {
        objs->missile[std::size_t(os->slot)] = m;
    }
}

// The three bars (CANON S14). Damage, casting, harvesting and crafting all
// land here — «действия платят в склад», now stated once for every body
// rather than once for the player and once for everyone else. Store-первый
// (рана ложится на ЗАПИСЬ зеркального тела), фолбэк — колонка арены
// (кусок 2; прежде — компонента через state_of).
inline ecs::Pools* pools_of(entt::registry& reg, entt::entity body) {
    const MacroHandle rec = macro_record_of(reg, body);
    if (rec.slot != kMacroNoSlot) {
        if (ecs::Pools* owned = body_state<ecs::Pools>(store_of(reg), rec))
            return owned;
    }
    return body_pools(reg, body);
}
inline const ecs::Pools* pools_of(const entt::registry& reg,
                                  entt::entity body) {
    return pools_of(const_cast<entt::registry&>(reg), body);
}

// ── АКТИВНОЕ ТЕЛО — ТРИ ДВЕРИ ОДНОЙ ССЫЛКИ (вердикт 2026-10-05) ─────────
// «Это игрок?» — сравнение со ссылкой; «какое тело игрока?» — чтение
// ссылки O(1) (прежний view<AvatarTag> сканировал реестр на каждый
// вопрос). Запись — ОДНА дверь: одержимость переносит тело одной
// перезаписью, половинчатое состояние «тег снят, тег не поставлен»
// невыразимо по построению.
inline void set_avatar(entt::registry& reg, entt::entity e) {
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return;          // фикстура без арены — транзит
    if (e == entt::null) {
        objs->avatarId = 0;               // ссылки нет
        objs->avatarEnttBits = 0xFFFFFFFFu;
        return;
    }
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;            // тело без слота — транзит
    objs->avatarSlot = os->slot;
    objs->avatarId = objs->id[std::size_t(os->slot)];
    objs->avatarEnttBits = std::uint32_t(entt::to_integral(e));
}
inline bool avatar_ref_live(const SubObjects& o) {
    return o.avatarId != 0u
        && o.id[std::size_t(o.avatarSlot)] == o.avatarId
        && (o.flags[std::size_t(o.avatarSlot)] & kObjAlive) != 0u;
}
inline bool is_avatar(const entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return false;
    SubObjects* const* po = reg.ctx().find<SubObjects*>();
    if (po == nullptr || !avatar_ref_live(**po)) return false;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    return os != nullptr && os->slot == (*po)->avatarSlot;
}
inline entt::entity avatar_entity(const entt::registry& reg) {
    SubObjects* const* po = reg.ctx().find<SubObjects*>();
    if (po == nullptr || !avatar_ref_live(**po)) return entt::null;
    const entt::entity e = entt::entity((*po)->avatarEnttBits);
    return reg.valid(e) ? e : entt::null;
}

} // namespace sm::sub

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
#include "sub/objects.h"    // SubObjects — единый массив объектов сцены (M-150);
                            // он же несёт macro/anketa.h (колонки листа и
                            // зеркала, ломоть 2)
#include "sub/doors.h"      // слот-формы ВСЕХ дверей арены — ЕДИНСТВЕННОЕ тело
                            // каждого закона; двери ниже лишь переводят
                            // сущность в слот (ломоть 7, ступень ii)

#include <entt/entt.hpp>

namespace sm::sub {

// ── ШОВНЫЕ СЛОТ-ДВЕРИ (стор — ЯВНЫЙ параметр; это двери ШВА, не
// арены — macro/store.h в doors.h запрещён каналом) ────────────────
// ── Запись зеркального тела: стор — ЯВНЫЙ параметр ──────────────────────
// Бэклинк + валидность записи; невалидный хэндл = тело само себе запись
// (протухший бэклинк деградирует в тело, не в ничто — шапка record.h).
inline MacroHandle slot_macro_record(const SubObjects& o, int s,
                                     const MacroStore& st) {
    const MacroHandle rec = slot_macro_origin(o, s);
    if (rec.slot == kMacroNoSlot || !st.valid(rec)) return MacroHandle{};
    return rec;
}
// Состояние записи (сумка/черты/гир/книга): ТОЛЬКО запись — самозаписи нет
// (0a ломтя 7).
template <class C>
inline C* slot_state(const SubObjects& o, int s, MacroStore& st) {
    const MacroHandle rec = slot_macro_record(o, s, st);
    if (rec.slot == kMacroNoSlot) return nullptr;
    return body_state<C>(st, rec);
}
// Бары: store-первый (рана ложится на ЗАПИСЬ зеркального тела), фолбэк —
// колонка арены (кусок 2).
inline ecs::Pools* slot_pools_of(SubObjects& o, int s, MacroStore& st) {
    const MacroHandle rec = slot_macro_record(o, s, st);
    if (rec.slot != kMacroNoSlot) {
        if (ecs::Pools* owned = body_state<ecs::Pools>(st, rec)) return owned;
    }
    return slot_pools(o, s);
}
// Лист — ТОТ ЖЕ закон, что у баров: запись первой, фолбэк — колонка sheet
// СВОЕГО слота (компонента CharacterSheet умерла ломтём 2; пустой лист —
// законное значение, ЗАКОН АНКЕТЫ п.4). Дверь ОДНА на вопрос «чей это
// лист» ((ii-б) ломтя 7): до неё закон жил ДВУМЯ носителями — специализация
// state_of<CharacterSheet> для сущности и РУЧНАЯ пара «slot_state + фолбэк
// на колонку» у каста (spell_book.cpp), то есть два ответа на один вопрос
// мира (DOD п.6). Фолд остальных владений (сумка/черты/гир/книга) не
// тронут — они состояние ЗАПИСИ и колонками арены не становятся никогда.
inline CharacterSheet* slot_sheet_of(SubObjects& o, int s, MacroStore& st) {
    const MacroHandle rec = slot_macro_record(o, s, st);
    if (rec.slot != kMacroNoSlot) {
        if (CharacterSheet* owned = body_state<CharacterSheet>(st, rec))
            return owned;
    }
    return slot_sheet(o, s);
}



// ── СЛОТ ТЕЛА — ПРЕАМБУЛА ОСТАВШИХСЯ ДВЕРЕЙ (ломоть 7, ступень ii) ──────
// FIELD-адаптеры умерли ломтём C (2026-10-16): звонящие зовут слот-формы
// doors.h сами. Здесь остались ТОЛЬКО двери, которым нужна сущность по
// существу транзита: body_slot (перевод сущность → слот; умирает с
// ObjectSlot, ломоть D) и три двери аватара ниже (их держат engine/possess —
// aim_target и одержимость пока адресуют тела сущностью). -1 значит «тела
// на арене нет»: сущность нулевая/мертва ИЛИ слота у неё нет.
inline int body_slot(const entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return -1;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    return os != nullptr ? int(os->slot) : -1;
}

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

// (FIELD-адаптеры — object_flag*, body_kind/level/pools/combat/sheet/
// standing/sprite/light/macro_origin/debt/ai/pos/visual/missile,
// going_home/airborne, set_*, clear_*, state_of<C>, macro_record_of,
// pools_of — умерли ломтём C 2026-10-16: их законы живут ОДНИМ телом в
// слот-формах sub/doors.h и в шовных слот-дверях выше; звонящие берут слот
// сами. Entity-двери снаряда умерли раньше, в ступени (v).)

// ── АКТИВНОЕ ТЕЛО — ДВЕРИ ОДНОЙ ССЫЛКИ (вердикт 2026-10-05) ─────────────
// «Это игрок?» — сравнение со ссылкой; «какое тело игрока?» — чтение
// ссылки O(1) (прежний view<AvatarTag> сканировал реестр на каждый
// вопрос). Запись — ОДНА дверь: одержимость переносит тело одной
// перезаписью, половинчатое состояние «тег снят, тег не поставлен»
// невыразимо по построению.
inline void set_avatar(entt::registry& reg, entt::entity e) {
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return;          // фикстура без арены — транзит
    if (e == entt::null) {
        objs->avatar = ObjRef{};          // ссылки нет (id == 0)
        objs->avatarEnttBits = 0xFFFFFFFFu;
        return;
    }
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    if (os == nullptr) return;            // тело без слота — транзит
    objs->avatar = ObjRef{os->slot, objs->id[std::size_t(os->slot)]};
    objs->avatarEnttBits = std::uint32_t(entt::to_integral(e));
}
inline bool avatar_ref_live(const SubObjects& o) {
    return o.avatar.id != 0u
        && o.id[std::size_t(o.avatar.slot)] == o.avatar.id
        && (o.flags[std::size_t(o.avatar.slot)] & kObjAlive) != 0u;
}
// (is_avatar умер ломтём C — ноль звонящих: вопрос «это игрок?» задаётся
// слотом, `SubObjects::is_avatar_slot`, или сравнением ссылок ObjRef.)
inline entt::entity avatar_entity(const entt::registry& reg) {
    SubObjects* const* po = reg.ctx().find<SubObjects*>();
    if (po == nullptr || !avatar_ref_live(**po)) return entt::null;
    const entt::entity e = entt::entity((*po)->avatarEnttBits);
    return reg.valid(e) ? e : entt::null;
}

} // namespace sm::sub

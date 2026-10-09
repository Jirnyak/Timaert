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



// ── СЛОТ ТЕЛА — ПРЕАМБУЛА КАЖДОГО АДАПТЕРА (ломоть 7, ступень ii) ───────
// Своего тела закона ниже нет НИ У ОДНОЙ двери: каждая переводит СУЩНОСТЬ в
// СЛОТ и зовёт слот-форму (sub/doors.h), где семантика «нет» — бит маски,
// сентинел в значении или безусловная колонка — написана однажды. -1 значит
// «тела на арене нет»: сущность нулевая/мертва ИЛИ слота у неё нет
// (фикстура транзита) — читатели отвечают «нет», писатели молчат, ровно как
// до ступени. Арена берётся ОДНОЙ дверью objects_find; у const-читателей
// const_cast снимает const с РЕЕСТРА (поиск в ctx его не меняет, а сама
// арена лежит в ctx не-const указателем) — тот же приём, что у
// const-перегрузок ниже. Транзит кончится ступенью (vi): звонящие станут
// звать слот-формы сами, и этот файл исчезнет целиком.
inline int body_slot(const entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return -1;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    return os != nullptr ? int(os->slot) : -1;
}

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
// Арена здесь тоже из ctx напрямую — дверь objects_find объявлена ниже
// (моста арены), а закон бэклинка живёт целиком в slot_macro_record.
inline MacroHandle macro_record_of(const entt::registry& reg,
                                   entt::entity body) {
    const int s = body_slot(reg, body);
    if (s < 0) return MacroHandle{};
    SubObjects* const* objs = reg.ctx().find<SubObjects*>();
    if (objs == nullptr) return MacroHandle{};
    MacroStore* const* st = reg.ctx().find<MacroStore*>();
    if (st == nullptr) return MacroHandle{};
    return slot_macro_record(**objs, s, **st);
}

// THE accessor every typed door below is made of. One template, deliberately:
// the alternative is a hand-written run of near-identical `pools_of` /
// `bag_of` / `worn_of` functions, and a field that falls out of a hand-written
// run is this project's oldest bug shape (the fold-up that dropped a component
// and froze the world's AI — memory: handwritten-foldup-drops-fields). Adding
// a kind of owned state means adding one line, not a fifth twin.
//
// САМОЗАПИСИ НЕТ (0a ломтя 7): арм «тело без записи — своя компонента»
// снесён — ни одна строка src/ не прикрепляла C-компоненту к телу никогда
// (перепись: 0 emplace), и «оба нуля — предельный случай (голое тело толпы),
// не ветка» (damage.cpp). Записи нет — состояния нет; контрабандная
// компонента на теле двери НЕВИДИМА (свидетель в spawn_parity).
template <class C>
inline C* state_of(entt::registry& reg, entt::entity body) {
    const int s = body_slot(reg, body);
    if (s < 0) return nullptr;
    SubObjects* const* objs = reg.ctx().find<SubObjects*>();
    if (objs == nullptr) return nullptr;
    MacroStore* const* st = reg.ctx().find<MacroStore*>();
    if (st == nullptr) return nullptr;
    return slot_state<C>(**objs, s, **st);
}

template <class C>
inline const C* state_of(const entt::registry& reg, entt::entity body) {
    return state_of<C>(const_cast<entt::registry&>(reg), body);
}

// Позиция через state_of НЕ ходит НИКОГДА: она не «состояние записи», а
// колонка арены (ломоть 5), и генерик с try_get после смерти компоненты
// собирался бы молча и всегда отвечал nullptr. Запрет под компилятором;
// единственная дверь — body_pos ниже.
template <>
ecs::Position* state_of<ecs::Position>(entt::registry&, entt::entity) = delete;
template <>
const ecs::Position* state_of<ecs::Position>(const entt::registry&,
                                             entt::entity) = delete;

// (Зеркало стояния — КОЛОНКА standing@src/sub/objects.h с ломтя 2; его
// замер и смысл — у колонки, гейт — refresh_body_strike@src/sub/spawn.cpp,
// двери body_standing/set_body_standing — ниже моста арены. pools_of — там
// же: его закон «запись первой, колонка фолбэком» живёт в slot_pools_of,
// и ему нужна дверь арены.)

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
    const int s = body_slot(reg, e);
    if (s < 0) return false;
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr && slot_flag(*o, s, bit);
}
inline void object_flag_set(entt::registry& reg, entt::entity e,
                            std::uint16_t bit) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_flag_set(*o, s, bit);
}
inline void object_flag_clear(entt::registry& reg, entt::entity e,
                              std::uint16_t bit) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_flag_clear(*o, s, bit);
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
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr ? slot_kind(*o, s) : nullptr;
}
inline void set_body_kind(entt::registry& reg, entt::entity e,
                          ecs::NPCKind k) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_kind(*o, s, k);
}
inline std::int16_t body_level(const entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return 0;
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr ? slot_level(*o, s) : std::int16_t(0);
}
inline void set_body_level(entt::registry& reg, entt::entity e,
                           std::int16_t v) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_level(*o, s, v);
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
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_pools(*o, s) : nullptr;
}
inline const ecs::Pools* body_pools(const entt::registry& reg,
                                    entt::entity e) {
    return body_pools(const_cast<entt::registry&>(reg), e);
}
inline void set_body_pools(entt::registry& reg, entt::entity e,
                           ecs::Pools p) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_pools(*o, s, p);
}
inline ecs::Combat* body_combat(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_combat(*o, s) : nullptr;
}
inline const ecs::Combat* body_combat(const entt::registry& reg,
                                      entt::entity e) {
    return body_combat(const_cast<entt::registry&>(reg), e);
}
inline void set_body_combat(entt::registry& reg, entt::entity e,
                            const ecs::Combat& c) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_combat(*o, s, c);
}
// Разоружить тело (бывший remove<Combat> — смоук-нейтрализация): лист
// гаснет битом, колонка зануляется, чтобы протухшие числа не пережили слот
// (зануление — в теле slot_clear_combat).
inline void clear_body_combat(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_clear_combat(*o, s);
}
// ── ЛИСТ И ЗЕРКАЛО — КОЛОНКИ АРЕНЫ (M-150 ломоть 2, листья) ─────────────
// Лист — БЕЗУСЛОВНАЯ колонка (бита нет): «листа нет» и нулевой лист — одно
// значение для каждого читателя мира (все коэрсили nullptr в ноль), а
// пустой лист — законное значение, идущее общим путём (ЗАКОН АНКЕТЫ п.4).
// nullptr отсюда значит ровно «тела нет на арене» (нет слота/арены) — та же
// честная ветка транзита, что у остальных дверей.
inline CharacterSheet* body_sheet(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_sheet(*o, s) : nullptr;
}
inline const CharacterSheet* body_sheet(const entt::registry& reg,
                                        entt::entity e) {
    return body_sheet(const_cast<entt::registry&>(reg), e);
}
inline void set_body_sheet(entt::registry& reg, entt::entity e,
                           const CharacterSheet& sh) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_sheet(*o, s, sh);
}
// Зеркало стояния: бит kObjStandingMirror ставит ТОЛЬКО эта дверь — «я
// зеркало» и числа зеркала невыразимы порознь (objects.h, маска). Пишут в
// зеркало ПО ССЫЛКЕ (refresh_body_strike@src/sub/spawn.cpp: `*cache = now`),
// в doors.h нет мутирующей перегрузки.
inline BonusTotals* body_standing(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    if (o == nullptr) return nullptr;
    return slot_standing(*o, s);
}
inline void set_body_standing(entt::registry& reg, entt::entity e,
                              const BonusTotals& t) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_standing(*o, s, t);
}
// ── ВИДЫ — КОЛОНКИ АРЕНЫ (M-150 ломоть 3) ────────────────────────────────
// Спрайт: бит kObjHasSprite ставит только дверь; снимать некому (remove в
// дереве ноль — вид рождается с объектом и умирает со слотом).
// (Как и у зеркала, слот-формы видов в doors.h читающие — const_cast ниже
// держит прежний мутабельный тип дверей, не второе тело закона.)
inline ecs::Sprite* body_sprite(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    if (o == nullptr) return nullptr;
    return slot_sprite(*o, s);
}
inline const ecs::Sprite* body_sprite(const entt::registry& reg,
                                      entt::entity e) {
    return body_sprite(const_cast<entt::registry&>(reg), e);
}
inline void set_body_sprite(entt::registry& reg, entt::entity e,
                            const ecs::Sprite& sp) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_sprite(*o, s, sp);
}
// Свет: бит kObjHasLight — членство в сборе света (SSBO на 16 мест отбирает
// БЛИЖАЙШИХ; radius-0 кандидаты вытесняли бы настоящие огни — довод бита у
// маски objects.h). Гаснуть умеет при живом слоте: clear — бывший
// remove<LightEmitter> смоук-гейтов, бит гаснет И колонка зануляется
// (образец clear_body_combat: протухшие флоаты не переживают роль).
inline ecs::LightEmitter* body_light(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    if (o == nullptr) return nullptr;
    return slot_light(*o, s);
}
inline const ecs::LightEmitter* body_light(const entt::registry& reg,
                                           entt::entity e) {
    return body_light(const_cast<entt::registry&>(reg), e);
}
inline void set_body_light(entt::registry& reg, entt::entity e,
                           const ecs::LightEmitter& le) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_light(*o, s, le);
}
inline void clear_body_light(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_clear_light(*o, s);
}
// ── ШОВ И КВИТАНЦИЯ — КОЛОНКИ АРЕНЫ (M-150 ломоть 4) ────────────────────
// СЫРОЙ бэклинк БЕЗ store-гарды: наличие (slot != kMacroNoSlot) и
// валидность — РАЗНЫЕ вопросы. Лестница лидера-убийцы и щит жнеца сцены
// различают «бэклинк есть, но протух» от «бэклинка нет вовсе»; за живым
// хэндлом иди в macro_record_of (он прибавляет store.valid).
inline MacroHandle body_macro_origin(const entt::registry& reg,
                                     entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return MacroHandle{};
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr ? slot_macro_origin(*o, s) : MacroHandle{};
}
inline void set_body_origin(entt::registry& reg, entt::entity e,
                            MacroHandle h) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_origin(*o, s, h);
}
// Квитанция derived-тела: «займа нет» = amount == 0 — тот же сентинел,
// которым судит единственный расчётчик settle_macro_debt. Пишет ТОЛЬКО
// stamp_macro_debt (sub/spawn.h — закон «no stamp, no borrowing»).
inline const ecs::MacroDebt* body_debt(const entt::registry& reg,
                                       entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr ? slot_debt(*o, s) : nullptr;
}
inline void set_body_debt(entt::registry& reg, entt::entity e,
                          const ecs::MacroDebt& d) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_debt(*o, s, d);
}
// Лист через ОДНУ дверь состояния (специализация self-фолбэка): запись
// отвечает колонкой store, как всякое владение; тело без записи — колонкой
// sheet СВОЕГО слота (компонента CharacterSheet умерла ломтём 2). Фолд
// остальных владений (сумка/черты/гир/книга) не тронут — они состояние
// ЗАПИСИ и колонками арены не становятся никогда.
template <>
inline CharacterSheet* state_of<CharacterSheet>(entt::registry& reg,
                                                entt::entity body) {
    const MacroHandle rec = macro_record_of(reg, body);
    if (rec.slot != kMacroNoSlot) {
        if (CharacterSheet* owned =
                body_state<CharacterSheet>(store_of(reg), rec))
            return owned;
    }
    return body_sheet(reg, body);
}
// ── ДВИЖЕНИЕ/ДУМКА — КОЛОНКИ АРЕНЫ (M-150 ломоть 2 кусок 3) ─────────────
// Мозг: бит kObjHasAi + колонка (Wander = 0 законен — сентинела нет).
// Визуальная позиция — безусловная колонка слота (нулевая скорость =
// интерполятор стоит). «Домой»/«в воздухе» — ленивые состояния: прежнее
// «наличие компоненты» стало битом, числа — колонкой.
inline ecs::SubworldAi* body_ai(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_ai(*o, s) : nullptr;
}
inline const ecs::SubworldAi* body_ai(const entt::registry& reg,
                                      entt::entity e) {
    return body_ai(const_cast<entt::registry&>(reg), e);
}
inline void set_body_ai(entt::registry& reg, entt::entity e,
                        const ecs::SubworldAi& a) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_ai(*o, s, a);
}
// Позиция — БЕЗУСЛОВНАЯ колонка слота (ломоть 5): ни бита, ни сентинела —
// бесместного объекта в кубе арены не бывает, nullptr здесь значит ровно
// «слота нет» (транзит-фикстура без арены). Писатели законные: alloc()
// при рождении (позиция — его аргумент), мувер x/y (разброс), проходы
// земли/полёта z, ребейз шва, снарядный тик, телепорты смоуков.
inline ecs::Position* body_pos(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_pos(*o, s) : nullptr;
}
inline const ecs::Position* body_pos(const entt::registry& reg,
                                     entt::entity e) {
    return body_pos(const_cast<entt::registry&>(reg), e);
}
inline ecs::VisualPos* body_visual(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_visual(*o, s) : nullptr;
}
inline const ecs::VisualPos* body_visual(const entt::registry& reg,
                                         entt::entity e) {
    return body_visual(const_cast<entt::registry&>(reg), e);
}
inline void set_body_visual(entt::registry& reg, entt::entity e,
                            ecs::VisualPos v) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_visual(*o, s, v);
}
inline ecs::GoingHome* going_home(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    if (o == nullptr) return nullptr;
    return slot_going_home(*o, s);
}
inline const ecs::GoingHome* going_home(const entt::registry& reg,
                                        entt::entity e) {
    return going_home(const_cast<entt::registry&>(reg), e);
}
inline void set_going_home(entt::registry& reg, entt::entity e,
                           ecs::GoingHome g) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_going_home(*o, s, g);
}
// «Домой» гаснет ОДНИМ битом (числа цели остаются — их читает только
// открытый бит); «в воздухе» гаснет битом И зануляет вертикаль, образцом
// clear_body_combat/clear_body_light: протухший флоат не переживает роль.
// Оба зануления живут в телах slot_clear_*.
inline void clear_going_home(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_clear_going_home(*o, s);
}
inline float* airborne_vz(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_airborne_vz(*o, s) : nullptr;
}
inline float* set_airborne(entt::registry& reg, entt::entity e, float vz) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_set_airborne(*o, s, vz) : nullptr;
}
inline void clear_airborne(entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_clear_airborne(*o, s);
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
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    return o != nullptr ? slot_projectile(*o, s) : nullptr;
}
inline const ecs::Projectile* projectile_of(const entt::registry& reg,
                                            entt::entity e) {
    return projectile_of(const_cast<entt::registry&>(reg), e);
}
inline bool is_projectile(const entt::registry& reg, entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return false;
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr && slot_is_projectile(*o, s);
}
inline void set_projectile(entt::registry& reg, entt::entity e,
                           const ecs::Projectile& p) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_projectile(*o, s, p);
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
                             const ecs::Projectile& p,
                             const ecs::Position& at) {
    SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return true;
    const int slot = objs->alloc(at);
    if (slot < 0) return false;
    reg.emplace<ecs::ObjectSlot>(e, std::uint16_t(slot));
    set_projectile(reg, e, p);
    return true;
}

inline const ecs::MissileAttack* body_missile(const entt::registry& reg,
                                              entt::entity e) {
    const int s = body_slot(reg, e);
    if (s < 0) return nullptr;
    const SubObjects* o = objects_find(const_cast<entt::registry&>(reg));
    return o != nullptr ? slot_missile(*o, s) : nullptr;
}
inline void set_body_missile(entt::registry& reg, entt::entity e,
                             ecs::MissileAttack m) {
    const int s = body_slot(reg, e);
    if (s < 0) return;
    if (SubObjects* o = objects_find(reg)) slot_set_missile(*o, s, m);
}

// The three bars (CANON S14). Damage, casting, harvesting and crafting all
// land here — «действия платят в склад», now stated once for every body
// rather than once for the player and once for everyone else. Store-первый
// (рана ложится на ЗАПИСЬ зеркального тела), фолбэк — колонка арены
// (кусок 2; прежде — компонента через state_of).
inline ecs::Pools* pools_of(entt::registry& reg, entt::entity body) {
    const int s = body_slot(reg, body);
    if (s < 0) return nullptr;
    SubObjects* o = objects_find(reg);
    if (o == nullptr) return nullptr;
    MacroStore* const* st = reg.ctx().find<MacroStore*>();
    // Стора нет (фикстура с голым реестром) — «записи нет», тот же ответ,
    // что у протухшего бэклинка: бары берутся колонкой.
    if (st == nullptr) return slot_pools(*o, s);
    return slot_pools_of(*o, s, **st);
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
inline bool is_avatar(const entt::registry& reg, entt::entity e) {
    if (e == entt::null || !reg.valid(e)) return false;
    SubObjects* const* po = reg.ctx().find<SubObjects*>();
    if (po == nullptr || !avatar_ref_live(**po)) return false;
    const auto* os = reg.try_get<ecs::ObjectSlot>(e);
    return os != nullptr && os->slot == (*po)->avatar.slot;
}
inline entt::entity avatar_entity(const entt::registry& reg) {
    SubObjects* const* po = reg.ctx().find<SubObjects*>();
    if (po == nullptr || !avatar_ref_live(**po)) return entt::null;
    const entt::entity e = entt::entity((*po)->avatarEnttBits);
    return reg.valid(e) ? e : entt::null;
}

} // namespace sm::sub

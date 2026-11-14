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

#include <algorithm>         // std::max/std::min — двери зеркала баров
#include "ecs/components.h"
#include "bus/store.h"
#include "sub/objects.h"    // SubObjects — единый массив объектов сцены (M-150);
                            // он же несёт bus/anketa.h (колонки листа и
                            // зеркала, ломоть 2)
#include "sub/doors.h"      // слот-формы ВСЕХ дверей арены — ЕДИНСТВЕННОЕ тело
                            // каждого закона; двери ниже лишь переводят
                            // сущность в слот (ломоть 7, ступень ii)
#include "tables/stock_key.h" // словарь рядов ведомости: расписка займа головы
                            // называет свой ряд числом, и дверь ниже спрашивает
                            // ИМЕННО ряд существ (каталог, не состояние — §11)

namespace sm::sub {

// ── ШОВНЫЕ СЛОТ-ДВЕРИ (стор — ЯВНЫЙ параметр; это двери ШВА, не
// арены — дверь арены о записи не знает, шапка doors.h) ────────────
// ── Запись зеркального тела: стор — ЯВНЫЙ параметр ──────────────────────
// Бэклинк + валидность записи; невалидный хэндл = тело само себе запись
// (протухший бэклинк деградирует в тело, не в ничто — шапка record.h).
//
// РАСПИСКА СИЛЬНЕЕ ЗЕРКАЛА (ломоть 7 ЗАХВАТА, M-421). Голова инвентаря,
// несущая ТОЧКУ ВЕДЕНИЯ сквада (перенавешенный бэклинк после нокаута
// лидера), ОСТАЁТСЯ ГОЛОВОЙ: бьётся СВОЕЙ строкой, листа записи не носит
// (ЗАКОН ДВУХ ТАБЛИЦ: у объекта анкеты нет) и актором репутации не
// становится (вердикт: «именно аватаоры а не их инвентарь»). Бэклинк на
// ней — только точка: её читают fold/пины/курс напрямую через
// slot_macro_origin, а всё ЗЕРКАЛЬНОЕ (бары, лист, гир, стандинг) гейтится
// здесь. До M-421 тел с распиской И бэклинком не существовало — гейт
// ничего не менял бы.
inline MacroHandle slot_macro_record(const SubObjects& o, int s,
                                     const MacroStore& st) {
    if (const auto* debt = slot_debt(o, s);
        debt && debt->stock == std::uint8_t(MacroStock::Creatures))
        return MacroHandle{};
    const MacroHandle rec = slot_macro_origin(o, s);
    if (rec.id == 0 || !st.valid(rec)) return MacroHandle{};
    return rec;
}
// ЧЬИМ ВОПЛОЩЕНИЕМ СТОИТ ЭТО ТЕЛО — лестница ДВУХ честных рождений, одно
// написание (M-319A). Та же пара, что несёт шапка файла: тело-АВАТАР назвал
// свою запись бэклинком (`MacroOrigin`), а производная ГОЛОВА инвентаря —
// распиской займа (`MacroDebt::subject` есть ординал сквада, чьим существом
// она стоит). Ответ «никак» (пустой хэндл) законен и значит «ничей»: зверь,
// роллённый из сида клетки, проп, тело фикстуры — наверху о них не помнит
// никто, и спрашивать не у кого.
//
// ТРЕТЬЕЙ СТУПЕНИ ЗДЕСЬ НЕТ СОЗНАТЕЛЬНО: «тело игрока / его солдат → сквад
// игрока» требует ФРЕЙМА (род 4), а дверь шва мира не видит и видеть не
// должна — звонящий, у которого фрейм есть, добирает её сам поверх этого
// ответа (жнец XP в `src/sub/engine.cpp`).
inline MacroHandle slot_squad_of(const SubObjects& o, int s,
                                 const MacroStore& st) {
    if (const MacroHandle rec = slot_macro_record(o, s, st); rec.id != 0)
        return rec;
    if (const auto* debt = slot_debt(o, s);
        debt && debt->stock == std::uint8_t(MacroStock::Creatures)) {
        return macro_handle_by_spawn_id(st, std::uint32_t(debt->subject));
    }
    return MacroHandle{};
}

// Состояние записи (сумка/черты/гир/книга): ТОЛЬКО запись — самозаписи нет
// (0a ломтя 7).
template <class C>
inline C* slot_state(const SubObjects& o, int s, MacroStore& st) {
    const MacroHandle rec = slot_macro_record(o, s, st);
    if (rec.id == 0) return nullptr;
    return body_state<C>(st, rec);
}
// Бары: store-первый (рана ложится на ЗАПИСЬ зеркального тела), фолбэк —
// колонка арены (кусок 2).
inline ecs::Pools* slot_pools_of(SubObjects& o, int s, MacroStore& st) {
    const MacroHandle rec = slot_macro_record(o, s, st);
    if (rec.id != 0) {
        if (ecs::Pools* owned = body_state<ecs::Pools>(st, rec)) return owned;
    }
    return slot_pools(o, s);
}
// Лист — ТОТ ЖЕ закон, что у баров: запись первой, фолбэк — колонка sheet
// СВОЕГО слота (компонента CharacterSheet умерла ломтём 2; пустой лист —
// законное значение, ЗАКОН АНКЕТЫ п.5). Дверь ОДНА на вопрос «чей это
// лист» ((ii-б) ломтя 7): до неё закон жил ДВУМЯ носителями — специализация
// state_of<CharacterSheet> для сущности и РУЧНАЯ пара «slot_state + фолбэк
// на колонку» у каста (spell_book.cpp), то есть два ответа на один вопрос
// мира (DOD п.6). Фолд остальных владений (сумка/черты/гир/книга) не
// тронут — они состояние ЗАПИСИ и колонками арены не становятся никогда.
inline CharacterSheet* slot_sheet_of(SubObjects& o, int s, MacroStore& st) {
    const MacroHandle rec = slot_macro_record(o, s, st);
    if (rec.id != 0) {
        if (CharacterSheet* owned = body_state<CharacterSheet>(st, rec))
            return owned;
    }
    return slot_sheet(o, s);
}

// ЗЕРКАЛО БАРОВ — КОПИЯ ЗАПИСИ, ОДНО НАПИСАНИЕ НА ОБОИХ ЗВОНЯЩИХ: рождение
// аватара и тик-топ зеркала (`spawn_player_entity` и
// `mirror_bodies_from_record` в `src/sub/engine.cpp`; прежде каждый писал эту
// арифметику сам). У шкалы есть ВЕРХ — бар не выше потолка, потолок не ниже
// единицы (страж делителя у читателей доли бара), — и НЕТ НИЗА: ноль здоровья
// есть СОБЫТИЕ, а не край шкалы (ЗАКОН КЛАМПА), и −150 против −2 — разница
// между смертью и разрывом тела, которую зеркало обязано донести до конца
// шага. Здесь стоял `clamp(hp, 0, maxHp)` — он стирал её у всякого тела,
// павшего ниже нуля, при том что запись (дверь урона пишет её свободно в
// минус, `apply_damage@src/sub/damage.cpp`) её помнила.
inline ecs::Pools mirror_of_record(const ecs::Pools& record) {
    ecs::Pools m = record;
    m.maxHp = std::max(1, m.maxHp);
    m.hp = std::min(m.hp, m.maxHp);
    return m;
}


// (Мост единого массива объектов — objects_of/objects_find/objects_attach —
// УМЕР ломтём E вместе с реестром: арена ходит ЯВНЫМ параметром всюду, от
// движка до фикстур. Хук on_destroy умер ещё ломтём D вместе с ObjectSlot:
// слот освобождает каждый путь смерти сам, рукой arena.free — она
// идемпотентна, добивающие свипы сцены безвредны по построению.)

// (body_slot и entity-двери аватара — set_avatar/avatar_entity — умерли
// ломтём D вместе с ObjectSlot: тело адресуется слотом, активное тело —
// ссылка ObjRef, пишет её одна дверь set_avatar_slot@sub/doors.h, живость
// судит avatar_ref_live там же. FIELD-адаптеры умерли раньше, ломтём C.)

} // namespace sm::sub

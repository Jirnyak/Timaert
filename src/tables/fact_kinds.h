// ТАБЛИЦА ДЕЯНИЙ — авторские колонки интереса и славы на словарь FactKind.
//
// Переехала из macro/chronicle.h ломтём 5 M-171: каталог — `constexpr`-строки
// по обе стороны границы (AGENTS §11, ФРЕЙМ п.8), и субмир читает его law-
// фully (`fact_kind_def` — горизонт дедупа зон), а ЛЕТОПИСЬ (лог, двери
// записи и чтения) осталась макро-слоем в macro/chronicle.h: она состояние
// мира, не каталог. Выведенные из таблицы законы славы едут ВМЕСТЕ с ней —
// число в прозе рядом с таблицей есть второй словарь (ЗАКОН СЛОВАРЯ п.3).
#pragma once

#include "bus/fact.h"
#include "core/table_guard.h"

#include <cstdint>

namespace sm {

// ── THE TABLE OF DEEDS ───────────────────────────────────────────────────
//
// What the WORLD decided to remember — not what a mechanism happened to do.
// The distinction is the owner's (2026-08-27) and it is load-bearing:
//
//     «интеракшн это просто факт, и он КОНТЕКСТНЫЙ, и уже контекст даёт славу»
//
// The subworld hangs an INTERACTION on any place or object, and what an
// interaction means can be anything — opening a door, drinking from a well,
// draining a spire. So "Interacted" is not a deed; it is a mechanism, and a
// generic row for it could carry no honest weight: whatever number it held
// would be wrong for a door or wrong for a spire.
//
// Therefore the CONTEXT chooses the deed, and the deed carries the weight. Most
// interactions choose none at all — a door opened is not something the world
// remembers, and a chronicle that filed it would be a chronicle of doors.
//
// A row, so a new deed is one line and no code. The columns are what a READER
// needs in order to judge a fact without understanding its kind: how long it
// stays news, and what it makes of the doer.
struct FactKindDef {
    // MUST equal the row's index in kFactKinds (guard below the table).
    FactKind    id;
    const char* key;      // authoring id; runtime addresses by ordinal
    const char* label;    // what a human reads
    // How long this kind stays INTERESTING, in days. Not a lifetime — the
    // chronicle keeps every fact for the whole epoch — but what a reader means
    // by "recent" when it asks about this kind. A monster's kill is news for a season; a trade is
    // stale in a week.
    std::uint16_t interestDays;
    // WHAT DOING THIS IS WORTH BY ITSELF — the deed's BASE, for a deed done
    // to nobody in particular. Felling a forest is 1; making a field is a
    // little more; finding a thing the world did not know is more again.
    //
    // It is a BASE and not the answer, because the answer is contextual
    // (owner, 2026-08-27): what a deed is worth depends on WHAT IT WAS DONE
    // TO. See `renown_for_deed` below — this column is the half that does not
    // need a victim.
    std::uint16_t baseRenown;
    // DOES THE OBJECT'S FAME RUB OFF? The victim's share (a tenth, below) is
    // paid for deeds done AGAINST somebody — killing, robbing, beating a
    // renowned one makes a name. A MUTUAL exchange pays no share (owner,
    // 2026-08-29: «не надо чтобы за торговлю давалась слава партнёра,
    // торговля — маленькое дело»): before this column, the daily caravan
    // Traded fact tithed the partner city's renown to the home city, and
    // commerce out-famed war. Default true, because every other kind with an
    // object IS a deed against it.
    bool victimShare = true;
};

inline constexpr FactKindDef kFactKinds[] = {
    // Rows stand in ENUM ORDER — the guard below enforces it, and it caught a
    // reshuffle-for-readability the moment it was tried. Grouping lives in the
    // comments; the order belongs to the enum.
    //                                                    interest   base
    {FactKind::None,         "none",          "—",              0,      0},
    // Deeds against SOMEBODY. Their base is small on purpose: what these are
    // really worth comes from WHOM they were done to (renown_for_deed).
    {FactKind::Killed,       "killed",        "Killed",        32,      1},
    // victimShare false (owner 2026-09-02: «что это за слава за то, что
    // умер — это бред»): dying to a famous killer ennobles NOBODY — the
    // bereaved home files the loss for its READERS (grudges, revenge
    // quests, the danger price), not for standing.
    {FactKind::Died,         "died",          "Died",          32,      0, false},
    // (РЯД `Battle` СНЕСЁН 2026-11-03 вместе со своим родом — ноль
    //  отправителей, DOD п.9; некролог в `src/bus/fact.h`.)
    // What a place suffers or does. It earns the PLACE its standing.
    // victimShare false: a deal is mutual, not a deed against the partner.
    {FactKind::Traded,       "traded",        "Traded",         8,      0, false},
    // Deeds against the LAND, which has no standing of its own — so their base
    // IS the answer, and it is small.
    // (РЯДЫ `Gathered`/`Built`/`OwnerChanged`/`Spawned` СНЕСЕНЫ 2026-11-03
    //  вместе со своими родами — у каждого было НОЛЬ отправителей во всём
    //  дереве (DOD п.9, вердикт владельца «Снести сирот сейчас»); поимённые
    //  некрологи с причиной — в `src/bus/fact.h`. С ними ушёл и пример
    //  владельца про «срубить лес — дело, которое мир замечает немного»: его
    //  место займёт та строка, которую заведут вместе с механикой.)
    {FactKind::Starved,      "starved",       "Starved",       32,      0},
    {FactKind::Explored,     "explored",      "Explored",      64,      2},
    {FactKind::Drained,      "drained",       "Drained",       64,     10},
    {FactKind::QuestTaken,   "quest_taken",   "Took contract", 32,      1},
    {FactKind::QuestDone,    "quest_done",    "Kept contract", 32,      5},
    // Weekly-interest weather like Traded: the tithe is mutual order, not a
    // deed against the partner.
    {FactKind::Taxed,        "taxed",         "Paid tithe",     8,      0, false},
};
static_assert(sizeof(kFactKinds) / sizeof(kFactKinds[0])
                  == std::size_t(FactKind::Count),
              "kFactKinds must carry one row per FactKind");
static_assert(rows_in_enum_order(kFactKinds, &FactKindDef::id),
              "kFactKinds rows must stand in FactKind order");

inline constexpr const FactKindDef& fact_kind_def(FactKind k) {
    return kFactKinds[std::size_t(k)];
}

// САМЫЙ ДЛИННЫЙ ГОРИЗОНТ ИНТЕРЕСА — ВЫВЕДЕН ИЗ ТАБЛИЦЫ, А НЕ НАЗВАН В ПРОЗЕ
// (M-254, 2026-10-12). Читатель — окно фактов фрейма
// (`fill_local_frame@src/macro/frame_fill.cpp`): горизонт окна есть самый
// длинный интерес таблицы. До той правки он жил ЧИСЛОМ В КОММЕНТАРИИ — «64 дня
// (строки Built/Explored)». Число в прозе рядом с таблицей, из которой оно
// выводится, есть второй словарь (ЗАКОН СЛОВАРЯ п.3): правка строки таблицы
// молча расходилась бы с ним. (Заведён он был первым членом формулы размера
// забывающего кольца летописи; кольца нет с M-423, читатель окна остался.)
inline constexpr std::uint16_t chronicle_longest_interest_days() {
    std::uint16_t longest = 0;
    for (const FactKindDef& d : kFactKinds) {
        if (d.interestDays > longest) longest = d.interestDays;
    }
    return longest;
}
inline constexpr std::uint16_t kChronicleLongestInterestDays =
    chronicle_longest_interest_days();

// ── WHAT A DEED IS WORTH: THE VICTIM ANSWERS ─────────────────────────────
//
// Owner's ruling, 2026-08-27: renown is CONTEXTUAL, and the context is what
// the deed was done TO. «Уже у нас есть стоимость/уровень, и если это сквад —
// ЕГО СЛАВА; естественно, убийство славного делает славным.»
//
// So there was no "importance" number to invent and balance: every macro
// entity already carries what it is worth, and that number is its own renown.
// Killing a nobody is worth the deed's base; killing a legend is worth a share
// of the legend. The rule is recursive BY CONSTRUCTION — fame is made of fame
// — and nothing had to be added to say so.
//
// THE SHARE is a tenth, and the number is the sentence rather than a dial:
//
//     ten victories over a man's equals make you his equal.
//
// Less, and a lifetime of war never makes a name; more, and one lucky ambush
// crowns a nobody.
inline constexpr int kRenownShareDivisor = 10;

inline constexpr std::uint32_t renown_for_deed(FactKind kind,
                                               std::uint32_t victimRenown) {
    if (kind >= FactKind::Count) return 0u;
    const FactKindDef& d = fact_kind_def(kind);
    // The share is paid for deeds AGAINST somebody; a mutual deal pays no
    // share (`victimShare` — owner, 2026-08-29).
    return std::uint32_t(d.baseRenown)
         + (d.victimShare
                ? victimRenown / std::uint32_t(kRenownShareDivisor) : 0u);
}

// WHEN A NOBODY BECOMES SOMEBODY — and the number is DERIVED, not chosen: it
// is the most any single deed is worth AGAINST A NOBODY. So the rule reads as
// a sentence about the world rather than as a threshold —
//
//     you become a figure by doing ONCE what a figure does,
//     or by adding up enough lesser things,
//     or by beating somebody who already was one.
//
// That third road is the recursion paying off. Retune a row and the bar moves
// with it, because the bar IS the table.
inline constexpr int renown_to_be_named() {
    int most = 0;
    for (const FactKindDef& d : kFactKinds) {
        if (int(d.baseRenown) > most) most = int(d.baseRenown);
    }
    return most;
}
inline constexpr int kRenownToBeNamed = renown_to_be_named();

// Is this band a figure yet? ONE number decides, so nothing can disagree with
// it — there is no flag beside the counter to fall out of step.
inline constexpr bool renown_is_named(std::uint32_t renown) {
    return renown >= std::uint32_t(kRenownToBeNamed);
}
static_assert(kRenownToBeNamed > 0,
              "if no deed is worth renown, nobody could ever become a figure");

} // namespace sm

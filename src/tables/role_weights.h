// ВЕСА РОЛЕЙ — таблица по ординалу существа (одна строка на NPCType): куда
// строка тратит бюджет атрибутов и скиллов при генерации анкеты. КАТАЛОГ, не
// состояние: та же строка по обе стороны границы миров (наряд M-181; жила в
// macro/character_sheet.h — слияние анкеты выделило её сюда, потому что
// таблица по ординалу существа живёт в tables/, а не в анкете).
#pragma once

#include "core/table_guard.h"
#include "tables/attributes.h"
#include "tables/npc.h"

#include <cstddef>
#include <cstdint>

namespace sm {

// Per-role stat emphasis. Weights are RELATIVE — the level's point budget is
// distributed across them, so only the ratios matter, not the magnitudes.
// Every attribute keeps a floor weight of 1 so no stat is ever pinned at its
// base of 1. This is pure tunable data (see MASTER_PROMPT §9.3); adding a
// role = one more row.
//
// ЗДЕСЬ СТОЯЛО «and every row has at least one non-zero skill weight» — ЭТО
// БЫЛО НЕВЕРНО, и неверно задолго до 2026-10-12: у `NPCType::Dragon` все
// веса навыков НУЛЕВЫЕ, о чём говорит собственный комментарий его строки
// («скиллы зверя пусты — его бой живёт в комбат-шаблоне»). Пустая строка
// ЗАКОННА, и дверь генерации её держит: `make_character_sheet@src/macro/
// anketa.h` выходит из цикла обучения, когда взвешенных навыков в роде нет
// («fewer than 5 weighted skills in the row»). Выдумывать дракону вес ради
// красоты обещания запрещено — это подгонка СИСТЕМЫ ПОД ЧИСЛА наоборот.
// ── ПОЛНЫЙ НАБОР ВЕСОВ — ТИП, КОТОРЫЙ НЕЛЬЗЯ СОБРАТЬ НЕПОЛНЫМ (M-251) ─────
//
// ЗДЕСЬ СТОЯЛИ ДВА ГОЛЫХ МАССИВА, И ШАПКА НАД НИМИ ОБЕЩАЛА ДОСЛОВНО: «adding
// a skill to the registry makes every role state what it thinks of it — a
// compile error is the right way to ask that question, and a silent zero is
// the wrong one». ОБЕЩАНИЕ НЕ ИСПОЛНЯЛОСЬ НИКОГДА: позиционному списку C++
// разрешено быть КОРОЧЕ своего массива, поэтому хвост получал ровно тот
// молчаливый ноль, который текст объявлял невозможным, и компилятор вопроса
// не задавал. Замер 2026-10-12: инициализаторов в строке было 33 при массиве
// длиной 37 (после реза M-144 — 29 при 33), то есть ЧЕТЫРЕ РЕМЕСЛА не были
// названы НИ ОДНИМ из 46 родов.
//
// ПОЧЕМУ НЕ designated initializers, КОТОРЫХ ЖДАЛ РЕЕСТР: `[SkillId::Sword] =
// 2` — это C99, а не C++; C++20/23 дали их только ПОЛЯМ СТРУКТУРЫ, и даже там
// поле разрешено НЕ НАЗВАТЬ. Они сделали бы пропуск заметным, но не
// несобираемым, то есть лечили бы симптом.
//
// ПОЧЕМУ НЕ КОЛОНКА-ПЕЧАТЬ («последняя клетка обязана быть магическим
// байтом»): она отвечает на вопрос компилятора ЦЕНОЙ КОЛОНКИ С НУЛЁМ
// РАНТАЙМ-ЧИТАТЕЛЕЙ, то есть платит сиротой (DOD п.9) и кладёт в строку мира
// фальшивые данные.
//
// ЧТО СТОИТ ВМЕСТО НИХ: набор есть ТИП, а дверь к нему — функция, у которой
// АРИТЕТ вызова и есть условие существования. Неполный набор собрать нечем, и
// сообщение называет закон. Пустота при этом по-прежнему законна — но она
// становится ВЫСКАЗЫВАНИЕМ («этот род о ремёслах думает ноль»), а не
// умолчанием. Негативный контроль прогнан в обе стороны: снятый вес и
// добавленный в реестр навык дают `static_assert`, правка прозы молчит.
struct AttrWeights {
    std::uint8_t w[std::size_t(AttributeId::Count)];
    constexpr std::uint8_t operator[](std::size_t i) const { return w[i]; }
};
struct SkillWeights {
    std::uint8_t w[std::size_t(SkillId::Count)];
    constexpr std::uint8_t operator[](std::size_t i) const { return w[i]; }
};

template <class... A>
constexpr AttrWeights role_attrs(A... a) {
    static_assert(sizeof...(A) == std::size_t(AttributeId::Count),
                  "строка весов обязана высказаться о КАЖДОМ атрибуте: "
                  "аргументов ровно AttributeId::Count, иначе хвост получил "
                  "бы молчаливый ноль (M-251)");
    return AttrWeights{{std::uint8_t(a)...}};
}
template <class... A>
constexpr SkillWeights role_skills(A... a) {
    static_assert(sizeof...(A) == std::size_t(SkillId::Count),
                  "строка весов обязана высказаться о КАЖДОМ навыке: "
                  "аргументов ровно SkillId::Count. Назвал новый навык в "
                  "реестре — ответь за него в каждом роде; ноль законен, "
                  "молчание нет (M-251)");
    return SkillWeights{{std::uint8_t(a)...}};
}

struct RoleWeights {
    // MUST equal the row's index in kRoleWeights (guard below the table).
    NPCType type;
    // AttributeId order, ПОЛНЫМ НАБОРОМ — см. контур выше.
    AttrWeights attr;
    // SkillId order; the weighted pick indexes this directly. Полным набором.
    SkillWeights skill;
};
// Attr columns follow the canon-eight order (2026-09-03). The old VIT column
// merged into END as max(vit, end) — whichever of the two carried the row's
// identity (the bear's VIT, the rabbit's END) keeps it.
//
// Skill columns follow the canon-32 order (skills-64, same date). A role's
// weights answer TWO questions with one row: which 5 skills its people LEARN
// at creation (weighted picks, no repeats) and where their level points go
// (weighted spends into the learned set only). The flavor writes itself:
// the guard's blade is his trade (Sword) and his coat is Heavy Armor; a beast
// fights through Armsmaster (its body IS the weapon) and wears Unarmored.
//
// skills: Swd Axe Spr Mac Dag Bow Stf|Hvy Lgt Una Shd|Fir Wat Air Ear Arc Voi|
//         Arm Spl|Bod Med Mar Ath Wgt|Trv Acr Sct|Trd|Unr|Bsm Tlr Msn Alc
// (Unr = Unarmed, appended v79 like its enum row; 0 everywhere until a
// brawler role wants it — beasts keep fighting through Armsmaster.
// Bsm/Tlr/Msn/Alc — РЕМЁСЛА: ноль у всех 46 родов, и это ВЫСКАЗЫВАНИЕ, а
// не умолчание — ремесло ОТКРЫВАЕТ РЕЦЕПТ, а не множит число, и анкета,
// сгенерированная ремесленником, никого ремесленником не делает, пока у
// мира нет двери найма ремесла.)
//
// ЧЕТЫРЕ КОЛОНКИ УШЛИ ИЗ ЭТОЙ ЛЕГЕНДЫ 2026-10-12 (M-144): `Prs` стояла
// четвёртой в дорожной группе, `Qtm`/`For`/`Lrn` — хвостом хозяйственной.
// Снято по одной клетке из КАЖДОЙ из 46 строк, позиционно; что все выжившие
// веса остались БАЙТ В БАЙТ теми же, доказано снимком таблицы ПО ИМЕНАМ до и
// после реза (1702 пары «род · навык» → 1518, различий в выживших 0).
//
// И ЧЕТЫРЕ КОЛОНКИ РЕМЁСЕЛ ВЕРНУЛИСЬ В ЭТУ ЛЕГЕНДУ ТОГО ЖЕ ДНЯ (M-251): их
// тут не было ВОВСЕ, потому что строка несла 29 инициализаторов в массиве
// длиной 33 и хвост молча нулевался — ровно то, что шапка объявляла
// невозможным. Теперь каждая строка зовёт `role_skills` ПОЛНЫМ набором,
// аритет судит КОМПИЛЯТОР (контур выше), а веса сверены ПО ИМЕНАМ заново:
// 1518 пар, различий 0. Ни одна анкета мира не изменилась, и не могла —
// молчаливый ноль и явный ноль суть одно ЗНАЧЕНИЕ; правка меняет не числа, а
// то, КТО ЗА НИХ ОТВЕЧАЕТ.
inline constexpr RoleWeights kRoleWeights[int(NPCType::Count)] = {
    // Peasant     — hardy laborer: pitchfork and flail, forage and endure
    {NPCType::Peasant, role_attrs(3, 3, 1, 1, 1, 1, 1, 2),
     role_skills(0,0,2,1,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 1,0, 3,0,3,1,2, 1,0,0, 0, 0, 0,0,0,0)},
    // Merchant    — social, lucky, sedentary: the ledger, not the blade
    {NPCType::Merchant, role_attrs(1, 2, 2, 1, 1, 3, 4, 3),
     role_skills(0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,1,0,2, 2,0,0, 4, 0, 0,0,0,0)},
    // Caravan     — mobile trader: lives on the road, hence the road skills
    // Cha 5: the trade house on wheels — its whole market edge is this row
    // (owner 2026-08-30: «у каравана в таблице выше уровень и харизма»).
    // Bandit      — aggressive raider: knife and bow, fast on his feet
    {NPCType::Bandit, role_attrs(4, 3, 1, 1, 3, 2, 1, 1),
     role_skills(2,0,0,0,3,2,0, 0,2,1,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,1, 2,1,2, 0, 0, 0,0,0,0)},
    // Guard       — disciplined tank: sword, shield and heavy plate
    {NPCType::Guard, role_attrs(4, 4, 1, 1, 2, 1, 2, 2),
     role_skills(3,0,2,1,0,0,0, 3,0,0,3, 0,0,0,0,0,0, 2,0, 3,0,2,1,1, 1,0,0, 0, 0, 0,0,0,0)},
    // Witch       — practical caster: hedge schools, a staff to lean on
    {NPCType::Witch, role_attrs(1, 2, 4, 4, 1, 2, 2, 3),
     role_skills(0,0,0,0,1,0,2, 0,0,2,0, 2,2,0,2,0,1, 0,4, 0,4,1,1,0, 1,0,0, 0, 0, 0,0,0,0)},
    // Sorceress   — elite caster: the high schools, arcane first
    {NPCType::Sorceress, role_attrs(1, 2, 5, 4, 1, 3, 3, 3),
     role_skills(0,0,0,0,0,0,2, 0,1,2,0, 3,0,2,0,3,2, 0,5, 0,4,1,1,0, 1,0,0, 0, 0, 0,0,0,0)},
    // (Здесь стояли подписи `Miner` и `Clay-digger` — РОДОВ С ТАКИМИ ИМЕНАМИ
    // В `NPCType` НЕТ, профессии добычи ушли из реестра раньше, и подписи
    // переживали свои строки. Единственным их содержанием был «глаз, читающий
    // жилу», то есть `Prospecting`, снесённый 2026-10-12 M-144, — так что
    // чинить тут было нечего, кроме удаления.)
    // ── The creature rows ────────────────────────────────────────────────
    // A beast has a sheet like a man has a sheet (owner, 2026-08-20: one
    // system, "у всех лист статов как в обливионе"). What differs is only the
    // EMPHASIS, and it is the same five shapes over and over: prey is all
    // endurance and speed, a grazer is prey with mass, a predator buys speed
    // with muscle, a brute is muscle without speed, and the unnatural things
    // are built like soldiers because that is what they fight as. A beast's
    // weapon skill is Armsmaster (its body is the weapon) and its armor is
    // Unarmored (its hide is the coat); prey reads the wind (Scouting).
    {NPCType::Rabbit,       role_attrs(1, 4, 1, 1, 5, 3, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 0,0, 1,0,3,5,0, 2,3,2, 0, 0, 0,0,0,0)},
    {NPCType::Deer,         role_attrs(2, 4, 1, 1, 5, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 2,0,3,5,0, 2,2,2, 0, 0, 0,0,0,0)},
    {NPCType::Fox,          role_attrs(2, 3, 2, 1, 4, 3, 1, 2),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 2,0, 1,0,2,4,0, 2,1,3, 0, 0, 0,0,0,0)},
    {NPCType::Wolf,         role_attrs(4, 3, 1, 1, 4, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 4,0, 2,0,2,3,0, 2,0,3, 0, 0, 0,0,0,0)},
    {NPCType::Bear,         role_attrs(5, 5, 1, 1, 2, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,1,0, 1,0,1, 0, 0, 0,0,0,0)},
    {NPCType::Boar,         role_attrs(4, 4, 1, 1, 3, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 4,0,1,2,0, 1,0,1, 0, 0, 0,0,0,0)},
    {NPCType::Snake,        role_attrs(3, 2, 1, 1, 3, 3, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 4,0, 1,0,1,2,0, 1,0,1, 0, 0, 0,0,0,0)},
    {NPCType::Hawk,         role_attrs(2, 3, 2, 1, 5, 3, 1, 2),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 2,0, 1,0,2,5,0, 2,0,3, 0, 0, 0,0,0,0)},
    {NPCType::Frog,         role_attrs(1, 3, 1, 1, 3, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 1,0,2,3,0, 1,3,0, 0, 0, 0,0,0,0)},
    {NPCType::Goat,         role_attrs(2, 4, 1, 1, 4, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,4,0, 3,3,0, 0, 0, 0,0,0,0)},
    {NPCType::Eagle,        role_attrs(2, 3, 2, 1, 5, 3, 1, 2),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 3,0, 1,0,2,5,0, 2,0,3, 0, 0, 0,0,0,0)},
    {NPCType::Croc,         role_attrs(4, 4, 1, 1, 2, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 4,0,1,1,0, 1,0,1, 0, 0, 0,0,0,0)},
    {NPCType::Goblin,       role_attrs(3, 3, 2, 1, 3, 2, 1, 1),
     role_skills(0,0,1,0,2,1,0, 0,1,1,0, 0,0,0,0,0,0, 2,0, 2,0,2,3,1, 2,0,1, 0, 0, 0,0,0,0)},
    {NPCType::Skeleton,     role_attrs(3, 4, 1, 1, 2, 1, 1, 1),
     role_skills(2,0,1,0,0,0,0, 1,0,1,1, 0,0,0,0,0,0, 3,0, 3,0,1,1,0, 0,0,0, 0, 0, 0,0,0,0)},
    {NPCType::Troll,        role_attrs(5, 5, 1, 1, 1, 1, 1, 1),
     role_skills(0,0,0,2,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,1,0, 1,0,0, 0, 0, 0,0,0,0)},
    {NPCType::SwampThing,   role_attrs(4, 4, 1, 2, 1, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,2,0,1,0,0, 3,1, 4,1,1,1,0, 0,0,0, 0, 0, 0,0,0,0)},
    // Ice is the Water school (S15 remap): the wraith casts what it is.
    {NPCType::IceWraith,    role_attrs(3, 3, 3, 4, 3, 2, 1, 2),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,3,1,0,0,2, 1,3, 1,3,1,3,0, 1,0,0, 0, 0, 0,0,0,0)},
    {NPCType::SandScorpion, role_attrs(3, 3, 1, 1, 3, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 2,0,1, 0, 0, 0,0,0,0)},
    {NPCType::StoneGolem,   role_attrs(5, 5, 1, 1, 1, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 3,0, 5,0,1,1,0, 0,0,0, 0, 0, 0,0,0,0)},
    // Adventurer — the player's row. Even weights on purpose: a generated
    // adventurer is a blank slate, and the player's OWN points are spent by
    // him, not rolled by this table (it answers only when something asks the
    // world for "an adventurer", e.g. a projected body of his squad).
    {NPCType::Adventurer,   role_attrs(2, 2, 2, 2, 2, 2, 2, 2),
     role_skills(1,1,1,1,1,1,1, 1,1,1,1, 1,1,1,1,1,1, 1,1, 1,1,1,1,1, 1,1,1, 1, 0, 0,0,0,0)},
    // Silver-miner — the miner's body, row for row.
    // Tax-collector — a courier's legs, a clerk's head.
    {NPCType::TaxCollector, role_attrs(2, 3, 2, 2, 3, 1, 2, 2),
     role_skills(0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,2,3,2, 3,0,0, 2, 0, 0,0,0,0)},
    // Road ambusher — the bandit's sheet, row for row: what sets him apart
    // lives in his combat template (the road-wide eye, the HP), not here.
    {NPCType::RoadAmbusher, role_attrs(4, 3, 1, 1, 3, 2, 1, 1),
     role_skills(2,0,0,0,3,2,0, 0,2,1,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,1, 2,1,2, 0, 0, 0,0,0,0)},
    // Дракон — зверь силы и воли: STR/END тяжёлые, интеллект древний;
    // скиллы зверя пусты — его бой живёт в комбат-шаблоне (3d20 Fire),
    // не в оружейных рядах человека.
    {NPCType::Dragon, role_attrs(8, 6, 4, 5, 3, 2, 1, 4),
     role_skills(0,0,0,0,0,0,0, 0,0,0,0, 0,0,0,0,0,0, 0,0, 0,0,0,0,0, 0,0,0, 0, 0, 0,0,0,0)},
    // ── The populated bestiary (2026-09-11) ──────────────────────────────
    // Same reading as the creature rows above: a beast fights through
    // Armsmaster (its body IS the weapon) and wears Unarmored (its hide is
    // the coat); a thing that carries a weapon names it. The sheet is what
    // makes two rows with similar stat blocks feel different at level 5 —
    // the ghoul spends its points on legs, the zombie on Body, so the gap
    // between them WIDENS as the world's danger climbs.
    // attrs: Str End Int Wil Spd Lck Cha Wis
    // Giant rat — legs and teeth, nothing else.
    {NPCType::GiantRat,     role_attrs(1, 3, 1, 1, 5, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 1,0,0,4,0, 0,2,2, 0, 0, 0,0,0,0)},
    // Cave bat — the fastest thing in the table, and the frailest.
    {NPCType::CaveBat,      role_attrs(1, 2, 1, 1, 6, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 1,0,0,5,0, 0,3,3, 0, 0, 0,0,0,0)},
    // Kobold — a scavenger with a knife: spear-and-dagger, and a forager's
    // eye for what was dropped.
    {NPCType::Kobold,       role_attrs(2, 2, 2, 1, 4, 3, 1, 1),
     role_skills(0,0,1,0,2,0,0, 0,1,1,0, 0,0,0,0,0,0, 2,0, 1,0,1,3,0, 1,2,2, 0, 0, 0,0,0,0)},
    {NPCType::CaveSpider,   role_attrs(3, 3, 1, 1, 4, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 0,2,1, 0, 0, 0,0,0,0)},
    // Imp — a spark with legs: Fire is its one school and it never learns
    // a second.
    {NPCType::Imp,          role_attrs(2, 2, 3, 2, 5, 3, 1, 2),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 3,0,0,0,0,0, 1,2, 1,0,0,4,0, 0,3,1, 0, 0, 0,0,0,0)},
    // Zombie — all of it goes into Body. It does not dodge and does not run.
    {NPCType::Zombie,       role_attrs(4, 5, 1, 1, 1, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 3,0, 5,0,1,0,1, 0,0,0, 0, 0, 0,0,0,0)},
    // Orc — a soldier by any other name: axe, heavy coat, march discipline.
    {NPCType::Orc,          role_attrs(4, 4, 1, 2, 3, 2, 1, 1),
     role_skills(1,3,0,0,0,0,0, 2,0,1,0, 0,0,0,0,0,0, 3,0, 3,0,2,2,0, 1,0,1, 0, 0, 0,0,0,0)},
    // Ghoul — the zombie's points, spent on legs instead of meat.
    {NPCType::Ghoul,        role_attrs(3, 3, 1, 1, 4, 2, 1, 1),
     role_skills(0,0,0,0,1,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 0,2,1, 0, 0, 0,0,0,0)},
    {NPCType::Harpy,        role_attrs(2, 3, 1, 2, 5, 3, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 1,0,0,5,0, 0,3,2, 0, 0, 0,0,0,0)},
    // Cultist — the crowd's only educated man: staff, Arcane, and the
    // Meditation that says he chose this. (Его `Learning` 2 снесён вместе с
    // навыком 2026-10-12, M-144; «он выбрал это сам» теперь говорит глубина
    // медитации, а не строка, которую никто не читал.)
    {NPCType::Cultist,      role_attrs(2, 2, 4, 4, 2, 2, 2, 3),
     role_skills(0,0,0,0,0,0,2, 0,1,0,0, 2,0,0,0,3,1, 0,4, 1,3,0,1,0, 0,0,0, 0, 0, 0,0,0,0)},
    // Gargoyle — masonry that flies: Earth in its fists, Body under them.
    {NPCType::Gargoyle,     role_attrs(5, 5, 1, 2, 2, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,2,0,0, 3,0, 4,0,1,2,1, 0,0,0, 0, 0, 0,0,0,0)},
    // Wraith — Void where the ice wraith keeps cold, and the Meditation
    // that makes it a caster's problem rather than a brawler's.
    {NPCType::Wraith,       role_attrs(3, 3, 3, 4, 4, 2, 1, 2),
     role_skills(0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,1,3, 2,3, 1,3,0,3,0, 0,1,0, 0, 0, 0,0,0,0)},
    // Ogre — the heaviest hands in the table under the dragon's.
    {NPCType::Ogre,         role_attrs(6, 6, 1, 1, 1, 1, 1, 1),
     role_skills(0,0,0,2,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,0,2, 0,0,0, 0, 0, 0,0,0,0)},
    // Minotaur — an ogre that kept its legs: the row that punishes running.
    {NPCType::Minotaur,     role_attrs(6, 5, 2, 2, 3, 1, 1, 1),
     role_skills(1,2,0,0,0,0,0, 1,0,2,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 0,0,0, 0, 0, 0,0,0,0)},
    {NPCType::Basilisk,     role_attrs(4, 4, 1, 2, 3, 2, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,2,0,0, 4,0, 3,0,1,1,0, 0,0,1, 0, 0, 0,0,0,0)},
    // Lich — the top of the climb: Will and Intellect above everything,
    // Void and Arcane, and the deepest Spellpower in the table.
    {NPCType::Lich,         role_attrs(2, 3, 6, 6, 1, 2, 2, 5),
     role_skills(0,0,0,0,0,0,2, 0,0,0,0, 1,0,0,0,3,4, 0,5, 1,4,0,0,0, 0,0,0, 0, 0, 0,0,0,0)},
    // Horse — the goat's own build, a size up: body and speed, no craft.
    {NPCType::Horse,        role_attrs(4, 5, 1, 1, 4, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,4,0, 3,3,0, 0, 0, 0,0,0,0)},
    // Sheep — строка козы НА СТУПЕНЬ НИЖЕ, и ровно в двух местах ВЫШЕ: стадо
    // идёт весь день (Marathon 3 против 2, End 4 держится), а всё, чем коза
    // живёт на скале, у овцы падает в ноль — Athletics 4 → 1, Acrobatics
    // 3 → 0, Spd 4 → 2. Armsmaster 1 → 0: бой этой строки живёт в
    // комбат-шаблоне целиком, как у дракона, и пустой вес навыка законен
    // (дверь генерации выходит из обучения, когда взвешенных навыков нет).
    {NPCType::Sheep,        role_attrs(1, 4, 1, 1, 2, 1, 1, 1),
     role_skills(0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 0,0, 2,0,3,1,0, 3,0,0, 0, 0, 0,0,0,0)},
};

static_assert(rows_in_enum_order(kRoleWeights, &RoleWeights::type),
              "kRoleWeights row order must mirror NPCType");

inline const RoleWeights& role_weights(NPCType role) {
    const int idx = int(role);
    if (idx < 0 || idx >= int(NPCType::Count)) return kRoleWeights[0];
    return kRoleWeights[idx];
}

} // namespace sm

// Контур строк файла — судит компилятор (`core/row_law.h`, наряд M-169);
// включение внизу, чтобы не двигать номера строк (§13 п.5).
#include "core/row_law.h"
TIMAERT_ROW(sm::AttrWeights);
TIMAERT_ROW(sm::SkillWeights);
TIMAERT_ROW(sm::RoleWeights);

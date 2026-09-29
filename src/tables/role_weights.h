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
// base of 1, and every row has at least one non-zero skill weight. This is
// pure tunable data (see MASTER_PROMPT §9.3); adding a role = one more row.
struct RoleWeights {
    // MUST equal the row's index in kRoleWeights (guard below the table).
    NPCType type;
    // AttributeId order; sized by the enum for the same reason the skill
    // weights are: naming a new attribute must ASK every role what it thinks,
    // and a compile error asks better than a silent zero.
    std::uint8_t attr[std::size_t(AttributeId::Count)];
    // SkillId order; the weighted pick indexes this directly. Sized by the
    // enum, not by a literal 8, so adding a skill to the registry makes every
    // role state what it thinks of it — a compile error is the right way to
    // ask that question, and a silent zero is the wrong one.
    std::uint8_t skill[std::size_t(SkillId::Count)];
};

// Attr columns follow the canon-eight order (2026-09-03). The old VIT column
// merged into END as max(vit, end) — whichever of the two carried the row's
// identity (the bear's VIT, the rabbit's END) keeps it.
//
// Skill columns follow the canon-32 order (skills-64, same date). A role's
// weights answer TWO questions with one row: which 5 skills its people LEARN
// at creation (weighted picks, no repeats) and where their level points go
// (weighted spends into the learned set only). The flavor writes itself:
// the miner's weapon is his pick (Mace) and his eye is Prospecting; a beast
// fights through Armsmaster (its body IS the weapon) and wears Unarmored.
//
// skills: Swd Axe Spr Mac Dag Bow Stf|Hvy Lgt Una Shd|Fir Wat Air Ear Arc Voi|
//         Arm Spl|Bod Med Mar Ath Wgt|Trv Acr Sct Prs|Trd Qtm For Lrn|Unr
// (Unr = Unarmed, appended v79 like its enum row; 0 everywhere until a
// brawler role wants it — beasts keep fighting through Armsmaster.)
inline constexpr RoleWeights kRoleWeights[int(NPCType::Count)] = {
    // Peasant     — hardy laborer: pitchfork and flail, forage and endure
    {NPCType::Peasant, {3, 3, 1, 1, 1, 1, 1, 2},
     {0,0,2,1,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 1,0, 3,0,3,1,2, 1,0,0,0, 0,0,3,1,0}},
    // Merchant    — social, lucky, sedentary: the ledger, not the blade
    {NPCType::Merchant, {1, 2, 2, 1, 1, 3, 4, 3},
     {0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,1,0,2, 2,0,0,0, 4,2,0,2,0}},
    // Caravan     — mobile trader: lives on the road, hence the road skills
    // Cha 5: the trade house on wheels — its whole market edge is this row
    // (owner 2026-08-30: «у каравана в таблице выше уровень и харизма»).
    // Bandit      — aggressive raider: knife and bow, fast on his feet
    {NPCType::Bandit, {4, 3, 1, 1, 3, 2, 1, 1},
     {2,0,0,0,3,2,0, 0,2,1,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,1, 2,1,2,0, 0,0,1,0,0}},
    // Guard       — disciplined tank: sword, shield and heavy plate
    {NPCType::Guard, {4, 4, 1, 1, 2, 1, 2, 2},
     {3,0,2,1,0,0,0, 3,0,0,3, 0,0,0,0,0,0, 2,0, 3,0,2,1,1, 1,0,0,0, 0,0,0,0,0}},
    // Witch       — practical caster: hedge schools, a staff to lean on
    {NPCType::Witch, {1, 2, 4, 4, 1, 2, 2, 3},
     {0,0,0,0,1,0,2, 0,0,2,0, 2,2,0,2,0,1, 0,4, 0,4,1,1,0, 1,0,0,0, 0,0,1,2,0}},
    // Sorceress   — elite caster: the high schools, arcane first
    {NPCType::Sorceress, {1, 2, 5, 4, 1, 3, 3, 3},
     {0,0,0,0,0,0,2, 0,1,2,0, 3,0,2,0,3,2, 0,5, 0,4,1,1,0, 1,0,0,0, 0,0,0,2,0}},
    // Miner       — strong laborer: the pick swings like a mace, the eye
    // reads the vein (Prospecting is the trade's whole point)
    // Clay-digger — hardy laborer (the peasant's build, wetter)
    // ── The creature rows ────────────────────────────────────────────────
    // A beast has a sheet like a man has a sheet (owner, 2026-08-20: one
    // system, "у всех лист статов как в обливионе"). What differs is only the
    // EMPHASIS, and it is the same five shapes over and over: prey is all
    // endurance and speed, a grazer is prey with mass, a predator buys speed
    // with muscle, a brute is muscle without speed, and the unnatural things
    // are built like soldiers because that is what they fight as. A beast's
    // weapon skill is Armsmaster (its body is the weapon) and its armor is
    // Unarmored (its hide is the coat); prey reads the wind (Scouting).
    {NPCType::Rabbit,       {1, 4, 1, 1, 5, 3, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 0,0, 1,0,3,5,0, 2,3,2,0, 0,0,0,0,0}},
    {NPCType::Deer,         {2, 4, 1, 1, 5, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 2,0,3,5,0, 2,2,2,0, 0,0,0,0,0}},
    {NPCType::Fox,          {2, 3, 2, 1, 4, 3, 1, 2},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 2,0, 1,0,2,4,0, 2,1,3,0, 0,0,0,0,0}},
    {NPCType::Wolf,         {4, 3, 1, 1, 4, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 4,0, 2,0,2,3,0, 2,0,3,0, 0,0,0,0,0}},
    {NPCType::Bear,         {5, 5, 1, 1, 2, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,1,0, 1,0,1,0, 0,0,0,0,0}},
    {NPCType::Boar,         {4, 4, 1, 1, 3, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 4,0,1,2,0, 1,0,1,0, 0,0,0,0,0}},
    {NPCType::Snake,        {3, 2, 1, 1, 3, 3, 1, 1},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 4,0, 1,0,1,2,0, 1,0,1,0, 0,0,0,0,0}},
    {NPCType::Hawk,         {2, 3, 2, 1, 5, 3, 1, 2},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 2,0, 1,0,2,5,0, 2,0,3,0, 0,0,0,0,0}},
    {NPCType::Frog,         {1, 3, 1, 1, 3, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 1,0,2,3,0, 1,3,0,0, 0,0,0,0,0}},
    {NPCType::Goat,         {2, 4, 1, 1, 4, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,4,0, 3,3,0,0, 0,0,0,0,0}},
    {NPCType::Eagle,        {2, 3, 2, 1, 5, 3, 1, 2},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 3,0, 1,0,2,5,0, 2,0,3,0, 0,0,0,0,0}},
    {NPCType::Croc,         {4, 4, 1, 1, 2, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 4,0,1,1,0, 1,0,1,0, 0,0,0,0,0}},
    {NPCType::Goblin,       {3, 3, 2, 1, 3, 2, 1, 1},
     {0,0,1,0,2,1,0, 0,1,1,0, 0,0,0,0,0,0, 2,0, 2,0,2,3,1, 2,0,1,0, 0,0,0,0,0}},
    {NPCType::Skeleton,     {3, 4, 1, 1, 2, 1, 1, 1},
     {2,0,1,0,0,0,0, 1,0,1,1, 0,0,0,0,0,0, 3,0, 3,0,1,1,0, 0,0,0,0, 0,0,0,0,0}},
    {NPCType::Troll,        {5, 5, 1, 1, 1, 1, 1, 1},
     {0,0,0,2,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,1,0, 1,0,0,0, 0,0,0,0,0}},
    {NPCType::SwampThing,   {4, 4, 1, 2, 1, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,3,0, 0,2,0,1,0,0, 3,1, 4,1,1,1,0, 0,0,0,0, 0,0,0,0,0}},
    // Ice is the Water school (S15 remap): the wraith casts what it is.
    {NPCType::IceWraith,    {3, 3, 3, 4, 3, 2, 1, 2},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,3,1,0,0,2, 1,3, 1,3,1,3,0, 1,0,0,0, 0,0,0,0,0}},
    {NPCType::SandScorpion, {3, 3, 1, 1, 3, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 2,0,1,0, 0,0,0,0,0}},
    {NPCType::StoneGolem,   {5, 5, 1, 1, 1, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,0,0,0, 3,0, 5,0,1,1,0, 0,0,0,0, 0,0,0,0,0}},
    // Adventurer — the player's row. Even weights on purpose: a generated
    // adventurer is a blank slate, and the player's OWN points are spent by
    // him, not rolled by this table (it answers only when something asks the
    // world for "an adventurer", e.g. a projected body of his squad).
    {NPCType::Adventurer,   {2, 2, 2, 2, 2, 2, 2, 2},
     {1,1,1,1,1,1,1, 1,1,1,1, 1,1,1,1,1,1, 1,1, 1,1,1,1,1, 1,1,1,1, 1,1,1,1,0}},
    // Silver-miner — the miner's body, row for row.
    // Tax-collector — a courier's legs, a clerk's head.
    {NPCType::TaxCollector, {2, 3, 2, 2, 3, 1, 2, 2},
     {0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,2,3,2, 3,0,0,0, 2,2,0,2,0}},
    // Road ambusher — the bandit's sheet, row for row: what sets him apart
    // lives in his combat template (the road-wide eye, the HP), not here.
    {NPCType::RoadAmbusher, {4, 3, 1, 1, 3, 2, 1, 1},
     {2,0,0,0,3,2,0, 0,2,1,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,1, 2,1,2,0, 0,0,1,0,0}},
    // Дракон — зверь силы и воли: STR/END тяжёлые, интеллект древний;
    // скиллы зверя пусты — его бой живёт в комбат-шаблоне (3d20 Fire),
    // не в оружейных рядах человека.
    {NPCType::Dragon, {8, 6, 4, 5, 3, 2, 1, 4},
     {0,0,0,0,0,0,0, 0,0,0,0, 0,0,0,0,0,0, 0,0, 0,0,0,0,0, 0,0,0,0, 0,0,0,0,0}},
    // ── The populated bestiary (2026-09-11) ──────────────────────────────
    // Same reading as the creature rows above: a beast fights through
    // Armsmaster (its body IS the weapon) and wears Unarmored (its hide is
    // the coat); a thing that carries a weapon names it. The sheet is what
    // makes two rows with similar stat blocks feel different at level 5 —
    // the ghoul spends its points on legs, the zombie on Body, so the gap
    // between them WIDENS as the world's danger climbs.
    // attrs: Str End Int Wil Spd Lck Cha Wis
    // Giant rat — legs and teeth, nothing else.
    {NPCType::GiantRat,     {1, 3, 1, 1, 5, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 1,0,0,4,0, 0,2,2,0, 0,0,0,0,0}},
    // Cave bat — the fastest thing in the table, and the frailest.
    {NPCType::CaveBat,      {1, 2, 1, 1, 6, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 1,0,0,5,0, 0,3,3,0, 0,0,0,0,0}},
    // Kobold — a scavenger with a knife: spear-and-dagger, and a forager's
    // eye for what was dropped.
    {NPCType::Kobold,       {2, 2, 2, 1, 4, 3, 1, 1},
     {0,0,1,0,2,0,0, 0,1,1,0, 0,0,0,0,0,0, 2,0, 1,0,1,3,0, 1,2,2,0, 0,0,1,0,0}},
    {NPCType::CaveSpider,   {3, 3, 1, 1, 4, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 0,2,1,0, 0,0,0,0,0}},
    // Imp — a spark with legs: Fire is its one school and it never learns
    // a second.
    {NPCType::Imp,          {2, 2, 3, 2, 5, 3, 1, 2},
     {0,0,0,0,0,0,0, 0,0,1,0, 3,0,0,0,0,0, 1,2, 1,0,0,4,0, 0,3,1,0, 0,0,0,0,0}},
    // Zombie — all of it goes into Body. It does not dodge and does not run.
    {NPCType::Zombie,       {4, 5, 1, 1, 1, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 3,0, 5,0,1,0,1, 0,0,0,0, 0,0,0,0,0}},
    // Orc — a soldier by any other name: axe, heavy coat, march discipline.
    {NPCType::Orc,          {4, 4, 1, 2, 3, 2, 1, 1},
     {1,3,0,0,0,0,0, 2,0,1,0, 0,0,0,0,0,0, 3,0, 3,0,2,2,0, 1,0,1,0, 0,0,0,0,0}},
    // Ghoul — the zombie's points, spent on legs instead of meat.
    {NPCType::Ghoul,        {3, 3, 1, 1, 4, 2, 1, 1},
     {0,0,0,0,1,0,0, 0,0,2,0, 0,0,0,0,0,0, 3,0, 2,0,1,3,0, 0,2,1,0, 0,0,0,0,0}},
    {NPCType::Harpy,        {2, 3, 1, 2, 5, 3, 1, 1},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 2,0, 1,0,0,5,0, 0,3,2,0, 0,0,0,0,0}},
    // Cultist — the crowd's only educated man: staff, Arcane, and the
    // Learning that says he chose this.
    {NPCType::Cultist,      {2, 2, 4, 4, 2, 2, 2, 3},
     {0,0,0,0,0,0,2, 0,1,0,0, 2,0,0,0,3,1, 0,4, 1,3,0,1,0, 0,0,0,1, 0,0,0,2,0}},
    // Gargoyle — masonry that flies: Earth in its fists, Body under them.
    {NPCType::Gargoyle,     {5, 5, 1, 2, 2, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,4,0, 0,0,0,2,0,0, 3,0, 4,0,1,2,1, 0,0,0,0, 0,0,0,0,0}},
    // Wraith — Void where the ice wraith keeps cold, and the Meditation
    // that makes it a caster's problem rather than a brawler's.
    {NPCType::Wraith,       {3, 3, 3, 4, 4, 2, 1, 2},
     {0,0,0,0,0,0,0, 0,0,2,0, 0,0,0,0,1,3, 2,3, 1,3,0,3,0, 0,1,0,0, 0,0,0,0,0}},
    // Ogre — the heaviest hands in the table under the dragon's.
    {NPCType::Ogre,         {6, 6, 1, 1, 1, 1, 1, 1},
     {0,0,0,2,0,0,0, 0,0,3,0, 0,0,0,0,0,0, 4,0, 5,0,1,0,2, 0,0,0,0, 0,0,0,0,0}},
    // Minotaur — an ogre that kept its legs: the row that punishes running.
    {NPCType::Minotaur,     {6, 5, 2, 2, 3, 1, 1, 1},
     {1,2,0,0,0,0,0, 1,0,2,0, 0,0,0,0,0,0, 4,0, 4,0,2,2,0, 0,0,0,0, 0,0,0,0,0}},
    {NPCType::Basilisk,     {4, 4, 1, 2, 3, 2, 1, 1},
     {0,0,0,0,0,0,0, 0,0,3,0, 0,0,0,2,0,0, 4,0, 3,0,1,1,0, 0,0,1,0, 0,0,0,0,0}},
    // Lich — the top of the climb: Will and Intellect above everything,
    // Void and Arcane, and the deepest Spellpower in the table.
    {NPCType::Lich,         {2, 3, 6, 6, 1, 2, 2, 5},
     {0,0,0,0,0,0,2, 0,0,0,0, 1,0,0,0,3,4, 0,5, 1,4,0,0,0, 0,0,0,2, 0,0,0,3,0}},
    // Horse — the goat's own build, a size up: body and speed, no craft.
    {NPCType::Horse,        {4, 5, 1, 1, 4, 1, 1, 1},
     {0,0,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,4,0, 3,3,0,0, 0,0,0,0,0}},
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
TIMAERT_ROW(sm::RoleWeights);

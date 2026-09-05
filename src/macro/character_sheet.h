#pragma once

#include "core/table_guard.h"
#include "macro/army.h"      // CombatTemplate (per-role authored base)
#include "macro/attributes.h"
#include "macro/bonus.h"
#include "macro/npc.h"

#include <cstddef>
#include <cstdint>

namespace sm {

// ── Universal character sheet ──────────────────────────────────────────────
//
// The SINGLE representation shared by the player and every humanoid NPC. It
// bundles the persistent RPG facets — attributes, skills and the
// level/XP economy — exactly as the player carries them today (see
// `PlayerState` in macro/state.h). Combat numbers (HP/MP/SP, damage) are
// DERIVED from this sheet, never stored inside it (the player keeps a
// `CombatStats`; an NPC gets ECS `Health`/`Combat`).
//
// EVERY body carries one — humanoid, creature and player alike (CANON S14).
// The one birth door (`emplace_body`, sub/spawn.cpp) builds the sheet from the
// body's `kNpcTypeDefs` row, applies the leader's aura, then projects combat
// numbers from it; "monsters are sheet-less" died with the second table
// (2026-08-20).
//
// Field order mirrors the player's save layout so the same struct can later be
// embedded in `PlayerState` without changing the on-disk save bytes.
//
// Header-only (like attributes.h) so it links into every consumer — the game
// and each hand-curated test executable — with no CMake source-list edits.
struct CharacterSheet {
    Attributes attributes;
    Skills     skills;
    LevelData  levelData;
};

namespace csheet_detail {

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
    // Woodcutter  — strong laborer: the axe is the trade and the argument
    {NPCType::Woodcutter, {4, 3, 1, 1, 1, 1, 1, 1},
     {0,4,0,0,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,1,4, 1,0,0,0, 0,0,2,0,0}},
    // Merchant    — social, lucky, sedentary: the ledger, not the blade
    {NPCType::Merchant, {1, 2, 2, 1, 1, 3, 4, 3},
     {0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,1,0,2, 2,0,0,0, 4,2,0,2,0}},
    // Caravan     — mobile trader: lives on the road, hence the road skills
    // Cha 5: the trade house on wheels — its whole market edge is this row
    // (owner 2026-08-30: «у каравана в таблице выше уровень и харизма»).
    {NPCType::Caravan, {2, 3, 1, 1, 3, 2, 5, 2},
     {0,0,0,1,0,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,2,3,2, 4,0,0,0, 4,3,2,1,0}},
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
    {NPCType::Miner, {4, 3, 1, 1, 1, 1, 1, 1},
     {0,0,0,4,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,1,4, 0,0,0,4, 0,0,1,0,0}},
    // Quarryman   — strong laborer: stone over ore, the back over the eye
    {NPCType::Quarryman, {4, 3, 1, 1, 1, 1, 1, 1},
     {0,0,0,4,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,1,5, 0,0,0,2, 0,0,1,0,0}},
    // Clay-digger — hardy laborer (the peasant's build, wetter)
    {NPCType::ClayDigger, {3, 3, 1, 1, 1, 1, 1, 2},
     {0,0,2,1,0,0,0, 0,0,2,0, 0,0,0,0,0,0, 1,0, 3,0,3,1,2, 1,0,0,0, 0,0,2,1,0}},
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
    // Vendor — the village hauler on the town road: a labourer's back with a
    // seller's tongue (the Woodcutter body, a pinch of the Merchant charm).
    {NPCType::Vendor,       {3, 3, 1, 1, 1, 1, 1, 2},
     {0,0,0,1,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 2,0,2,1,3, 2,0,0,0, 3,1,1,1,0}},
    // Silver-miner — the miner's body, row for row.
    {NPCType::SilverMiner,  {4, 3, 1, 1, 1, 1, 1, 1},
     {0,0,0,4,0,0,0, 0,0,1,0, 0,0,0,0,0,0, 1,0, 3,0,2,1,4, 0,0,0,4, 0,0,1,0,0}},
    // Tax-collector — a courier's legs, a clerk's head.
    {NPCType::TaxCollector, {2, 3, 2, 2, 3, 1, 2, 2},
     {0,0,0,0,1,0,0, 0,1,0,0, 0,0,0,0,0,0, 0,0, 1,1,2,3,2, 3,0,0,0, 2,2,0,2,0}},
};

static_assert(rows_in_enum_order(kRoleWeights, &RoleWeights::type),
              "kRoleWeights row order must mirror NPCType");

inline const RoleWeights& role_weights(NPCType role) {
    const int idx = int(role);
    if (idx < 0 || idx >= int(NPCType::Count)) return kRoleWeights[0];
    return kRoleWeights[idx];
}

// Tiny deterministic LCG (Numerical Recipes constants). Not for security — it
// exists only to make per-seed stat allocation stable and reproducible.
struct SheetRng {
    std::uint32_t s;
    std::uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s;
    }
};

// Fold three inputs into one non-zero seed (boost::hash_combine style).
inline std::uint32_t mix32(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t h = a * 2654435761u;
    h ^= b + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= c + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h ? h : 0x1u;
}

// Pick an index in [0,N) with probability proportional to its weight.
template <std::size_t N>
int weighted_pick(const std::uint8_t (&w)[N], std::uint32_t roll) {
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < N; ++i) total += w[i];
    if (total == 0) return 0; // degenerate row — should be impossible
    std::uint32_t r = roll % total;
    for (std::size_t i = 0; i < N; ++i) {
        if (r < w[i]) return int(i);
        r -= w[i];
    }
    return int(N - 1);
}

} // namespace csheet_detail

// THE seed of a macro leader's sheet, derived from his save-stable spawn
// ordinal (ecs::MacroSpawnId — the one identity that survives save/load).
// The macro layer never stored a leader's birth sheet seed, so every consumer
// that needs the leader AS A SHEET — the auto-resolve, the level-up ceiling
// recompute, the SP/travel caches — must derive it from the ordinal, and must
// derive it IDENTICALLY. This function is that law's single home; it used to
// be restated at each call site (squad.h twice, tests once), which is exactly
// how twin formulas drift.
inline std::uint32_t leader_sheet_seed(std::uint32_t spawnOrdinal) {
    return spawnOrdinal * 2654435761u + 0x51ADu;
}

// Procedurally builds a sheet for a humanoid NPC `role` at a given `level`.
//
// Deterministic in `seed`: no global RNG and no external Rng object — the same
// (role, level, seed) always yields the same sheet, matching the subworld's
// "everything regenerates from the seed" contract. The generator spends the
// EXACT player point economy for `level` (CANON S14, 2026-09-03): creation is
// 5 attribute points and 5 LEARN PICKS, every level adds +1 attribute and +1
// skill point, and skill points spend ONLY into what the creation picks
// taught — so a level-N NPC is budget-identical to a level-N player, merely
// allocated toward its role. Points are fully consumed (levelData pools end
// at 0). Plot NPCs supply an authored sheet instead of calling this.
inline CharacterSheet make_character_sheet(NPCType role, int level,
                                           std::uint32_t seed) {
    if (level < 1) level = 1;

    CharacterSheet cs;
    cs.levelData           = default_level_data();
    cs.levelData.level     = level;
    cs.levelData.exp       = 0;
    cs.levelData.expToNext = exp_to_next_level(level);
    cs.levelData.attributePoints = 5 + (level - 1);
    cs.levelData.skillPoints     = (level - 1);

    const csheet_detail::RoleWeights& w = csheet_detail::role_weights(role);
    // TWO streams, and LEVEL is in NEITHER seed — this is what makes a
    // levelling leader's sheet MONOTONIC: the level-N sheet is the level-N-1
    // sheet plus exactly one more attribute pick and one more skill pick
    // (each loop below draws a prefix-stable sequence). The old generator
    // mixed the level into one stream, so every level-up RE-ROLLED the whole
    // sheet and a leader could visibly get WEAKER by growing — the perversity
    // hid under the fat legacy +3/level and surfaced with the 1:1 economy.
    // Растёт то, что мир хранит (S14) — a sheet only ever grows.
    csheet_detail::SheetRng attrRng{
        csheet_detail::mix32(seed, std::uint32_t(role), 0xA77Bu)};
    csheet_detail::SheetRng skillRng{
        csheet_detail::mix32(seed, std::uint32_t(role), 0x5C11u)};

    while (cs.levelData.attributePoints > 0) {
        const int pick = csheet_detail::weighted_pick(w.attr, attrRng.next());
        if (!spend_attribute_point(cs.levelData, cs.attributes,
                                   AttributeId(std::uint8_t(pick))))
            break; // safety net; the loop guard already prevents this
    }
    // CREATION: learn 5 distinct skills by the role's weights — a weighted
    // pick that lands on a known skill rerolls, and if the row runs out of
    // distinct weighted skills (a narrow beast), the pick learns the first
    // still-unknown weighted skill instead of spinning.
    while (cs.levelData.learnPicks > 0) {
        bool learned = false;
        for (int attempt = 0; attempt < 16 && !learned; ++attempt) {
            const int pick =
                csheet_detail::weighted_pick(w.skill, skillRng.next());
            learned = spend_learn_pick(cs.levelData, cs.skills,
                                       SkillId(std::uint8_t(pick)));
        }
        if (learned) continue;
        for (int i = 0; i < int(SkillId::Count) && !learned; ++i)
            if (w.skill[std::size_t(i)] > 0)
                learned = spend_learn_pick(cs.levelData, cs.skills,
                                           SkillId(std::uint8_t(i)));
        if (!learned) break;   // fewer than 5 weighted skills in the row
    }
    // LEVELS: points spend only into the learned set (THE learn law) — the
    // weights are re-read through a mask so an unknown skill cannot draw.
    while (cs.levelData.skillPoints > 0) {
        std::uint8_t known[std::size_t(SkillId::Count)] = {};
        for (int i = 0; i < int(SkillId::Count); ++i)
            if (cs.skills.rank[std::size_t(i)] > 0
                && cs.skills.rank[std::size_t(i)] < kMaxSkillRank)
                known[std::size_t(i)] = w.skill[std::size_t(i)] > 0
                                            ? w.skill[std::size_t(i)] : 1;
        const int pick = csheet_detail::weighted_pick(known, skillRng.next());
        if (!spend_skill_point(cs.levelData, cs.skills,
                               SkillId(std::uint8_t(pick))))
            break;   // every learned skill at mastery (or nothing learned)
    }
    return cs;
}

// THE application: the base sheet plus everything standing, as a COPY.
//
// Non-destructive on purpose, and it is the whole reason this file exists. The
// old `apply_aura` wrote deltas into the member's stored sheet at birth — no
// source, no removal, no recompute — so a buff outlived every reason for it.
// Here the stored sheet stays the character the player built, and what fights
// is the sum of that character and what he is currently wearing, standing in
// and blessed by. Take the item off and the next call simply does not add it.
inline CharacterSheet effective_sheet(const CharacterSheet& base,
                                      const BonusTotals& t) {
    CharacterSheet out = base;
    for (int i = 0; i < kMaxAttributes; ++i) {
        const int v = int(out.attributes.score[std::size_t(i)])
                    + int(t.attr[std::size_t(i)]);
        // A score floors at 1, never 0: the asymptotic formulas divide by
        // (score + 50) and a character reduced to nothing is a design
        // question, not an arithmetic one.
        out.attributes.score[std::size_t(i)] =
            std::uint8_t(std::clamp(v, 1, kMaxAttributeScore));
    }
    for (int i = 0; i < kMaxSkills; ++i) {
        const int v = int(out.skills.rank[std::size_t(i)])
                    + int(t.skill[std::size_t(i)]);
        out.skills.rank[std::size_t(i)] =
            std::uint8_t(std::clamp(v, 0, kMaxSkillRank));
    }
    return out;
}

// ── What a LEADER's sheet gives his squad ────────────────────────────────
//
// Owner, 2026-08-27: «хватит контекста макросквада (минимум систем) — при
// загрузке отряда в субмире они всё равно берутся из сквада/ландмарка/зон/
// таблиц, так что просто скиллы и перки СКВАДА (это и есть лидер) модифицируют
// юнитов в бою, и всё универсально».
//
// So there is no aura SYSTEM. There was one — its own header, its own
// `AuraMods` type, its own add/collect/apply verbs — and every bit of it said
// the same thing this one function says: the leader's sheet contributes
// bonuses, and bonuses are rows of the one registry. A second container for a
// number that already had one is exactly what CANON S26 forbids; killing it
// costs the game nothing because squad == leader (S4) and the leader's sheet
// was always the only source.
//
// SOURCES are the extension axis: perk rows (returning with the perk
// redesign, CANON S14), and the leader's skills, charisma and carried gear
// when their turns come. Each is a few lines appending into the same totals,
// and no consumer ever learns where a modifier came from.
//
// EMPTY since the 2026-09-03 perk purge: the one aura row (Leader → +1 vit)
// died with the perk system it hung on. The DOOR stays — every body-birth
// already walks through it — so the redesigned perks feed rows, not code.
inline BonusTotals squad_bonuses(const CharacterSheet&) {
    return BonusTotals{};
}

// Derive combat numbers for a humanoid from its CharacterSheet, layered on top
// of an authored per-role `base` (the NPC registry's CombatTemplate). Returns a
// CombatTemplate so spawn code stays a one-line swap (base → projected) before
// it fills ECS Health/Combat.
//
// The projection reuses the EXACT player formulas from attributes.h, so player
// and NPC sit on ONE combat curve:
//   hp     = (base.hp + end·10) · (1 + bodybuilding·0.05)      // calculate_combat_stats
//   damage = base.damage + (missile ? intl·(1+spellcraft·0.05)  // caster: spell stats
//                                    : str ·(1+fighter   ·0.05)) // melee : physical stats
// The authored template supplies the per-role HP/damage FLOOR plus the attack
// identity (speed / range / cooldown / kind / missile params / label), all
// preserved verbatim. Level is captured implicitly by the sheet's spent points
// (a level-N sheet has more attributes/skills), so callers MUST NOT apply an
// additional per-level multiplier on top of this — the sheet IS the scaling.
//
// Every body passes through here — a wolf exactly like a spearman (CANON S14):
// its row supplies the floor and the attack identity, its sheet supplies the
// scaling.
inline CombatTemplate project_combat(const CharacterSheet& sheet,
                                     const CombatTemplate& base) {
    CombatTemplate out = base; // keep attack identity + label + missile params
    const CombatStats cs =
        calculate_combat_stats(sheet.attributes, sheet.skills, int(base.hp));
    const DerivedBonuses d =
        calculate_derived(sheet.attributes, sheet.skills);
    const float atkBonus = (base.attackKind == CombatTemplate::Missile)
                               ? d.rawSpellDamage
                               : d.rawPhysDamage;
    out.hp      = float(cs.maxHp);
    // Attributes ADD to the row's dice (CANON S14: «атрибуты складывают»),
    // floored to the int house — the strike assembly (roll_strike) does the
    // rest. The sheet's LCK rides along for the crit door.
    out.flatAdd = std::int16_t(std::floor(atkBonus));
    out.luck    = std::uint8_t(sheet.attributes.of(AttributeId::Lck));
    return out;
}

// THE whole-number bar a body of this sheet carries — one question, one
// answer. It was written twice, once per birth (sub/spawn.cpp's derived body
// and macro/npc_spawn.cpp's tracked one), and a wound crosses between those
// two layers as a FRACTION of exactly this number: the day the two floors
// disagreed by one point, the crossing would have leaked hp in one direction.
inline int body_max_hp(const CharacterSheet& sheet, const CombatTemplate& base) {
    return std::max(1, int(std::floor(project_combat(sheet, base).hp)));
}

} // namespace sm

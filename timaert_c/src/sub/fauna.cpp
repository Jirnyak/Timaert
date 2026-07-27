// Faithful port of `src/game/subworld/fauna.ts`.
//   - 1:1 stat lines / weights / colours / counts.
//   - String factionId → `FaunaFaction` enum (zero-cost).
//   - String AI       → `FaunaAi` enum (zero-cost).
//   - Tables stored as null-terminated arrays of `const FaunaEntry*` so
//     they live in `.rodata` and never allocate.
#include "sub/fauna.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

using CA = CreatureArchetype; // body plan for the procedural creature billboard

// ── Combat templates for wildlife ────────────────────────────────────
//
// Mirrors the TS critter list verbatim (hp / damage / speed / range /
// cooldown / label match the .ts file line-for-line).
static const FaunaEntry kRabbit  {"rabbit", "Rabbit",        15, FaunaFaction::Wildlife, FaunaAi::Flee,
    {  5,  0, 55, 0, 9.0f, "Rbt"}, 1, 0xB8A080u, 0.4f, CA::Quadruped};
static const FaunaEntry kDeer    {"deer", "Deer",          12, FaunaFaction::Wildlife, FaunaAi::Flee,
    { 15,  2, 50, 2, 2.0f, "Der"}, 1, 0xA08060u, 0.6f, CA::Quadruped};
static const FaunaEntry kFox     {"fox", "Fox",            8, FaunaFaction::Wildlife, FaunaAi::Wander,
    { 12,  4, 45, 2, 1.2f, "Fox"}, 1, 0xCC6633u, 0.5f, CA::Quadruped};
static const FaunaEntry kWolf    {"wolf", "Wolf",           6, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 30, 10, 50, 3, 1.0f, "Wlf"}, 2, 0x666666u, 0.7f, CA::Quadruped};
static const FaunaEntry kBear    {"bear", "Bear",           3, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 80, 18, 35, 3, 1.5f, "Ber"}, 3, 0x5A3A1Au, 1.0f, CA::Quadruped};
static const FaunaEntry kBoar    {"boar", "Boar",           5, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 40, 12, 40, 3, 1.2f, "Bor"}, 2, 0x6B4E37u, 0.7f, CA::Quadruped};
static const FaunaEntry kSnake   {"snake", "Snake",          4, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 10,  8, 30, 2, 0.8f, "Snk"}, 1, 0x3A5A2Au, 0.3f, CA::Serpent};
static const FaunaEntry kHawk    {"hawk", "Hawk",           3, FaunaFaction::Wildlife, FaunaAi::Wander,
    {  8,  5, 60, 3, 1.0f, "Hwk"}, 1, 0x8B6B4Bu, 0.4f, CA::Avian};
static const FaunaEntry kFrog    {"frog", "Frog",          10, FaunaFaction::Wildlife, FaunaAi::Flee,
    {  3,  0, 30, 0, 9.0f, "Frg"}, 1, 0x2A8A2Au, 0.3f, CA::Critter};
static const FaunaEntry kGoat    {"goat", "Mountain Goat",  8, FaunaFaction::Wildlife, FaunaAi::Flee,
    { 20,  5, 40, 2, 1.5f, "Mgt"}, 1, 0xB0A090u, 0.6f, CA::Quadruped};
static const FaunaEntry kEagle   {"eagle", "Eagle",          4, FaunaFaction::Wildlife, FaunaAi::Wander,
    { 12,  7, 65, 3, 1.0f, "Egl"}, 2, 0x5A4030u, 0.5f, CA::Avian};
static const FaunaEntry kCroc    {"crocodile", "Crocodile",      4, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 50, 15, 25, 3, 1.5f, "Crc"}, 3, 0x4A6A3Au, 0.8f, CA::Quadruped};

// ── Monsters ────────────────────────────────────────────────────────
static const FaunaEntry kGoblin  {"goblin", "Goblin",         4, FaunaFaction::Demons,  FaunaAi::Combat,
    { 25,  8, 40, 3, 1.0f, "Gbl"}, 2, 0x4A8A2Au, 0.6f, CA::Biped};
static const FaunaEntry kSkeleton{"skeleton", "Skeleton",       3, FaunaFaction::Demons,  FaunaAi::Combat,
    { 35, 10, 30, 3, 1.2f, "Skl"}, 3, 0xD0C8B0u, 0.6f, CA::Undead};
static const FaunaEntry kTroll   {"troll", "Troll",          1, FaunaFaction::Demons,  FaunaAi::Combat,
    {120, 25, 25, 4, 2.0f, "Trl"}, 5, 0x3A6A3Au, 1.2f, CA::Biped};
static const FaunaEntry kSwampThing{"swamp_thing", "Swamp Thing",  3, FaunaFaction::Demons,  FaunaAi::Combat,
    { 60, 14, 20, 4, 1.5f, "Swt"}, 3, 0x2A4A1Au, 0.9f, CA::Biped};
static const FaunaEntry kIceWraith{"ice_wraith", "Ice Wraith",    2, FaunaFaction::Demons,  FaunaAi::Combat,
    { 45, 16, 35, 5, 1.3f, "Iwr"}, 4, 0xA0D0E0u, 0.7f, CA::Undead};
static const FaunaEntry kSandScorpion{"sand_scorpion", "Sand Scorpion",5,FaunaFaction::Demons, FaunaAi::Combat,
    { 35, 12, 35, 3, 1.0f, "Ssc"}, 2, 0xC0A050u, 0.6f, CA::Quadruped};
static const FaunaEntry kStoneGolem{"stone_golem", "Stone Golem",  1, FaunaFaction::Demons,  FaunaAi::Combat,
    {150, 20, 15, 4, 2.5f, "Glm"}, 5, 0x7A7A7Au, 1.3f, CA::Hulk};

// ── Per-table entry arrays (null-terminated) ─────────────────────────
//
// Using `const FaunaEntry* const[]` lets each entry be referenced from
// many tables without copying. Order matches TS.

static const FaunaEntry* const kMeadow []  = { &kRabbit, &kDeer, &kFox, &kWolf, &kHawk, &kBoar };
static const FaunaEntry* const kForest []  = { &kRabbit, &kDeer, &kFox, &kWolf, &kBear, &kBoar, &kGoblin };
static const FaunaEntry* const kTaiga  []  = { &kRabbit, &kDeer, &kWolf, &kBear, &kFox };
static const FaunaEntry* const kTundra []  = { &kRabbit, &kWolf, &kFox, &kIceWraith };
static const FaunaEntry* const kSnow   []  = { &kRabbit, &kWolf, &kIceWraith };
static const FaunaEntry* const kDesert []  = { &kSnake, &kHawk, &kSandScorpion };
static const FaunaEntry* const kSteppe []  = { &kRabbit, &kDeer, &kFox, &kHawk, &kSnake, &kBoar };
static const FaunaEntry* const kSwamp  []  = { &kSnake, &kFrog, &kCroc, &kSwampThing };
static const FaunaEntry* const kTropics[]  = { &kSnake, &kBoar, &kCroc, &kDeer };
static const FaunaEntry* const kValley []  = { &kRabbit, &kDeer, &kFox, &kWolf, &kHawk, &kBoar };
static const FaunaEntry* const kMountain[] = { &kGoat, &kEagle, &kWolf, &kStoneGolem };
static const FaunaEntry* const kRuin   []  = { &kSkeleton, &kGoblin, &kTroll, &kSnake };
static const FaunaEntry* const kSpire  []  = { &kSkeleton, &kIceWraith, &kTroll, &kStoneGolem, &kGoblin };

// Pointer + count + counts + override.
static constexpr FaunaTable kTblMeadow   { kMeadow,   6, 2, 6, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblForest   { kForest,   7, 3, 8, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblTaiga    { kTaiga,    5, 2, 5, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblTundra   { kTundra,   4, 1, 4, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblSnow     { kSnow,     3, 1, 3, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblDesert   { kDesert,   3, 1, 4, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblSteppe   { kSteppe,   6, 2, 5, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblSwamp    { kSwamp,    4, 2, 6, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblTropics  { kTropics,  4, 2, 6, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblValley   { kValley,   6, 2, 6, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblMountain { kMountain, 4, 1, 4, FaunaFaction::Neutral, false };
static constexpr FaunaTable kTblRuin     { kRuin,     4, 2, 6, FaunaFaction::Demons,  true  };
static constexpr FaunaTable kTblSpire    { kSpire,    5, 4, 9, FaunaFaction::Demons,  true  };
static constexpr FaunaTable kTblEmpty    { nullptr,   0, 0, 0, FaunaFaction::Neutral, false };

const FaunaTable& get_fauna_table(Biome biome, FeatureType feature,
                                  LandmarkKind landmark) {
    // Landmark beats everything (cities have no wild fauna; ruins / spires
    // have their own monster tables).
    switch (landmark) {
        case LandmarkKind::City:    return kTblEmpty;
        case LandmarkKind::Village: return kTblEmpty;
        case LandmarkKind::Ruin:    return kTblRuin;
        case LandmarkKind::Spire:   return kTblSpire;
        default: break;
    }
    // Feature override (forest from FT_Tree, mountain from FT_Mountain).
    if (feature == FT_Tree)     return kTblForest;
    if (feature == FT_Mountain) return kTblMountain;
    // Biome default.
    switch (biome) {
        case Biome::Tundra:  return kTblTundra;
        case Biome::Taiga:   return kTblTaiga;
        case Biome::Snow:    return kTblSnow;
        case Biome::Valley:  return kTblValley;
        case Biome::Meadow:  return kTblMeadow;
        case Biome::Swamp:   return kTblSwamp;
        case Biome::Desert:  return kTblDesert;
        case Biome::Steppe:  return kTblSteppe;
        case Biome::Tropics: return kTblTropics;
        case Biome::Water:   return kTblEmpty;
    }
    return kTblMeadow;
}

std::vector<FaunaPick> roll_fauna(const FaunaTable& table,
                                  std::uint32_t& rngState) {
    std::vector<FaunaPick> out;
    if (table.entryCount == 0 || table.maxCount == 0) return out;

    Rng r(rngState);
    const int span  = int(table.maxCount) - int(table.minCount) + 1;
    const int count = int(table.minCount)
        + int(std::floor(r.next_f01() * float(span)));

    std::uint32_t total = 0;
    for (int i = 0; i < table.entryCount; ++i) total += table.entries[i]->weight;
    if (total == 0) { rngState = r.state; return out; }

    out.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        float roll = r.next_f01() * float(total);
        for (int e = 0; e < table.entryCount; ++e) {
            roll -= float(table.entries[e]->weight);
            if (roll <= 0.0f) {
                FaunaFaction fac = table.hasFactionOverride
                    ? table.factionOverride
                    : table.entries[e]->faction;
                out.push_back({table.entries[e], fac});
                break;
            }
        }
    }
    rngState = r.state;
    return out;
}

// ── Global monster registry ──────────────────────────────────────────
//
// Flat enumeration of every distinct creature, exactly once. The catalog
// index IS the stable creature id baked into ECS `NPCKind.type` as
// (0x100 | index) by the subworld spawn path. Order is append-only: never
// reorder (would silently re-key live entities); add new creatures at the end.
static const FaunaEntry* const kCreatureCatalog[] = {
    &kRabbit, &kDeer, &kFox, &kWolf, &kBear, &kBoar, &kSnake, &kHawk,
    &kFrog, &kGoat, &kEagle, &kCroc,
    &kGoblin, &kSkeleton, &kTroll, &kSwampThing, &kIceWraith,
    &kSandScorpion, &kStoneGolem,
};

std::span<const FaunaEntry* const> creature_catalog() {
    return std::span<const FaunaEntry* const>(kCreatureCatalog,
                                              std::size(kCreatureCatalog));
}

const FaunaEntry* creature_def(std::string_view id) {
    for (const FaunaEntry* e : kCreatureCatalog) {
        if (id == e->id) return e;
    }
    return nullptr;
}

int creature_index(const FaunaEntry* entry) {
    if (!entry) return -1;
    for (int i = 0; i < int(std::size(kCreatureCatalog)); ++i) {
        if (kCreatureCatalog[i] == entry) return i;
    }
    return -1;
}

const FaunaEntry* creature_def_from_kind(std::uint16_t kindType) {
    if (kindType < std::uint16_t{0x100}) return nullptr; // humanoid NPCType, not a monster
    const std::size_t idx = std::size_t(kindType & 0xFFu);
    if (idx >= std::size(kCreatureCatalog)) return nullptr;
    return kCreatureCatalog[idx];
}

} // namespace sm::sub

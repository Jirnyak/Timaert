// Faithful port of `src/game/subworld/fauna.ts`.
//   - 1:1 stat lines / weights / colours / counts.
//   - String factionId → `FaunaFaction` enum (zero-cost).
//   - String AI       → `FaunaAi` enum (zero-cost).
//   - Tables stored as null-terminated arrays of `const FaunaEntry*` so
//     they live in `.rodata` and never allocate.
#include "sub/fauna.h"
#include "core/rng.h"
#include <algorithm>

namespace sm::sub {

// ── Combat templates for wildlife ────────────────────────────────────
//
// Mirrors the TS critter list verbatim (hp / damage / speed / range /
// cooldown / label match the .ts file line-for-line).
static const FaunaEntry kRabbit  {"Rabbit",        15, FaunaFaction::Wildlife, FaunaAi::Flee,
    {  5,  0, 55, 0, 9.0f, "Rbt"}, 1, 0xB8A080u, 0.4f};
static const FaunaEntry kDeer    {"Deer",          12, FaunaFaction::Wildlife, FaunaAi::Flee,
    { 15,  2, 50, 2, 2.0f, "Der"}, 1, 0xA08060u, 0.6f};
static const FaunaEntry kFox     {"Fox",            8, FaunaFaction::Wildlife, FaunaAi::Wander,
    { 12,  4, 45, 2, 1.2f, "Fox"}, 1, 0xCC6633u, 0.5f};
static const FaunaEntry kWolf    {"Wolf",           6, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 30, 10, 50, 3, 1.0f, "Wlf"}, 2, 0x666666u, 0.7f};
static const FaunaEntry kBear    {"Bear",           3, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 80, 18, 35, 3, 1.5f, "Ber"}, 3, 0x5A3A1Au, 1.0f};
static const FaunaEntry kBoar    {"Boar",           5, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 40, 12, 40, 3, 1.2f, "Bor"}, 2, 0x6B4E37u, 0.7f};
static const FaunaEntry kSnake   {"Snake",          4, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 10,  8, 30, 2, 0.8f, "Snk"}, 1, 0x3A5A2Au, 0.3f};
static const FaunaEntry kHawk    {"Hawk",           3, FaunaFaction::Wildlife, FaunaAi::Wander,
    {  8,  5, 60, 3, 1.0f, "Hwk"}, 1, 0x8B6B4Bu, 0.4f};
static const FaunaEntry kFrog    {"Frog",          10, FaunaFaction::Wildlife, FaunaAi::Flee,
    {  3,  0, 30, 0, 9.0f, "Frg"}, 1, 0x2A8A2Au, 0.3f};
static const FaunaEntry kGoat    {"Mountain Goat",  8, FaunaFaction::Wildlife, FaunaAi::Flee,
    { 20,  5, 40, 2, 1.5f, "Mgt"}, 1, 0xB0A090u, 0.6f};
static const FaunaEntry kEagle   {"Eagle",          4, FaunaFaction::Wildlife, FaunaAi::Wander,
    { 12,  7, 65, 3, 1.0f, "Egl"}, 2, 0x5A4030u, 0.5f};
static const FaunaEntry kCroc    {"Crocodile",      4, FaunaFaction::Wildlife, FaunaAi::Combat,
    { 50, 15, 25, 3, 1.5f, "Crc"}, 3, 0x4A6A3Au, 0.8f};

// ── Monsters ────────────────────────────────────────────────────────
static const FaunaEntry kGoblin  {"Goblin",         4, FaunaFaction::Demons,  FaunaAi::Combat,
    { 25,  8, 40, 3, 1.0f, "Gbl"}, 2, 0x4A8A2Au, 0.6f};
static const FaunaEntry kSkeleton{"Skeleton",       3, FaunaFaction::Demons,  FaunaAi::Combat,
    { 35, 10, 30, 3, 1.2f, "Skl"}, 3, 0xD0C8B0u, 0.6f};
static const FaunaEntry kTroll   {"Troll",          1, FaunaFaction::Demons,  FaunaAi::Combat,
    {120, 25, 25, 4, 2.0f, "Trl"}, 5, 0x3A6A3Au, 1.2f};
static const FaunaEntry kSwampThing{"Swamp Thing",  3, FaunaFaction::Demons,  FaunaAi::Combat,
    { 60, 14, 20, 4, 1.5f, "Swt"}, 3, 0x2A4A1Au, 0.9f};
static const FaunaEntry kIceWraith{"Ice Wraith",    2, FaunaFaction::Demons,  FaunaAi::Combat,
    { 45, 16, 35, 5, 1.3f, "Iwr"}, 4, 0xA0D0E0u, 0.7f};
static const FaunaEntry kSandScorpion{"Sand Scorpion",5,FaunaFaction::Demons, FaunaAi::Combat,
    { 35, 12, 35, 3, 1.0f, "Ssc"}, 2, 0xC0A050u, 0.6f};
static const FaunaEntry kStoneGolem{"Stone Golem",  1, FaunaFaction::Demons,  FaunaAi::Combat,
    {150, 20, 15, 4, 2.5f, "Glm"}, 5, 0x7A7A7Au, 1.3f};

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
    int span  = int(table.maxCount) - int(table.minCount) + 1;
    int count = int(table.minCount) + int(r.next_u32() % std::uint32_t(span));

    std::uint32_t total = 0;
    for (int i = 0; i < table.entryCount; ++i) total += table.entries[i]->weight;
    if (total == 0) { rngState = r.state; return out; }

    out.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        std::uint32_t roll = r.next_u32() % total;
        std::uint32_t acc  = 0;
        for (int e = 0; e < table.entryCount; ++e) {
            acc += table.entries[e]->weight;
            if (roll < acc) {
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

} // namespace sm::sub

// Subworld fauna — biome / feature / landmark spawn tables. Pure data:
// each entry says what to spawn, how often, with what stats / colour.
// Faithful port of `src/game/subworld/fauna.ts`.
//
// Adding a creature: append a static `FaunaEntry` constant and reference
// it in one or more tables. Adding a biome: add a table + register it in
// `get_fauna_table`. No engine code changes ever needed.
#pragma once
#include <cstdint>
#include <vector>
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/army.h"

namespace sm::sub {

// Landmark kind on a macro cell. Drives ruin / spire / settlement-specific
// fauna routing. None = no landmark on this cell.
enum class LandmarkKind : std::uint8_t {
    None = 0, City, Village, Ruin, Spire,
};

// AI hint for the spawned entity.
enum class FaunaAi : std::uint8_t { Wander = 0, Flee = 1, Combat = 2 };

// Faction (mirrors TS string ids): wildlife is non-hostile prey; demons
// are always hostile; bandits are hostile to player faction.
enum class FaunaFaction : std::uint8_t {
    Neutral = 0, Wildlife = 1, Bandits = 2, Demons = 3,
};

struct FaunaEntry {
    const char*    label;
    std::uint16_t  weight;
    FaunaFaction   faction;
    FaunaAi        ai;
    CombatTemplate combat;
    std::uint8_t   baseLevel;
    std::uint32_t  color;       // 0xRRGGBB packed
    float          radius;
};

struct FaunaTable {
    const FaunaEntry* const* entries;
    std::uint16_t entryCount;
    std::uint8_t  minCount;
    std::uint8_t  maxCount;
    FaunaFaction  factionOverride;
    bool          hasFactionOverride;
};

// Resolve the table for a cell. Priority: landmark > feature > biome.
const FaunaTable& get_fauna_table(Biome biome, FeatureType feature,
                                  LandmarkKind landmark);

struct FaunaPick { const FaunaEntry* entry; FaunaFaction faction; };

// Sample [minCount, maxCount] then pick by weighted random.
std::vector<FaunaPick> roll_fauna(const FaunaTable& table,
                                  std::uint32_t& rngState);

} // namespace sm::sub

// Subworld fauna — biome / feature / landmark spawn tables. Pure data:
// each entry says what to spawn, how often, with what stats / colour.
// Faithful port of `src/game/subworld/fauna.ts`.
//
// Adding a creature: append a static `FaunaEntry` constant and reference
// it in one or more tables. Adding a biome: add a table + register it in
// `get_fauna_table`. No engine code changes ever needed.
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
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

// Procedural body plan for the subworld 3D creature billboard. The fauna table
// declares each creature's shape; the renderer draws the matching silhouette
// (and its real shadow). A drawn atlas/image sprite overrides the procedural
// body later (universal sprite resolver) with no engine change. Keep the values
// in sync with shaders/creature_sprite.glsl.
enum class CreatureArchetype : std::uint8_t {
    Quadruped = 0, // horizontal 4-legged beast (rabbit / wolf / bear / croc …)
    Avian     = 1, // winged (hawk / eagle)
    Serpent   = 2, // legless sinuous (snake)
    Biped     = 3, // upright humanoid monster (goblin / troll / swamp thing)
    Undead    = 4, // thin bony / ghostly upright (skeleton / ice wraith)
    Hulk      = 5, // massive blocky (stone golem)
    Critter   = 6, // tiny squat blob (frog)
};

struct FaunaEntry {
    const char*       id;          // stable machine id ("wolf") — source of truth
    const char*       label;       // display name ("Wolf")
    std::uint16_t     weight;
    FaunaFaction      faction;
    FaunaAi           ai;
    CombatTemplate    combat;
    std::uint8_t      baseLevel;
    std::uint32_t     color;       // 0xRRGGBB packed
    float             radius;
    CreatureArchetype archetype;   // procedural body plan (render only)
    const char*       lootId = nullptr; // loot-profile override; null => faction default
    std::uint16_t     xpReward = 0;     // per-creature XP base; 0 => generic level-scaled
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

// ── Global monster registry ──────────────────────────────────────────
// The single source of truth for every creature (rabbit → dragon), mirroring
// the item catalog (macro/items.h): a flat enumeration + stable-id lookup.
// The subworld spawn path bakes the ECS `NPCKind.type` as (0x100 | catalog
// index); the high 0x100 bit marks "monster" (vs a humanoid NPCType < Count),
// and `creature_def_from_kind` recovers the row on the death / loot path.
std::span<const FaunaEntry* const> creature_catalog();
const FaunaEntry* creature_def(std::string_view id);        // nullptr if unknown
const FaunaEntry* creature_def_from_kind(std::uint16_t kindType); // nullptr if not a monster
int               creature_index(const FaunaEntry* entry);  // -1 if not in the catalog

} // namespace sm::sub

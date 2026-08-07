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

namespace sm {

// Landmark kind on a macro cell. Drives ruin / spire / settlement-specific
// fauna routing. None = no landmark on this cell.
enum class LandmarkKind : std::uint8_t {
    None = 0, City, Village, Ruin, Spire,
};

// AI hint for the spawned entity.
enum class FaunaAi : std::uint8_t { Wander = 0, Flee = 1, Combat = 2 };

// Creature faction — the registry id string (macro/faction.h), the SAME key
// every other faction consumer uses. The old FaunaFaction enum was a fourth
// parallel vocabulary whose indices collided with the humanoid one (a bandit
// creature (2) and a bandit NPC (3) compared as enemies, a bandit NPC (3) and
// a DEMON creature (3) as brothers — the spell friendly-fire check used the
// raw index). One id space ends that class of bug. nullptr = factionless.

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
    const char*       factionId;   // registry id ("wildlife"), nullptr = none
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
    // Registry faction id forced onto every pick from this table (a ruin spawns
    // its wolves as demons), nullptr = keep each creature's own row faction.
    const char*   factionOverride;
};

// Resolve the table for a cell. Priority: landmark > forest > biome.
// `treeCount` is the cell's macro tree count (macro/tree_layer.h): a
// forest-CLASS cell (is_forest_cell) spawns forest fauna — including on a
// mountain — regardless of its base biome.
const FaunaTable& get_fauna_table(Biome biome, int treeCount,
                                  LandmarkKind landmark);

struct FaunaPick { const FaunaEntry* entry; const char* factionId; };

// Sample [minCount, maxCount] then pick by weighted random.
std::vector<FaunaPick> roll_fauna(const FaunaTable& table,
                                  std::uint32_t& rngState);

// ── The honest headcount (Session 16) ────────────────────────────────
// A cell's fauna CAPACITY — how many heads its own spawn table carries
// (the winning table's maxCount). This is the derived baseline of the
// fauna_count macro stock (macro/macro_stock.h): what stands in a cell
// nobody has hunted yet.
int fauna_cell_capacity(Biome biome, int treeCount, LandmarkKind landmark);

// The same capacity resolved from the macro cell's own data: biome via the
// one biome_at cascade (terrain rgba, the travel.cpp pattern), forest class
// from the tree layer, landmark from the named places standing on the cell
// (settlements / villages / spires — the same scan resolve_context runs).
// Null gs/terrain = no context = 0: nothing to embody.
struct GameState;
struct TerrainData;
struct TreeLayer;
int fauna_cell_capacity_at(const GameState* gs, const TerrainData* terrain,
                           const TreeLayer* trees, int x, int y);

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

} // namespace sm

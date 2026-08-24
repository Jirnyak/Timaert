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
#include "macro/behaviour.h"
#include "macro/landmark_registry.h"
#include "macro/npc.h"
#include "macro/sprite_rows.h"

namespace sm {

// The landmark vocabulary is LandmarkType (macro/landmark_registry.h) — the
// one registry enum. A private five-value "LandmarkKind" lived here until
// 2026-08-24; it was the third copy of the same idea, and the kinds it could
// not name (Lair, Shrine, Mine, Tower) could not route fauna at all.

// AI hint for the spawned entity.
// (`FaunaAi` lived here until 2026-08-20 — a second behaviour vocabulary for
// beasts. It is `AIBehaviour` now, the one every row speaks: macro/behaviour.h.)

// Creature faction — the registry id string (macro/faction.h), the SAME key
// every other faction consumer uses. The old FaunaFaction enum was a fourth
// parallel vocabulary whose indices collided with the humanoid one (a bandit
// creature (2) and a bandit NPC (3) compared as enemies, a bandit NPC (3) and
// a DEMON creature (3) as brothers — the spell friendly-fire check used the
// raw index). One id space ends that class of bug. nullptr = factionless.

// A creature row IS an npc row (owner, 2026-08-20: one system). The struct
// that used to live here is `NpcTypeDef` in macro/npc.h, and every creature is
// a member of the ONE table there — this alias survives only so the spawn
// tables below read as what they are: lists of rows a place may roll.
using FaunaEntry = NpcTypeDef;

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
                                  LandmarkType landmark);

struct FaunaPick { const FaunaEntry* entry; const char* factionId; };

// Sample [minCount, maxCount] then pick by weighted random.
std::vector<FaunaPick> roll_fauna(const FaunaTable& table,
                                  std::uint32_t& rngState);

// ── The honest headcount (Session 16) ────────────────────────────────
// A cell's fauna CAPACITY — how many heads its own spawn table carries
// (the winning table's maxCount). This is the derived baseline of the
// fauna_count macro stock (macro/macro_stock.h): what stands in a cell
// nobody has hunted yet.
int fauna_cell_capacity(Biome biome, int treeCount, LandmarkType landmark);

// The same capacity resolved from the macro cell's own data, through THE
// layer envelope (macro_stock.h MacroWorld): biome via the one biome_at
// cascade, forest class from the tree layer, landmark from the baked
// cell → landmark index (macro/landmark_grid.h — this function carried its
// own hand-written scan of the named places until 2026-08-24, the drifted
// second implementation of "what stands here", canon-audit C2). Missing
// layers read as no context: nothing to embody, 0.
struct MacroWorld;
int fauna_cell_capacity_at(const MacroWorld& w, int x, int y);

// (The breeding step is the ONE growth law — resource_fields_daily_growth,
// macro/resource_field.h: beasts breed where beasts are, capped by this
// capacity — and lives with the rows: macro/macro_stock.cpp.)

// ── Creature views over THE one body table ───────────────────────────
// The source of truth for every creature (rabbit → dragon) is the same
// `kNpcTypeDefs` row it is for a peasant (macro/npc.h, CANON S16); these are
// convenience views over its creature stripe (`is_creature_row`). Ids are one
// contiguous ordinal space — the old `0x100 | catalog index` encoding is DEAD
// (2026-08-20) — so `creature_def_from_kind` is just "the row, if it is a
// beast", serving the death / loot path.
std::span<const FaunaEntry* const> creature_catalog();
const FaunaEntry* creature_def(std::string_view id);        // nullptr if unknown
const FaunaEntry* creature_def_from_kind(std::uint16_t kindType); // nullptr if not a monster
int               creature_index(const FaunaEntry* entry);  // -1 if not in the catalog

} // namespace sm

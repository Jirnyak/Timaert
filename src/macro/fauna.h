// Subworld fauna — biome / feature / landmark spawn tables. Pure data:
// each entry says what to spawn, how often, with what stats / colour.
// Faithful port of `src/game/subworld/fauna.ts`.
//
// Adding a creature: one row of THE body table (macro/npc.h) that states its
// ground in its habitat mask (kSpawnHabitats). Adding a biome: its bit and
// its counts row. No engine code changes ever needed.
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

// ── THE spawn law (CANON S6/S12, owner 2026-08-24) ───────────────────
// Who a cell rolls = habitat × danger-match, over the ONE body table.
//
//   weight(row, cell) = row.weight × habitat(row, cell)
//                     × danger_match(spawn_strength(row), cell danger)
//
// · habitat — a bitmask COLUMN of the body table (kSpawnHabitats below,
//   enum-ordered beside the law that reads it): which biomes the row lives
//   in, plus the forest class and the den kinds. It replaced thirteen
//   hand-built list-tables and the switch ladder that chose between them
//   (canon-audit F5) — a new species states its ground in its own row.
// · spawn_strength — DERIVED, never hand-set: log₂ of the row's own combat
//   power (hp × damage/cooldown), normalized over the table — the weakest
//   row is 0, the strongest demon 255. Strengthen a row and it migrates to
//   dangerous ground by itself (S26: constants derive from world facts).
// · danger_match — symmetric around strength == danger (the owner's word:
//   hell is peopled by demons, not by demons plus rabbits), HALVING per
//   kDangerHalfLife bytes of mismatch, floor 1 — a doom-tier horror in a
//   safe meadow is «исчезающе малая вероятность», literally, never zero.
//   No cutoffs, no bands: composition flows across the map.
//
// The zone therefore changes WHAT SPAWNS and nothing else — the negative
// control that no number is scaled on the body after the pick stands
// (subworld_spawn_parity_test; the autolevel stays dead).

// Habitat bits: 0..10 = the Biome ordinals; then the derived classes.
inline constexpr std::uint16_t kHabForest = 1u << 11; // forest-CLASS cell
inline constexpr std::uint16_t kHabRuin   = 1u << 12; // ruin denizen
inline constexpr std::uint16_t kHabSpire  = 1u << 13; // spire denizen
inline constexpr std::uint16_t kHabTown   = 1u << 14; // settlement crowd
inline constexpr std::uint16_t hab(Biome b) {
    return std::uint16_t(1u << std::uint16_t(b));
}

// Halving distance of the match law. Derived, not tuned: ten halvings span
// the whole 0..255 continuum (a full-span mismatch lands at 2^-10 of the
// peak before the floor), so kDangerHalfLife = 256 / 10.
inline constexpr int kDangerHalfLife = 256 / 10;

// The derived strength byte of a row (see the law above). Computed once
// from the table itself; every consumer of "how dangerous is this thing" —
// spawn, and tomorrow loot quality and hire pricing — asks HERE.
std::uint8_t spawn_strength(NPCType t);

// The symmetric never-zero match term, in 1/1024ths of the peak.
std::uint32_t danger_match_weight(std::uint8_t strength, std::uint8_t danger);

// Everything the roll wants to know about the place — assembled by the
// caller from its CellContext / CellFacts. Zero members are legal silence.
struct SpawnContext {
    Biome        biome = Biome::Meadow;
    bool         forest = false;          // is_forest_cell(treeCount)
    LandmarkType landmark = LandmarkType::None;
    std::uint8_t danger = 0;              // the cell's danger byte (zones)
    // Live deposit kinds within the profession reach (bit per DepositKind):
    // the ground that raises a macro miner also puts him in the street crowd.
    std::uint8_t depositsNear = 0;
};

struct FaunaPick { const FaunaEntry* entry; const char* factionId; };

// Roll a cell's wild population by the law. Count comes from the place's
// counts row; faction comes from the landmark's spawnFaction column when it
// names one (a ruin's wolves ARE demons), else the row's own.
std::vector<FaunaPick> roll_spawns(const SpawnContext& ctx,
                                   std::uint32_t& rngState);

// One townsperson, by the SAME law over the kHabTown stripe: the row weights
// carry the old 55/21/21/3 mix as data, a profession row joins the crowd only
// where its deposit gate is open, and the danger match rides on top. This
// replaced the RNG-only pick_civilian_type — the crowd that could not tell an
// iron town from a swamp one (canon-audit F4).
NPCType pick_town_row(const SpawnContext& ctx, std::uint32_t& rngState);

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

// THE resource fields of the world — the owner's causality made structure:
//
//   клеточный мир → рельеф → климат → РЕСУРСЫ → и только потом заселение.
//
// A resource field is a per-cell quantity over the whole map whose BASELINE
// is a pure function of terrain/climate + the world seed. Features and
// landmarks NEVER enter a baseline: settlement is downstream of resources
// (a village sits by good land; good land does not appear because a village
// sat down). What settlement does is decide WHERE a resource gets embodied
// (ploughed parcels manifest the wheat a cell could always grow).
//
// Storage follows from the row's own law, in two dialects behind ONE door:
//   · SPARSE rows (wheat, fauna): capacity is capped by the baseline, so the
//     live state is baseline − scar and the world tends back to it — the
//     persisted set stays "cells play has scarred", self-erasing when healed.
//     Some fields' exact embodied yield is known only to subworld generation
//     (wheat parcels vs a garden plot), and a scar subtracts correctly from
//     ANY yield — a remaining-count override cannot, which is exactly how
//     fauna and crops briefly grew two dialects of one idea.
//   · CARRIER rows (trees): the field GROWS past its virgin derivation
//     (owner's law: the baseline is the world's initial condition, not an
//     attractor — untouched land thickens into чащобы), so sparseness is a
//     fiction; the live state is a dense grid the renderer already draws
//     (TreeLayer.data + revision), and the save carries the grid whole.
//
// The macro_stock ledger stays THE gameplay read/write door — its rows call
// through here. Deposits join as carrier rows in Inc B of this track.
#pragma once

#include "macro/seasons.h"
#include <cstdint>
#include <unordered_map>

namespace sm {

struct GameState;
struct TerrainData;
struct TreeLayer;
struct MacroWorld;

enum class ResourceFieldId : std::uint8_t {
    Wheat = 0,   // standing-wheat potential — fertility (climate G channel)
    Fauna,       // wild headcount — biome capacity (macro/fauna.h)
    Trees,       // forest of a cell — carrier row (macro/tree_layer.h grid)
    Clay,        // alluvial pits — carrier row (macro/deposit_layer.h)
    Iron,        // finite mountain veins — carrier row (deposit_layer.h)
    Stone,       // quasi-infinite quarries — carrier row (deposit_layer.h)
    Count,
};

// The wheat row's SCALE: the fertility channel (0..255) maps onto this many
// stands in a cell. It lives here, at the row's own door, because everyone
// who prices fertility must price it the same way — the field stamp's
// ploughable bar and the settlement score's arable term both derive from
// this number rather than each re-deriving one of their own.
constexpr int kMaxWheatStandsPerCell = 4096;   // po2 scale of the estimate

// ── The ONE growth/diffusion law (owner, R2 track) ────────────────────────
// Every field is BORN from time and context through the same walker; the
// nuances are the row's data, never a second system. The map is walked in
// EPOCH slices: a cell is due once per kGrowthEpochDays, and one game day
// processes cells/32 of the domain — smooth long-term dynamics need no
// full-map day and no per-tick work.
//
// kGrowthEpochDays = 32: the year is 128 days = 4 seasons × 32 — the same
// month the path grid is re-baked on (Session 21), and exactly the old
// wheat/fauna healing period, so their law (+1 per visit) is preserved by
// construction.
// THE season (owner, 2026-08-24: «все росты раз в сезон» — one rhythm for
// everything that grows: forest, beasts, wheat, iron, and the towns quote
// their population rate by the same period in econ_day.h). The epoch used
// to be its own literal 32 that merely HAPPENED to equal the season.
constexpr int kGrowthEpochDays = kDaysPerSeason;

inline bool growth_cell_due(std::uint32_t cellIdx, int day) {
    return int(cellIdx % std::uint32_t(kGrowthEpochDays))
        == day % kGrowthEpochDays;
}

// What ground the walker covers for a row:
//   None        — the field does not grow (stone, clay: quasi-static).
//   CarrierGrid — every cell of the dense carrier (trees: чащобы thicken,
//                 forests spread into neighbours).
//   OwnScars    — only cells play has scarred (wheat, fauna: capacity is
//                 the baseline, so unscarred cells have nowhere to grow).
//   Geology     — lump birth on the HOST row's cells that lack this row
//                 (iron: the scarcer the world's iron, the likelier a stone
//                 quarry turns out to hold a vein — the W2c rule as a row).
enum class GrowthDomain : std::uint8_t { None, CarrierGrid, OwnScars, Geology };

struct ResourceFieldDef {
    const char* id;
    // The pure baseline: world context → capacity of one WRAPPED cell.
    // Sparse rows read baseline − scar and clamp the scar to it; a carrier
    // row's baseline is its initial condition only (null: the carrier was
    // filled by worldgen and lives its own life from there).
    int (*baseline)(const MacroWorld& w, int x, int y);
    // The growth law. growthAt = units born at a DUE cell this visit (for
    // Geology: the lump a fresh vein opens with). Pure and deterministic —
    // context in, delta out; the walker owns dueness and the write.
    GrowthDomain growthDomain;
    int (*growthAt)(const MacroWorld& w, int x, int y);
    ResourceFieldId growthHost;   // Geology only: whose cells host the birth
    // Carrier hooks — both set = the row's live state is a dense structure
    // outside the scar maps (trees: the TreeLayer grid the map renders).
    // Read is the current count; apply clamps to the row's own cap, writes
    // the carrier and bumps its revision. Sparse rows leave both null.
    int  (*carrierRead)(const MacroWorld& w, int x, int y);
    void (*carrierApply)(MacroWorld& w, int x, int y, int delta);
};

// The registry row (macro_stock.cpp owns the table).
const ResourceFieldDef& resource_field_def(ResourceFieldId f);

// The current count of a cell. Sparse rows: capacity − scar, floored at
// zero, fail-closed without terrain. Carrier rows: the carrier's cell.
int resource_field_read(const MacroWorld& w, ResourceFieldId f, int x, int y);

// Move the field: negative spends, positive returns. Sparse rows clamp the
// scar to [0, baseline] and a healed cell self-erases; carrier rows clamp
// to their own cap and bump their carrier's revision.
void resource_field_apply(MacroWorld& w, ResourceFieldId f, int x, int y,
                          int delta);

// The raw scar of a cell (0 = unscarred) — what subworld generation
// subtracts from its own embodied yield (CellContext.cropHarvested).
// Sparse-dialect rows only; a carrier row has no scar and reads 0.
int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx);

// One daily step for EVERY field — THE growth/diffusion door: each row's
// due slice of its domain is visited, growthAt prices the birth, and the
// write goes through the row's own dialect (scar heal / carrier apply /
// genesis). Replaces both the old scar-heal walk and the bespoke iron
// discovery.
void resource_fields_daily_growth(MacroWorld& w, int day);

} // namespace sm

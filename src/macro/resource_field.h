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
// Storage is ONE dialect for every field: sparse SCAR overrides — units play
// has taken and regrowth has not yet returned. Some fields' exact embodied
// yield is known only to subworld generation (wheat parcels vs a garden
// plot), and a scar subtracts correctly from ANY yield — a remaining-count
// override cannot, which is exactly how fauna and crops briefly grew two
// dialects of one idea. A healed cell erases itself: the persisted set is
// "cells play has scarred", never the world. Regrowth is one unit per
// period per scarred cell, walked by ONE door on the daily world tick.
//
// The macro_stock ledger stays THE gameplay read/write door — its rows call
// through here. Trees and deposits still live in their own carriers (the
// map renders the tree grid; a deposit carries a KIND): their rows join
// this registry in the settlement-from-resources session (R2).
#pragma once

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
    Count,
};

// The wheat row's SCALE: the fertility channel (0..255) maps onto this many
// stands in a cell. It lives here, at the row's own door, because everyone
// who prices fertility must price it the same way — the field stamp's
// ploughable bar and the settlement score's arable term both derive from
// this number rather than each re-deriving one of their own.
constexpr int kMaxWheatStandsPerCell = 4096;   // po2 scale of the estimate

struct ResourceFieldDef {
    const char* id;
    // The pure baseline: world context → capacity of one WRAPPED cell.
    int (*baseline)(const MacroWorld& w, int x, int y);
    // Regrowth cadence in GAME days — a function pointer so the number
    // keeps living at its own single door (fauna.h / macro_stock.cpp).
    int (*regrowPeriodDays)();
};

// The registry row (macro_stock.cpp owns the table).
const ResourceFieldDef& resource_field_def(ResourceFieldId f);

// Capacity − scar, floored at zero. Fail-closed without terrain.
int resource_field_read(const MacroWorld& w, ResourceFieldId f, int x, int y);

// Move the field: negative spends (deepens the scar), positive returns
// (heals it). Scar clamps to [0, baseline]; a healed cell self-erases.
void resource_field_apply(MacroWorld& w, ResourceFieldId f, int x, int y,
                          int delta);

// The raw scar of a cell (0 = unscarred) — what subworld generation
// subtracts from its own embodied yield (CellContext.cropHarvested).
int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx);

// One daily step for EVERY field: on its own cadence, every scarred cell
// heals one unit through resource_field_apply (whose write self-cleans).
void resource_fields_daily_regrow(MacroWorld& w, int day);

} // namespace sm

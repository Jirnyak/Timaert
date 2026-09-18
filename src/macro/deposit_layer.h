// The world's mineral deposits — cells of the MAP, not lists on landmarks
// (owner's ruling, W2): clay by the rivers, stone and iron in the mountains.
// Agents farm the nearest deposit to their home exactly as woodcutters farm
// the nearest forest.
//
// Since R2 each kind is a CARRIER row of the resource-field registry
// (macro/resource_field.h): this layer is the rows' live state, mutated only
// through resource_field_apply, and the save carries the cells WHOLE (the
// same Persistence ruling the tree grid rides under). The derivation is the
// field's initial condition, rebuilt for a NEW game only.
//
// A cell may hold SEVERAL kinds — a discovered iron vein lives IN a stone
// mountain and the quarry does not vanish (owner: у каждого ресурса своё
// поле, никто не исчезает). That is why storage is one sparse map PER KIND,
// not one kind per cell: the old single-kind DepositCell could only express
// discovery as a kind-SWAP, which silently deleted the stone.
//
// An exhausted deposit LEAVES the map (annihilation law, 2026-08-28 — see
// DepositLayer::cells below); what the world misses is measured against the
// derived virginUnits baseline, and new iron is born by the Iron row's
// GrowthDomain::Geology walk (macro_stock.cpp), not by a bespoke rule here.
//
// Grain is deliberately NOT here (the Wheat row prices fertility), nor wood
// (the Trees row): each commodity lives through the carrier that already
// owns its kind of renewal.
#pragma once
#include "core/table_guard.h"
#include "macro/resource_field.h"   // ResourceGrid — THE shape of a row
#include "core/torus.h"
#include <cstdint>
#include <vector>

#include "macro/features.h"      // the kind's own mine feature (v71)
#include "macro/map_generator.h"

namespace sm {

enum class DepositKind : std::uint8_t {
    Clay = 0, Iron = 1, Stone = 2,
    // The mint metals (CANON S10). Appended — saved kind blocks stay ordered.
    // Copper and Gold joined silver 2026-09-18 with the three coin nominals
    // (verdict №1): a coin is struck from ITS OWN metal, so a world with one
    // mint metal could only ever strike one nominal.
    Silver = 3, Copper = 4, Gold = 5,
};
inline constexpr int kDepositKindCount = 6;

// ── THE deposit-kind registry (CANON S16) ────────────────────────────────
// One row per kind: the commodity it yields into the ONE dictionary, and how
// a settlement site prices a vein of it (settlement_score.cpp deposit_term —
// iron is the prize, stone and clay are common wealth). Two switch-shaped
// dictionaries carried these columns until 2026-08-29.
struct DepositDef {
    DepositKind kind;         // MUST equal the row's index (guard below)
    const char* commodityId;
    int         siteWorth;    // settlement-score base of a vein in reach
    // The kind's OWN mine feature (owner 2026-08-31, v71): «шахты-фичи
    // разных типов — золотая, серебряная, каменоломня» — the column the
    // mining crew stamps when it opens the vein (ai_gatherer).
    FeatureType mineFeature;
};
inline constexpr DepositDef kDepositDefs[kDepositKindCount] = {
    {DepositKind::Clay,  "clay",   8, FT_ClayPit},
    {DepositKind::Iron,  "iron",  16, FT_IronMine},
    {DepositKind::Stone, "stone",  8, FT_Quarry},
    // A mint vein prices like iron for a settlement site: the mint is
    // wealth, but the town still eats bread, not coins.
    {DepositKind::Silver, "silver", 16, FT_SilverMine},
    {DepositKind::Copper, "copper", 16, FT_CopperMine},
    {DepositKind::Gold,   "gold",   16, FT_GoldMine},
};
static_assert(rows_in_enum_order(kDepositDefs, &DepositDef::kind),
              "kDepositDefs row order must mirror DepositKind");
inline constexpr const DepositDef& deposit_def(DepositKind kind) {
    return kDepositDefs[std::size_t(kind)];
}

// The commodity id each kind yields — the ONE dictionary's noun.
const char* deposit_commodity_id(DepositKind kind);

struct DepositLayer {
    int width = 0;
    int height = 0;
    // kind -> THE FIELD of that kind: units per cell, 0 = no vein here.
    //
    // This was `unordered_map<cellIdx, units>` from the layer's first commit
    // (81379bf6), whose own message claimed "the proven tree-layer discipline"
    // while the struct did the opposite. A hash keeps the values and throws the
    // world's CONNECTEDNESS away — it answers "what is at exactly this key" and
    // nothing else — so the day geology was asked "is there a vein NEAR here"
    // the answer became a scan of every vein, per cell (problems.md §52). The
    // array over the torus IS the connected world, and it carries the reach
    // field that makes the neighbourhood question O(1) (resource_field.h).
    //
    // ANNIHILATION LAW (owner, 2026-08-28): a worked-out vein is a vein that no
    // longer exists. In a field that is the value 0 — the cell stays, as every
    // cell of the world does, and holds nothing. The DERIVED virgin baseline
    // below carries the scarcity the old "dry cells linger at 0" kept around
    // for. Mutate through the doors, never the array: the doors are what keep
    // the live count and the reach discs true.
    ResourceGrid cells[kDepositKindCount];
    // WHAT THE WORLD WAS BORN WITH, in units per kind — the scarcity baseline
    // (owner, 2026-08-28: "суммарно железа в мире"). DERIVED, never saved:
    // build_deposit_layer is a pure function of terrain + seed, and the load
    // path re-derives the layer before overlaying the save's cells, so the
    // baseline is recomputed for free every boot. Scarcity = 1 - live/virgin;
    // discovery may push live ABOVE virgin, which simply reads as "no
    // scarcity". 64-bit because stone on an all-mountain 1024^2 map is ~2^30
    // units and the growth law sums in 64-bit anyway.
    std::int64_t virginUnits[kDepositKindCount] = {};
    // Runtime dirty counter for future consumers; never serialized.
    std::uint32_t revision = 0;

    ResourceGrid& grid(DepositKind kind) {
        return cells[std::size_t(kind)];
    }
    const ResourceGrid& grid(DepositKind kind) const {
        return cells[std::size_t(kind)];
    }

    // Packs a WRAPPED cell into a flat index. The wrap itself is the one in
    // core/torus.h; it used to be written out twice inline right here.
    std::uint32_t wrap_index(int x, int y) const {
        return std::uint32_t(wrapi(y, height)) * std::uint32_t(width)
             + std::uint32_t(wrapi(x, width));
    }
    // The kind's units standing at a WRAPPED cell; 0 = no deposit here (a
    // worked-out one is annihilated, so "dry" is not a state a cell has).
    std::int32_t remaining_at(DepositKind kind, int x, int y) const {
        return cells[std::size_t(kind)].at(x, y);
    }
    // Any deposit of any kind here? (worldgen reporting, map tooltips)
    bool any_at(int x, int y) const {
        for (const auto& g : cells) if (g.at(x, y) != 0) return true;
        return false;
    }
    // THE neighbourhood question, in O(1) — "is a live vein of this kind
    // within a gatherer's reach of here". The field behind it is the row's
    // own (resource_field.h reachCells), stamped by the grid's writes.
    bool kind_near(DepositKind kind, int x, int y) const {
        return cells[std::size_t(kind)].near(x, y);
    }
};

// Size every kind's field to a world, zeroed, each carrying the reach radius
// its registry row declares. Worldgen calls it; so must any fixture that
// hand-places veins, because a field that was never allocated silently
// swallows every write.
void allocate_deposit_fields(DepositLayer& layer, int width, int height);

// Derive the deposit sites from terrain + seed. Deterministic; density and
// base amounts are the po2 constants in deposit_layer.cpp (clay 1/64 of
// river-adjacent land, stone 1/64 of mountains quasi-infinite, iron 1/256 of
// mountains finite).
DepositLayer build_deposit_layer(const TerrainData& terrain,
                                 std::uint32_t seed, float seaLevel);

// THE quantity door (the registry's carrier hook lands here): a write down
// to zero ANNIHILATES the cell, bumps the revision. A cell that was never a
// deposit of this kind is refused — MINING cannot invent geology; creation
// goes through create_deposit below, deliberately.
bool set_deposit_remaining(DepositLayer& layer, DepositKind kind,
                           int x, int y, std::int32_t remaining);

// The GENESIS door: the world creates geology — a discovered vein, a future
// growth law. Inserts (or refills) the kind's cell and bumps the revision.
void create_deposit(DepositLayer& layer, DepositKind kind,
                    int x, int y, std::int32_t amount);


// Load path (v37): overwrite the live cells with the save's (the save
// carries them whole). Width/height stay the layer's own — the version gate
// makes a foreign-map save unreachable; stale indices are dropped.
void restore_deposit_cells(DepositLayer& layer, const DepositLayer& loaded);

// (The bespoke W2c iron-discovery trio — discover_iron_vein, iron_depletion,
// iron_discovery_chance_per_day — died 2026-08-29: the LIVE law is the Iron
// row's GrowthDomain::Geology walk in macro_stock.cpp, «железо родится где
// мир оскудел», and the dead path guarded a second copy of it.)

// The lump a fresh vein of THIS KIND opens with — the Geology domain's
// growth number, read off the kind's own generation row (the `veinBase`
// column). One door for six metals: the two hand-written functions that
// stood here (iron_vein_lump / silver_vein_lump) were a per-kind dialect,
// and the third metal would have been a third copy. 0 = this kind does not
// regrow.
int deposit_vein_lump(DepositKind kind);

// The MINE's consolidation (owner 2026-08-31, CANON S10 «шахта — фича, как
// поле»): flood the locally CONNECTED cluster of same-kind veins
// (8-adjacency over live deposit cells) from the mine's cell and fold their
// units INTO that cell — the absorbed cells leave the map (the annihilation
// law), the mine's cell holds their sum, and every worksite/score law reads
// on unchanged because the stock never left the deposit layer. Returns the
// number of cells absorbed (0 = the mine sits on a lone vein — also fine).
int consolidate_deposit_cluster(DepositLayer& layer, DepositKind kind,
                                int x, int y);

} // namespace sm

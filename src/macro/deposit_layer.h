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
#include "core/torus.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "macro/features.h"      // the kind's own mine feature (v71)
#include "macro/map_generator.h"

namespace sm {

enum class DepositKind : std::uint8_t {
    Clay = 0, Iron = 1, Stone = 2,
    // The mint metal (CANON S10). Appended — saved kind blocks stay ordered.
    Silver = 3,
};
inline constexpr int kDepositKindCount = 4;

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
    // A silver vein prices like iron for a settlement site: the mint is
    // wealth, but the town still eats bread, not coins.
    {DepositKind::Silver, "silver", 16, FT_SilverMine},
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
    // kind → (cell index → remaining units), every entry ALIVE (> 0).
    // ANNIHILATION LAW (owner, 2026-08-28): a worked-out vein is a vein that
    // no longer exists — the cell leaves the map the moment it runs dry and
    // the chronicle keeps the deed. The old law ("a dry vein stays at 0")
    // kept dead geology around solely to derive scarcity; the DERIVED
    // baseline below carries that instead. Mutate through the registry only.
    std::unordered_map<std::uint32_t, std::int32_t>
        cells[kDepositKindCount];
    // WHAT THE WORLD WAS BORN WITH, in units per kind — the scarcity
    // baseline (owner, 2026-08-28: "суммарно железа в мире"). DERIVED, never
    // saved: build_deposit_layer is a pure function of terrain + seed, and
    // the load path re-derives the layer before overlaying the save's cells,
    // so the baseline is recomputed for free every boot. Scarcity =
    // 1 − live/virgin; discovery may push live ABOVE virgin, which simply
    // reads as "no scarcity". 64-bit because stone on an all-mountain 1024²
    // map is ~2^30 units and the growth law sums in 64-bit anyway.
    std::int64_t virginUnits[kDepositKindCount] = {};
    // Runtime dirty counter for future consumers; never serialized.
    std::uint32_t revision = 0;

    // ── THE REACH FIELD ──────────────────────────────────────────────────
    // "Is a live vein of this kind within a gatherer's reach of this cell?"
    // — asked per CELL, by the context assembler (macro/cell_facts.h
    // depositsNear, the street crowd's trade gate). It used to be answered by
    // scanning the whole vein list: 69 624 entries in a real world, and the
    // early break only fires for the rare cell that HAS one nearby, so the
    // scan was full for almost every caller. Measured: 161 µs per
    // resolve_context, 4.3 ms of a seam crossing's 6.8 ms — on the sacred
    // seam, for one byte (problems.md §52).
    //
    // A radius question over a sparse point set is a FIELD, never a scan
    // (AGENTS.md's O(N) bound says exactly this; the rule had simply never
    // been pointed at the context assembler). So the answer is stamped where
    // geology changes instead of recomputed where it is asked: a disc of
    // kGathererReach is written into this grid when a vein is born and erased
    // when it runs dry, and the question becomes one array read.
    //
    // COUNTS, not flags, because discs overlap: a cell reached by three veins
    // must survive two of them running dry. u16 because a cell can sit inside
    // the reach of at most (2r+1)² = 1089 veins of one kind, which does not
    // reach 65535 — and overlap that dense is exactly what a quarry field is.
    //
    // DERIVED, never saved (world_fields.h's own distinction): it is a pure
    // function of the vein set, so the load path re-stamps it after overlaying
    // the save's cells and the file learns nothing new.
    std::vector<std::uint16_t> reach[kDepositKindCount];

    // THE question, in O(1). False for a layer with no reach field built —
    // fail-closed: "no vein near" is the answer that grants nothing.
    bool kind_near(DepositKind kind, int x, int y) const {
        const auto& g = reach[std::size_t(kind)];
        if (width <= 0 || height <= 0 || g.empty()) return false;
        return g[wrap_index(x, y)] != 0u;
    }

    // Packs a WRAPPED cell into a flat index. The wrap itself is the one in
    // core/torus.h; it used to be written out twice inline right here.
    std::uint32_t wrap_index(int x, int y) const {
        return std::uint32_t(wrapi(y, height)) * std::uint32_t(width)
             + std::uint32_t(wrapi(x, width));
    }
    // The kind's units standing at a WRAPPED cell; null = no deposit here
    // (a worked-out one is annihilated, so "dry" is not a state a cell has).
    const std::int32_t* remaining_at(DepositKind kind, int x, int y) const {
        if (width <= 0 || height <= 0) return nullptr;
        const auto& m = cells[std::size_t(kind)];
        const auto it = m.find(wrap_index(x, y));
        return it == m.end() ? nullptr : &it->second;
    }
    // Any deposit of any kind here? (worldgen reporting, map tooltips)
    bool any_at(int x, int y) const {
        if (width <= 0 || height <= 0) return false;
        const std::uint32_t i = wrap_index(x, y);
        for (const auto& m : cells)
            if (m.count(i)) return true;
        return false;
    }
};

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

// Re-stamp the whole reach field from the vein set. The two doors above keep
// it in step incrementally, so this is only for the paths that install a vein
// set wholesale — worldgen's own build and the save overlay. Calling it after
// any number of door writes is a no-op in effect: the field is a pure function
// of the cells, which is also how `deposit_reach_test` checks the doors.
void rebuild_deposit_reach(DepositLayer& layer);

// Load path (v37): overwrite the live cells with the save's (the save
// carries them whole). Width/height stay the layer's own — the version gate
// makes a foreign-map save unreachable; stale indices are dropped.
void restore_deposit_cells(DepositLayer& layer, const DepositLayer& loaded);

// (The bespoke W2c iron-discovery trio — discover_iron_vein, iron_depletion,
// iron_discovery_chance_per_day — died 2026-08-29: the LIVE law is the Iron
// row's GrowthDomain::Geology walk in macro_stock.cpp, «железо родится где
// мир оскудел», and the dead path guarded a second copy of it.)

// The lump a fresh vein opens with (kIronBase) — the Iron row's growth
// number, living at the deposit table's own door.
int iron_vein_lump();
int silver_vein_lump();

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

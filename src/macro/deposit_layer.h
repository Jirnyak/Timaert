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

#include "macro/map_generator.h"

namespace sm {

enum class DepositKind : std::uint8_t { Clay = 0, Iron = 1, Stone = 2 };
inline constexpr int kDepositKindCount = 3;

// ── THE deposit-kind registry (CANON S16) ────────────────────────────────
// One row per kind: the commodity it yields into the ONE dictionary, and how
// a settlement site prices a vein of it (settlement_score.cpp deposit_term —
// iron is the prize, stone and clay are common wealth). Two switch-shaped
// dictionaries carried these columns until 2026-08-29.
struct DepositDef {
    DepositKind kind;         // MUST equal the row's index (guard below)
    const char* commodityId;
    int         siteWorth;    // settlement-score base of a vein in reach
};
inline constexpr DepositDef kDepositDefs[kDepositKindCount] = {
    {DepositKind::Clay,  "clay",   8},
    {DepositKind::Iron,  "iron",  16},
    {DepositKind::Stone, "stone",  8},
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

} // namespace sm

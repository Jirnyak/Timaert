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
// An exhausted deposit KEEPS its cell with remaining == 0: "the vein ran
// dry" is a visible fact of the world (and the input of the iron-discovery
// rule), not a silent disappearance.
//
// Grain is deliberately NOT here (the Wheat row prices fertility), nor wood
// (the Trees row): each commodity lives through the carrier that already
// owns its kind of renewal.
#pragma once
#include "core/torus.h"
#include <cstdint>
#include <unordered_map>

#include "macro/map_generator.h"

namespace sm {

enum class DepositKind : std::uint8_t { Clay = 0, Iron = 1, Stone = 2 };
inline constexpr int kDepositKindCount = 3;

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

// ── Iron discovery (owner's rule, W2c) ───────────────────────────────────
// As the world's iron runs OUT, the chance of striking a new vein rises:
// chance/day = depletion × 1/8 (a fully mined-out world prospects at
// 12.5 %/day; an untouched world never strikes anything). The vein opens in
// mountain context without new plumbing: a random STONE cell — mountain by
// construction — turns out to ALSO hold iron; the quarry stays.

// How much of the world's iron has been mined away, in [0, 1]: one minus
// live units over virginUnits — the born-with baseline the layer derives at
// build time (annihilated veins need no memorial to be missed).
float iron_depletion(const DepositLayer& layer);

inline float iron_discovery_chance_per_day(float depletion) {
    return depletion * (1.0f / 8.0f);
}

// Strike a new vein: an rng-chosen Stone cell that does not yet hold iron
// gains an Iron cell at kIronBase (through the genesis door). Returns false
// when no such stone cell exists.
bool discover_iron_vein(DepositLayer& layer, std::uint32_t roll);

// The lump a fresh vein opens with (kIronBase) — the Iron row's growth
// number, living at the deposit table's own door.
int iron_vein_lump();

} // namespace sm

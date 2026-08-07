// The world's mineral deposits — cells of the MAP, not lists on landmarks
// (owner's ruling, W2): clay by the rivers, stone and iron in the mountains.
// Agents farm the nearest deposit to their home exactly as woodcutters farm
// the nearest forest.
//
// Same discipline as macro/tree_layer.h, the proven template:
//   · the LAYER is derived — a pure function of terrain + worldSeed, rebuilt
//     every boot, never serialized;
//   · what play changes (a vein drained by miners) persists as SPARSE
//     overrides in GameState (v26), re-applied on load;
//   · ONE mutation door (set_deposit_remaining) keeps layer and overrides in
//     lockstep — never poke `cells` directly.
//
// An exhausted deposit KEEPS its cell with remaining == 0: "the vein ran
// dry" is a visible fact of the world (and the hook for the iron-discovery
// rule), not a silent disappearance.
//
// Grain is deliberately NOT here (it is the FT_Field feature), nor wood (the
// tree layer): each raw commodity lives in the world through the carrier
// that already owns its kind of renewal.
#pragma once
#include <cstdint>
#include <unordered_map>

#include "macro/map_generator.h"

namespace sm {

enum class DepositKind : std::uint8_t { Clay = 0, Iron = 1, Stone = 2 };

// The commodity id each kind yields — the ONE dictionary's noun.
const char* deposit_commodity_id(DepositKind kind);

struct DepositCell {
    DepositKind  kind;
    std::int32_t remaining;
};

// Sparse per-cell mutations: cell index (y*width+x) → packed
// {kind, remaining}. The value carries the KIND because play can now CHANGE
// it — a discovered vein (W2c) turns a stone quarry into an iron cell, and
// that must survive a load against the virgin derivation. GameState stores
// this map raw (like treeOverrides) to dodge include cycles.
using DepositOverrides = std::unordered_map<std::uint32_t, std::uint64_t>;

inline std::uint64_t pack_deposit_override(DepositKind kind,
                                           std::int32_t remaining) {
    const std::uint32_t r = remaining < 0 ? 0u : std::uint32_t(remaining);
    return (std::uint64_t(std::uint8_t(kind)) << 32) | std::uint64_t(r);
}
inline DepositKind override_kind(std::uint64_t v) {
    return DepositKind(std::uint8_t(v >> 32));
}
inline std::int32_t override_remaining(std::uint64_t v) {
    return std::int32_t(std::uint32_t(v));
}

struct DepositLayer {
    int width = 0;
    int height = 0;
    std::unordered_map<std::uint32_t, DepositCell> cells;
    // Runtime dirty counter for future consumers; never serialized.
    std::uint32_t revision = 0;

    const DepositCell* at(int x, int y) const {
        if (width <= 0 || height <= 0) return nullptr;
        const std::uint32_t xi = std::uint32_t(((x % width) + width) % width);
        const std::uint32_t yi = std::uint32_t(((y % height) + height) % height);
        const auto it = cells.find(yi * std::uint32_t(width) + xi);
        return it == cells.end() ? nullptr : &it->second;
    }
};

// Derive the deposit sites from terrain + seed. Deterministic; density and
// base amounts are the po2 constants in deposit_layer.cpp (clay 1/64 of
// river-adjacent land, stone 1/64 of mountains quasi-infinite, iron 1/256 of
// mountains finite).
DepositLayer build_deposit_layer(const TerrainData& terrain,
                                 std::uint32_t seed, float seaLevel);

// THE mutation door: clamps at zero (a dry vein stays a visible cell),
// records the override, bumps the revision. A cell that was never a deposit
// is refused — mining cannot invent geology (discovery is its own rule).
bool set_deposit_remaining(DepositLayer& layer, DepositOverrides& overrides,
                           int x, int y, std::int32_t remaining);

// Load path: re-apply persisted mutations onto the freshly derived layer.
// An override on a derived cell sets its kind and remaining; an override on
// a cell the derivation does not name CREATES it — that is how a discovered
// vein survives a load.
void apply_deposit_overrides(DepositLayer& layer,
                             const DepositOverrides& overrides);

// ── Iron discovery (owner's rule, W2c) ───────────────────────────────────
// As the world's iron runs OUT, the chance of striking a new vein rises:
// chance/day = depletion × 1/8 (a fully mined-out world prospects at
// 12.5 %/day; an untouched world never strikes anything). The vein opens in
// mountain context without new plumbing: a random STONE cell — mountain by
// construction — turns out to hold iron.

// How much of the world's iron has been mined away, in [0, 1]. The virgin
// amount is ironCells × kIronBase — dry cells keep their cells, so the
// baseline needs no stored state.
float iron_depletion(const DepositLayer& layer);

inline float iron_discovery_chance_per_day(float depletion) {
    return depletion * (1.0f / 8.0f);
}

// Strike a new vein: converts an rng-chosen Stone cell into Iron at
// kIronBase and records the override. Returns false when the world has no
// stone cell to strike.
bool discover_iron_vein(DepositLayer& layer, DepositOverrides& overrides,
                        std::uint32_t roll);

} // namespace sm

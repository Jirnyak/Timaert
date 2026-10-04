// THE FOREST, AS A TABLE — the numbers a forest IS, with no world attached.
//
// A forest is a NUMBER: how many trees a macro cell carries (macro/tree_layer.h
// owns the living grid of them). That grid is STATE and stays in macro/. What
// lives here is the TYPE half of the same subject — the ceiling, the class
// line, the ambient a biome carries, and the formula worldgen derives a virgin
// cell from. None of it knows a world: hand it a count and a biome and it
// answers, which is exactly the границa §11 draws between a catalogue row and
// an instance's numbers.
//
// WHY THE SPLIT EXISTS AT ALL, in numbers rather than in principle. Thirteen
// files under src/sub/ included macro/tree_layer.h, and a census of what they
// actually took found NINE taking nothing at all and three taking only the
// constants below — one single file (engine.cpp) wanted the grid. So the
// subworld was reaching across the two-worlds border for arithmetic that has
// no world in it, and the law's own remedy applies (§11): a table both sides
// may read is not a channel, it is a shared constant, and it belongs in a
// neutral layer where «legal read of a table» stops being indistinguishable
// from «illegal read of macro state».
#pragma once

#include "tables/biomes.h"
#include "tables/prop_profiles.h"

#include <cstdint>
#include <iterator>

namespace sm {

// 2^14 — the densest forest cell (a massif cell with 8 massif neighbours).
constexpr int kMaxTreesPerCell = 16384;

// Half the golden max (2^13): the forest-CLASS threshold — THE one binary
// "is forest" line. A cell at or above it behaves as forest everywhere:
// subworld Forest mode, the forest fauna table, the map tooltip, and the
// map sprite (macro.frag forestSpriteAt draws the full crisp crown at
// u_treeMap >= 0.5 and nothing below — pixel style, no fades). Massif
// interiors (frac ≥ 5/9) qualify; edges and biome ambience do not.
constexpr int kForestClassTreeCount = 8192;
inline bool is_forest_cell(int treeCount) {
    return treeCount >= kForestClassTreeCount;
}

// HOW MUCH OF A PLACE IS UNDER CANOPY — the count against its own ceiling,
// and the ONE spelling of that division. It was written out by hand wherever
// a continuous forest was wanted (the map sprite's R8 encode, the march cost,
// the danger boost, the night canopy), and a division repeated at four sites
// is four chances to pick a different denominator the day the ceiling moves.
// Clamped because a count arrives from a save, a field write or a resolver
// that may answer −1 for "unknown".
inline constexpr float forest_fraction01(int treeCount) {
    if (treeCount <= 0) return 0.0f;
    if (treeCount >= kMaxTreesPerCell) return 1.0f;
    return float(treeCount) / float(kMaxTreesPerCell);
}
// The same fraction as a BYTE, which is how every consumer that ships it to
// the GPU wants it (an R8 image, a column riding a struct's tail padding).
// Rounded, not truncated: 255 has to be reachable by a full cell.
inline constexpr std::uint8_t forest_fraction_byte(int treeCount) {
    return std::uint8_t(forest_fraction01(treeCount) * 255.0f + 0.5f);
}

// Ambient trees a biome carries OUTSIDE any forest massif — scattered lone
// trees, not woodland. Deliberately far below kForestClassTreeCount so biome
// ambience NEVER reads as forest (map sprite, Forest mode, fauna): the map
// shows the organic massifs, the ambience only thickens the subworld ground
// scatter. Relative order preserves biome character (taiga > swamp >
// meadow > … > desert).
// One row per biome, each carrying its own enum as a COLUMN so the guard can
// prove the order. A bare parallel array is the kNpcPurse scar waiting to
// happen: the day the enum grows, it silently zero-fills the tail or reads a
// neighbour's number, and nothing fails to compile.
struct BiomeTreeCountRow { Biome biome; std::uint16_t count; };
inline constexpr BiomeTreeCountRow kBiomeBaseTreeCount[std::size_t(Mountain) + 1] = {
    {Tundra,    200},
    {Taiga,    1400},
    {Snow,      300},
    {Valley,    800},
    {Meadow,    600},
    {Swamp,    1300},
    {Desert,     40},
    {Steppe,    250},
    {Tropics,  1450},
    {Water,       0},
    {Mountain,  250},
};
static_assert(rows_in_enum_order(kBiomeBaseTreeCount, &BiomeTreeCountRow::biome),
              "kBiomeBaseTreeCount row order must mirror Biome");

// THE ambience of a biome, fail-closed for a biome the table has not met.
inline constexpr int biome_base_tree_count(int b) {
    return (b >= 0 && b < int(std::size(kBiomeBaseTreeCount)))
        ? int(kBiomeBaseTreeCount[std::size_t(b)].count) : 0;
}

// The derivation formula: biome ambient base + the massif term,
// 16384 × (massif cells in the 3×3 / 9), clamped to the golden max.
// `forestFrac9` ∈ [0,1]. Water carries nothing.
inline std::uint16_t derived_tree_count(Biome biome, float forestFrac9) {
    if (biome == Biome::Water) return 0;
    const int b = int(biome);
    const int base = biome_base_tree_count(b);
    int c = base + int(float(kMaxTreesPerCell) * forestFrac9 + 0.5f);
    if (c > kMaxTreesPerCell) c = kMaxTreesPerCell;
    if (c < 0) c = 0;
    return std::uint16_t(c);
}

// WHICH TREE GROWS HERE — one row per biome, and the answer is a PROFILE,
// never a branch in the renderer. This is the whole of «деревья от биома»
// (owner, 2026-10-04: «нужно деревья таблицу от биомов субмодули иначе только
// сосны … просто от биома клетки контекст и генерятся эти деревья системно»):
// the cell hands over its biome, the table hands back a shape, and a module
// that wants its own wood adds a row rather than a case.
//
// Water carries a profile too, and that is not an oversight: the column must
// answer for every biome or the lookup needs a guard, and a water cell has no
// trees to put it on anyway (`derived_tree_count` returns 0 there).
struct BiomeTreeProfileRow { Biome biome; PropProfile profile; };
inline constexpr BiomeTreeProfileRow kBiomeTreeProfile[std::size_t(Mountain) + 1] = {
    {Tundra,   PropProfile::TreeScrub},      // wind-cut, low
    {Taiga,    PropProfile::TreeConifer},    // the spruce ladder
    {Snow,     PropProfile::TreeConifer},
    {Valley,   PropProfile::TreeBroadleaf},  // round heads along the water
    {Meadow,   PropProfile::TreeBroadleaf},
    {Swamp,    PropProfile::TreePalm},       // bare stems, crown on top
    {Desert,   PropProfile::TreeScrub},
    {Steppe,   PropProfile::TreeScrub},      // sparse, hard-leaved
    {Tropics,  PropProfile::TreePalm},
    {Water,    PropProfile::TreeScrub},      // no trees stand here at all
    {Mountain, PropProfile::TreeConifer},
};
static_assert(rows_in_enum_order(kBiomeTreeProfile,
                                 &BiomeTreeProfileRow::biome),
              "kBiomeTreeProfile row order must mirror Biome");

inline constexpr PropProfile biome_tree_profile(int b) {
    return (b >= 0 && b < int(std::size(kBiomeTreeProfile)))
        ? kBiomeTreeProfile[std::size_t(b)].profile
        : PropProfile::TreeConifer;
}

} // namespace sm

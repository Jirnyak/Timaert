// Subworld map data — tile types, structures, cell context.
// Mirrors subworld/map-data.ts (compact).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm::sub {

constexpr int kCellSize = 1024;          // tiles per macro cell
constexpr int kFullSize = kCellSize * 3; // 3×3 grid

// Tile constants for the subworld grid.
enum Tile : std::uint8_t {
    TILE_EMPTY = 0, TILE_GRASS, TILE_FIELD, TILE_TREE_DECOR,
    TILE_ROAD, TILE_HOUSE, TILE_WALL, TILE_WATER, TILE_SHORE,
    TILE_SQUARE, TILE_ROCK,
    TILE_COUNT,
};

// ── Ground movement table ──────────────────────────────────────────────────
// How fast a body crosses each ground type, as a multiplier on its own speed.
// ONE data row per tile id: this is the single source of truth for terrain cost,
// read by the mass-battle steering pass (sub/battle.h) through a plain pointer —
// no branch chain, no hardcoded "is it water" test anywhere in the engine.
// Adding a ground type = adding an enum value and a row here; the static_assert
// below refuses to build if the two ever drift apart.
//
// Hard blocking is NOT this table's job: solid structures (walls, houses)
// physically stop and carry bodies through sub/collide.h, which indexes the
// oriented Structure volumes below. The HOUSE/WALL rows here only price the
// unpainted verge tiles hugging a footprint — the solid itself refuses entry.
inline constexpr float kTileMovementSpeed[TILE_COUNT] = {
    1.00f,   // TILE_EMPTY      — untouched ground, treat as open
    1.00f,   // TILE_GRASS      — reference surface
    0.95f    // TILE_FIELD      — crops underfoot
    ,
    0.80f,   // TILE_TREE_DECOR — undergrowth
    1.15f,   // TILE_ROAD       — the reason roads exist
    0.35f,   // TILE_HOUSE      — squeezing through a building footprint
    0.30f,   // TILE_WALL       — same, worse
    0.45f,   // TILE_WATER      — wading (rivers are honest water cells)
    0.75f,   // TILE_SHORE      — wet sand / shallows
    1.10f,   // TILE_SQUARE     — paved settlement square
    0.65f,   // TILE_ROCK       — scree
};
static_assert(sizeof(kTileMovementSpeed) / sizeof(float) == std::size_t(TILE_COUNT),
              "every Tile id needs exactly one movement row");

enum class SubworldMode : std::uint8_t {
    Open, City, Village, Forest, Mountain, Swamp, Ruin, Water, Grassland, Road, Spire,
    Field,
};

enum class CellLandmarkKind : std::uint8_t {
    None = 0, City, Village, Ruin, Spire,
};

// CellContext — what the macroworld knows about a single cell.
struct CellContext {
    int   cx, cy;
    float macroHeight;   // 0..1
    float macroTemperature = 0.5f; // 0..1, used for TS tree species bands
    Biome biome;
    FeatureType feature;
    int   landmarkSettlementId; // -1 = none
    int   landmarkSize;         // population / strength
    CellLandmarkKind landmarkKind = CellLandmarkKind::None;
    // Owning kingdom of the landmark on this cell (index into Politik::kingdoms;
    // -1 = none / unowned). The subworld does not know what a kingdom IS — it
    // carries the index so the engine can resolve the citizens' faction through
    // the one macro resolver (faction_index_for_kingdom), instead of every
    // settlement in the world fielding imperial guards.
    int   kingdomIdx = -1;
    std::uint32_t seed;
    // Macro TreeLayer count for this cell (0..16384); -1 = unknown — the
    // generator re-derives it from biome/features (tests, bare resolvers).
    int treeCount = -1;
    // Furrow orientation of this cell's ploughed-field material, resolved at
    // macro resolve from the WRAPPED cell coords (field_furrows_vertical,
    // sub/material.h — the twin of the map's macro.frag furrow hash). Carried
    // like macroTemperature: generation ignores it, the renderer's material
    // grid consumes it, so what the map paints is what lies underfoot.
    bool fieldFurrowsVert = false;
};

// The effective landmark of a cell for terrain purposes. A macro settlement
// projects as a City cell even when landmarkKind is None (mirrors
// resolve_mode's `landmarkSettlementId >= 0` branch) — one helper so terrain
// flattening and mode resolution can never disagree.
inline CellLandmarkKind effective_landmark(const CellContext& ctx) {
    if (ctx.landmarkKind != CellLandmarkKind::None) return ctx.landmarkKind;
    if (ctx.landmarkSettlementId >= 0) return CellLandmarkKind::City;
    return CellLandmarkKind::None;
}

struct Structure {
    enum Kind : std::uint8_t { Tree = 0, Rock, House, Wall, Bridge } kind;
    // Footprint silhouette. Box is the default; Cylinder renders (and collides)
    // as a round prism — wall towers, gate jambs, the spire. One byte, not a
    // new Kind: shape is orthogonal to what the thing IS.
    enum Shape : std::uint8_t { Box = 0, Cylinder = 1 };
    float x, y;
    float radius;        // XZ half-extent (bounding); billboard scale for Tree
    float height;        // metres; NEGATIVE = decayed/abandoned (map_factory.h)
    // ── Universal oriented-box extension (all zero ⇒ legacy square box). ──
    float yaw   = 0.0f;  // rotation about vertical, radians, tile-space CCW
    float hx    = 0.0f;  // half-extent along local X, tiles (0 ⇒ radius)
    float hy    = 0.0f;  // half-extent along local Y, tiles (0 ⇒ radius)
    float zBase = 0.0f;  // bottom lift above the terrain seat, metres — gate
                         // lintels / decks: bodies pass beneath, stand on top
    Shape shape = Box;
};

// ── Structure geometry — the ONE contract shared by the 3D renderer and the
// collision index (sub/collide.h). Both derive a structure's oriented footprint
// and vertical span from these helpers, so what you SEE is exactly what blocks
// and carries you. ──────────────────────────────────────────────────────────

inline float structure_half_x(const Structure& s) {
    return s.hx > 0.0f ? s.hx : s.radius;
}
inline float structure_half_y(const Structure& s) {
    return s.hy > 0.0f ? s.hy : s.radius;
}

// Per-kind floors keeping degenerate records visible/solid. Formerly renderer
// literals; shared here so collision can never disagree with the pixels.
inline constexpr float structure_min_half_xy(Structure::Kind k) {
    return k == Structure::Wall ? 1.2f : 1.6f;
}
inline constexpr float structure_min_height(Structure::Kind k) {
    return k == Structure::Wall ? 4.0f : 3.5f;
}

// What a broken / felled structure drops, as a key into the ONE loot registry
// (`roll_loot_profile`, macro/items.h) — the same resolver a kill goes through.
// Empty id = this kind drops nothing (yet).
//
// This is the third per-kind datum after the size floors above, and like them
// it is written as a fork because there are two kinds worth forking on. When
// environment props land (rocks, bushes, cairns) these collapse into ONE row
// table — form, size band, solidity, loot, per-biome rate — and the forks go
// away. Until then: data lives here, next to its siblings, not in the caller.
inline constexpr const char* structure_loot_id(Structure::Kind k) {
    return k == Structure::Tree ? "tree" : "";
}

// Visible/solid height in metres. A decayed record (negative height — the
// map_factory.h sign-bit overlay) collapses to the per-kind stub floor, which
// is exactly how the renderer has always drawn it. The floor exists to keep
// degenerate LEGACY grounded records visible; a zBase-lifted body (gate
// lintel, deck) states its thickness explicitly and is never inflated —
// otherwise a 3 m lintel would grow a phantom metre that blocks whoever
// stands on it.
inline float structure_visible_height(const Structure& s) {
    const float minH = structure_min_height(s.kind);
    if (s.height < 0.0f) return minH;
    if (s.zBase > 0.0f) return s.height;
    return std::max(s.height, minH);
}

// Vertical solid span in absolute metres given the terrain sample at the
// structure's centre (`seatM`). Grounded bodies rest on the seat; zBase lifts
// the bottom clear of it (gate lintels, decks).
inline void structure_solid_span(const Structure& s, float seatM,
                                 float& z0, float& z1) {
    z0 = seatM + s.zBase;
    z1 = z0 + structure_visible_height(s);
}

struct SubworldMapData {
    std::vector<std::uint8_t> tiles;     // FullSize × FullSize
    std::vector<std::uint8_t> trav;      // 0 = wall, 1 = walkable
    std::vector<float>        heightmap; // FullSize × FullSize
    std::vector<Structure>    structures;
    float waterLevel = 0.4f;
};

inline std::size_t tile_index(int x, int y) { return std::size_t(y) * kFullSize + x; }

// ── Geometry helpers (port of subworld/map-data.ts) ──────────────────

// Compass direction index — clockwise from north. Matches the 8 neighbour
// slots in NeighborGrid (N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7).
enum class Dir : std::uint8_t {
    N = 0, NE = 1, E = 2, SE = 3,
    S = 4, SW = 5, W = 6, NW = 7,
};

inline constexpr int kDirOffsets[8][2] = {
    { 0, -1}, { 1, -1}, { 1,  0}, { 1,  1},
    { 0,  1}, {-1,  1}, {-1,  0}, {-1, -1},
};

} // namespace sm::sub

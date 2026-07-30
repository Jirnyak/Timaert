// Subworld map data — tile types, structures, cell context.
// Mirrors subworld/map-data.ts (compact).
#pragma once
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
// Values are deliberately conservative: nothing is a hard wall, because the
// subworld has no body-vs-structure collision yet, and silently pinning a
// charging army against an un-navigable tile would look worse than wading.
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
    std::uint32_t seed;
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
    float x, y;
    float radius;
    float height;
};

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

inline Dir opposite_dir(Dir d) {
    return Dir((std::uint8_t(d) + 4u) & 7u);
}

inline int dir_from_offset(int dx, int dy) {
    for (int d = 0; d < 8; ++d) {
        if (kDirOffsets[d][0] == dx && kDirOffsets[d][1] == dy) return d;
    }
    return -1;
}

// Shortest angular distance on the unit circle [0, 2π).
inline float angular_distance(float a, float b) {
    constexpr float kTwoPi = 6.28318530717958647692f;
    float d = std::fmod(std::fabs(a - b), kTwoPi);
    if (d > kTwoPi * 0.5f) d = kTwoPi - d;
    return d;
}

// Spiral search for a specific tile id around (tx, ty), bounded by
// `searchRadius`. Returns true and fills outX/outY (cell-centre +0.5 offset
// like the TS helper) when found, else false.
inline bool find_tile_near(const std::uint8_t* grid, int width, int height,
                           int tx, int ty, std::uint8_t tile, int searchRadius,
                           float& outX, float& outY) {
    for (int r = 0; r < searchRadius; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                int gx = tx + dx, gy = ty + dy;
                if (gx < 0 || gy < 0 || gx >= width || gy >= height) continue;
                if (grid[gy * width + gx] == tile) {
                    outX = float(gx) + 0.5f;
                    outY = float(gy) + 0.5f;
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace sm::sub

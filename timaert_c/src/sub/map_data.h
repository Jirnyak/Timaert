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
};

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

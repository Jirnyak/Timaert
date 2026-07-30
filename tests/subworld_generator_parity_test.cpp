#include "sub/gens/dispatch.h"
#include "macro/tree_layer.h"
#include "sub/base_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct Glade {
    int x;
    int y;
    int r;
};

float noise01(int x, int y, std::uint32_t seed) {
    return float(sm::hash3(std::uint32_t(x), std::uint32_t(y), seed))
        / 4294967295.0f;
}

float smooth_noise01(float x, float y, std::uint32_t seed) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = noise01(ix,     iy,     seed);
    const float n10 = noise01(ix + 1, iy,     seed);
    const float n01 = noise01(ix,     iy + 1, seed);
    const float n11 = noise01(ix + 1, iy + 1, seed);
    return n00 * (1.0f - sx) * (1.0f - sy)
         + n10 * sx * (1.0f - sy)
         + n01 * (1.0f - sx) * sy
         + n11 * sx * sy;
}

std::vector<Glade> expected_forest_glades(const sm::sub::CellContext& ctx) {
    constexpr int kGladeStep = 48;
    constexpr float kGladeThreshold = 0.72f;
    constexpr int kGladeRadiusMin = 6;
    constexpr int kGladeRadiusMax = 16;

    const int gox = ctx.cx * sm::sub::kCellSize;
    const int goy = ctx.cy * sm::sub::kCellSize;
    const int startGX = gox + ((kGladeStep - (gox % kGladeStep)) % kGladeStep);
    const int startGY = goy + ((kGladeStep - (goy % kGladeStep)) % kGladeStep);
    const int endGX = gox + sm::sub::kCellSize;
    const int endGY = goy + sm::sub::kCellSize;

    std::vector<Glade> out;
    out.reserve(64);
    for (int gy = startGY; gy < endGY; gy += kGladeStep) {
        for (int gx = startGX; gx < endGX; gx += kGladeStep) {
            const float n = smooth_noise01(float(gx) * 0.007f + 777.0f,
                                           float(gy) * 0.007f + 888.0f,
                                           ctx.seed);
            if (n < kGladeThreshold) continue;
            const float rn = noise01(gx * 5 + 99999, gy * 7 + 88888, ctx.seed);
            const int radius = kGladeRadiusMin
                + int(std::floor(rn * float(kGladeRadiusMax - kGladeRadiusMin + 1)));
            out.push_back({gx - gox, gy - goy, radius});
        }
    }
    return out;
}

int fail(const char* msg) {
    std::cerr << msg << "\n";
    return 1;
}

bool heightmaps_nearly_equal(const std::vector<float>& a,
                             const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > 0.00001f) return false;
    }
    return true;
}

void fill_flat_neighbors(float nbH[9], sm::Biome nbB[9], std::uint8_t nbF[9],
                         sm::Biome biome, sm::FeatureType feature) {
    for (int i = 0; i < 9; ++i) {
        nbH[i] = 0.62f;
        nbB[i] = biome;
        nbF[i] = std::uint8_t(sm::FT_None);
    }
    nbF[4] = std::uint8_t(feature);
}

int wall_structures_on_protected_tiles(const sm::sub::SubworldMapData& map) {
    int blocked = 0;
    for (const sm::sub::Structure& s : map.structures) {
        if (s.kind != sm::sub::Structure::Wall) continue;
        // Gate lintels (zBase > 0) legitimately BRIDGE the road: their solid
        // span starts above head height, so a record over a road tile is the
        // design, not a blocked gate.
        if (s.zBase > 0.0f) continue;
        const int x = std::clamp(int(std::floor(s.x)), 0, sm::sub::kCellSize - 1);
        const int y = std::clamp(int(std::floor(s.y)), 0, sm::sub::kCellSize - 1);
        const std::uint8_t tile = map.tiles[std::size_t(y) * sm::sub::kCellSize + x];
        if (tile == sm::sub::TILE_ROAD || tile == sm::sub::TILE_SQUARE
         || tile == sm::sub::TILE_HOUSE || tile == sm::sub::TILE_FIELD) {
            ++blocked;
        }
    }
    return blocked;
}

std::uint32_t expected_edge_seed(const sm::sub::CellContext& ctx, int dx, int dy) {
    const std::int64_t a = std::int64_t(ctx.cx) * 100003 + std::int64_t(ctx.cy);
    const std::int64_t b = std::int64_t(ctx.cx + dx) * 100003
                         + std::int64_t(ctx.cy + dy);
    const std::int64_t lo = std::min(a, b);
    const std::int64_t hi = std::max(a, b);
    const std::uint32_t seed =
        std::uint32_t(lo * 374761393ll) ^ std::uint32_t(hi * 1274126177ll);
    return seed == 0u ? 1u : seed;
}

void expected_edge_anchor(const sm::sub::CellContext& ctx, int dx, int dy,
                          int& ox, int& oy) {
    sm::Rng r(expected_edge_seed(ctx, dx, dy));
    const int edgePos = int(std::floor(float(sm::sub::kCellSize)
        * (0.35f + r.next_f01() * 0.3f)));
    if (dx == 0) {
        ox = std::clamp(edgePos, 1, sm::sub::kCellSize - 2);
        oy = dy < 0 ? 1 : sm::sub::kCellSize - 2;
    } else if (dy == 0) {
        ox = dx > 0 ? sm::sub::kCellSize - 2 : 1;
        oy = std::clamp(edgePos, 1, sm::sub::kCellSize - 2);
    } else {
        ox = dx > 0 ? sm::sub::kCellSize - 2 : 1;
        oy = dy > 0 ? sm::sub::kCellSize - 2 : 1;
    }
}

bool anchor_is_road(const sm::sub::SubworldMapData& map,
                    const sm::sub::CellContext& ctx,
                    int dx, int dy) {
    int ax = 0;
    int ay = 0;
    expected_edge_anchor(ctx, dx, dy, ax, ay);
    return map.tiles[std::size_t(ay) * sm::sub::kCellSize + ax]
        == sm::sub::TILE_ROAD;
}

} // namespace

int main() {
    using namespace sm;
    using namespace sm::sub;

    CellContext ctx{};
    ctx.cx = 4;
    ctx.cy = 7;
    ctx.macroHeight = 0.62f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    // Forest is a COUNT class now (macro/tree_layer.h): a massif-interior
    // count drives Forest mode, no feature byte involved.
    ctx.treeCount = kMaxTreesPerCell;
    ctx.landmarkSettlementId = -1;
    ctx.landmarkSize = 0;
    ctx.seed = 0x12345678u;

    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    int nbForest[9];
    for (int i = 0; i < 9; ++i) nbForest[i] = kMaxTreesPerCell;

    SubworldMapData out{};
    dispatch_generate(ctx, nbH, nbB, nbF, out, nullptr, nbForest);

    if (resolve_mode(ctx) != SubworldMode::Forest) {
        return fail("forest-class tree count did not resolve to Forest mode");
    }
    if (out.tiles.size() != std::size_t(kCellSize) * kCellSize) {
        return fail("forest generator produced wrong tile count");
    }
    if (out.trav.size() != out.tiles.size() || out.heightmap.size() != out.tiles.size()) {
        return fail("forest generator produced mismatched map buffers");
    }

    const std::vector<Glade> glades = expected_forest_glades(ctx);
    if (glades.size() < 8) {
        return fail("forest glade constants produced too few test glades");
    }

    int treeStructures = 0;
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::Tree) ++treeStructures;
    }
    if (treeStructures <= 0) {
        return fail("forest generator produced no tree structures");
    }

    const int center = kCellSize / 2;

    for (const Glade& g : glades) {
        const int r2 = g.r * g.r;
        for (int dy = -g.r; dy <= g.r; ++dy) {
            for (int dx = -g.r; dx <= g.r; ++dx) {
                if (dx * dx + dy * dy > r2) continue;
                const int px = g.x + dx;
                const int py = g.y + dy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                if (out.tiles[std::size_t(py) * kCellSize + px] == TILE_TREE_DECOR) {
                    return fail("forest glade still contains tree decor tiles");
                }
            }
        }
        for (const Structure& s : out.structures) {
            if (s.kind != Structure::Tree) continue;
            const int tx = int(std::floor(s.x));
            const int ty = int(std::floor(s.y));
            const int dx = tx - g.x;
            const int dy = ty - g.y;
            if (dx * dx + dy * dy <= r2) {
                return fail("forest glade still contains tree structures");
            }
        }
    }

    CellContext grassTrail{};
    grassTrail.cx = 8;
    grassTrail.cy = -5;
    grassTrail.macroHeight = 0.62f;
    grassTrail.biome = Meadow;
    grassTrail.feature = FT_None;
    grassTrail.landmarkSettlementId = -1;
    grassTrail.landmarkSize = 0;
    grassTrail.landmarkKind = CellLandmarkKind::None;
    grassTrail.seed = 0x51525354u;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_DirtRoad);
    SubworldMapData grassTrailOut{};
    dispatch_generate(grassTrail, nbH, nbB, nbF, grassTrailOut);
    if (resolve_mode(grassTrail) != SubworldMode::Grassland
        || anchor_is_road(grassTrailOut, grassTrail, -1, 0)
        || anchor_is_road(grassTrailOut, grassTrail, 1, 0)
        || grassTrailOut.tiles[std::size_t(center) * kCellSize + center]
            == TILE_ROAD) {
        return fail("grassland wilderness cell must NOT carve roads "
                    "(TS needsRoadStitch requires a road-like centre)");
    }

    CellContext forestTrail = ctx;
    forestTrail.cx = -8;
    forestTrail.cy = 6;
    forestTrail.seed = 0x61626364u;
    forestTrail.treeCount = kMaxTreesPerCell;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[1] = std::uint8_t(FT_Road);
    nbF[7] = std::uint8_t(FT_DirtRoad);
    SubworldMapData forestTrailOut{};
    dispatch_generate(forestTrail, nbH, nbB, nbF, forestTrailOut, nullptr,
                      nbForest);
    if (anchor_is_road(forestTrailOut, forestTrail, 0, -1)
        || anchor_is_road(forestTrailOut, forestTrail, 0, 1)
        || forestTrailOut.tiles[std::size_t(center) * kCellSize + center]
            == TILE_ROAD) {
        return fail("forest wilderness cell must NOT carve roads "
                    "(TS needsRoadStitch requires a road-like centre)");
    }

    CellContext swampTrail{};
    swampTrail.cx = 6;
    swampTrail.cy = 9;
    swampTrail.macroHeight = 0.57f;
    swampTrail.biome = Swamp;
    swampTrail.feature = FT_None;
    swampTrail.landmarkSettlementId = -1;
    swampTrail.landmarkSize = 0;
    swampTrail.landmarkKind = CellLandmarkKind::None;
    swampTrail.seed = 0x71727374u;
    fill_flat_neighbors(nbH, nbB, nbF, Swamp, FT_None);
    nbF[5] = std::uint8_t(FT_Road);
    SubworldMapData swampTrailOut{};
    dispatch_generate(swampTrail, nbH, nbB, nbF, swampTrailOut);
    if (resolve_mode(swampTrail) != SubworldMode::Swamp
        || anchor_is_road(swampTrailOut, swampTrail, 1, 0)
        || swampTrailOut.tiles[std::size_t(center) * kCellSize + center]
            == TILE_ROAD) {
        return fail("swamp wilderness cell must NOT carve roads "
                    "(TS needsRoadStitch requires a road-like centre)");
    }

    CellContext mountainTrail{};
    mountainTrail.cx = -9;
    mountainTrail.cy = -7;
    mountainTrail.macroHeight = 0.84f;
    mountainTrail.biome = Mountain;   // elevation-classified biome, not a feature
    mountainTrail.feature = FT_None;
    mountainTrail.landmarkSettlementId = -1;
    mountainTrail.landmarkSize = 0;
    mountainTrail.landmarkKind = CellLandmarkKind::None;
    mountainTrail.seed = 0x81828384u;
    fill_flat_neighbors(nbH, nbB, nbF, Mountain, FT_None);
    nbF[1] = std::uint8_t(FT_Road);
    SubworldMapData mountainTrailOut{};
    dispatch_generate(mountainTrail, nbH, nbB, nbF, mountainTrailOut);
    if (resolve_mode(mountainTrail) != SubworldMode::Mountain
        || anchor_is_road(mountainTrailOut, mountainTrail, 0, -1)
        || mountainTrailOut.tiles[std::size_t(center) * kCellSize + center]
            == TILE_ROAD) {
        return fail("mountain wilderness cell must NOT carve roads "
                    "(TS needsRoadStitch requires a road-like centre)");
    }

    CellContext spire{};
    spire.cx = -2;
    spire.cy = 3;
    spire.macroHeight = 0.62f;
    spire.biome = Meadow;
    spire.feature = FT_None;
    spire.landmarkSettlementId = 77;
    spire.landmarkSize = 0;
    spire.landmarkKind = CellLandmarkKind::Spire;
    spire.seed = 0x2468ACE0u;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData spireOut{};
    dispatch_generate(spire, nbH, nbB, nbF, spireOut);
    if (resolve_mode(spire) != SubworldMode::Spire) {
        return fail("spire landmark did not resolve to Spire mode");
    }

    int towerCount = 0;
    for (const Structure& s : spireOut.structures) {
        if (s.kind == Structure::Wall
            && int(std::floor(s.x)) == center
            && int(std::floor(s.y)) == center
            && std::fabs(s.radius - 7.0f) < 0.001f
            && std::fabs(s.height - 96.0f) < 0.001f
            && s.shape == Structure::Cylinder) {
            ++towerCount;
        }
    }
    if (towerCount != 1) {
        return fail("spire generator did not create one TS-sized round central tower");
    }

    int scorchRock = 0;
    int craterRing = 0;
    int craterBad = 0;
    constexpr int kScorchRadius = 90;
    constexpr int kCraterInner = 18;
    constexpr int kCraterOuter = kCraterInner + 22;
    for (int y = center - kScorchRadius; y <= center + kScorchRadius; ++y) {
        for (int x = center - kScorchRadius; x <= center + kScorchRadius; ++x) {
            const int dx = x - center;
            const int dy = y - center;
            const int d2 = dx * dx + dy * dy;
            const std::uint8_t tile = spireOut.tiles[std::size_t(y) * kCellSize + x];
            if (d2 <= kScorchRadius * kScorchRadius && tile == TILE_ROCK) {
                ++scorchRock;
            }
            if (d2 >= kCraterInner * kCraterInner && d2 <= kCraterOuter * kCraterOuter) {
                ++craterRing;
                if (tile != TILE_ROCK && tile != TILE_SQUARE) {
                    ++craterBad;
                }
            }
        }
    }
    if (scorchRock <= 0 || craterRing <= 0 || craterBad != 0) {
        return fail("spire scorch/crater tile invariants failed");
    }

    for (const Structure& s : spireOut.structures) {
        if (s.kind != Structure::Tree) continue;
        const int tx = int(std::floor(s.x));
        const int ty = int(std::floor(s.y));
        const int dx = tx - center;
        const int dy = ty - center;
        if (dx * dx + dy * dy < kScorchRadius * kScorchRadius) {
            return fail("spire scorch radius still contains tree structures");
        }
    }

    CellContext ruin{};
    ruin.cx = 8;
    ruin.cy = -5;
    ruin.macroHeight = 0.62f;
    ruin.biome = Meadow;
    ruin.feature = FT_None;
    ruin.landmarkSettlementId = 88;
    ruin.landmarkSize = 4;
    ruin.landmarkKind = CellLandmarkKind::Ruin;
    ruin.seed = 0x13579BDFu;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData ruinOut{};
    dispatch_generate(ruin, nbH, nbB, nbF, ruinOut);
    if (resolve_mode(ruin) != SubworldMode::Ruin) {
        return fail("ruin landmark did not resolve to Ruin mode");
    }
    int wallTiles = 0;
    int blockedWallTiles = 0;
    int centerSquares = 0;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (ruinOut.tiles[idx] == TILE_WALL) {
                ++wallTiles;
                if (ruinOut.trav[idx] == 0) ++blockedWallTiles;
            }
            const int dx = x - center;
            const int dy = y - center;
            if (dx * dx + dy * dy <= 64 && ruinOut.tiles[idx] == TILE_SQUARE) {
                ++centerSquares;
            }
        }
    }
    if (wallTiles < 100 || blockedWallTiles != wallTiles || centerSquares <= 0) {
        return fail("ruin wall/square invariants failed");
    }

    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[5] = std::uint8_t(FT_Road);
    SubworldMapData ruinRoadOut{};
    dispatch_generate(ruin, nbH, nbB, nbF, ruinRoadOut);
    int ruinEastX, ruinEastY;
    expected_edge_anchor(ruin, 1, 0, ruinEastX, ruinEastY);
    int ruinRoadTiles = 0;
    for (const std::uint8_t tile : ruinRoadOut.tiles) {
        if (tile == TILE_ROAD) ++ruinRoadTiles;
    }
    if (ruinRoadOut.tiles[std::size_t(ruinEastY) * kCellSize + ruinEastX] != TILE_ROAD
        || ruinRoadTiles <= 0
        || wall_structures_on_protected_tiles(ruinRoadOut) != 0) {
        return fail("ruin generator ignored anchored road gate");
    }

    CellContext city{};
    city.cx = -11;
    city.cy = 6;
    city.macroHeight = 0.64f;
    city.biome = Meadow;
    city.feature = FT_None;
    city.landmarkSettlementId = 101;
    city.landmarkSize = 6000;
    city.landmarkKind = CellLandmarkKind::City;
    city.seed = 0x0BADF00Du;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData cityOut{};
    dispatch_generate(city, nbH, nbB, nbF, cityOut);
    if (resolve_mode(city) != SubworldMode::City) {
        return fail("city landmark did not resolve to City mode");
    }
    int cityHouses = 0;
    int cityWalls = 0;
    int cityFields = 0;
    int citySquares = 0;
    int cityRoads = 0;
    int cityKeeps = 0;
    const int cityBlockedWallRecords = wall_structures_on_protected_tiles(cityOut);
    for (const Structure& s : cityOut.structures) {
        if (s.kind == Structure::House) ++cityHouses;
        if (s.kind == Structure::Wall) ++cityWalls;
        if (s.kind == Structure::House
            && s.height >= 9.5f
            && std::fabs(s.x - float(center)) < 16.0f
            && s.y < float(center)) {
            ++cityKeeps;
        }
    }
    for (const std::uint8_t tile : cityOut.tiles) {
        if (tile == TILE_FIELD) ++cityFields;
        if (tile == TILE_SQUARE) ++citySquares;
        if (tile == TILE_ROAD) ++cityRoads;
    }
    if (cityHouses < 40 || cityWalls < 40 || cityFields <= 0
        || citySquares <= 0 || cityRoads <= 0 || cityKeeps != 1
        || cityBlockedWallRecords != 0) {
        std::cerr << "city counts houses=" << cityHouses
                  << " walls=" << cityWalls
                  << " fields=" << cityFields
                  << " squares=" << citySquares
                  << " roads=" << cityRoads
                  << " keeps=" << cityKeeps
                  << " blockedWallRecords=" << cityBlockedWallRecords << "\n";
        return fail("city generator settlement invariants failed");
    }

    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_DirtRoad);
    SubworldMapData cityAlignedOut{};
    dispatch_generate(city, nbH, nbB, nbF, cityAlignedOut);
    int cityWestX, cityWestY, cityEastX, cityEastY;
    expected_edge_anchor(city, -1, 0, cityWestX, cityWestY);
    expected_edge_anchor(city, 1, 0, cityEastX, cityEastY);
    if (cityAlignedOut.tiles[std::size_t(cityWestY) * kCellSize + cityWestX] != TILE_ROAD
        || cityAlignedOut.tiles[std::size_t(cityEastY) * kCellSize + cityEastX] != TILE_ROAD
        || cityAlignedOut.tiles[std::size_t(0) * kCellSize + center] == TILE_ROAD
        || cityAlignedOut.tiles[std::size_t(kCellSize - 1) * kCellSize + center] == TILE_ROAD
        || wall_structures_on_protected_tiles(cityAlignedOut) != 0) {
        return fail("city generator ignored neighbour road directions");
    }

    CellContext village{};
    village.cx = 13;
    village.cy = -3;
    village.macroHeight = 0.59f;
    village.biome = Meadow;
    village.feature = FT_None;
    village.landmarkSettlementId = 202;
    village.landmarkSize = 120;
    village.landmarkKind = CellLandmarkKind::Village;
    village.seed = 0x00C0FFEEu;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData villageOut{};
    dispatch_generate(village, nbH, nbB, nbF, villageOut);
    if (resolve_mode(village) != SubworldMode::Village) {
        return fail("village landmark did not resolve to Village mode");
    }
    int villageHouses = 0;
    int villageWalls = 0;
    int villageFields = 0;
    int villageSquares = 0;
    int villageRoads = 0;
    const int villageBlockedWallRecords = wall_structures_on_protected_tiles(villageOut);
    for (const Structure& s : villageOut.structures) {
        if (s.kind == Structure::House) ++villageHouses;
        if (s.kind == Structure::Wall) ++villageWalls;
    }
    for (const std::uint8_t tile : villageOut.tiles) {
        if (tile == TILE_FIELD) ++villageFields;
        if (tile == TILE_SQUARE) ++villageSquares;
        if (tile == TILE_ROAD) ++villageRoads;
    }
    if (villageHouses < 10 || villageWalls <= 0 || villageFields <= 0
        || villageSquares <= 0 || villageRoads <= 0
        || villageBlockedWallRecords != 0) {
        std::cerr << "village counts houses=" << villageHouses
                  << " walls=" << villageWalls
                  << " fields=" << villageFields
                  << " squares=" << villageSquares
                  << " roads=" << villageRoads << "\n";
        return fail("village generator settlement invariants failed");
    }

    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_DirtRoad);
    SubworldMapData villageAlignedOut{};
    dispatch_generate(village, nbH, nbB, nbF, villageAlignedOut);
    int villageWestX, villageWestY, villageEastX, villageEastY;
    expected_edge_anchor(village, -1, 0, villageWestX, villageWestY);
    expected_edge_anchor(village, 1, 0, villageEastX, villageEastY);
    if (villageAlignedOut.tiles[std::size_t(villageWestY) * kCellSize + villageWestX] != TILE_ROAD
        || villageAlignedOut.tiles[std::size_t(villageEastY) * kCellSize + villageEastX] != TILE_ROAD
        || villageAlignedOut.tiles[std::size_t(0) * kCellSize + center] == TILE_ROAD
        || villageAlignedOut.tiles[std::size_t(kCellSize - 1) * kCellSize + center] == TILE_ROAD
        || wall_structures_on_protected_tiles(villageAlignedOut) != 0) {
        return fail("village generator ignored neighbour road directions");
    }

    CellContext road{};
    road.cx = 1;
    road.cy = -9;
    road.macroHeight = 0.35f;
    road.biome = Water;
    road.feature = FT_Road;
    road.landmarkSettlementId = -1;
    road.landmarkSize = 0;
    road.landmarkKind = CellLandmarkKind::None;
    road.seed = 0x10203040u;
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_Road);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_Road);

    SubworldMapData roadOut{};
    dispatch_generate(road, nbH, nbB, nbF, roadOut);
    if (resolve_mode(road) != SubworldMode::Road) {
        return fail("water road feature did not resolve to Road mode");
    }

    int bridgeCount = 0;
    int roadTiles = 0;
    int roadWestX, roadWestY, roadEastX, roadEastY;
    expected_edge_anchor(road, -1, 0, roadWestX, roadWestY);
    expected_edge_anchor(road, 1, 0, roadEastX, roadEastY);
    const float expectedBridgeX = (float(roadWestX) + float(roadEastX)) * 0.5f;
    const float expectedBridgeY = (float(roadWestY) + float(roadEastY)) * 0.5f;
    for (const Structure& s : roadOut.structures) {
        if (s.kind != Structure::Bridge) continue;
        ++bridgeCount;
        if (std::fabs(s.x - expectedBridgeX) > 0.001f
            || std::fabs(s.y - expectedBridgeY) > 0.001f
            || std::fabs(s.height - 3.0f) > 0.001f
            || s.radius < 500.0f) {
            return fail("water road bridge dimensions do not match TS constants");
        }
    }
    for (const std::uint8_t tile : roadOut.tiles) {
        if (tile == TILE_ROAD) ++roadTiles;
    }
    if (bridgeCount != 1 || roadTiles <= 0
        || roadOut.tiles[std::size_t(roadWestY) * kCellSize + roadWestX] != TILE_ROAD
        || roadOut.tiles[std::size_t(roadEastY) * kCellSize + roadEastX] != TILE_ROAD) {
        return fail("water road bridge/tile invariants failed");
    }

    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_Road);
    nbF[5] = std::uint8_t(FT_Road);
    SubworldMapData roadSingleOut{};
    dispatch_generate(road, nbH, nbB, nbF, roadSingleOut);
    int roadSingleBridgeCount = 0;
    int roadSingleEastX, roadSingleEastY;
    expected_edge_anchor(road, 1, 0, roadSingleEastX, roadSingleEastY);
    // TS road-generator: a single-connection cell carves from the edge anchor
    // to the cell CENTRE (a natural terminus), and the bridge spans that same
    // anchor→centre segment.
    const int roadSingleCenter = kCellSize / 2;
    const float expectedSingleBridgeX =
        (float(roadSingleEastX) + float(roadSingleCenter)) * 0.5f;
    const float expectedSingleBridgeY =
        (float(roadSingleEastY) + float(roadSingleCenter)) * 0.5f;
    for (const Structure& s : roadSingleOut.structures) {
        if (s.kind != Structure::Bridge) continue;
        ++roadSingleBridgeCount;
        if (std::fabs(s.x - expectedSingleBridgeX) > 0.001f
            || std::fabs(s.y - expectedSingleBridgeY) > 0.001f
            || std::fabs(s.height - 3.0f) > 0.001f) {
            return fail("single-neighbour water bridge did not match anchored road");
        }
    }
    if (roadSingleBridgeCount != 1
        || roadSingleOut.tiles[std::size_t(roadSingleEastY) * kCellSize
                              + roadSingleEastX] != TILE_ROAD
        || roadSingleOut.tiles[std::size_t(roadSingleCenter) * kCellSize
                              + roadSingleCenter] != TILE_ROAD) {
        return fail("single-neighbour road anchor invariants failed");
    }

    if (std::fabs(biome_config(Meadow).treeDensity - 0.035f) > 0.0001f
        || biome_config(Meadow).treeStep != 4
        || std::fabs(biome_config(Taiga).treeDensity - 0.20f) > 0.0001f
        || biome_config(Taiga).treeStep != 3
        || std::fabs(biome_config(Swamp).treeDensity - 0.08f) > 0.0001f
        || biome_config(Swamp).treeStep != 3) {
        return fail("biome config constants drifted from TS base-generator.ts");
    }

    CellContext grass{};
    grass.cx = -6;
    grass.cy = 2;
    grass.macroHeight = 0.62f;
    grass.biome = Meadow;
    grass.feature = FT_None;
    grass.landmarkSettlementId = -1;
    grass.landmarkSize = 0;
    grass.landmarkKind = CellLandmarkKind::None;
    grass.seed = 0xAABBCCDDu;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData grassOut{};
    dispatch_generate(grass, nbH, nbB, nbF, grassOut);
    if (resolve_mode(grass) != SubworldMode::Grassland
        || std::fabs(grassOut.waterLevel - WATER_LEVEL) > 0.0001f) {
        return fail("plain wilderness did not resolve to TS grassland/waterLevel");
    }

    CellContext invalidFeatureGrass = grass;
    invalidFeatureGrass.feature = static_cast<FeatureType>(255u);
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[0] = 255u;
    nbF[4] = 255u;
    SubworldMapData invalidFeatureGrassOut{};
    dispatch_generate(invalidFeatureGrass, nbH, nbB, nbF, invalidFeatureGrassOut);
    if (invalidFeatureGrassOut.tiles != grassOut.tiles
        || invalidFeatureGrassOut.trav != grassOut.trav
        || invalidFeatureGrassOut.structures.size() != grassOut.structures.size()
        || !heightmaps_nearly_equal(invalidFeatureGrassOut.heightmap,
                                    grassOut.heightmap)) {
        return fail("subworld dispatch did not fail closed for invalid feature bytes");
    }

    // Direct heightmap coverage for the elevation-classified Mountain biome.
    // Relief amplification keys off nbBiome==Mountain (base_generator ridge /
    // peak passes), so a Mountain neighbourhood must produce a strictly larger
    // vertical range than an otherwise identical plains neighbourhood — the
    // only difference between the two calls is the biome, isolating the
    // biome-driven amplification (mountains are no longer a feature).
    std::vector<float> directPlainsHeightmap;
    std::vector<float> directMountainHeightmap;
    float directNbH[9];
    Biome plainsNbB[9];
    Biome mountainNbB[9];
    for (int i = 0; i < 9; ++i) {
        directNbH[i] = 0.84f;
        plainsNbB[i] = Meadow;
        mountainNbB[i] = Mountain;
    }
    generate_heightmap(directPlainsHeightmap, 32, directNbH, plainsNbB,
                       Meadow, grass.seed);
    generate_heightmap(directMountainHeightmap, 32, directNbH, mountainNbB,
                       Mountain, grass.seed);
    const auto vertical_range = [](const std::vector<float>& hm) {
        float lo = 99.0f, hi = -99.0f;
        for (const float h : hm) {
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
        return hm.empty() ? 0.0f : hi - lo;
    };
    if (directPlainsHeightmap.size() != directMountainHeightmap.size()
        || directMountainHeightmap.empty()
        || vertical_range(directMountainHeightmap)
               <= vertical_range(directPlainsHeightmap)) {
        return fail("Mountain-biome neighbourhood did not amplify heightmap relief");
    }

    int grassTrees = 0;
    for (const Structure& s : grassOut.structures) {
        if (s.kind == Structure::Tree) ++grassTrees;
    }
    if (grassTrees <= 0) {
        return fail("grassland generator produced no universal tree scatter");
    }

    CellContext water{};
    water.cx = 5;
    water.cy = 5;
    water.macroHeight = 0.28f;
    water.biome = Water;
    water.feature = FT_None;
    water.landmarkSettlementId = -1;
    water.landmarkSize = 0;
    water.landmarkKind = CellLandmarkKind::None;
    water.seed = 0x31415926u;
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_None);
    for (float& h : nbH) h = water.macroHeight;

    SubworldMapData waterOut{};
    dispatch_generate(water, nbH, nbB, nbF, waterOut);
    int waterTiles = 0;
    int waterTrees = 0;
    for (const std::uint8_t tile : waterOut.tiles) {
        if (tile == TILE_WATER) ++waterTiles;
    }
    for (const Structure& s : waterOut.structures) {
        if (s.kind == Structure::Tree) ++waterTrees;
    }
    if (resolve_mode(water) != SubworldMode::Water
        || std::fabs(waterOut.waterLevel - WATER_LEVEL) > 0.0001f
        || waterTiles != int(waterOut.tiles.size())
        || waterTrees != 0) {
        return fail("water generator invariants failed");
    }

    CellContext coast = water;
    coast.macroHeight = 0.38f;
    coast.seed = 0x514C0A57u;
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_None);
    for (float& h : nbH) h = coast.macroHeight;
    nbH[2] = 0.62f; nbB[2] = Meadow;
    nbH[5] = 0.62f; nbB[5] = Meadow;
    nbH[8] = 0.62f; nbB[8] = Meadow;

    SubworldMapData coastOut{};
    dispatch_generate(coast, nbH, nbB, nbF, coastOut);
    int coastWater = 0;
    int coastShore = 0;
    int coastLand = 0;
    int badCoastWater = 0;
    int badCoastLand = 0;
    for (std::size_t i = 0; i < coastOut.tiles.size(); ++i) {
        const std::uint8_t tile = coastOut.tiles[i];
        const float h = coastOut.heightmap[i];
        if (tile == TILE_WATER) {
            ++coastWater;
            if (h > WATER_LEVEL + 0.0001f) ++badCoastWater;
        } else {
            if (tile == TILE_SHORE) ++coastShore;
            if (tile == TILE_GRASS) ++coastLand;
            // Land/shore must not dip below the water plane (no submerged
            // land). The shore band [WATER_LEVEL, WATER_LEVEL + kLandMargin)
            // is the intentional smooth beach, not a violation.
            if (h < WATER_LEVEL - 0.0001f) ++badCoastLand;
        }
    }
    if (coastWater <= 0 || coastShore <= 0 || coastLand <= 0
        || badCoastWater != 0 || badCoastLand != 0) {
        std::cerr << "coast water=" << coastWater
                  << " shore=" << coastShore
                  << " land=" << coastLand
                  << " badWater=" << badCoastWater
                  << " badLand=" << badCoastLand << "\n";
        return fail("coastal water cell did not expose TS-style shore/land bands");
    }

    CellContext swamp{};
    swamp.cx = -4;
    swamp.cy = -4;
    swamp.macroHeight = 0.56f;
    swamp.biome = Swamp;
    swamp.feature = FT_None;
    swamp.landmarkSettlementId = -1;
    swamp.landmarkSize = 0;
    swamp.landmarkKind = CellLandmarkKind::None;
    swamp.seed = 0x27182818u;
    fill_flat_neighbors(nbH, nbB, nbF, Swamp, FT_None);

    SubworldMapData swampOut{};
    dispatch_generate(swamp, nbH, nbB, nbF, swampOut);
    int swampTrees = 0;
    for (const Structure& s : swampOut.structures) {
        if (s.kind == Structure::Tree) ++swampTrees;
    }
    if (resolve_mode(swamp) != SubworldMode::Swamp
        || std::fabs(swampOut.waterLevel - WATER_LEVEL) > 0.0001f
        || swampTrees <= 0) {
        return fail("swamp generator invariants failed");
    }

    CellContext mountain{};
    mountain.cx = 12;
    mountain.cy = -12;
    mountain.macroHeight = 0.84f;
    mountain.biome = Mountain;   // elevation-classified biome, not a feature
    mountain.feature = FT_None;
    mountain.landmarkSettlementId = -1;
    mountain.landmarkSize = 0;
    mountain.landmarkKind = CellLandmarkKind::None;
    mountain.seed = 0x55667788u;
    fill_flat_neighbors(nbH, nbB, nbF, Mountain, FT_None);

    SubworldMapData mountainOut{};
    dispatch_generate(mountain, nbH, nbB, nbF, mountainOut);
    float mountainMin = 99.0f;
    float mountainMax = -99.0f;
    for (const float h : mountainOut.heightmap) {
        mountainMin = std::min(mountainMin, h);
        mountainMax = std::max(mountainMax, h);
    }
    if (resolve_mode(mountain) != SubworldMode::Mountain
        || std::fabs(mountainOut.waterLevel - WATER_LEVEL) > 0.0001f
        || mountainMax <= mountainMin) {
        return fail("mountain generator invariants failed");
    }

    std::cout << "subworld_generator_parity_test ok: forest_glades="
              << glades.size() << " tree_structures=" << treeStructures
              << " spire_scorch_rock=" << scorchRock
              << " ruin_wall_tiles=" << wallTiles
              << " city_houses=" << cityHouses
              << " village_houses=" << villageHouses
              << " road_bridge_count=" << bridgeCount
              << " grassland_trees=" << grassTrees
              << " swamp_trees=" << swampTrees << "\n";
    return 0;
}

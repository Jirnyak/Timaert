#include "sub/gens/dispatch.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "subworld_city_gen_test FAIL: %s\n", msg);
    return 1;
}

std::size_t count_tile(const sm::sub::SubworldMapData& map, std::uint8_t tile) {
    std::size_t n = 0;
    for (std::uint8_t t : map.tiles) {
        if (t == tile) ++n;
    }
    return n;
}

std::size_t count_structure(const sm::sub::SubworldMapData& map,
                            sm::sub::Structure::Kind kind) {
    std::size_t n = 0;
    for (const auto& s : map.structures) {
        if (s.kind == kind) ++n;
    }
    return n;
}

bool blocked_tiles_match_traversability(const sm::sub::SubworldMapData& map) {
    if (map.tiles.size() != map.trav.size()) return false;
    for (std::size_t i = 0; i < map.tiles.size(); ++i) {
        const std::uint8_t t = map.tiles[i];
        const bool blocked = t == sm::sub::TILE_HOUSE || t == sm::sub::TILE_WALL;
        if (blocked && map.trav[i] != 0) return false;
    }
    return true;
}

bool trees_respect_core_clear_radius(const sm::sub::SubworldMapData& map, float radius) {
    const float cx = float(sm::sub::kCellSize) * 0.5f;
    const float cy = float(sm::sub::kCellSize) * 0.5f;
    const float r2 = radius * radius;
    for (const auto& s : map.structures) {
        if (s.kind != sm::sub::Structure::Tree) continue;
        const float dx = s.x - cx;
        const float dy = s.y - cy;
        if (dx * dx + dy * dy < r2) return false;
    }
    return true;
}

} // namespace

int main() {
    sm::sub::CellContext ctx{};
    ctx.cx = 41;
    ctx.cy = 9;
    ctx.macroHeight = 0.66f;
    ctx.biome = sm::Biome::Meadow;
    ctx.feature = sm::FT_None;
    ctx.landmarkSettlementId = 12;
    ctx.landmarkSize = 6400;
    ctx.seed = 0xC17A551u;

    float nbHeights[9]{};
    sm::Biome nbBiome[9]{};
    std::uint8_t nbFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        nbHeights[i] = 0.66f;
        nbBiome[i] = sm::Biome::Meadow;
        nbFeature[i] = std::uint8_t(sm::FT_None);
    }
    // Four cardinal road neighbours force city roads/gates to align to macro connectivity.
    nbFeature[1] = std::uint8_t(sm::FT_Road);
    nbFeature[3] = std::uint8_t(sm::FT_Road);
    nbFeature[5] = std::uint8_t(sm::FT_Road);
    nbFeature[7] = std::uint8_t(sm::FT_Road);

    if (sm::sub::resolve_mode(ctx) != sm::sub::SubworldMode::City) {
        return fail("city landmark does not resolve to city mode");
    }

    sm::sub::SubworldMapData a;
    sm::sub::SubworldMapData b;
    sm::sub::dispatch_generate(ctx, nbHeights, nbBiome, nbFeature, a);
    sm::sub::dispatch_generate(ctx, nbHeights, nbBiome, nbFeature, b);

    if (a.tiles != b.tiles) return fail("generation is not deterministic");
    if (a.trav != b.trav) return fail("traversability is not deterministic");
    if (a.heightmap != b.heightmap) return fail("heightmap is not deterministic");

    if (a.tiles.size() != std::size_t(sm::sub::kCellSize) * sm::sub::kCellSize) {
        return fail("tile map has wrong size");
    }
    const std::size_t roads = count_tile(a, sm::sub::TILE_ROAD);
    const std::size_t squares = count_tile(a, sm::sub::TILE_SQUARE);
    const std::size_t houses = count_tile(a, sm::sub::TILE_HOUSE);
    const std::size_t fields = count_tile(a, sm::sub::TILE_FIELD);
    const std::size_t walls = count_tile(a, sm::sub::TILE_WALL);
    const std::size_t houseStructs = count_structure(a, sm::sub::Structure::House);
    const std::size_t wallStructs = count_structure(a, sm::sub::Structure::Wall);
    const std::size_t treeStructs = count_structure(a, sm::sub::Structure::Tree);

    if (count_tile(a, sm::sub::TILE_EMPTY) != 0) return fail("empty tiles remain after city fill");
    if (roads < 5000) return fail("city roads missing");
    if (squares < 20) return fail("central square / urban squares missing");
    if (houses < 500) return fail("city houses missing");
    if (fields < 500) return fail("outer fields missing");
    if (walls < 100) return fail("city walls missing");
    if (houseStructs < 20) return fail("house structures missing");
    if (wallStructs < 10) return fail("wall structures missing");
    if (!blocked_tiles_match_traversability(a)) return fail("blocked tiles are walkable");
    if (!trees_respect_core_clear_radius(a, 120.0f)) return fail("trees violate city clear radius");

    std::fprintf(stderr,
        "subworld_city_gen_test PASS roads=%zu square=%zu houses=%zu fields=%zu walls=%zu house_struct=%zu wall_struct=%zu trees=%zu\n",
        roads, squares, houses, fields, walls, houseStructs, wallStructs, treeStructs);
    return 0;
}

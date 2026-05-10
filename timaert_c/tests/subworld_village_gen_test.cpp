#include "sub/gens/dispatch.h"

#include <cstdio>
#include <cstdint>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "subworld_village_gen_test FAIL: %s\n", msg);
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

} // namespace

int main() {
    sm::sub::CellContext ctx{};
    ctx.cx = 17;
    ctx.cy = 23;
    ctx.macroHeight = 0.62f;
    ctx.biome = sm::Biome::Meadow;
    ctx.feature = sm::FT_None;
    ctx.landmarkSettlementId = 4;
    ctx.landmarkSize = 120;
    ctx.seed = 0x12345678u;

    float nbHeights[9]{};
    sm::Biome nbBiome[9]{};
    std::uint8_t nbFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        nbHeights[i] = 0.62f;
        nbBiome[i] = sm::Biome::Meadow;
        nbFeature[i] = std::uint8_t(sm::FT_None);
    }
    // East-west road neighbours force the TS village road alignment path.
    nbFeature[3] = std::uint8_t(sm::FT_Road);
    nbFeature[5] = std::uint8_t(sm::FT_Road);

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
    if (count_tile(a, sm::sub::TILE_EMPTY) != 0) return fail("empty tiles remain after village fill");
    if (count_tile(a, sm::sub::TILE_ROAD) < 1000) return fail("village roads missing");
    if (count_tile(a, sm::sub::TILE_SQUARE) == 0) return fail("village square missing");
    if (count_tile(a, sm::sub::TILE_HOUSE) == 0) return fail("village houses missing");
    if (count_tile(a, sm::sub::TILE_FIELD) == 0) return fail("village fields missing");
    if (count_tile(a, sm::sub::TILE_WALL) == 0) return fail("village palisade missing");
    if (count_structure(a, sm::sub::Structure::House) == 0) return fail("house structures missing");
    if (count_structure(a, sm::sub::Structure::Wall) == 0) return fail("wall structures missing");
    if (!blocked_tiles_match_traversability(a)) return fail("blocked tiles are walkable");

    std::fprintf(stderr,
        "subworld_village_gen_test PASS roads=%zu square=%zu houses=%zu fields=%zu walls=%zu structures=%zu\n",
        count_tile(a, sm::sub::TILE_ROAD),
        count_tile(a, sm::sub::TILE_SQUARE),
        count_tile(a, sm::sub::TILE_HOUSE),
        count_tile(a, sm::sub::TILE_FIELD),
        count_tile(a, sm::sub::TILE_WALL),
        a.structures.size());
    return 0;
}

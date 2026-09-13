#include "sub/gens/kit/tiles.h"

#include "sub/base_generator.h"

#include <algorithm>
#include <vector>

namespace sm::sub::kit {

void clear_decor_tiles(SubworldMapData& out) {
    for (std::uint8_t& t : out.tiles) {
        if (t == TILE_TREE_DECOR) t = TILE_GRASS;
    }
}

void stamp_rect(SubworldMapData& out, int x, int y, int w, int h,
                std::uint8_t tile, std::uint8_t trav) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kCellSize, x + w);
    const int y1 = std::min(kCellSize, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            const std::size_t idx = std::size_t(yy) * kCellSize + xx;
            out.tiles[idx] = tile;
            out.trav[idx] = trav;
        }
    }
}

bool has_tile_near(const SubworldMapData& out, int x, int y,
                   int radius, std::uint8_t tile) {
    const int x0 = std::max(0, x - radius);
    const int y0 = std::max(0, y - radius);
    const int x1 = std::min(kCellSize - 1, x + radius);
    const int y1 = std::min(kCellSize - 1, y + radius);
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            if (out.tiles[std::size_t(yy) * kCellSize + xx] == tile) {
                return true;
            }
        }
    }
    return false;
}

void sync_water_tiles_from_heightmap(SubworldMapData& out) {
    if (out.tiles.size() != out.heightmap.size()) return;
    if (out.heightmap.size() != std::size_t(kCellSize) * kCellSize) return;

    constexpr int kShoreRadius = 6;
    const int w = kCellSize;
    const int hgt = kCellSize;

    std::vector<std::uint8_t> water(out.heightmap.size(), 0);
    std::vector<std::uint8_t> nearX(out.heightmap.size(), 0);
    std::vector<std::uint8_t> nearWater(out.heightmap.size(), 0);

    for (std::size_t i = 0; i < out.heightmap.size(); ++i) {
        water[i] = out.heightmap[i] < WATER_LEVEL ? 1u : 0u;
    }

    std::vector<int> prefix(std::size_t(std::max(w, hgt)) + 1u, 0);
    for (int y = 0; y < hgt; ++y) {
        const int row = y * w;
        prefix[0] = 0;
        for (int x = 0; x < w; ++x) {
            prefix[std::size_t(x + 1)] = prefix[std::size_t(x)]
                + int(water[std::size_t(row + x)]);
        }
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - kShoreRadius);
            const int x1 = std::min(w, x + kShoreRadius + 1);
            nearX[std::size_t(row + x)] =
                prefix[std::size_t(x1)] > prefix[std::size_t(x0)] ? 1u : 0u;
        }
    }
    for (int x = 0; x < w; ++x) {
        prefix[0] = 0;
        for (int y = 0; y < hgt; ++y) {
            prefix[std::size_t(y + 1)] = prefix[std::size_t(y)]
                + int(nearX[std::size_t(y) * w + x]);
        }
        for (int y = 0; y < hgt; ++y) {
            const int y0 = std::max(0, y - kShoreRadius);
            const int y1 = std::min(hgt, y + kShoreRadius + 1);
            nearWater[std::size_t(y) * w + x] =
                prefix[std::size_t(y1)] > prefix[std::size_t(y0)] ? 1u : 0u;
        }
    }

    for (std::size_t i = 0; i < out.heightmap.size(); ++i) {
        const std::uint8_t tile = out.tiles[i];
        // Built ground keeps its surface: the terrain may not un-decide what a
        // generator decided (sub/map_data.h kTileBuilt).
        if (tile_is(tile, kTileBuilt)) continue;

        const float h = out.heightmap[i];
        if (water[i]) {
            out.tiles[i] = TILE_WATER;
        } else if (h < kWetEdgeTop && nearWater[i]) {
            out.tiles[i] = TILE_SHORE;
        } else if (tile == TILE_WATER || tile == TILE_SHORE) {
            out.tiles[i] = TILE_GRASS;
        }
    }
}

} // namespace sm::sub::kit

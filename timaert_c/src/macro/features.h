// Per-cell feature byte grid (between biome and landmark). Mirrors features.ts.
#pragma once
#include <cstdint>
#include <vector>

namespace sm {

enum FeatureType : std::uint8_t {
    FT_None = 0, FT_Road = 1, FT_Tree = 2, FT_Mountain = 3, FT_DirtRoad = 4,
};

struct FeatureLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;

    void resize(int w, int h) {
        width = w; height = h;
        data.assign(std::size_t(w) * h, 0);
    }
    FeatureType at(int x, int y) const {
        int wx = ((x % width) + width) % width;
        int wy = ((y % height) + height) % height;
        return FeatureType(data[std::size_t(wy) * width + wx]);
    }
    void set(int x, int y, FeatureType t) {
        int wx = ((x % width) + width) % width;
        int wy = ((y % height) + height) % height;
        data[std::size_t(wy) * width + wx] = std::uint8_t(t);
    }
};

} // namespace sm

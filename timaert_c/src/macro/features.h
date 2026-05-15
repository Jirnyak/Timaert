// Per-cell feature byte grid (between biome and landmark). Mirrors features.ts.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sm {

enum FeatureType : std::uint8_t {
    FT_None = 0, FT_Road = 1, FT_Tree = 2, FT_Mountain = 3, FT_DirtRoad = 4,
};

static_assert(FT_None == 0, "FeatureType must match features.ts byte contract");
static_assert(FT_Road == 1, "FeatureType must match features.ts byte contract");
static_assert(FT_Tree == 2, "FeatureType must match features.ts byte contract");
static_assert(FT_Mountain == 3, "FeatureType must match features.ts byte contract");
static_assert(FT_DirtRoad == 4, "FeatureType must match features.ts byte contract");

// TS default: webgl-context.ts snowLevel (0.80) minus GameScreen feature offset (0.05).
inline constexpr float kDefaultFeatureMountainThreshold = 0.75f;

struct FeatureLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;

    static bool is_valid_byte(std::uint8_t value) {
        return value <= FT_DirtRoad;
    }

    static FeatureType decode(std::uint8_t value) {
        switch (value) {
            case FT_None:
            case FT_Road:
            case FT_Tree:
            case FT_Mountain:
            case FT_DirtRoad:
                return FeatureType(value);
            default:
                return FT_None;
        }
    }

    static bool cell_count_for(int w, int h, std::size_t &out) {
        out = 0;
        if (w <= 0 || h <= 0)
            return false;
        if (std::size_t(w) > std::numeric_limits<std::size_t>::max() / std::size_t(h))
            return false;
        out = std::size_t(w) * std::size_t(h);
        return true;
    }

    static int wrap_coord(int value, int limit) {
        if (limit <= 0)
            return 0;
        const std::int64_t l = std::int64_t(limit);
        std::int64_t r = std::int64_t(value) % l;
        if (r < 0)
            r += l;
        return int(r);
    }

    std::size_t cell_count() const {
        std::size_t n = 0;
        return cell_count_for(width, height, n) ? n : 0u;
    }

    bool has_complete_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n;
    }

    bool has_invalid_cell_bytes() const {
        const std::size_t n = cell_count();
        if (n == 0u || data.size() < n)
            return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (!is_valid_byte(data[i]))
                return true;
        }
        return false;
    }

    bool copy_sanitized_cells(std::vector<std::uint8_t> &out) const {
        const std::size_t n = cell_count();
        if (n == 0u || data.size() < n) {
            out.clear();
            return false;
        }
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = std::uint8_t(decode(data[i]));
        return true;
    }

    const std::uint8_t *complete_cells_or_sanitized(std::vector<std::uint8_t> &out) const {
        const std::size_t n = cell_count();
        if (n == 0u || data.size() < n) {
            out.clear();
            return nullptr;
        }
        bool copied = false;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t value = data[i];
            if (is_valid_byte(value))
                continue;
            if (!copied) {
                out.assign(data.data(), data.data() + n);
                copied = true;
            }
            out[i] = std::uint8_t(FT_None);
        }
        if (copied)
            return out.data();
        out.clear();
        return data.data();
    }

    bool covers(int w, int h) const {
        std::size_t n = 0;
        return width == w && height == h && cell_count_for(w, h, n) && data.size() >= n;
    }

    void resize(int w, int h) {
        std::size_t n = 0;
        if (!cell_count_for(w, h, n)) {
            width = 0;
            height = 0;
            data.clear();
            return;
        }
        width = w;
        height = h;
        data.assign(n, 0);
    }
    FeatureType at(int x, int y) const {
        if (width <= 0 || height <= 0 || data.empty()) return FT_None;
        const int wx = wrap_coord(x, width);
        const int wy = wrap_coord(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        if (i >= data.size()) return FT_None;
        return decode(data[i]);
    }
    void set(int x, int y, FeatureType t) {
        if (width <= 0 || height <= 0 || data.empty()) return;
        const int wx = wrap_coord(x, width);
        const int wy = wrap_coord(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        if (i >= data.size()) return;
        data[i] = std::uint8_t(decode(std::uint8_t(t)));
    }
};

} // namespace sm

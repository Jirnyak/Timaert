// Procedural heraldic flag generator — port of `src/game/flag-generator.ts`.

#include "macro/flag_generator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sm {

namespace {

constexpr int kSize = 128;

// TS RNG: `(sin(seed++) * 10000) - floor(...)`. Bit-identical reproduction.
struct SinRng {
    double seed;
    explicit SinRng(std::uint32_t s) : seed(static_cast<double>(s)) {}
    double next() {
        const double x = std::sin(seed) * 10000.0;
        seed += 1.0;
        return x - std::floor(x);
    }
    int randInt(int lo, int hi) {
        return static_cast<int>(std::floor(next() * (hi - lo + 1))) + lo;
    }
};

struct Color { std::uint8_t r, g, b, a = 255; };

inline Color pick_color(SinRng& rng, int minV, int maxV) {
    const int r = rng.randInt(minV, maxV);
    const int g = rng.randInt(minV, maxV);
    const int b = rng.randInt(minV, maxV);
    return Color{
        static_cast<std::uint8_t>(r),
        static_cast<std::uint8_t>(g),
        static_cast<std::uint8_t>(b),
        255
    };
}

inline void put(std::uint8_t* px, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
    const std::size_t i = (static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)) * 4;
    px[i + 0] = c.r;
    px[i + 1] = c.g;
    px[i + 2] = c.b;
    px[i + 3] = c.a;
}

inline void fill_rect(std::uint8_t* px, int x, int y, int w, int h, Color c) {
    const int x1 = std::clamp(x,         0, kSize);
    const int y1 = std::clamp(y,         0, kSize);
    const int x2 = std::clamp(x + w,     0, kSize);
    const int y2 = std::clamp(y + h,     0, kSize);
    for (int yy = y1; yy < y2; ++yy) {
        for (int xx = x1; xx < x2; ++xx) put(px, xx, yy, c);
    }
}

// Filled disc (midpoint).
inline void fill_circle(std::uint8_t* px, int cx, int cy, int radius, Color c) {
    const int r2 = radius * radius;
    const int x1 = std::max(0, cx - radius);
    const int y1 = std::max(0, cy - radius);
    const int x2 = std::min(kSize - 1, cx + radius);
    const int y2 = std::min(kSize - 1, cy + radius);
    for (int yy = y1; yy <= y2; ++yy) {
        const int dy = yy - cy;
        for (int xx = x1; xx <= x2; ++xx) {
            const int dx = xx - cx;
            if (dx * dx + dy * dy <= r2) put(px, xx, yy, c);
        }
    }
}

// Filled convex polygon via scanline. Used for diagonal band + diamond +
// triangles. Mirrors Canvas2D's `beginPath+lineTo+fill` shapes used in TS.
inline void fill_polygon(std::uint8_t* px,
                         const std::array<std::pair<int,int>, 8>& pts,
                         int n, Color c) {
    int yMin = pts[0].second, yMax = pts[0].second;
    for (int i = 1; i < n; ++i) {
        yMin = std::min(yMin, pts[static_cast<std::size_t>(i)].second);
        yMax = std::max(yMax, pts[static_cast<std::size_t>(i)].second);
    }
    yMin = std::max(yMin, 0);
    yMax = std::min(yMax, kSize - 1);

    for (int yy = yMin; yy <= yMax; ++yy) {
        // Find x intersections with each polygon edge.
        std::array<int, 16> xs{};
        int xc = 0;
        for (int i = 0; i < n; ++i) {
            const auto& a = pts[static_cast<std::size_t>(i)];
            const auto& b = pts[static_cast<std::size_t>((i + 1) % n)];
            const int ya = a.second, yb = b.second;
            if ((ya <= yy && yb > yy) || (yb <= yy && ya > yy)) {
                const float t = static_cast<float>(yy - ya)
                              / static_cast<float>(yb - ya);
                const int x = a.first
                            + static_cast<int>(t * static_cast<float>(b.first - a.first));
                if (xc < 16) xs[static_cast<std::size_t>(xc++)] = x;
            }
        }
        std::sort(xs.begin(), xs.begin() + xc);
        for (int i = 0; i + 1 < xc; i += 2) {
            const int x1 = std::max(0, xs[static_cast<std::size_t>(i)]);
            const int x2 = std::min(kSize - 1, xs[static_cast<std::size_t>(i + 1)]);
            for (int xx = x1; xx <= x2; ++xx) put(px, xx, yy, c);
        }
    }
}

} // namespace

FlagBitmap generate_flag(std::uint32_t seed) {
    FlagBitmap fb;
    fb.W = kSize;
    fb.H = kSize;
    fb.rgba.assign(static_cast<std::size_t>(kSize * kSize * 4), 0);
    std::uint8_t* px = fb.rgba.data();

    SinRng rng(seed);

    // Palette: base / accent / detail / highlight.
    const Color palette[4] = {
        pick_color(rng, 50,  200),
        pick_color(rng, 80,  240),
        pick_color(rng, 60,  220),
        pick_color(rng, 120, 255),
    };

    // 1. Background.
    fill_rect(px, 0, 0, kSize, kSize, palette[0]);

    if (rng.next() > 0.5) {
        for (int i = 0; i < kSize; i += 16) {
            if (rng.next() > 0.4) {
                fill_rect(px, 0, i, kSize, 8, palette[1]);
            }
        }
    }

    // 2. Main body — pick one of four shapes.
    {
        constexpr int nModes = 4;
        const int mode = rng.randInt(0, nModes - 1);
        const Color c  = palette[1];
        switch (mode) {
            case 0: { // rect
                const int m = rng.randInt(12, 24);
                fill_rect(px, m, m, kSize - m * 2, kSize - m * 2, c);
                break;
            }
            case 1: { // diagonal band
                const int t = rng.randInt(16, 32);
                std::array<std::pair<int,int>, 8> pts{{
                    {0, t}, {t, 0}, {kSize, kSize - t}, {kSize - t, kSize},
                    {0,0},{0,0},{0,0},{0,0}
                }};
                fill_polygon(px, pts, 4, c);
                break;
            }
            case 2: { // circle
                const int r = rng.randInt(24, 40);
                fill_circle(px, kSize / 2, kSize / 2, r, c);
                break;
            }
            default: { // split
                const int split = rng.randInt(48, 80);
                fill_rect(px, 0, 0, split, kSize, c);
                break;
            }
        }
    }

    // 3. Divisors / symbols.
    {
        constexpr int nModes = 4;
        const int mode = rng.randInt(0, nModes - 1);
        const Color c  = palette[2];
        switch (mode) {
            case 0: { // cross
                const int t = 10;
                fill_rect(px, kSize / 2 - t / 2, kSize / 4, t,         kSize / 2, c);
                fill_rect(px, kSize / 4,         kSize / 2 - t / 2, kSize / 2, t, c);
                break;
            }
            case 1: { // diamond
                const int s = 56 / 2;
                std::array<std::pair<int,int>, 8> pts{{
                    {kSize / 2,     kSize / 2 - s},
                    {kSize / 2 + s, kSize / 2    },
                    {kSize / 2,     kSize / 2 + s},
                    {kSize / 2 - s, kSize / 2    },
                    {0,0},{0,0},{0,0},{0,0}
                }};
                fill_polygon(px, pts, 4, c);
                break;
            }
            case 2: { // triangles (left + right)
                std::array<std::pair<int,int>, 8> tri1{{
                    {0, 0}, {kSize / 2, kSize / 2}, {0, kSize},
                    {0,0},{0,0},{0,0},{0,0},{0,0}
                }};
                fill_polygon(px, tri1, 3, c);
                std::array<std::pair<int,int>, 8> tri2{{
                    {kSize, 0}, {kSize / 2, kSize / 2}, {kSize, kSize},
                    {0,0},{0,0},{0,0},{0,0},{0,0}
                }};
                fill_polygon(px, tri2, 3, c);
                break;
            }
            default: { // bars
                for (int i = 0; i < 3; ++i) {
                    fill_rect(px, 8, 24 + i * 24, kSize - 16, 8, c);
                }
                break;
            }
        }
    }

    // 4. Additional details.
    {
        constexpr int nModes = 3;
        const int mode = rng.randInt(0, nModes - 1);
        const Color c  = palette[3];
        if (mode == 0) { // stars (cross-shaped pixels)
            const int n = rng.randInt(3, 6);
            for (int i = 0; i < n; ++i) {
                const int x = rng.randInt(16, kSize - 16);
                const int y = rng.randInt(16, kSize - 16);
                fill_rect(px, x - 4, y - 1, 8, 2, c);
                fill_rect(px, x - 1, y - 4, 2, 8, c);
            }
        } else if (mode == 1) { // circles
            const int n = rng.randInt(4, 8);
            for (int i = 0; i < n; ++i) {
                const int cx = rng.randInt(12, kSize - 12);
                const int cy = rng.randInt(12, kSize - 12);
                const int r  = rng.randInt(3, 6);
                fill_circle(px, cx, cy, r, c);
            }
        } else { // squares
            const int n = rng.randInt(4, 8);
            for (int i = 0; i < n; ++i) {
                const int x = rng.randInt(12, kSize - 12);
                const int y = rng.randInt(12, kSize - 12);
                const int s = rng.randInt(6, 12);
                fill_rect(px, x, y, s, s, c);
            }
        }
    }

    return fb;
}

} // namespace sm

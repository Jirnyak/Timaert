#include "macro/map_generator.h"
#include "gl/helpers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace sm {

// ---- Shaders ----
static const char* kVS = R"(#version 330 core
layout(location=0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// Procedural climate generator — faithful port of `src/webgl/shaders.ts`
// terrain shader. Uses **periodic Perlin noise** (Ken Perlin's permutation
// table) so the master texture tiles seamlessly: a 1024² map wraps with no
// visible seam at uv=0/1 in either axis. Required for our toroidal world.
static const char* kFS = R"(#version 330 core
in vec2 v_uv;
out vec4 fragColor;

uniform float u_seed;
uniform float u_seaLevel;
uniform float u_heightScale;
uniform float u_moistureScale;
uniform float u_continentScale;
uniform float u_continentIntensity;
uniform float u_ridgeIntensity;
uniform float u_domainWarp;
uniform float u_temperatureVariation;

// Ken Perlin's classic 256-entry permutation table, doubled to 512 to avoid
// modulo on indices. Identical to the TS shader so noise output matches.
const int perm[512] = int[512](
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180
);

float fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

// Periodic Perlin noise: lattice indices wrap modulo `period` so the field
// repeats every `period` units in `p`. With pos = uv*8 and period=8 the
// texture tiles exactly across uv [0,1] → seamless wraparound.
float periodicNoise(vec2 p, float period, float seed) {
    p += seed;
    float px = mod(p.x, period);
    float py = mod(p.y, period);
    int xi  = int(floor(px)) & 255;
    int yi  = int(floor(py)) & 255;
    int xi1 = int(mod(floor(px) + 1.0, period)) & 255;
    int yi1 = int(mod(floor(py) + 1.0, period)) & 255;
    float xf = fract(px);
    float yf = fract(py);
    float u  = fade(xf);
    float v  = fade(yf);
    int aa = perm[perm[xi]  + yi];
    int ab = perm[perm[xi]  + yi1];
    int ba = perm[perm[xi1] + yi];
    int bb = perm[perm[xi1] + yi1];
    float x1 = mix(grad(aa, xf, yf      ), grad(ba, xf - 1.0, yf      ), u);
    float x2 = mix(grad(ab, xf, yf - 1.0), grad(bb, xf - 1.0, yf - 1.0), u);
    return mix(x1, x2, v);
}

// fbm(p, octaves, persistence, period, seed) — exact TS signature/semantics.
float fbm(vec2 p, int oct, float persistence, float period, float seed) {
    float value = 0.0;
    float amp   = 1.0;
    float freq  = 1.0;
    float total = 0.0;
    for (int i = 0; i < 8; i++) {
        if (i >= oct) break;
        value += amp * periodicNoise(p * freq, period * freq, seed + float(i) * 100.0);
        total += amp;
        amp   *= persistence;
        freq  *= 2.0;
    }
    return value / total;
}

void main() {
    vec2 uv  = v_uv;
    vec2 pos = uv * 8.0;

    // Domain warp. Period 8 matches the base scale → tiles cleanly.
    float warpX = fbm(pos + vec2(0.0, 0.0), 3, 0.5, 8.0, u_seed + 50.0);
    float warpY = fbm(pos + vec2(5.2, 1.3), 3, 0.5, 8.0, u_seed + 60.0);
    vec2 q = vec2(warpX, warpY) * u_domainWarp;

    // Base height (6 octaves @ period 8).
    int heightOct = 6;
    float noiseHeight = fbm(pos + q, heightOct, 0.5, 8.0, u_seed) * 0.5 + 0.5;

    // Continental structure — low frequency. Period scales with cScale to
    // keep the periodic Perlin tile aligned with the sampled domain.
    float cScale        = max(0.001, u_continentScale);
    float continentBias = fbm(pos * cScale, 2, 0.5, 8.0 * cScale, u_seed + 700.0);
    noiseHeight += continentBias * u_continentIntensity;

    // Mountain ridges: 1-abs(noise) creates connected chains. period = 8*0.7.
    float ridgeBase = fbm(pos * 0.7, 3, 0.55, 5.6, u_seed + 800.0);
    float ridge     = pow(1.0 - abs(ridgeBase), 3.0) * u_ridgeIntensity;
    noiseHeight += ridge;
    noiseHeight = clamp(noiseHeight, 0.0, 1.0);
    noiseHeight = pow(noiseHeight, u_heightScale);

    // Moisture — period 4 → tiles twice across the map.
    float noiseMoist = fbm(pos + q * 0.5, 4, 0.5, 4.0, u_seed + 200.0) * 0.5 + 0.5;
    noiseMoist = pow(noiseMoist, u_moistureScale);

    // Temperature — latitude-driven with a noise contribution.
    float latitude  = 1.0 - abs(uv.y - 0.5) * 2.0; // 1 at equator, 0 at poles
    float noiseTemp = fbm(pos, 3, 0.5, 4.0, u_seed + 300.0) * 0.5 + 0.5;
    float temp01    = latitude * (1.0 - u_temperatureVariation)
                    + noiseTemp * u_temperatureVariation;
    temp01 = clamp(temp01, 0.0, 1.0);

    float mask = noiseHeight < u_seaLevel ? 0.0 : 1.0;

    fragColor = vec4(noiseHeight, noiseMoist, temp01, mask);
}
)";

namespace {

constexpr std::uint16_t kRiverDistInf = 65535u;
constexpr int kRiverMaxBuckets = 4096;
constexpr int kRiverExploreCap = 60000;
constexpr int kRiverDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

inline int wrap_cell(int v, int size) {
    int m = v % size;
    return m < 0 ? m + size : m;
}

inline int cell_index(int x, int y, int w) {
    return y * w + x;
}

inline std::uint8_t sea_level_byte(float seaLevel) {
    const int v = int(std::floor(std::clamp(seaLevel, 0.0f, 1.0f) * 255.0f));
    return std::uint8_t(std::clamp(v, 0, 255));
}

struct RiverCandidate {
    int idx = 0;
    std::uint16_t wd = 0;
};

struct RiverTraceScratch {
    std::vector<int> gScore;
    std::vector<int> parent;
    std::vector<std::uint32_t> tag;
    std::array<std::vector<int>, kRiverMaxBuckets> buckets;
    std::uint32_t generation = 1u;

    void init(std::size_t n) {
        gScore.assign(n, std::numeric_limits<int>::max());
        parent.assign(n, -1);
        tag.assign(n, 0u);
    }

    void begin() {
        ++generation;
        if (generation == 0u) {
            std::fill(tag.begin(), tag.end(), 0u);
            generation = 1u;
        }
        for (auto& b : buckets) {
            b.clear();
        }
    }

    int score(int idx) const {
        return tag[std::size_t(idx)] == generation
            ? gScore[std::size_t(idx)]
            : std::numeric_limits<int>::max();
    }

    void set(int idx, int g, int p) {
        const std::size_t k = std::size_t(idx);
        tag[k] = generation;
        gScore[k] = g;
        parent[k] = p;
    }

    void enqueue(int idx, int f) {
        const int bucket = std::clamp(f, 0, kRiverMaxBuckets - 1);
        buckets[std::size_t(bucket)].push_back(idx);
    }
};

std::vector<std::pair<int, int>> build_river_path(int source, int goal,
                                                   const RiverTraceScratch& scratch,
                                                   int w) {
    std::vector<std::pair<int, int>> path;
    int cur = goal;
    while (cur != source) {
        path.push_back({cur % w, cur / w});
        const int previous = scratch.parent[std::size_t(cur)];
        if (previous < 0) {
            break;
        }
        cur = previous;
    }
    path.push_back({source % w, source / w});
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<std::pair<int, int>> trace_river_to_water(
    int source,
    const std::vector<std::uint16_t>& edgeDist,
    const std::vector<std::uint16_t>& waterDist,
    const std::vector<std::uint8_t>& height,
    const std::vector<std::uint8_t>& riverMask,
    std::uint8_t seaLevel8,
    int w,
    int h,
    RiverTraceScratch& scratch) {

    scratch.begin();
    scratch.set(source, 0, -1);
    scratch.enqueue(source, int(waterDist[std::size_t(source)]));

    int explored = 0;
    for (int b = 0; b < kRiverMaxBuckets && explored < kRiverExploreCap; ++b) {
        std::vector<int>& bucket = scratch.buckets[std::size_t(b)];
        while (!bucket.empty() && explored < kRiverExploreCap) {
            const int cur = bucket.back();
            bucket.pop_back();
            ++explored;

            const int g = scratch.score(cur);
            if (g == std::numeric_limits<int>::max()) {
                continue;
            }

            const bool done = height[std::size_t(cur)] <= seaLevel8
                || (cur != source && riverMask[std::size_t(cur)] > 0);
            if (done) {
                return build_river_path(source, cur, scratch, w);
            }

            const int cx = cur % w;
            const int cy = cur / w;
            for (const auto& d : kRiverDirs) {
                const int nx = wrap_cell(cx + d[0], w);
                const int ny = wrap_cell(cy + d[1], h);
                const int ni = cell_index(nx, ny, w);
                const int ed = std::min<int>(edgeDist[std::size_t(ni)], 15);
                const int cost = 1 + ed * ed + (int(height[std::size_t(ni)]) >> 5);
                const int ng = g + cost;
                if (ng >= scratch.score(ni)) {
                    continue;
                }

                scratch.set(ni, ng, cur);
                scratch.enqueue(ni, ng + int(waterDist[std::size_t(ni)]));
            }
        }
    }

    return {};
}

void stamp_river_path(const std::vector<std::pair<int, int>>& path,
                      const std::vector<std::uint8_t>& height,
                      std::uint8_t seaLevel8,
                      std::vector<std::uint8_t>& riverMask,
                      int w,
                      int h,
                      const std::vector<std::uint16_t>& waterDist) {
    for (const auto& p : path) {
        const int px = p.first;
        const int py = p.second;
        const std::uint16_t wd = waterDist[std::size_t(py * w + px)];
        const int radius = wd < 4u ? 1 : 0;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) {
                    continue;
                }
                const int nx = wrap_cell(px + dx, w);
                const int ny = wrap_cell(py + dy, h);
                const int ni = cell_index(nx, ny, w);
                if (height[std::size_t(ni)] > seaLevel8) {
                    riverMask[std::size_t(ni)] = 255;
                }
            }
        }
    }
}

int count_river_neighbours(const std::vector<std::uint8_t>& riverMask,
                           int idx,
                           int w,
                           int h) {
    const int x = idx % w;
    const int y = idx / w;
    int count = 0;
    for (const auto& d : kRiverDirs) {
        const int ni = cell_index(wrap_cell(x + d[0], w), wrap_cell(y + d[1], h), w);
        if (riverMask[std::size_t(ni)] > 0) {
            ++count;
        }
    }
    return count;
}

std::vector<int> find_river_tips(const std::vector<std::uint8_t>& riverMask,
                                 const std::vector<std::uint8_t>& height,
                                 std::uint8_t seaLevel8,
                                 int w,
                                 int h) {
    std::vector<int> tips;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = cell_index(x, y, w);
            if (riverMask[std::size_t(idx)] == 0 || height[std::size_t(idx)] <= seaLevel8) {
                continue;
            }

            int riverNbrs = 0;
            bool hasSea = false;
            for (const auto& d : kRiverDirs) {
                const int ni = cell_index(wrap_cell(x + d[0], w), wrap_cell(y + d[1], h), w);
                if (riverMask[std::size_t(ni)] > 0) {
                    ++riverNbrs;
                }
                if (height[std::size_t(ni)] <= seaLevel8) {
                    hasSea = true;
                }
            }

            if (riverNbrs == 1 && !hasSea) {
                tips.push_back(idx);
            }
        }
    }
    return tips;
}

bool continue_river_from_tip(int tipIdx,
                             std::vector<std::uint8_t>& riverMask,
                             const std::vector<std::uint16_t>& edgeDist,
                             const std::vector<std::uint16_t>& waterDist,
                             const std::vector<std::uint8_t>& height,
                             std::uint8_t seaLevel8,
                             int w,
                             int h,
                             RiverTraceScratch& scratch) {
    std::vector<int> masked;
    int cur = tipIdx;
    for (int steps = 0; steps < 200; ++steps) {
        if (cur != tipIdx) {
            masked.push_back(cur);
            riverMask[std::size_t(cur)] = 0;
        }

        const int cx = cur % w;
        const int cy = cur / w;
        int next = -1;
        int nextRiverNbrs = 0;
        for (const auto& d : kRiverDirs) {
            const int ni = cell_index(wrap_cell(cx + d[0], w), wrap_cell(cy + d[1], h), w);
            if (riverMask[std::size_t(ni)] > 0) {
                next = ni;
                nextRiverNbrs = count_river_neighbours(riverMask, ni, w, h);
                break;
            }
        }

        if (next < 0 || nextRiverNbrs >= 3) {
            break;
        }
        cur = next;
    }

    const std::vector<std::pair<int, int>> path =
        trace_river_to_water(tipIdx, edgeDist, waterDist, height,
                             riverMask, seaLevel8, w, h, scratch);

    for (int idx : masked) {
        riverMask[std::size_t(idx)] = 255;
    }

    if (path.size() >= 3) {
        stamp_river_path(path, height, seaLevel8, riverMask, w, h, waterDist);
        return true;
    }
    return false;
}

void continue_dead_end_rivers(std::vector<std::uint8_t>& riverMask,
                              const std::vector<std::uint16_t>& edgeDist,
                              const std::vector<std::uint16_t>& waterDist,
                              const std::vector<std::uint8_t>& height,
                              std::uint8_t seaLevel8,
                              int w,
                              int h,
                              RiverTraceScratch& scratch) {
    for (int pass = 0; pass < 5; ++pass) {
        const std::vector<int> tips = find_river_tips(riverMask, height, seaLevel8, w, h);
        int resolved = 0;
        for (int tipIdx : tips) {
            if (continue_river_from_tip(tipIdx, riverMask, edgeDist, waterDist,
                                        height, seaLevel8, w, h, scratch)) {
                ++resolved;
            }
        }
        if (resolved == 0) {
            break;
        }
    }
}

void remove_straight_river_runs(std::vector<std::uint8_t>& riverMask,
                                TerrainData& td,
                                std::uint8_t seaLevel8) {
    const int w = td.width;
    const int h = td.height;
    const int n = w * h;
    constexpr int minRun = 50;
    std::vector<std::uint8_t> thin(std::size_t(n), 0);
    std::vector<std::uint8_t> removeMark(std::size_t(n), 0);

    auto isWater = [&](int idx) {
        return riverMask[std::size_t(idx)] > 0
            || td.rgba[std::size_t(idx) * 4 + 0] <= seaLevel8;
    };

    auto waterNeighbourCount = [&](int x, int y) {
        int count = 0;
        for (const auto& d : kRiverDirs) {
            const int ni = cell_index(wrap_cell(x + d[0], w), wrap_cell(y + d[1], h), w);
            if (isWater(ni)) {
                ++count;
            }
        }
        return count;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = cell_index(x, y, w);
            if (riverMask[std::size_t(idx)] > 0 && waterNeighbourCount(x, y) <= 2) {
                thin[std::size_t(idx)] = 1;
            }
        }
    }

    auto flushHorizontal = [&](int y, int endX, int run) {
        if (run < minRun) {
            return;
        }
        for (int k = 1; k <= run; ++k) {
            removeMark[std::size_t(y * w + wrap_cell(endX - k, w))] = 1;
        }
    };

    auto flushVertical = [&](int x, int endY, int run) {
        if (run < minRun) {
            return;
        }
        for (int k = 1; k <= run; ++k) {
            removeMark[std::size_t(wrap_cell(endY - k, h) * w + x)] = 1;
        }
    };

    for (int y = 0; y < h; ++y) {
        int run = 0;
        for (int x = 0; x < w + minRun; ++x) {
            const int xx = x % w;
            if (thin[std::size_t(y * w + xx)]) {
                ++run;
            } else {
                flushHorizontal(y, xx, run);
                run = 0;
            }
        }
    }

    for (int x = 0; x < w; ++x) {
        int run = 0;
        for (int y = 0; y < h + minRun; ++y) {
            const int yy = y % h;
            if (thin[std::size_t(yy * w + x)]) {
                ++run;
            } else {
                flushVertical(x, yy, run);
                run = 0;
            }
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = cell_index(x, y, w);
            if (!removeMark[std::size_t(idx)]) {
                continue;
            }

            int sumH = 0;
            int sumM = 0;
            int sumT = 0;
            int count = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int ni = cell_index(wrap_cell(x + dx, w), wrap_cell(y + dy, h), w);
                    if (removeMark[std::size_t(ni)]) {
                        continue;
                    }
                    const std::size_t s = std::size_t(ni) * 4;
                    sumH += td.rgba[s + 0];
                    sumM += td.rgba[s + 1];
                    sumT += td.rgba[s + 2];
                    ++count;
                }
            }

            if (count > 0) {
                const std::size_t s = std::size_t(idx) * 4;
                td.rgba[s + 0] = std::uint8_t(std::max<int>(int(seaLevel8) + 1, (sumH + count / 2) / count));
                td.rgba[s + 1] = std::uint8_t(std::clamp((sumM + count / 2) / count, 0, 255));
                td.rgba[s + 2] = std::uint8_t(std::clamp((sumT + count / 2) / count, 0, 255));
            }
            riverMask[std::size_t(idx)] = 0;
        }
    }
}

void generate_river_data(TerrainData& td, const LayerParameters& params) {
    const int w = td.width;
    const int h = td.height;
    const int n = w * h;
    const std::uint8_t seaLevel8 = sea_level_byte(params.seaLevel);

    td.riverData.assign(std::size_t(n), 0);
    if (n <= 0 || td.rgba.size() < std::size_t(n) * 4) {
        return;
    }

    std::vector<std::uint8_t> heightBytes(static_cast<std::size_t>(n));
    std::vector<std::uint8_t> moistureBytes(static_cast<std::size_t>(n));
    std::vector<std::uint8_t> temperatureBytes(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const std::size_t s = std::size_t(i) * 4;
        heightBytes[std::size_t(i)] = td.rgba[s + 0];
        moistureBytes[std::size_t(i)] = td.rgba[s + 1];
        temperatureBytes[std::size_t(i)] = td.rgba[s + 2];
    }

    std::vector<std::uint8_t> biome(std::size_t(n), 255);
    for (int i = 0; i < n; ++i) {
        if (heightBytes[std::size_t(i)] <= seaLevel8) {
            continue;
        }
        biome[std::size_t(i)] = std::uint8_t(
            std::min(1, int(temperatureBytes[std::size_t(i)]) / 128) * 3
            + std::min(2, int(moistureBytes[std::size_t(i)]) / 86));
    }

    std::vector<std::uint16_t> edgeDist(std::size_t(n), kRiverDistInf);
    std::vector<int> edgeQueue;
    edgeQueue.reserve(std::size_t(n) / 8);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = cell_index(x, y, w);
            const std::uint8_t b = biome[std::size_t(idx)];
            if (b == 255) {
                continue;
            }
            for (const auto& d : kRiverDirs) {
                const int ni = cell_index(wrap_cell(x + d[0], w), wrap_cell(y + d[1], h), w);
                if (biome[std::size_t(ni)] != b) {
                    edgeDist[std::size_t(idx)] = 0;
                    edgeQueue.push_back(idx);
                    break;
                }
            }
        }
    }
    if (edgeQueue.empty()) {
        return;
    }

    std::size_t head = 0;
    while (head < edgeQueue.size()) {
        const int idx = edgeQueue[head++];
        const std::uint16_t d = edgeDist[std::size_t(idx)];
        if (d >= 15u) {
            continue;
        }
        const int bx = idx % w;
        const int by = idx / w;
        for (const auto& dir : kRiverDirs) {
            const int ni = cell_index(wrap_cell(bx + dir[0], w), wrap_cell(by + dir[1], h), w);
            if (edgeDist[std::size_t(ni)] > std::uint16_t(d + 1u)
                && biome[std::size_t(ni)] != 255) {
                edgeDist[std::size_t(ni)] = std::uint16_t(d + 1u);
                edgeQueue.push_back(ni);
            }
        }
    }

    std::vector<std::uint16_t> waterDist(std::size_t(n), kRiverDistInf);
    std::vector<int> waterQueue;
    waterQueue.reserve(std::size_t(n) / 4);
    for (int i = 0; i < n; ++i) {
        if (heightBytes[std::size_t(i)] <= seaLevel8) {
            waterDist[std::size_t(i)] = 0;
            waterQueue.push_back(i);
        }
    }
    if (waterQueue.empty()) {
        return;
    }

    head = 0;
    while (head < waterQueue.size()) {
        const int idx = waterQueue[head++];
        const std::uint16_t d = waterDist[std::size_t(idx)];
        const int bx = idx % w;
        const int by = idx / w;
        for (const auto& dir : kRiverDirs) {
            const int ni = cell_index(wrap_cell(bx + dir[0], w), wrap_cell(by + dir[1], h), w);
            if (waterDist[std::size_t(ni)] > std::uint16_t(d + 1u)) {
                waterDist[std::size_t(ni)] = std::uint16_t(d + 1u);
                waterQueue.push_back(ni);
            }
        }
    }

    std::vector<RiverCandidate> candidates;
    candidates.reserve(std::size_t(n) / 32);
    for (int i = 0; i < n; ++i) {
        if (heightBytes[std::size_t(i)] > seaLevel8
            && edgeDist[std::size_t(i)] <= 2u
            && waterDist[std::size_t(i)] > 4u) {
            candidates.push_back({i, waterDist[std::size_t(i)]});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const RiverCandidate& a, const RiverCandidate& b) {
                  return a.wd > b.wd;
              });

    std::vector<std::uint8_t> taken(std::size_t(n), 0);
    auto markTaken = [&](int idx, int radius) {
        const int sx = idx % w;
        const int sy = idx / w;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx * dx + dy * dy > radius * radius) {
                    continue;
                }
                taken[std::size_t(cell_index(wrap_cell(sx + dx, w), wrap_cell(sy + dy, h), w))] = 1;
            }
        }
    };

    std::vector<int> sources;
    sources.reserve(candidates.size());
    constexpr int kMinSpacing = 12;
    for (const RiverCandidate& c : candidates) {
        if (taken[std::size_t(c.idx)]) {
            continue;
        }
        sources.push_back(c.idx);
        markTaken(c.idx, kMinSpacing);
    }

    RiverTraceScratch scratch;
    scratch.init(std::size_t(n));
    for (int src : sources) {
        const std::vector<std::pair<int, int>> raw =
            trace_river_to_water(src, edgeDist, waterDist, heightBytes,
                                 td.riverData, seaLevel8, w, h, scratch);
        if (raw.size() < 15) {
            continue;
        }
        stamp_river_path(raw, heightBytes, seaLevel8, td.riverData, w, h, waterDist);
    }

    continue_dead_end_rivers(td.riverData, edgeDist, waterDist, heightBytes,
                             seaLevel8, w, h, scratch);
    remove_straight_river_runs(td.riverData, td, seaLevel8);

    const std::uint8_t carveH = std::uint8_t(std::max(1, int(seaLevel8) - 8));
    for (int i = 0; i < n; ++i) {
        const std::size_t s = std::size_t(i) * 4;
        if (td.riverData[std::size_t(i)] > 0 && td.rgba[s + 0] > seaLevel8) {
            td.rgba[s + 0] = std::min(td.rgba[s + 0], carveH);
        }
    }

    for (int i = 0; i < n; ++i) {
        const std::size_t s = std::size_t(i) * 4;
        td.rgba[s + 3] = td.rgba[s + 0] < seaLevel8 ? 0 : 255;
    }
}

} // namespace

TerrainData generate_terrain(int w, int h, const LayerParameters& params) {
    TerrainData td;
    td.width = w; td.height = h;
    td.rgba.assign(std::size_t(w) * h * 4, 0);
    td.riverData.assign(std::size_t(w) * h, 0);

    GLuint prog = gl_link(kVS, kFS);
    if (!prog) {
        std::fprintf(stderr, "[macro] terrain shader failed; falling back to flat ground\n");
        for (std::size_t i = 0; i < td.rgba.size(); i += 4) {
            td.rgba[i + 0] = 128; td.rgba[i + 1] = 128; td.rgba[i + 2] = 128; td.rgba[i + 3] = 255;
        }
        td.texture = gl_make_texture_rgba8(w, h, td.rgba.data(), GL_LINEAR, GL_LINEAR, GL_REPEAT);
        td.riverTexture = gl_make_texture_r8(w, h, td.riverData.data(), GL_LINEAR, GL_REPEAT);
        return td;
    }

    FBO fbo; fbo.create_rgba8(w, h, GL_NEAREST);
    fbo.bind();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glUniform1f(glGetUniformLocation(prog, "u_seed"), params.seed);
    glUniform1f(glGetUniformLocation(prog, "u_seaLevel"), params.seaLevel);
    glUniform1f(glGetUniformLocation(prog, "u_heightScale"), params.heightScale);
    glUniform1f(glGetUniformLocation(prog, "u_moistureScale"), params.moistureScale);
    glUniform1f(glGetUniformLocation(prog, "u_continentScale"), params.continentScale);
    glUniform1f(glGetUniformLocation(prog, "u_continentIntensity"), params.continentIntensity);
    glUniform1f(glGetUniformLocation(prog, "u_ridgeIntensity"), params.ridgeIntensity);
    glUniform1f(glGetUniformLocation(prog, "u_domainWarp"), params.domainWarp);
    glUniform1f(glGetUniformLocation(prog, "u_temperatureVariation"), params.temperatureVariation);

    FullscreenQuad q; q.create();
    q.draw();
    glFinish();
    gl_read_pixels_rgba8(w, h, td.rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    generate_river_data(td, params);
    glBindTexture(GL_TEXTURE_2D, fbo.color);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, td.rgba.data());
    td.riverTexture = gl_make_texture_r8(w, h, td.riverData.data(), GL_LINEAR, GL_REPEAT);

    q.destroy();
    glDeleteProgram(prog);

    // Keep the FBO color texture as our master.
    td.texture = fbo.color;
    fbo.color = 0;
    fbo.destroy();
    glBindTexture(GL_TEXTURE_2D, td.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return td;
}

void destroy_terrain(TerrainData& t) {
    if (t.texture) { glDeleteTextures(1, &t.texture); t.texture = 0; }
    if (t.riverTexture) { glDeleteTextures(1, &t.riverTexture); t.riverTexture = 0; }
    t.rgba.clear();
    t.riverData.clear();
    t.width = t.height = 0;
}

} // namespace sm

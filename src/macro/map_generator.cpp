#include "macro/map_generator.h"
#include "macro/biomes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace sm {

// ---- Climate master synth (CPU) ----
//
// The master climate texture -- height (R), moisture (G), temperature (B) and
// land mask (A) -- is generated on the CPU. It was formerly a GL FBO fragment
// pass read back into td.rgba; the CPU port removes the GPU->CPU readback stall
// and the OpenGL dependency ahead of the Vulkan cutover while producing the
// same periodic-Perlin / fBM field (a faithful port of `src/webgl/shaders.ts`).
// Periodic Perlin keeps the master seamless on the toroidal world.

namespace {

// Ken Perlin's classic 256-entry permutation table, doubled to 512 to avoid a
// modulo on indices.
constexpr int kPerm[512] = {
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
};

inline float pn_fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float pn_mod(float x, float y) { return x - y * std::floor(x / y); }
inline float pn_fract(float x) { return x - std::floor(x); }
inline float pn_mix(float a, float b, float t) { return a + t * (b - a); }

inline float pn_grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0f * v : 2.0f * v);
}

// Periodic Perlin noise -- lattice indices wrap modulo `period` so the field
// repeats every `period` units, tiling seamlessly across uv[0,1].
inline float periodic_noise(float pX, float pY, float period, float seed) {
    pX += seed;
    pY += seed;
    const float px = pn_mod(pX, period);
    const float py = pn_mod(pY, period);
    const int xi  = int(std::floor(px)) & 255;
    const int yi  = int(std::floor(py)) & 255;
    const int xi1 = int(pn_mod(std::floor(px) + 1.0f, period)) & 255;
    const int yi1 = int(pn_mod(std::floor(py) + 1.0f, period)) & 255;
    const float xf = pn_fract(px);
    const float yf = pn_fract(py);
    const float u  = pn_fade(xf);
    const float v  = pn_fade(yf);
    const int aa = kPerm[kPerm[xi]  + yi];
    const int ab = kPerm[kPerm[xi]  + yi1];
    const int ba = kPerm[kPerm[xi1] + yi];
    const int bb = kPerm[kPerm[xi1] + yi1];
    const float x1 = pn_mix(pn_grad(aa, xf, yf),        pn_grad(ba, xf - 1.0f, yf),        u);
    const float x2 = pn_mix(pn_grad(ab, xf, yf - 1.0f), pn_grad(bb, xf - 1.0f, yf - 1.0f), u);
    return pn_mix(x1, x2, v);
}

// fBM over periodic Perlin -- same octave / persistence / period semantics as
// the former shader (capped at 8 octaves).
inline float terrain_fbm(float pX, float pY, int oct, float persistence,
                         float period, float seed) {
    float value = 0.0f, amp = 1.0f, freq = 1.0f, total = 0.0f;
    for (int i = 0; i < 8; ++i) {
        if (i >= oct) break;
        value += amp * periodic_noise(pX * freq, pY * freq,
                                      period * freq, seed + float(i) * 100.0f);
        total += amp;
        amp   *= persistence;
        freq  *= 2.0f;
    }
    return value / total;
}

inline std::uint8_t to_unorm8(float v) {
    return std::uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// Fill td.rgba with the climate master: height (R), moisture (G),
// temperature (B) and land mask (A). Faithful CPU port of the former GL synth.
void synth_master(TerrainData& td, const LayerParameters& p) {
    const int w = td.width, h = td.height;
    const float seed      = p.seed;
    const int   heightOct = int(p.heightOctaves);
    const int   moistOct  = int(p.moistureOctaves);
    const float cScale    = std::max(0.001f, p.continentScale);
    for (int y = 0; y < h; ++y) {
        const float uy = (float(y) + 0.5f) / float(h);
        for (int x = 0; x < w; ++x) {
            const float ux   = (float(x) + 0.5f) / float(w);
            const float posX = ux * 8.0f;
            const float posY = uy * 8.0f;

            // Domain warp (period 8 -> tiles cleanly).
            const float warpX = terrain_fbm(posX, posY, 3, 0.5f, 8.0f, seed + 50.0f);
            const float warpY = terrain_fbm(posX + 5.2f, posY + 1.3f, 3, 0.5f, 8.0f, seed + 60.0f);
            const float qx = warpX * p.domainWarp;
            const float qy = warpY * p.domainWarp;

            // Base height.
            float noiseHeight = terrain_fbm(posX + qx, posY + qy, heightOct, 0.5f, 8.0f, seed) * 0.5f + 0.5f;

            // Continental structure (low frequency).
            const float continentBias = terrain_fbm(posX * cScale, posY * cScale, 2, 0.5f, 8.0f * cScale, seed + 700.0f);
            noiseHeight += continentBias * p.continentIntensity;

            // Mountain ridges: 1-abs(noise) makes connected chains.
            const float ridgeBase = terrain_fbm(posX * 0.7f, posY * 0.7f, 3, 0.55f, 5.6f, seed + 800.0f);
            const float ridge = std::pow(1.0f - std::fabs(ridgeBase), 3.0f) * p.ridgeIntensity;
            noiseHeight += ridge;
            noiseHeight = std::clamp(noiseHeight, 0.0f, 1.0f);
            noiseHeight = std::pow(noiseHeight, p.heightScale);

            // Moisture (period 4 -> tiles twice).
            float noiseMoist = terrain_fbm(posX + qx * 0.5f, posY + qy * 0.5f, moistOct, 0.5f, 4.0f, seed + 200.0f) * 0.5f + 0.5f;
            noiseMoist = std::pow(noiseMoist, p.moistureScale);

            // Temperature: latitude-driven with a noise contribution.
            const float latitude  = 1.0f - std::fabs(uy - 0.5f) * 2.0f;
            const float noiseTemp = terrain_fbm(posX, posY, 3, 0.5f, 4.0f, seed + 300.0f) * 0.5f + 0.5f;
            float temp01 = latitude * (1.0f - p.temperatureVariation) + noiseTemp * p.temperatureVariation;
            temp01 = std::clamp(temp01, 0.0f, 1.0f);

            const std::size_t s = (std::size_t(y) * w + x) * 4;
            td.rgba[s + 0] = to_unorm8(noiseHeight);
            td.rgba[s + 1] = to_unorm8(noiseMoist);
            td.rgba[s + 2] = to_unorm8(temp01);
            td.rgba[s + 3] = noiseHeight < p.seaLevel ? std::uint8_t(0) : std::uint8_t(255);
        }
    }
}

} // namespace

namespace {

constexpr std::uint16_t kRiverDistInf = 65535u;
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

// River meander noise — a low-frequency, seed-stable value-noise added to the
// river trace cost. It bends least-cost paths into natural curves instead of
// long axis-aligned Manhattan runs, while the small amplitude keeps rivers
// hugging the Voronoi biome edge they follow.
constexpr int kRiverMeanderAmp = 7;      // max extra trace cost from meander
constexpr int kRiverMeanderCoarse = 22;  // broad meander lattice spacing (cells)
constexpr int kRiverMeanderFine = 8;     // fine wiggle lattice spacing (cells)

// Gentle downhill bias. An uphill step adds (rise >> kRiverClimbShift) to the
// trace cost; downhill/flat steps pay nothing extra. This curves rivers off
// ridges and down slopes toward the sea instead of letting them march straight
// across highland plateaus. Deliberately small next to the ed*ed biome-edge
// term so the Voronoi-edge routing rivers follow still dominates the path.
constexpr int kRiverClimbShift = 1;

inline std::uint32_t river_hash(int x, int y, std::uint32_t seed) {
    std::uint32_t hsh = seed * 374761393u
        + static_cast<std::uint32_t>(x) * 668265263u
        + static_cast<std::uint32_t>(y) * 2246822519u;
    hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
    return hsh ^ (hsh >> 16);
}

inline float river_lattice(int gx, int gy, int periodX, int periodY, std::uint32_t seed) {
    const int wx = wrap_cell(gx, periodX);
    const int wy = wrap_cell(gy, periodY);
    return float(river_hash(wx, wy, seed) & 0xffffu) * (1.0f / 65535.0f);
}

// Toroidal bilinear value-noise at cell (x,y); `cell` is the lattice spacing.
inline float river_meander_noise(int x, int y, int w, int h, int cell, std::uint32_t seed) {
    const int periodX = std::max(1, w / cell);
    const int periodY = std::max(1, h / cell);
    const float fx = float(x) / float(cell);
    const float fy = float(y) / float(cell);
    const int x0 = int(std::floor(fx));
    const int y0 = int(std::floor(fy));
    const float tx = fx - float(x0);
    const float ty = fy - float(y0);
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float v00 = river_lattice(x0,     y0,     periodX, periodY, seed);
    const float v10 = river_lattice(x0 + 1, y0,     periodX, periodY, seed);
    const float v01 = river_lattice(x0,     y0 + 1, periodX, periodY, seed);
    const float v11 = river_lattice(x0 + 1, y0 + 1, periodX, periodY, seed);
    const float a = v00 + (v10 - v00) * sx;
    const float b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sy;
}

struct RiverCandidate {
    int idx = 0;
    std::uint16_t wd = 0;
};

// A* over the river-cost field uses a binary min-heap keyed on
// f = g + waterDist. waterDist (BFS step-distance to the nearest sea cell) is a
// consistent heuristic because every step costs >= 1, so the first pop of a
// node is its optimal cost and lazy deletion is valid. The previous queue was a
// fixed 4096-bucket Dial queue; any river whose accumulated cost exceeded that
// ceiling saturated into the last bucket and was popped LIFO, collapsing the
// search into a depth-first walk -- the source of the long, unnatural,
// ridge-crossing rivers. A heap has no ceiling. Ties break on cell index so the
// river layout is bit-identical across STL implementations (MSVC vs libc++).
struct RiverHeapEntry {
    int f = 0;
    int g = 0;
    int idx = 0;
};

struct RiverHeapWorse {
    bool operator()(const RiverHeapEntry& a, const RiverHeapEntry& b) const {
        if (a.f != b.f) {
            return a.f > b.f;   // higher f = lower priority (sinks in the heap)
        }
        return a.idx > b.idx;   // deterministic, cross-platform tie-break
    }
};

struct RiverTraceScratch {
    std::vector<int> gScore;
    std::vector<int> parent;
    std::vector<std::uint32_t> tag;
    std::vector<RiverHeapEntry> heap;   // reused binary-heap storage
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
        heap.clear();
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

    void push(int idx, int g, int f) {
        heap.push_back(RiverHeapEntry{f, g, idx});
        std::push_heap(heap.begin(), heap.end(), RiverHeapWorse{});
    }

    bool pop(RiverHeapEntry& out) {
        if (heap.empty()) {
            return false;
        }
        std::pop_heap(heap.begin(), heap.end(), RiverHeapWorse{});
        out = heap.back();
        heap.pop_back();
        return true;
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
    const std::vector<std::uint8_t>& meander,
    RiverTraceScratch& scratch) {

    scratch.begin();
    scratch.set(source, 0, -1);
    scratch.push(source, 0, int(waterDist[std::size_t(source)]));

    int explored = 0;
    RiverHeapEntry top;
    while (explored < kRiverExploreCap && scratch.pop(top)) {
        const int cur = top.idx;
        // Lazy deletion: a stale heap entry (a cheaper path to `cur` was found
        // after this one was pushed) no longer matches the best score -- skip it.
        if (top.g != scratch.score(cur)) {
            continue;
        }
        ++explored;

        const bool done = height[std::size_t(cur)] <= seaLevel8
            || (cur != source && riverMask[std::size_t(cur)] > 0);
        if (done) {
            return build_river_path(source, cur, scratch, w);
        }

        const int cx = cur % w;
        const int cy = cur / w;
        const int curH = int(height[std::size_t(cur)]);
        for (const auto& d : kRiverDirs) {
            const int nx = wrap_cell(cx + d[0], w);
            const int ny = wrap_cell(cy + d[1], h);
            const int ni = cell_index(nx, ny, w);
            const int ed = std::min<int>(edgeDist[std::size_t(ni)], 15);
            const int nH = int(height[std::size_t(ni)]);
            const int climb = nH > curH ? ((nH - curH) >> kRiverClimbShift) : 0;
            const int cost = 1 + ed * ed + (nH >> 5)
                + int(meander[std::size_t(ni)]) + climb;
            const int ng = top.g + cost;
            if (ng >= scratch.score(ni)) {
                continue;
            }

            scratch.set(ni, ng, cur);
            scratch.push(ni, ng, ng + int(waterDist[std::size_t(ni)]));
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
                             const std::vector<std::uint8_t>& meander,
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
                             riverMask, seaLevel8, w, h, meander, scratch);

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
                              const std::vector<std::uint8_t>& meander,
                              RiverTraceScratch& scratch) {
    for (int pass = 0; pass < 5; ++pass) {
        const std::vector<int> tips = find_river_tips(riverMask, height, seaLevel8, w, h);
        int resolved = 0;
        for (int tipIdx : tips) {
            if (continue_river_from_tip(tipIdx, riverMask, edgeDist, waterDist,
                                        height, seaLevel8, w, h, meander, scratch)) {
                ++resolved;
            }
        }
        if (resolved == 0) {
            break;
        }
    }
}

} // namespace  (anonymous river/terrain helpers end here)

// Second CPU synth pass. Reads td.rgba (height/moisture/temperature), traces
// least-cost rivers hugging climate-biome edges toward the nearest sea, stamps
// them into td.riverData, and carves river cells below sea level so they
// classify as Biome::Water. Moved OUT of the anonymous namespace so the river
// generation test suite can drive it on a controlled synthetic TerrainData; the
// helpers above keep internal linkage and stay visible for the rest of this TU.
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
        // The ONE climate classifier (biomes.h biome_from_climate): the 3x3
        // enum is row-major, so the Biome id IS row*3+col. The old inline
        // byte formula (min(1,t/128)*3 + min(2,m/86)) collapsed temperature
        // to two rows — Desert/Steppe/Tropics were unreachable, so river
        // banks traced against a 2x3 world.
        biome[std::size_t(i)] = std::uint8_t(biome_from_climate(
            float(temperatureBytes[std::size_t(i)]) / 255.0f,
            float(moistureBytes[std::size_t(i)]) / 255.0f));
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
                  // Longest-first (farthest from sea), with an index tie-break so
                  // the ordering is a total order -- identical across STL impls.
                  if (a.wd != b.wd) {
                      return a.wd > b.wd;
                  }
                  return a.idx < b.idx;
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

    std::vector<std::uint8_t> meander(std::size_t(n), 0);
    {
        const std::uint32_t mseed =
            static_cast<std::uint32_t>(params.seed) * 2654435761u + 1u;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float lo = river_meander_noise(x, y, w, h, kRiverMeanderCoarse, mseed);
                const float hi = river_meander_noise(x, y, w, h, kRiverMeanderFine,
                                                     mseed ^ 0x9e3779b9u);
                const float v = 0.65f * lo + 0.35f * hi;
                meander[std::size_t(cell_index(x, y, w))] = std::uint8_t(
                    std::clamp(int(v * float(kRiverMeanderAmp) + 0.5f), 0, kRiverMeanderAmp));
            }
        }
    }

    RiverTraceScratch scratch;
    scratch.init(std::size_t(n));
    for (int src : sources) {
        const std::vector<std::pair<int, int>> raw =
            trace_river_to_water(src, edgeDist, waterDist, heightBytes,
                                 td.riverData, seaLevel8, w, h, meander, scratch);
        if (raw.size() < 15) {
            continue;
        }
        stamp_river_path(raw, heightBytes, seaLevel8, td.riverData, w, h, waterDist);
    }

    continue_dead_end_rivers(td.riverData, edgeDist, waterDist, heightBytes,
                             seaLevel8, w, h, meander, scratch);

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

TerrainData generate_terrain(int w, int h, const LayerParameters& params) {
    TerrainData td;
    td.width = w; td.height = h;
    td.rgba.assign(std::size_t(w) * h * 4, 0);
    td.riverData.assign(std::size_t(w) * h, 0);

    // Climate master (height/moisture/temperature/mask) -- CPU synth, no GL.
    synth_master(td, params);

    // Rivers trace over the height/mask channels (already CPU).
    generate_river_data(td, params);

    return td;
}

void destroy_terrain(TerrainData& t) {
    t.rgba.clear();
    t.riverData.clear();
    t.width = t.height = 0;
}

} // namespace sm

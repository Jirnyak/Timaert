#include "sub/gens/city_generator.h"
#include "sub/base_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sm::sub {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr int kStreetWidth = 1;
constexpr int kMaxGateAngles = 16;

struct CityPoint { float x, y; };
struct CityDir { float angle; int tx, ty; };
struct CityNode { float x, y; bool isMain; };
struct CityEdge { int p1, p2; };
struct CityHouse { int x, y, w, h; float rotation; };
struct CityField { float x, y, w, h, rotation; };

struct CityWall {
    std::vector<CityPoint> nodes;
    float avgRadius = 0.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float gateHalfArc = 0.0f;
    float gateAngles[kMaxGateAngles]{};
    int gateCount = 0;
};

struct GrowTip {
    float x, y, angle;
    int depth;
    int energy;
};

struct CityState {
    Rng rng;
    std::uint32_t seed;
    int centerX;
    int centerY;
    std::vector<CityNode> nodes;
    std::vector<CityEdge> edges;
    std::vector<CityHouse> houses;
    std::vector<CityWall> walls;
    std::vector<CityField> fields;
    std::vector<std::vector<CityPoint>> roadPaths;

    explicit CityState(std::uint32_t s)
        : rng(s), seed(s), centerX(kCellSize / 2), centerY(kCellSize / 2) {
        if (rng.next_f01() > 0.5f) centerX += rng_int(-10, 10);
        if (rng.next_f01() > 0.5f) centerY += rng_int(-10, 10);
        nodes.reserve(256);
        edges.reserve(384);
        houses.reserve(768);
        walls.reserve(4);
        fields.reserve(128);
        roadPaths.reserve(8);
    }

    int rng_int(int lo, int hi) {
        return lo + int(rng.next_u32() % std::uint32_t(hi - lo + 1));
    }

    float rng_float(float lo, float hi) {
        return lo + rng.next_f01() * (hi - lo);
    }
};

static bool in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < kCellSize && y < kCellSize;
}

static void city_set_tile(SubworldMapData& out, int x, int y, std::uint8_t tile) {
    if (!in_bounds(x, y)) return;
    const std::size_t idx = std::size_t(y) * kCellSize + x;
    out.tiles[idx] = tile;
    out.trav[idx] = (tile == TILE_HOUSE || tile == TILE_WALL) ? 0 : 1;
}

static bool city_is_road_feature(std::uint8_t f) {
    return f == FT_Road || f == FT_DirtRoad;
}

static bool city_is_free_for_house(const SubworldMapData& out, int x, int y, int w, int h) {
    if (x < 0 || y < 0 || x + w >= kCellSize || y + h >= kCellSize) return false;
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            if (out.tiles[std::size_t(yy) * kCellSize + xx] != TILE_EMPTY) return false;
        }
    }
    return true;
}

static bool city_is_ground_or_empty(std::uint8_t tile) {
    return tile == TILE_EMPTY || tile == TILE_GRASS || tile == TILE_TREE_DECOR;
}

static bool city_has_urban_neighbor(const SubworldMapData& out, int x, int y, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (!in_bounds(px, py)) continue;
            const auto t = out.tiles[std::size_t(py) * kCellSize + px];
            if (t == TILE_ROAD || t == TILE_HOUSE || t == TILE_SQUARE || t == TILE_WALL) {
                return true;
            }
        }
    }
    return false;
}

static float terrain_noise_local(int x, int y, std::uint32_t seed) {
    std::uint32_t v = (std::uint32_t(x) * 374761393u) ^ (std::uint32_t(y) * 668265263u)
                    ^ (seed * 2246822519u);
    v = (v ^ (v >> 13)) * 1274126177u;
    v ^= v >> 16;
    return float(v) / 4294967295.0f;
}

static float smooth_local_noise(float x, float y, std::uint32_t seed) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = terrain_noise_local(ix,     iy,     seed);
    const float n10 = terrain_noise_local(ix + 1, iy,     seed);
    const float n01 = terrain_noise_local(ix,     iy + 1, seed);
    const float n11 = terrain_noise_local(ix + 1, iy + 1, seed);
    return n00 * (1.0f - sx) * (1.0f - sy)
         + n10 * sx          * (1.0f - sy)
         + n01 * (1.0f - sx) * sy
         + n11 * sx          * sy;
}

static std::vector<CityDir> city_default_dirs(const CityState& cs) {
    std::vector<CityDir> dirs;
    dirs.reserve(4);
    dirs.push_back({0.0f, kCellSize - 1, cs.centerY});
    dirs.push_back({kPi, 0, cs.centerY});
    dirs.push_back({kPi * 0.5f, cs.centerX, kCellSize - 1});
    dirs.push_back({-kPi * 0.5f, cs.centerX, 0});
    return dirs;
}

static std::vector<CityDir> city_dirs_from_neighbors(const CityState& cs,
                                                     const std::uint8_t nbFeature[9]) {
    std::vector<CityDir> dirs;
    dirs.reserve(8);
    for (int d = 0; d < 8; ++d) {
        const int dx = kDirOffsets[d][0];
        const int dy = kDirOffsets[d][1];
        const int idx = (dy + 1) * 3 + (dx + 1);
        if (!city_is_road_feature(nbFeature[idx])) continue;
        const int tx = std::clamp(int(std::lround(float(cs.centerX) + float(dx) * float(kCellSize) * 0.49f)),
                                  1, kCellSize - 2);
        const int ty = std::clamp(int(std::lround(float(cs.centerY) + float(dy) * float(kCellSize) * 0.49f)),
                                  1, kCellSize - 2);
        dirs.push_back({std::atan2(float(dy), float(dx)), tx, ty});
    }
    return dirs.size() < 2 ? city_default_dirs(cs) : dirs;
}

static void city_mark_organic_main_road(SubworldMapData& out, CityState& cs,
                                        int x1, int y1, int x2, int y2,
                                        float baseAngle) {
    const float dx = float(x2 - x1);
    const float dy = float(y2 - y1);
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) return;
    const int steps = int(std::ceil(dist));
    std::vector<CityPoint> centerline;
    centerline.reserve(std::size_t(steps) + 1u);

    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        float x = float(x1) + dx * t;
        float y = float(y1) + dy * t;
        if (t > 0.08f && t < 0.92f) {
            const float waveAmp = 3.5f + cs.rng_float(-1.0f, 1.0f);
            const float perp = baseAngle + kPi * 0.5f;
            const float offset = std::sin(t * dist * 0.015f + float(cs.seed)) * waveAmp;
            x += std::cos(perp) * offset;
            y += std::sin(perp) * offset;
        }
        centerline.push_back({x, y});
        const int ix = int(std::floor(x));
        const int iy = int(std::floor(y));
        for (int yy = -kStreetWidth; yy <= kStreetWidth; ++yy) {
            for (int xx = -kStreetWidth; xx <= kStreetWidth; ++xx) {
                city_set_tile(out, ix + xx, iy + yy, TILE_ROAD);
            }
        }
    }
    cs.roadPaths.push_back(std::move(centerline));
}

static void city_generate_main_roads(SubworldMapData& out, CityState& cs,
                                     const std::uint8_t nbFeature[9]) {
    cs.nodes.push_back({float(cs.centerX), float(cs.centerY), true});
    const auto dirs = city_dirs_from_neighbors(cs, nbFeature);
    for (const auto& d : dirs) {
        const int nodeId = int(cs.nodes.size());
        cs.nodes.push_back({float(d.tx), float(d.ty), true});
        cs.edges.push_back({0, nodeId});
        city_mark_organic_main_road(out, cs, cs.centerX, cs.centerY, d.tx, d.ty, d.angle);
    }
}

static void city_generate_square(SubworldMapData& out, CityState& cs, int population) {
    const int size = std::max(5, std::min(10, 5 + population / 5000));
    const int x0 = cs.centerX - size / 2;
    const int y0 = cs.centerY - size / 2;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            city_set_tile(out, x0 + x, y0 + y, TILE_SQUARE);
        }
    }
}

static void city_place_keep(SubworldMapData& out, CityState& cs, int population) {
    const int baseSize = std::max(6, std::min(16, 4 + population / 1500));
    const int kw = baseSize + cs.rng_int(0, 2);
    const int kh = baseSize + cs.rng_int(0, 2);
    const int kx = cs.centerX - kw / 2;
    const int ky = cs.centerY - kh / 2 - baseSize / 2 - 2;

    for (int y = 0; y < kh; ++y) {
        for (int x = 0; x < kw; ++x) {
            city_set_tile(out, kx + x, ky + y, TILE_HOUSE);
        }
    }
    cs.houses.push_back({kx, ky, kw, kh, cs.rng_float(-0.05f, 0.05f)});
}

static bool city_house_overlaps_road(const SubworldMapData& out, const CityHouse& h) {
    for (int y = h.y; y < h.y + h.h; ++y) {
        for (int x = h.x; x < h.x + h.w; ++x) {
            if (!in_bounds(x, y)) continue;
            if (out.tiles[std::size_t(y) * kCellSize + x] == TILE_ROAD) return true;
        }
    }
    return false;
}

static void city_clear_house_from_grid(SubworldMapData& out, const CityHouse& h) {
    for (int y = h.y; y < h.y + h.h; ++y) {
        for (int x = h.x; x < h.x + h.w; ++x) {
            if (!in_bounds(x, y)) continue;
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] == TILE_HOUSE) {
                out.tiles[idx] = TILE_EMPTY;
                out.trav[idx] = 1;
            }
        }
    }
}

static int city_remove_houses_touching_roads(SubworldMapData& out, CityState& cs) {
    std::size_t write = 0;
    int removed = 0;
    for (std::size_t read = 0; read < cs.houses.size(); ++read) {
        if (city_house_overlaps_road(out, cs.houses[read])) {
            city_clear_house_from_grid(out, cs.houses[read]);
            ++removed;
            continue;
        }
        if (write != read) cs.houses[write] = cs.houses[read];
        ++write;
    }
    cs.houses.resize(write);
    return removed;
}

static int city_mark_street_and_remove_houses(SubworldMapData& out, CityState& cs,
                                              float x1, float y1, float x2, float y2) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) return 0;
    const int steps = int(std::ceil(dist * 2.0f));
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int cx = int(std::floor(x1 + dx * t));
        const int cy = int(std::floor(y1 + dy * t));
        for (int yy = -kStreetWidth; yy <= kStreetWidth; ++yy) {
            for (int xx = -kStreetWidth; xx <= kStreetWidth; ++xx) {
                city_set_tile(out, cx + xx, cy + yy, TILE_ROAD);
            }
        }
    }
    return city_remove_houses_touching_roads(out, cs);
}

static int city_find_nearest_node(const CityState& cs, float x, float y) {
    int bestId = 0;
    float bestD2 = 3.4e38f;
    for (int i = 0; i < int(cs.nodes.size()); ++i) {
        const float dx = cs.nodes[std::size_t(i)].x - x;
        const float dy = cs.nodes[std::size_t(i)].y - y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2 = d2;
            bestId = i;
        }
    }
    return bestId;
}

static int city_pick_tip_mut(CityState& cs, const std::vector<GrowTip>& tips) {
    if (tips.size() <= 1) return 0;
    float total = 0.0f;
    for (const auto& tip : tips) {
        const float dx = tip.x - float(cs.centerX);
        const float dy = tip.y - float(cs.centerY);
        total += 1.0f / (1.0f + std::sqrt(dx * dx + dy * dy) * 0.005f);
    }
    float r = cs.rng.next_f01() * total;
    for (int i = 0; i < int(tips.size()); ++i) {
        const float dx = tips[std::size_t(i)].x - float(cs.centerX);
        const float dy = tips[std::size_t(i)].y - float(cs.centerY);
        r -= 1.0f / (1.0f + std::sqrt(dx * dx + dy * dy) * 0.005f);
        if (r <= 0.0f) return i;
    }
    return int(tips.size()) - 1;
}

static float city_branch_probability(int depth, float centerDist, float maxRadius) {
    const float distFactor = 1.0f - centerDist / maxRadius;
    const float depthPenalty = float(depth) * 0.12f;
    return std::max(0.05f, 0.45f * distFactor - depthPenalty);
}

static const CityEdge* city_choose_house_edge(CityState& cs, const CityEdge* forced) {
    if (forced) return forced;
    if (cs.edges.empty()) return nullptr;
    for (int attempt = 0; attempt < 24; ++attempt) {
        const auto& e = cs.edges[std::size_t(cs.rng_int(0, int(cs.edges.size()) - 1))];
        if (!cs.nodes[std::size_t(e.p1)].isMain || !cs.nodes[std::size_t(e.p2)].isMain) {
            return &e;
        }
    }
    return nullptr;
}

static bool city_try_place_house(SubworldMapData& out, CityState& cs, const CityEdge* forced = nullptr) {
    const CityEdge* selected = city_choose_house_edge(cs, forced);
    if (!selected) return false;
    const auto& n1 = cs.nodes[std::size_t(selected->p1)];
    const auto& n2 = cs.nodes[std::size_t(selected->p2)];
    const float dx = n2.x - n1.x;
    const float dy = n2.y - n1.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.1f) return false;
    const float px = -dy / len;
    const float py =  dx / len;

    for (int attempt = 0; attempt < 60; ++attempt) {
        const float t = cs.rng_float(0.05f, 0.95f);
        const float sx = n1.x + dx * t;
        const float sy = n1.y + dy * t;
        const float side = cs.rng.next_f01() > 0.5f ? 1.0f : -1.0f;
        const float distance = 2.0f + float(cs.rng_int(1, 4));
        const int hx = int(std::floor(sx + px * distance * side));
        const int hy = int(std::floor(sy + py * distance * side));
        const int w = cs.rng_int(3, 5);
        const int h = cs.rng_int(3, 5);
        if (hx < 5 || hy < 5 || hx + w >= kCellSize - 5 || hy + h >= kCellSize - 5) continue;
        if (!city_is_free_for_house(out, hx, hy, w, h)) continue;

        for (int y = hy; y < hy + h; ++y) {
            for (int x = hx; x < hx + w; ++x) {
                city_set_tile(out, x, y, TILE_HOUSE);
            }
        }
        cs.houses.push_back({hx, hy, w, h, std::atan2(dy, dx) + cs.rng_float(-0.3f, 0.3f)});
        return true;
    }
    return false;
}

static bool city_edge_exists(const CityState& cs, int a, int b) {
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    for (const auto& e : cs.edges) {
        if (std::min(e.p1, e.p2) == lo && std::max(e.p1, e.p2) == hi) return true;
    }
    return false;
}

static void city_connect_nearby_nodes(SubworldMapData& out, CityState& cs) {
    const int maxLinks = std::min(30, int(cs.nodes.size()) * 12 / 100);
    int added = 0;
    for (int i = 5; i < int(cs.nodes.size()) && added < maxLinks; ++i) {
        if (cs.nodes[std::size_t(i)].isMain) continue;
        for (int j = i + 1; j < int(cs.nodes.size()) && added < maxLinks; ++j) {
            if (cs.nodes[std::size_t(j)].isMain) continue;
            const float dx = cs.nodes[std::size_t(i)].x - cs.nodes[std::size_t(j)].x;
            const float dy = cs.nodes[std::size_t(i)].y - cs.nodes[std::size_t(j)].y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > 30.0f * 30.0f || d2 < 10.0f * 10.0f) continue;
            if (cs.rng.next_f01() > 0.2f) continue;
            if (city_edge_exists(cs, i, j)) continue;

            cs.edges.push_back({i, j});
            const auto edge = cs.edges.back();
            const int removed = city_mark_street_and_remove_houses(
                out, cs, cs.nodes[std::size_t(i)].x, cs.nodes[std::size_t(i)].y,
                cs.nodes[std::size_t(j)].x, cs.nodes[std::size_t(j)].y);
            for (int k = 0; k < removed + 2; ++k) {
                city_try_place_house(out, cs, &edge);
            }
            ++added;
        }
    }
}

static void city_seed_tip(std::vector<GrowTip>& tips,
                          float x, float y, float angle, int depth, int energy) {
    tips.push_back({x, y, angle, depth, energy});
}

static void city_grow_mycelium(SubworldMapData& out, CityState& cs, int population) {
    const int targetHouses = std::max(20, int(std::floor(std::pow(float(population), 0.8f))));
    const float maxRadius = float(kCellSize) * 0.38f;
    const float maxRadiusSq = maxRadius * maxRadius;

    std::vector<GrowTip> tips;
    std::vector<GrowTip> pendingTips;
    std::size_t pendingRead = 0;
    tips.reserve(32);
    pendingTips.reserve(64);

    for (int i = 1; i < int(cs.nodes.size()); ++i) {
        const auto& n = cs.nodes[std::size_t(i)];
        if (!n.isMain) continue;
        const float ang = std::atan2(n.y - float(cs.centerY), n.x - float(cs.centerX));
        const float dist = 25.0f + cs.rng_float(0.0f, 15.0f);
        const float sx = float(cs.centerX) + std::cos(ang) * dist;
        const float sy = float(cs.centerY) + std::sin(ang) * dist;
        city_seed_tip(tips, sx, sy, ang + (cs.rng.next_f01() > 0.5f ? 0.7f : -0.7f),
                      0, 8 + cs.rng_int(0, 4));
        city_seed_tip(tips, sx, sy, ang + (cs.rng.next_f01() > 0.5f ? -0.7f : 0.7f),
                      0, 8 + cs.rng_int(0, 4));

        const float dist2 = 50.0f + cs.rng_float(0.0f, 30.0f);
        city_seed_tip(tips,
                      float(cs.centerX) + std::cos(ang) * dist2,
                      float(cs.centerY) + std::sin(ang) * dist2,
                      ang + cs.rng_float(-0.5f, 0.5f),
                      1, 6 + cs.rng_int(0, 3));
    }

    const int extraTips = std::max(6, population / 1000);
    for (int i = 0; i < extraTips; ++i) {
        const float ang = cs.rng_float(0.0f, kTwoPi);
        const float dist = cs.rng_float(15.0f, 45.0f);
        city_seed_tip(tips,
                      float(cs.centerX) + std::cos(ang) * dist,
                      float(cs.centerY) + std::sin(ang) * dist,
                      ang + cs.rng_float(-0.8f, 0.8f),
                      0, 6 + cs.rng_int(0, 5));
    }

    int housesPlaced = int(cs.houses.size());
    int safety = 0;
    const int maxIterations = targetHouses * 50;
    while (!tips.empty() && housesPlaced < targetHouses && safety < maxIterations) {
        ++safety;
        const int tipIdx = city_pick_tip_mut(cs, tips);
        GrowTip& tip = tips[std::size_t(tipIdx)];
        tip.angle += cs.rng_float(-0.35f, 0.35f);
        const float stepLength = cs.rng_float(10.0f, 20.0f);
        const float nx = tip.x + std::cos(tip.angle) * stepLength;
        const float ny = tip.y + std::sin(tip.angle) * stepLength;

        constexpr float kMargin = 20.0f;
        if (nx < kMargin || ny < kMargin
            || nx >= float(kCellSize) - kMargin
            || ny >= float(kCellSize) - kMargin) {
            tips[std::size_t(tipIdx)] = tips.back();
            tips.pop_back();
            continue;
        }

        const float cdx = nx - float(cs.centerX);
        const float cdy = ny - float(cs.centerY);
        const float centerDistSq = cdx * cdx + cdy * cdy;
        if (centerDistSq > maxRadiusSq) {
            tips[std::size_t(tipIdx)] = tips.back();
            tips.pop_back();
            continue;
        }

        bool tooClose = false;
        constexpr float kMinSpacingSq = 8.0f * 8.0f;
        for (const auto& node : cs.nodes) {
            if (node.isMain) continue;
            const float ndx = node.x - nx;
            const float ndy = node.y - ny;
            if (ndx * ndx + ndy * ndy < kMinSpacingSq) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) {
            tip.angle += cs.rng_float(-0.5f, 0.5f);
            --tip.energy;
            if (tip.energy <= 0) {
                tips[std::size_t(tipIdx)] = tips.back();
                tips.pop_back();
            }
            continue;
        }

        const int parentId = city_find_nearest_node(cs, tip.x, tip.y);
        const int newId = int(cs.nodes.size());
        cs.nodes.push_back({nx, ny, false});
        cs.edges.push_back({parentId, newId});
        const auto edge = cs.edges.back();
        city_mark_street_and_remove_houses(
            out, cs,
            cs.nodes[std::size_t(parentId)].x, cs.nodes[std::size_t(parentId)].y,
            nx, ny);

        const int housesForSegment = cs.rng_int(3, 8);
        for (int h = 0; h < housesForSegment && housesPlaced < targetHouses; ++h) {
            if (city_try_place_house(out, cs, &edge)) ++housesPlaced;
        }
        for (int h = 0; h < 3 && housesPlaced < targetHouses; ++h) {
            if (city_try_place_house(out, cs)) ++housesPlaced;
        }

        tip.x = nx;
        tip.y = ny;
        --tip.energy;

        const float centerDist = std::sqrt(centerDistSq);
        if (tip.depth < 4
            && cs.rng.next_f01() < city_branch_probability(tip.depth, centerDist, maxRadius)) {
            const float branchAngle = tip.angle
                + (cs.rng.next_f01() > 0.5f ? 1.0f : -1.0f)
                * (0.5f + cs.rng_float(0.0f, 0.6f));
            const int energy = std::max(2, tip.energy - 1 + cs.rng_int(-1, 2));
            pendingTips.push_back({nx, ny, branchAngle, tip.depth + 1, energy});
        }

        if (tip.energy <= 0) {
            tips[std::size_t(tipIdx)] = tips.back();
            tips.pop_back();
        }
        while (tips.size() < 8 && pendingRead < pendingTips.size()) {
            tips.push_back(pendingTips[pendingRead++]);
        }
    }

    city_connect_nearby_nodes(out, cs);
}

static void city_count_neighbor_types(const SubworldMapData& out, int x, int y,
                                      int& roadN, int& houseN) {
    roadN = 0;
    houseN = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const auto t = out.tiles[std::size_t(y + dy) * kCellSize + (x + dx)];
            if (t == TILE_ROAD) ++roadN;
            if (t == TILE_HOUSE) ++houseN;
        }
    }
}

static void city_convert_excess_roads(SubworldMapData& out) {
    for (int y = 2; y < kCellSize - 2; ++y) {
        for (int x = 2; x < kCellSize - 2; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] != TILE_ROAD) continue;
            int roadN = 0, houseN = 0;
            city_count_neighbor_types(out, x, y, roadN, houseN);
            if (roadN >= 5 && houseN == 0) {
                city_set_tile(out, x, y, TILE_SQUARE);
            }
        }
    }
}

static float city_compute_house_extent(const CityState& cs) {
    if (cs.houses.empty()) return 60.0f;
    std::vector<float> d2s;
    d2s.reserve(cs.houses.size());
    for (const auto& h : cs.houses) {
        const float dx = float(h.x) - float(cs.centerX);
        const float dy = float(h.y) - float(cs.centerY);
        d2s.push_back(dx * dx + dy * dy);
    }
    const std::size_t nth = std::min(d2s.size() - 1u, std::size_t(float(d2s.size()) * 0.9f));
    std::nth_element(d2s.begin(), d2s.begin() + std::ptrdiff_t(nth), d2s.end());
    return std::sqrt(d2s[nth]);
}

static CityPoint city_center_of_mass(const CityState& cs) {
    if (cs.nodes.empty()) return {float(kCellSize) * 0.5f, float(kCellSize) * 0.5f};
    float sumX = 0.0f;
    float sumY = 0.0f;
    for (const auto& n : cs.nodes) {
        sumX += n.x;
        sumY += n.y;
    }
    const float inv = 1.0f / float(cs.nodes.size());
    return {sumX * inv, sumY * inv};
}

static bool city_segment_intersection(const CityPoint& a1, const CityPoint& a2,
                                      const CityPoint& b1, const CityPoint& b2,
                                      CityPoint& hit) {
    const float dx1 = a2.x - a1.x;
    const float dy1 = a2.y - a1.y;
    const float dx2 = b2.x - b1.x;
    const float dy2 = b2.y - b1.y;
    const float denom = dx1 * dy2 - dy1 * dx2;
    if (std::fabs(denom) < 0.001f) return false;
    const float t = ((b1.x - a1.x) * dy2 - (b1.y - a1.y) * dx2) / denom;
    const float u = ((b1.x - a1.x) * dy1 - (b1.y - a1.y) * dx1) / denom;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) return false;
    hit = {a1.x + t * dx1, a1.y + t * dy1};
    return true;
}

static void city_add_gate(CityWall& wall, float angle) {
    for (int i = 0; i < wall.gateCount; ++i) {
        if (angular_distance(wall.gateAngles[i], angle) < 0.03f) return;
    }
    if (wall.gateCount < kMaxGateAngles) wall.gateAngles[wall.gateCount++] = angle;
}

static void city_find_wall_gates(const CityState& cs, CityWall& wall) {
    const int segments = int(wall.nodes.size());
    for (const auto& path : cs.roadPaths) {
        for (std::size_t p = 0; p + 1 < path.size(); ++p) {
            for (int w = 0; w < segments; ++w) {
                CityPoint hit{};
                if (city_segment_intersection(path[p], path[p + 1],
                                              wall.nodes[std::size_t(w)],
                                              wall.nodes[std::size_t((w + 1) % segments)],
                                              hit)) {
                    city_add_gate(wall, std::atan2(hit.y - wall.centerY, hit.x - wall.centerX));
                }
            }
        }
    }
    for (const auto& e : cs.edges) {
        const CityPoint a{cs.nodes[std::size_t(e.p1)].x, cs.nodes[std::size_t(e.p1)].y};
        const CityPoint b{cs.nodes[std::size_t(e.p2)].x, cs.nodes[std::size_t(e.p2)].y};
        for (int w = 0; w < segments; ++w) {
            CityPoint hit{};
            if (city_segment_intersection(a, b,
                                          wall.nodes[std::size_t(w)],
                                          wall.nodes[std::size_t((w + 1) % segments)],
                                          hit)) {
                city_add_gate(wall, std::atan2(hit.y - wall.centerY, hit.x - wall.centerX));
            }
        }
    }
    if (wall.gateCount == 0) {
        wall.gateAngles[0] = 0.0f;
        wall.gateAngles[1] = kPi;
        wall.gateAngles[2] = kPi * 0.5f;
        wall.gateAngles[3] = -kPi * 0.5f;
        wall.gateCount = 4;
    }
}

static bool city_is_gate_angle(const CityWall& wall, float angle) {
    for (int i = 0; i < wall.gateCount; ++i) {
        if (angular_distance(wall.gateAngles[i], angle) <= wall.gateHalfArc) return true;
    }
    return false;
}

static void city_stamp_wall(SubworldMapData& out, const CityWall& wall) {
    const int segments = int(wall.nodes.size());
    int wallStep = 0;
    for (int i = 0; i < segments; ++i) {
        const CityPoint a = wall.nodes[std::size_t(i)];
        const CityPoint b = wall.nodes[std::size_t((i + 1) % segments)];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const int steps = std::max(1, int(std::ceil(std::sqrt(dx * dx + dy * dy) * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            const float x = a.x + dx * t;
            const float y = a.y + dy * t;
            const float angle = std::atan2(y - wall.centerY, x - wall.centerX);
            if (city_is_gate_angle(wall, angle)) continue;
            const int ix = int(std::floor(x));
            const int iy = int(std::floor(y));
            city_set_tile(out, ix, iy, TILE_WALL);
            if ((wallStep++ & 7) == 0) {
                out.structures.push_back({Structure::Wall, float(ix) + 0.5f, float(iy) + 0.5f, 0.7f, 12.0f});
            }
        }
    }
}

static void city_build_wall(SubworldMapData& out, CityState& cs,
                            float radius, int segments, float roughness) {
    CityWall wall{};
    wall.nodes.reserve(std::size_t(segments));
    const CityPoint center = city_center_of_mass(cs);
    wall.centerX = center.x;
    wall.centerY = center.y;
    const float angleStep = kTwoPi / float(segments);
    const float phase1 = cs.rng_float(0.0f, kTwoPi);
    const float phase2 = cs.rng_float(0.0f, kTwoPi);
    for (int i = 0; i < segments; ++i) {
        const float angle = float(i) * angleStep;
        const float harmonic = std::sin(angle * 3.0f + phase1) * 0.32f
                             + std::sin(angle * 5.0f + phase2) * 0.18f;
        const float radialJitter = cs.rng_float(-1.0f, 1.0f) * radius * roughness * 0.18f;
        const float r = radius + radius * roughness * harmonic + radialJitter;
        wall.nodes.push_back({center.x + std::cos(angle) * r,
                              center.y + std::sin(angle) * r});
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < segments; ++i) {
            const auto previous = wall.nodes[std::size_t((i - 1 + segments) % segments)];
            const auto curr = wall.nodes[std::size_t(i)];
            const auto next = wall.nodes[std::size_t((i + 1) % segments)];
            wall.nodes[std::size_t(i)] = {
                (previous.x + curr.x * 2.0f + next.x) * 0.25f,
                (previous.y + curr.y * 2.0f + next.y) * 0.25f};
        }
    }
    float totalRadius = 0.0f;
    for (const auto& p : wall.nodes) {
        const float dx = p.x - wall.centerX;
        const float dy = p.y - wall.centerY;
        totalRadius += std::sqrt(dx * dx + dy * dy);
    }
    wall.avgRadius = totalRadius / float(std::max(1, segments));
    city_find_wall_gates(cs, wall);
    wall.gateHalfArc = std::max(0.08f, (8.0f + 4.0f * float(kStreetWidth)) / wall.avgRadius);
    city_stamp_wall(out, wall);
    cs.walls.push_back(std::move(wall));
}

static void city_ensure_walls(SubworldMapData& out, CityState& cs, int population) {
    constexpr int thresholds[5] = {0, 2000, 5000, 10000, 20000};
    int targetRings = 1;
    for (int i = 1; i < 5; ++i) {
        if (population >= thresholds[i]) ++targetRings;
    }
    const float houseExtent = city_compute_house_extent(cs);
    while (int(cs.walls.size()) < targetRings) {
        const int ringIndex = int(cs.walls.size());
        const float fraction = float(ringIndex + 1) / float(targetRings);
        const float radius = std::max(25.0f, houseExtent * fraction * 1.1f + float(ringIndex) * 8.0f);
        const int segments = std::max(16, 18 + ringIndex * 6);
        const float roughness = 0.12f + float(ringIndex) * 0.025f;
        city_build_wall(out, cs, radius, segments, roughness);
    }
}

static bool city_inside_wall(const CityWall& wall, float px, float py) {
    const float dx = px - wall.centerX;
    const float dy = py - wall.centerY;
    const float d2 = dx * dx + dy * dy;
    const float inner = wall.avgRadius * 0.72f;
    if (d2 <= inner * inner) return true;
    const float outer = wall.avgRadius * 1.35f;
    if (d2 >= outer * outer) return false;

    bool inside = false;
    const int n = int(wall.nodes.size());
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const auto a = wall.nodes[std::size_t(i)];
        const auto b = wall.nodes[std::size_t(j)];
        if ((a.y > py) != (b.y > py)
            && px < (b.x - a.x) * (py - a.y) / ((b.y - a.y) == 0.0f ? 0.00001f : (b.y - a.y)) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

static bool city_has_nearby_road(const SubworldMapData& out, int x, int y, int radius) {
    for (int dy = -radius; dy <= radius; dy += 3) {
        for (int dx = -radius; dx <= radius; dx += 3) {
            const int px = x + dx;
            const int py = y + dy;
            if (in_bounds(px, py) && out.tiles[std::size_t(py) * kCellSize + px] == TILE_ROAD) {
                return true;
            }
        }
    }
    return false;
}

static void city_infill_houses_inside_walls(SubworldMapData& out, CityState& cs) {
    if (cs.walls.empty()) return;
    const CityWall& outerWall = cs.walls.back();
    constexpr int scanStep = 6;
    for (int sy = 20; sy < kCellSize - 20; sy += scanStep) {
        for (int sx = 20; sx < kCellSize - 20; sx += scanStep) {
            if (!city_inside_wall(outerWall, float(sx) + 0.5f, float(sy) + 0.5f)) continue;
            if (!city_has_nearby_road(out, sx, sy, 15)) continue;
            const int w = cs.rng_int(3, 5);
            const int h = cs.rng_int(3, 5);
            const int hx = sx + cs.rng_int(-2, 2);
            const int hy = sy + cs.rng_int(-2, 2);
            if (hx < 5 || hy < 5 || hx + w >= kCellSize - 5 || hy + h >= kCellSize - 5) continue;
            if (!city_is_free_for_house(out, hx - 1, hy - 1, w + 2, h + 2)) continue;
            for (int y = hy; y < hy + h; ++y) {
                for (int x = hx; x < hx + w; ++x) {
                    city_set_tile(out, x, y, TILE_HOUSE);
                }
            }
            cs.houses.push_back({hx, hy, w, h, cs.rng_float(-0.3f, 0.3f)});
        }
    }
}

static void city_fill_urban_spaces(SubworldMapData& out, const CityState& cs) {
    if (cs.walls.empty()) return;
    const CityWall& outerWall = cs.walls.back();
    for (int y = 2; y < kCellSize - 2; ++y) {
        for (int x = 2; x < kCellSize - 2; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] != TILE_EMPTY) continue;
            if (!city_inside_wall(outerWall, float(x) + 0.5f, float(y) + 0.5f)) continue;
            out.tiles[idx] = city_has_urban_neighbor(out, x, y, 1) ? std::uint8_t(TILE_SQUARE)
                                                                   : std::uint8_t(TILE_GRASS);
            out.trav[idx] = 1;
        }
    }
}

static bool city_place_field_plot(SubworldMapData& out, CityState& cs, const CityWall& outerWall) {
    const float outerLimit = float(kCellSize) * 0.48f;
    const float angle = cs.rng_float(0.0f, kTwoPi);
    const float radius = cs.rng_float(outerWall.avgRadius * 1.05f, outerLimit);
    const float cx = float(cs.centerX) + std::cos(angle) * radius;
    const float cy = float(cs.centerY) + std::sin(angle) * radius;
    const float fw = cs.rng_float(8.0f, 22.0f);
    const float fh = cs.rng_float(6.0f, 18.0f);
    const float rotation = cs.rng_float(-0.7f, 0.7f);
    const int halfDiag = int(std::ceil(std::sqrt(fw * fw + fh * fh) * 0.5f)) + 2;
    const int minX = int(std::floor(cx)) - halfDiag;
    const int maxX = int(std::ceil(cx)) + halfDiag;
    const int minY = int(std::floor(cy)) - halfDiag;
    const int maxY = int(std::ceil(cy)) + halfDiag;
    if (minX < 2 || minY < 2 || maxX >= kCellSize - 2 || maxY >= kCellSize - 2) return false;

    const float co = std::cos(rotation);
    const float si = std::sin(rotation);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float lx = (float(x) - cx) * co + (float(y) - cy) * si;
            const float ly = -(float(x) - cx) * si + (float(y) - cy) * co;
            if (std::fabs(lx) > fw * 0.5f || std::fabs(ly) > fh * 0.5f) continue;
            if (city_inside_wall(outerWall, float(x) + 0.5f, float(y) + 0.5f)) return false;
            const auto tile = out.tiles[std::size_t(y) * kCellSize + x];
            if (!city_is_ground_or_empty(tile)) return false;
        }
    }
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float lx = (float(x) - cx) * co + (float(y) - cy) * si;
            const float ly = -(float(x) - cx) * si + (float(y) - cy) * co;
            if (std::fabs(lx) <= fw * 0.5f && std::fabs(ly) <= fh * 0.5f) {
                city_set_tile(out, x, y, TILE_FIELD);
            }
        }
    }
    cs.fields.push_back({cx, cy, fw, fh, rotation});
    return true;
}

static CityPoint city_find_nearest_main_road_point(const CityState& cs, float fx, float fy) {
    CityPoint best{float(cs.centerX), float(cs.centerY)};
    float bestD2 = 3.4e38f;
    constexpr int sampleStep = 8;
    for (const auto& path : cs.roadPaths) {
        for (std::size_t i = 0; i < path.size(); i += sampleStep) {
            const float dx = path[i].x - fx;
            const float dy = path[i].y - fy;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestD2) {
                bestD2 = d2;
                best = path[i];
            }
        }
    }
    return best;
}

static void city_mark_line(SubworldMapData& out, CityState& cs,
                           float x1, float y1, float x2, float y2,
                           std::uint8_t value, int width) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) return;
    const int steps = int(std::ceil(dist * 2.0f));
    const int half = width / 2;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        float x = x1 + dx * t;
        float y = y1 + dy * t;
        if (i > 0 && i < steps) {
            x += cs.rng_float(-0.8f, 0.8f);
            y += cs.rng_float(-0.8f, 0.8f);
        }
        const int ix = int(std::floor(x));
        const int iy = int(std::floor(y));
        for (int yy = -half; yy <= half; ++yy) {
            for (int xx = -half; xx <= half; ++xx) {
                const int px = ix + xx;
                const int py = iy + yy;
                if (!in_bounds(px, py)) continue;
                const auto tile = out.tiles[std::size_t(py) * kCellSize + px];
                if (tile == TILE_HOUSE || tile == TILE_WALL) continue;
                city_set_tile(out, px, py, value);
            }
        }
    }
}

static bool city_has_field_neighbor(const SubworldMapData& out, int x, int y, int radius) {
    for (int dy = -radius; dy <= radius; dy += 2) {
        for (int dx = -radius; dx <= radius; dx += 2) {
            const int px = x + dx;
            const int py = y + dy;
            if (in_bounds(px, py) && out.tiles[std::size_t(py) * kCellSize + px] == TILE_FIELD) {
                return true;
            }
        }
    }
    return false;
}

static void city_place_outskirt_houses(SubworldMapData& out, CityState& cs,
                                       const CityWall& outerWall, int population) {
    const int targetHuts = std::max(2, std::min(60, population / 600));
    int placed = 0;
    int attempts = 0;
    while (placed < targetHuts && attempts < targetHuts * 30) {
        ++attempts;
        const float angle = cs.rng_float(0.0f, kTwoPi);
        const float radius = cs.rng_float(outerWall.avgRadius * 1.05f, outerWall.avgRadius * 1.6f);
        const int hx = int(std::floor(outerWall.centerX + std::cos(angle) * radius));
        const int hy = int(std::floor(outerWall.centerY + std::sin(angle) * radius));
        const int w = cs.rng_int(2, 3);
        const int h = cs.rng_int(2, 3);
        if (hx < 5 || hy < 5 || hx + w >= kCellSize - 5 || hy + h >= kCellSize - 5) continue;
        if (!city_has_field_neighbor(out, hx, hy, 8)) continue;

        bool free = true;
        for (int y = hy; y < hy + h && free; ++y) {
            for (int x = hx; x < hx + w; ++x) {
                const auto tile = out.tiles[std::size_t(y) * kCellSize + x];
                if (!city_is_ground_or_empty(tile)) {
                    free = false;
                    break;
                }
            }
        }
        if (!free) continue;
        for (int y = hy; y < hy + h; ++y) {
            for (int x = hx; x < hx + w; ++x) {
                city_set_tile(out, x, y, TILE_HOUSE);
            }
        }
        cs.houses.push_back({hx, hy, w, h, cs.rng_float(-0.3f, 0.3f)});
        ++placed;
    }
}

static void city_generate_farm_roads(SubworldMapData& out, CityState& cs,
                                     const CityWall& outerWall, int population) {
    if (cs.fields.empty()) return;
    const int roadCount = std::min(int(cs.fields.size()), std::max(2, population / 1500));
    const int step = std::max(1, int(cs.fields.size()) / roadCount);
    int count = 0;
    for (int i = 0; i < int(cs.fields.size()) && count < roadCount; i += step) {
        const auto& field = cs.fields[std::size_t(i)];
        const CityPoint junction = city_find_nearest_main_road_point(cs, field.x, field.y);
        city_mark_line(out, cs, junction.x, junction.y, field.x, field.y, TILE_ROAD, kStreetWidth);
        ++count;
    }
    city_place_outskirt_houses(out, cs, outerWall, population);
}

static void city_generate_outer_land_use(SubworldMapData& out, CityState& cs, int population) {
    if (cs.walls.empty()) return;
    const CityWall& outerWall = cs.walls.back();
    const float grassThreshold = -0.35f - float(population) / 50000.0f;
    for (int y = 2; y < kCellSize - 2; ++y) {
        for (int x = 2; x < kCellSize - 2; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] != TILE_EMPTY) continue;
            if (city_inside_wall(outerWall, float(x) + 0.5f, float(y) + 0.5f)
                || city_has_urban_neighbor(out, x, y, 2)) {
                continue;
            }
            const float noise = smooth_local_noise(float(x) * 0.07f, float(y) * 0.09f, cs.seed)
                + smooth_local_noise(float(x) * 0.03f + float(y) * 0.03f,
                                     float(y) * 0.03f - float(x) * 0.03f, cs.seed)
                + smooth_local_noise(float(x) * 0.011f + float(y) * 0.011f,
                                     float(x) * 0.011f - float(y) * 0.011f, cs.seed)
                - 1.5f;
            if (noise > grassThreshold) {
                out.tiles[idx] = TILE_GRASS;
                out.trav[idx] = 1;
            }
        }
    }

    const int targetFields = std::max(6, std::min(500, population / 50));
    int placed = 0;
    int attempts = 0;
    const int maxAttempts = targetFields * 25;
    while (placed < targetFields && attempts < maxAttempts) {
        ++attempts;
        if (city_place_field_plot(out, cs, outerWall)) ++placed;
    }
    city_generate_farm_roads(out, cs, outerWall, population);
}

static void city_build_house_structures(SubworldMapData& out, const CityState& cs) {
    for (const auto& h : cs.houses) {
        const float cx = float(h.x) + float(h.w) * 0.5f;
        const float cy = float(h.y) + float(h.h) * 0.5f;
        out.structures.push_back({
            Structure::House,
            cx,
            cy,
            float(std::max(h.w, h.h)) * 0.5f,
            6.0f
        });
        (void)h.rotation;
    }
}

} // namespace

void generate_city(const CellContext& ctx,
                   const std::uint8_t nbFeature[9],
                   SubworldMapData& out) {
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_EMPTY));
    out.trav.assign(std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();

    CityState cs(ctx.seed);
    const int population = std::max(ctx.landmarkSize, 1);

    city_generate_main_roads(out, cs, nbFeature);
    city_generate_square(out, cs, population);
    city_place_keep(out, cs, population);
    city_grow_mycelium(out, cs, population);
    city_convert_excess_roads(out);
    city_ensure_walls(out, cs, population);
    city_infill_houses_inside_walls(out, cs);
    city_fill_urban_spaces(out, cs);
    city_generate_outer_land_use(out, cs, population);
    city_build_house_structures(out, cs);

    const int clearRadius = cs.walls.empty()
        ? int(float(kCellSize) * 0.15f)
        : int(cs.walls.back().avgRadius * 1.05f);
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, clearRadius, ctx.seed);

    for (auto& tile : out.tiles) {
        if (tile == TILE_EMPTY) tile = TILE_GRASS;
    }
}

} // namespace sm::sub

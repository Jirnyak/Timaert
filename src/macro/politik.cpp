#include "macro/politik.h"
#include "macro/map_generator.h"
#include "core/rng.h"
#include "core/torus.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sm {

// Toroidal squared distance helper used by both placement and MST passes.
static inline int torus_dist2(int ax, int ay, int bx, int by, int W, int H) {
    int dx = std::abs(ax - bx);
    int dy = std::abs(ay - by);
    if (dx > W / 2) dx = W - dx;
    if (dy > H / 2) dy = H - dy;
    return dx * dx + dy * dy;
}

static int find_close_city(const Politik& p, int x, int y, int minDist,
                          int W, int H) {
    int md2 = minDist * minDist;
    for (int i = 0; i < int(p.cities.size()); ++i) {
        if (torus_dist2(p.cities[std::size_t(i)].x,
                        p.cities[std::size_t(i)].y, x, y, W, H) < md2)
            return i;
    }
    return -1;
}

static bool terrain_matches_map(const TerrainData* terrain, int mapW, int mapH) {
    return terrain
        && terrain->width == mapW
        && terrain->height == mapH
        && terrain->has_rgba_storage();
}

// Spiral search for nearest land tile within `maxR` cells. Returns true if
// found and writes the cell into (*outX, *outY); false otherwise.
static bool find_nearest_land(const TerrainData& td, std::uint8_t seaLevel8,
                              int cx, int cy, int maxR, int* outX, int* outY) {
    if (!td.has_rgba_storage()) return false;
    auto is_land = [&](int x, int y) {
        x = wrapi(x, td.width); y = wrapi(y, td.height);
        return td.rgba[std::size_t(y * td.width + x) * 4 + 0] >= seaLevel8;
    };
    if (is_land(cx, cy)) { *outX = wrapi(cx, td.width); *outY = wrapi(cy, td.height); return true; }
    for (int r = 1; r <= maxR; ++r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                int nx = cx + dx, ny = cy + dy;
                if (is_land(nx, ny)) {
                    *outX = wrapi(nx, td.width); *outY = wrapi(ny, td.height); return true;
                }
            }
        }
    }
    return false;
}

Politik generate_politik(std::uint32_t seed, int mapW, int mapH,
                        const TerrainData* terrain, std::uint8_t seaLevel8,
                        int targetTotalCities) {
    Politik P;
    std::size_t totalCells = 0;
    if (!TerrainData::cell_count_for(mapW, mapH, totalCells))
        return P;
    P.mapW = mapW; P.mapH = mapH;
    P.cellOwner.assign(totalCells, 0xff);
    const bool useTerrain = terrain_matches_map(terrain, mapW, mapH);

    Rng r(seed ^ 0xC001CAFE);
    const auto& defs = kingdom_defs();

    // ── Per-kingdom scaling for total city count ───────────────────
    // Registry mid-points sum to N0; if the caller requested `target`,
    // scale each kingdom by `target/N0` and round to the nearest int
    // (clamped to ≥1 so every kingdom keeps at least its capital).
    int registryTotal = 0;
    for (const auto& d : defs)
        registryTotal += (d.minCities + d.maxCities + 1) / 2;
    if (registryTotal < 1) registryTotal = 1;
    const float cityScale = (targetTotalCities > 0)
        ? float(targetTotalCities) / float(registryTotal)
        : 1.0f;

    // ── Data-driven minimum spacing ────────────────────────────────
    // Estimate total target city count, then derive a Poisson-disk
    // separation from the available (land) area: nearest-neighbour
    // distance for N uniformly-scattered points in area A is ≈ √(A/N).
    // We use 60% of that as the rejection radius — enough to feel
    // "natural" without leaving the map sparse.
    int totalTarget = (targetTotalCities > 0) ? targetTotalCities : registryTotal;
    if (totalTarget < 1) totalTarget = 1;
    int areaCells = int(std::min<std::size_t>(
        totalCells, std::size_t(std::numeric_limits<int>::max())));
    if (useTerrain) {
        // Fast land-count via subsampled scan (every 4th cell — 16× speed).
        int land = 0, sampled = 0;
        for (int y = 0; y < mapH; y += 4)
            for (int x = 0; x < mapW; x += 4) {
                ++sampled;
                if (terrain->rgba[std::size_t(y * mapW + x) * 4 + 0] >= seaLevel8) ++land;
            }
        if (sampled > 0)
            areaCells = int(float(land) / float(sampled) * float(mapW * mapH));
    }
    const int minDist = std::max(8,
        int(0.60f * std::sqrt(float(areaCells) / float(totalTarget))));

    // Land predicate (defaults to "everywhere is land" when no terrain).
    auto is_land = [&](int x, int y) -> bool {
        if (!useTerrain) return true;
        x = wrapi(x, mapW); y = wrapi(y, mapH);
        return terrain->rgba[std::size_t(y * mapW + x) * 4 + 0] >= seaLevel8;
    };

    // Seed capitals + cities per kingdom.
    for (int k = 0; k < int(defs.size()); ++k) {
        const auto& def = defs[std::size_t(k)];
        Kingdom kg;
        kg.id = def.id;
        kg.name = def.name;
        kg.lineage = def.lineage;
        kg.color = def.color_rgb;
        kg.language = create_language(seed ^ std::uint32_t(k * 0x9E3779B1));

        int effMin = std::max(1, int(std::lround(float(def.minCities) * cityScale)));
        int effMax = std::max(effMin, int(std::lround(float(def.maxCities) * cityScale)));
        int target = effMin + int(r.next_u32() % std::uint32_t(effMax - effMin + 1));

        // Capital — start at def coords, snap to nearest land respecting
        // minDist against earlier capitals.
        int cx = wrapi(int(def.cx * mapW), mapW);
        int cy = wrapi(int(def.cy * mapH), mapH);
        if (useTerrain) {
            int nx = cx, ny = cy;
            // Walk a spiral; accept first land tile that's also far enough
            // from already-placed capitals.
            bool placed = false;
            for (int rad = 0; rad <= 120 && !placed; ++rad) {
                if (rad == 0) {
                    if (is_land(cx, cy) && find_close_city(P, cx, cy, minDist, mapW, mapH) < 0) {
                        nx = cx; ny = cy; placed = true; break;
                    }
                    continue;
                }
                for (int dy = -rad; dy <= rad && !placed; ++dy) {
                    for (int dx = -rad; dx <= rad && !placed; ++dx) {
                        if (std::abs(dx) != rad && std::abs(dy) != rad) continue;
                        int tx = wrapi(cx + dx, mapW), ty = wrapi(cy + dy, mapH);
                        if (!is_land(tx, ty)) continue;
                        if (find_close_city(P, tx, ty, minDist, mapW, mapH) >= 0) continue;
                        nx = tx; ny = ty; placed = true;
                    }
                }
            }
            cx = nx; cy = ny;
        }
        kg.capitalCityIdx = int(P.cities.size());
        City cap; cap.x = cx; cap.y = cy;
        cap.name = generate_name(kg.language, std::uint32_t(P.cities.size()) * 2654435761u);
        cap.kingdomIdx = k;
        for (int& c : cap.connections) c = -1;
        cap.population = 4000 + int(r.next_u32() % 3000u);
        P.cities.push_back(std::move(cap));
        kg.cityIdxs.push_back(kg.capitalCityIdx);

        // Scatter remaining cities with **organic growth** clustering:
        // each new city picks a random already-placed kingdom city as
        // anchor (capital weighted slightly higher) and jitters at a
        // small radius (~2-3× minDist). This produces natural chains
        // and clusters — towns sprout near towns — instead of uniform
        // random scatter. The anchor radius is bounded by `scatterMax`
        // so big maps still spread the kingdom across its region.
        const int scatterMax = std::max(120, std::max(mapW / 6, minDist * 4));
        const int jitter = std::clamp(minDist * 3, 24, scatterMax);
        const int landSearch = std::max(8, minDist / 2);
        for (int n = 1; n < target; ++n) {
            for (int tries = 0; tries < 64; ++tries) {
                // Pick parent: 35% capital, 65% any existing kingdom city.
                int anchorIdx = kg.capitalCityIdx;
                if (kg.cityIdxs.size() > 1 && (r.next_u32() % 100u) >= 35u) {
                    anchorIdx = kg.cityIdxs[std::size_t(
                        r.next_u32() % std::uint32_t(kg.cityIdxs.size()))];
                }
                const int ax = P.cities[std::size_t(anchorIdx)].x;
                const int ay = P.cities[std::size_t(anchorIdx)].y;
                int rx = wrapi(ax + int(r.next_u32() % std::uint32_t(2 * jitter + 1))
                                  - jitter, mapW);
                int ry = wrapi(ay + int(r.next_u32() % std::uint32_t(2 * jitter + 1))
                                  - jitter, mapH);
                if (!is_land(rx, ry)) {
                    if (useTerrain && !find_nearest_land(*terrain, seaLevel8, rx, ry,
                                                         landSearch, &rx, &ry))
                        continue;
                }
                if (find_close_city(P, rx, ry, minDist, mapW, mapH) >= 0) continue;
                int idx = int(P.cities.size());
                City c; c.x = rx; c.y = ry;
                c.name = generate_name(kg.language, std::uint32_t(idx) * 2654435761u);
                c.kingdomIdx = k;
                for (int& cc : c.connections) cc = -1;
                c.population = 800 + int(r.next_u32() % 1500u);
                P.cities.push_back(std::move(c));
                kg.cityIdxs.push_back(idx);
                break;
            }
        }
        P.kingdoms.push_back(std::move(kg));
    }

    // ── Global top-up pass ─────────────────────────────────────────
    // If per-kingdom scattering came up short of `targetTotalCities`
    // (common on huge maps where capitals start clustered), do a global
    // dart-throw assigning each new city to the nearest kingdom by
    // toroidal capital distance. Cap attempts so we never spin forever
    // on saturated maps.
    if (targetTotalCities > 0
        && int(P.cities.size()) < targetTotalCities
        && useTerrain
        && !P.cities.empty()) {
        const int deficit = targetTotalCities - int(P.cities.size());
        const int maxGlobalTries = std::max(512, deficit * 32);
        const int topupJitter = std::clamp(minDist * 3, 24,
                                std::max(120, std::max(mapW / 6, minDist * 4)));
        for (int t = 0; t < maxGlobalTries
                     && int(P.cities.size()) < targetTotalCities; ++t) {
            // Anchor on a random existing city → keeps clustering natural.
            const int anchor = int(r.next_u32() % std::uint32_t(P.cities.size()));
            const int ax = P.cities[std::size_t(anchor)].x;
            const int ay = P.cities[std::size_t(anchor)].y;
            int rx = wrapi(ax + int(r.next_u32() % std::uint32_t(2 * topupJitter + 1))
                              - topupJitter, mapW);
            int ry = wrapi(ay + int(r.next_u32() % std::uint32_t(2 * topupJitter + 1))
                              - topupJitter, mapH);
            if (!is_land(rx, ry)) continue;
            if (find_close_city(P, rx, ry, minDist, mapW, mapH) >= 0) continue;
            // Inherit anchor's kingdom — clusters stay politically coherent.
            int bestK = P.cities[std::size_t(anchor)].kingdomIdx;
            int idx = int(P.cities.size());
            City c; c.x = rx; c.y = ry;
            c.name = generate_name(P.kingdoms[std::size_t(bestK)].language,
                                   std::uint32_t(idx) * 2654435761u);
            c.kingdomIdx = bestK;
            for (int& cc : c.connections) cc = -1;
            c.population = 600 + int(r.next_u32() % 1200u);
            P.cities.push_back(std::move(c));
            P.kingdoms[std::size_t(bestK)].cityIdxs.push_back(idx);
        }
    }

    // ── Per-kingdom MST (Prim's) rooted at the capital, plus 1 extra ──
    // nearest unconnected edge per city for redundancy (max 8 conns).
    auto add_conn = [&](int a, int b) {
        for (int& c : P.cities[std::size_t(a)].connections)
            if (c == b) return;          // already connected
        for (int& c : P.cities[std::size_t(a)].connections)
            if (c == -1) { c = b; return; }
    };
    auto has_conn = [&](int a, int b) {
        for (int c : P.cities[std::size_t(a)].connections) if (c == b) return true;
        return false;
    };
    auto conn_count = [&](int a) {
        int n = 0;
        for (int c : P.cities[std::size_t(a)].connections) if (c != -1) ++n;
        return n;
    };
    auto torus_d2 = [&](int ai, int bi) {
        int dx = std::abs(P.cities[std::size_t(ai)].x - P.cities[std::size_t(bi)].x);
        int dy = std::abs(P.cities[std::size_t(ai)].y - P.cities[std::size_t(bi)].y);
        if (dx > mapW / 2) dx = mapW - dx;
        if (dy > mapH / 2) dy = mapH - dy;
        return dx * dx + dy * dy;
    };

    // Redundancy-edge fan guard: cos(~26°). A second road leaving a city
    // within this angle of one it already has reads as a doubled diagonal, so
    // it is suppressed (see the extra-edge pass below). 0.90 ≈ cos(25.8°):
    // wide-angle loops (genuine alternate routes) survive, shallow fans don't.
    const float kRoadFanCosThreshold = 0.90f;

    for (auto& kg : P.kingdoms) {
        const auto& idxs = kg.cityIdxs;
        if (idxs.size() < 2) continue;

        // Prim's MST seeded from capital (idxs[0]).
        std::vector<std::uint8_t> inTree(idxs.size(), 0);
        inTree[0] = 1;
        for (std::size_t added = 1; added < idxs.size(); ++added) {
            int bestFrom = -1, bestTo = -1, bestD = (1 << 30);
            for (std::size_t i = 0; i < idxs.size(); ++i) {
                if (!inTree[i]) continue;
                for (std::size_t j = 0; j < idxs.size(); ++j) {
                    if (inTree[j]) continue;
                    int d = torus_d2(idxs[i], idxs[j]);
                    if (d < bestD) { bestD = d; bestFrom = int(i); bestTo = int(j); }
                }
            }
            if (bestFrom < 0) break;
            add_conn(idxs[std::size_t(bestFrom)], idxs[std::size_t(bestTo)]);
            add_conn(idxs[std::size_t(bestTo)], idxs[std::size_t(bestFrom)]);
            inTree[std::size_t(bestTo)] = 1;
        }

        // One extra nearest-non-connected edge per city (cap at 4 conns) for
        // loop redundancy — but SKIP a candidate whose bearing nearly
        // duplicates a road an endpoint already has. Without this, three
        // roughly-collinear cities A–B–C (MST links A–B, B–C) get a bypass
        // A→C fanning off almost parallel to A→B: a doubled diagonal that adds
        // no real alternate route. The guard is strictly subtractive — it only
        // suppresses near-parallel fans, never removes an MST edge nor adds a
        // long cross-map road — so it cannot disconnect the kingdom.
        //
        // shadows_existing(from,to): does the edge from→to run nearly parallel
        // to a road `from` already has? Checked at BOTH ends below, because the
        // mirrored edge (add_conn(nearest,self)) would double a diagonal at the
        // far city just as visibly as at the near one.
        auto shadows_existing = [&](int from, int to) {
            for (int ex : P.cities[std::size_t(from)].connections) {
                if (ex < 0 || ex == to) continue;
                if (torus_bearings_parallel(
                        P.cities[std::size_t(from)].x, P.cities[std::size_t(from)].y,
                        P.cities[std::size_t(ex)].x,   P.cities[std::size_t(ex)].y,
                        P.cities[std::size_t(to)].x,   P.cities[std::size_t(to)].y,
                        mapW, mapH, kRoadFanCosThreshold))
                    return true;
            }
            return false;
        };
        for (int self : idxs) {
            if (conn_count(self) >= 4) continue;
            int nearest = -1, nd = (1 << 30);
            for (int other : idxs) {
                if (other == self || has_conn(self, other)) continue;
                if (shadows_existing(self, other) || shadows_existing(other, self))
                    continue;
                int d = torus_d2(self, other);
                if (d < nd) { nd = d; nearest = other; }
            }
            if (nearest >= 0) { add_conn(self, nearest); add_conn(nearest, self); }
        }
    }

    // ── Bridge adjacent kingdoms: one road between the closest city pair, ──
    // skip if absurdly distant (> 35% of half-diagonal squared, TS parity).
    {
        const double mapDiag2 = double(mapW) * double(mapW)
                              + double(mapH) * double(mapH);
        const int bridgeMaxD2 = int(std::min<double>(
            (0.35 * 0.35) * mapDiag2,
            double(std::numeric_limits<int>::max())));
        for (std::size_t i = 0; i < P.kingdoms.size(); ++i) {
            for (std::size_t j = i + 1; j < P.kingdoms.size(); ++j) {
                const auto& A = P.kingdoms[i].cityIdxs;
                const auto& B = P.kingdoms[j].cityIdxs;
                int bestA = -1, bestB = -1, bestD = (1 << 30);
                for (int a : A) for (int b : B) {
                    int d = torus_d2(a, b);
                    if (d < bestD) { bestD = d; bestA = a; bestB = b; }
                }
                if (bestA < 0 || bestD > bridgeMaxD2) continue;
                if (has_conn(bestA, bestB)) continue;
                add_conn(bestA, bestB); add_conn(bestB, bestA);
            }
        }
    }

    // Voronoi cellOwner — flood toroidal nearest-city.
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            int best = -1; float bd = 1e30f;
            for (std::size_t i = 0; i < P.cities.size(); ++i) {
                float d = torus_dist_sq(float(x), float(y),
                                        float(P.cities[i].x), float(P.cities[i].y),
                                        float(mapW), float(mapH));
                if (d < bd) { bd = d; best = P.cities[i].kingdomIdx; }
            }
            P.cellOwner[std::size_t(y) * mapW + x] = std::uint8_t(best & 0xff);
        }
    }
    return P;
}

void snap_cities_to_land(Politik& p, const TerrainData& td,
                         std::uint8_t seaLevel8, int radius) {
    if (!td.has_rgba_storage()) return;
    const int W = td.width, H = td.height;
    auto is_land = [&](int x, int y) {
        x = wrapi(x, W); y = wrapi(y, H);
        return td.rgba[std::size_t(y * W + x) * 4 + 0] >= seaLevel8;
    };
    for (auto& c : p.cities) {
        if (is_land(c.x, c.y)) continue;
        // Spiral outward in concentric square rings.
        int found = 0, fx = c.x, fy = c.y;
        for (int r = 1; r <= radius && !found; ++r) {
            for (int dy = -r; dy <= r && !found; ++dy) {
                for (int dx = -r; dx <= r && !found; ++dx) {
                    if (std::abs(dx) != r && std::abs(dy) != r) continue;
                    int nx = c.x + dx, ny = c.y + dy;
                    if (is_land(nx, ny)) { fx = wrapi(nx, W); fy = wrapi(ny, H); found = 1; }
                }
            }
        }
        c.x = fx; c.y = fy;
    }
}

// ── Multi-source BFS Voronoi over land cells (TS buildCellOwnership). ──
// Plus lake-snap for any kingdom whose def has capital_requires_lake.
void finalize_politik(Politik& p, const TerrainData& td, std::uint8_t seaLevel8) {
    if (!td.has_rgba_storage()) return;
    const int W = td.width, H = td.height;
    auto is_land = [&](int x, int y) {
        return td.rgba[(std::size_t(y) * W + x) * 4 + 0] >= seaLevel8;
    };
    auto is_water = [&](int x, int y) {
        return td.rgba[(std::size_t(y) * W + x) * 4 + 0] < seaLevel8;
    };
    auto count_local_water = [&](int cx, int cy, int r) {
        int n = 0;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (is_water(wrapi(cx + dx, W), wrapi(cy + dy, H))) ++n;
        return n;
    };

    // Lake-snap: nudge lake-required capitals to a coastal cell with ≥4
    // water neighbours in radius 8.
    const auto& defs = kingdom_defs();
    for (std::size_t k = 0; k < defs.size() && k < p.kingdoms.size(); ++k) {
        if (!defs[k].capital_requires_lake) continue;
        Kingdom& kg = p.kingdoms[k];
        if (kg.capitalCityIdx < 0) continue;
        City& cap = p.cities[std::size_t(kg.capitalCityIdx)];
        if (count_local_water(cap.x, cap.y, 6) >= 4) continue;
        bool placed = false;
        for (int r = 1; r < 80 && !placed; ++r) {
            for (int dy = -r; dy <= r && !placed; ++dy) {
                for (int dx = -r; dx <= r && !placed; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    int x = wrapi(cap.x + dx, W), y = wrapi(cap.y + dy, H);
                    if (!is_land(x, y)) continue;
                    if (count_local_water(x, y, 8) >= 4) {
                        cap.x = x; cap.y = y; placed = true;
                    }
                }
            }
        }
    }

    // Multi-source BFS — each city is a seed. owner = kingdomIdx + 1, 0 = unowned.
    const std::size_t n = std::size_t(W) * H;
    std::vector<std::uint8_t> owner(n, 0);
    std::vector<int> queue;
    queue.reserve(n);
    for (const City& c : p.cities) {
        if (c.kingdomIdx < 0) continue;
        int x = wrapi(c.x, W), y = wrapi(c.y, H);
        if (!is_land(x, y)) continue;
        std::size_t idx = std::size_t(y) * W + x;
        if (owner[idx]) continue;
        owner[idx] = std::uint8_t(c.kingdomIdx + 1);
        queue.push_back(int(idx));
    }
    for (std::size_t head = 0; head < queue.size(); ++head) {
        int idx = queue[head];
        int y = idx / W, x = idx - y * W;
        std::uint8_t kid = owner[std::size_t(idx)];
        const int nbrs[4][2] = {
            {x, (y + 1) % H}, {x, (y + H - 1) % H},
            {(x + 1) % W, y}, {(x + W - 1) % W, y},
        };
        for (auto& nb : nbrs) {
            std::size_t ni = std::size_t(nb[1]) * W + nb[0];
            if (owner[ni]) continue;
            if (!is_land(nb[0], nb[1])) continue;
            owner[ni] = kid;
            queue.push_back(int(ni));
        }
    }

    // Translate owner (1-based, 0=unowned) → cellOwner (0-based, 0xff=unowned).
    p.cellOwner.assign(n, 0xff);
    for (std::size_t i = 0; i < n; ++i)
        if (owner[i]) p.cellOwner[i] = std::uint8_t(owner[i] - 1);
}

} // namespace sm

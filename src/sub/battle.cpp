#include "sub/battle.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace sm::sub {
namespace {

inline float length2d(float ax, float ay) {
    return std::sqrt(ax * ax + ay * ay);
}

// Where in a bucket a given unit starts its walk.
//
// This is NOT cosmetic. The visit ceilings (maxPickVisits / maxSepVisits) bound
// the cost of a crowded bucket, but a walk that always starts at the bucket's
// FIRST item makes every unit sample the same prefix — so a body sitting behind
// that prefix is blind to the neighbours actually overlapping it. Measured with
// 600 bodies spawned on one spot: the first ~48 unpacked and left, after which
// the remaining ~550 saw no neighbour within touch distance and stayed welded
// together forever (mean nearest-neighbour 0.35 units). Rotating the start per
// unit makes every body sample a different slice, so overlap always resolves,
// and it stays a pure function of the index — determinism intact.
inline std::uint32_t bucket_offset(int i, std::uint32_t span) {
    return span > 1u ? (std::uint32_t(i) * 2654435761u) % span : 0u;
}

// Deterministic unit direction from two indices. Used only when two bodies sit
// at the SAME point, where the separation direction is undefined: without it a
// perfectly stacked pair would stay stacked forever. Pure function of the
// indices ⇒ same-seed reproducible, no RNG state in the hot loop.
inline void tie_break_dir(int i, int j, float& dx, float& dy) {
    const std::uint32_t h =
        (std::uint32_t(i) * 2654435761u) ^ (std::uint32_t(j) * 2246822519u);
    const float ang = float(h & 0xFFFFu) * (6.2831853f / 65536.0f);
    dx = std::cos(ang);
    dy = std::sin(ang);
}

} // namespace

// ── FactionSet ─────────────────────────────────────────────────────────────

void FactionSet::clear() {
    for (int i = 0; i < count; ++i) {
        ids[i] = nullptr;
        enemyMask[i] = 0ull;
    }
    count = 0;
}

int FactionSet::find(const char* id) const {
    if (!id || id[0] == '\0') return -1;
    for (int i = 0; i < count; ++i) {
        // Pointer-first: faction ids come from static tables, so identical
        // factions are usually the identical pointer and strcmp never runs.
        if (ids[i] == id) return i;
    }
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(ids[i], id) == 0) return i;
    }
    return -1;
}

int FactionSet::intern(const char* id) {
    const int found = find(id);
    if (found >= 0) return found;
    if (!id || id[0] == '\0' || count >= kMaxBattleFactions) return -1;
    ids[count] = id;
    enemyMask[count] = 0ull;
    return count++;
}

void build_faction_masks(FactionSet& fs,
                         int (*relation)(void* user, const char* a, const char* b),
                         void* user, int hostileBelow) {
    if (!relation) return;
    for (int a = 0; a < fs.count; ++a) {
        std::uint64_t mask = 0ull;
        for (int b = 0; b < fs.count; ++b) {
            if (a == b) continue;              // never hostile to your own faction
            if (relation(user, fs.ids[a], fs.ids[b]) < hostileBelow)
                mask |= (1ull << b);
        }
        fs.enemyMask[a] = mask;
    }
}

// ── BattleUnits ────────────────────────────────────────────────────────────

void BattleUnits::reserve(int n) {
    const std::size_t c = std::size_t(n);
    x.reserve(c); y.reserve(c); z.reserve(c);
    vx.reserve(c); vy.reserve(c);
    radius.reserve(c); speed.reserve(c); reach.reserve(c); sight.reserve(c);
    enemyMask.reserve(c); faction.reserve(c); flags.reserve(c);
    target.reserve(c); inReach.reserve(c);
}

void BattleUnits::clear() {
    count = 0;
    maxRadius = 0.0f;
    maxReach = 0.0f;
    x.clear(); y.clear(); z.clear();
    vx.clear(); vy.clear();
    radius.clear(); speed.clear(); reach.clear(); sight.clear();
    enemyMask.clear(); faction.clear(); flags.clear();
    target.clear(); inReach.clear();
}

int BattleUnits::add(const BattleUnitDesc& d) {
    if (count >= kMaxBattleUnits) return -1;
    const int idx = count++;
    x.push_back(d.x); y.push_back(d.y); z.push_back(d.z);
    vx.push_back(d.vx); vy.push_back(d.vy);
    radius.push_back(d.radius); speed.push_back(d.speed);
    reach.push_back(d.reach); sight.push_back(d.sight);
    enemyMask.push_back(d.enemyMask);
    faction.push_back(d.faction);
    flags.push_back(d.flags);
    target.push_back(-1);
    inReach.push_back(0u);
    maxRadius = std::max(maxRadius, d.radius);
    maxReach = std::max(maxReach, d.reach);
    return idx;
}

// ── Grid sizing, from the crowd's own geometry ─────────────────────────────

float fine_cell_for(const BattleUnits& u, const BattleParams& prm) {
    return std::max(prm.fineCellMin, u.maxRadius * prm.fineCellPerRadius);
}

float pick_cell_for(const BattleUnits& u, const BattleParams& prm) {
    const float want = (u.maxReach + 2.0f * u.maxRadius) * prm.pickCellPerReach;
    return std::max(std::max(prm.pickCellMin, want), fine_cell_for(u, prm));
}

// ── Bucket grid ────────────────────────────────────────────────────────────

void build_unit_grid(UnitGrid& g, const BattleUnits& u, float cell, int maxDim) {
    const int n = u.count;
    g.items.clear();
    if (n <= 0 || cell <= 0.0f || maxDim < 1) {
        g.cell = cell > 0.0f ? cell : 1.0f;
        g.originX = g.originY = 0.0f;
        g.cols = g.rows = 1;
        g.begin.assign(2u, 0u);
        return;
    }

    // Bounding box of the actual crowd: a 500-man melee in one corner costs a
    // few hundred cells, not the 3072² window.
    float minX = u.x[0], maxX = u.x[0], minY = u.y[0], maxY = u.y[0];
    for (int i = 1; i < n; ++i) {
        minX = std::min(minX, u.x[std::size_t(i)]);
        maxX = std::max(maxX, u.x[std::size_t(i)]);
        minY = std::min(minY, u.y[std::size_t(i)]);
        maxY = std::max(maxY, u.y[std::size_t(i)]);
    }

    // Grow the cell rather than the allocation when the crowd is spread wide.
    // Density is low in that case, so a coarser cell costs nothing in visits.
    float c = cell;
    for (;;) {
        const float spanX = maxX - minX, spanY = maxY - minY;
        const int wantCols = int(spanX / c) + 1;
        const int wantRows = int(spanY / c) + 1;
        if ((wantCols <= maxDim && wantRows <= maxDim) || c > 1.0e6f) {
            g.cols = std::max(1, wantCols);
            g.rows = std::max(1, wantRows);
            break;
        }
        c *= 2.0f;
    }
    g.cell = c;
    g.originX = minX;
    g.originY = minY;

    const std::size_t cells = std::size_t(g.cols) * std::size_t(g.rows);
    g.begin.assign(cells + 1u, 0u);

    // Pass 1 — per-cell counts (offset by one, so the prefix sum lands in place).
    for (int i = 0; i < n; ++i) {
        const int ci = g.row_of(u.y[std::size_t(i)]) * g.cols
                     + g.col_of(u.x[std::size_t(i)]);
        ++g.begin[std::size_t(ci) + 1u];
    }
    for (std::size_t i = 1; i <= cells; ++i) g.begin[i] += g.begin[i - 1u];

    // Pass 2 — scatter. A local cursor copy keeps `begin` intact.
    g.items.resize(std::size_t(n));
    g.cursor.assign(g.begin.begin(), g.begin.end() - 1);
    for (int i = 0; i < n; ++i) {
        const int ci = g.row_of(u.y[std::size_t(i)]) * g.cols
                     + g.col_of(u.x[std::size_t(i)]);
        g.items[g.cursor[std::size_t(ci)]++] = i;
    }
}

// ── Influence field + alert chain ──────────────────────────────────────────

void build_influence_field(InfluenceField& f, const BattleUnits& u,
                           float cell, float worldSize) {
    f.cell = cell > 0.0f ? cell : 1.0f;
    // Bbox of the crowd, snapped to cell boundaries and capped at the window.
    const int maxDim = std::max(1, int(worldSize / f.cell) + 1);
    if (u.count > 0) {
        float minX = u.x[0], maxX = u.x[0], minY = u.y[0], maxY = u.y[0];
        for (int i = 1; i < u.count; ++i) {
            minX = std::min(minX, u.x[std::size_t(i)]);
            maxX = std::max(maxX, u.x[std::size_t(i)]);
            minY = std::min(minY, u.y[std::size_t(i)]);
            maxY = std::max(maxY, u.y[std::size_t(i)]);
        }
        f.originX = std::floor(minX / f.cell) * f.cell;
        f.originY = std::floor(minY / f.cell) * f.cell;
        f.cols = std::clamp(int((maxX - f.originX) / f.cell) + 1, 1, maxDim);
        f.rows = std::clamp(int((maxY - f.originY) / f.cell) + 1, 1, maxDim);
    } else {
        f.originX = f.originY = 0.0f;
        f.cols = f.rows = 1;
    }
    f.presentMask = 0ull;
    f.planeCount = 0;
    for (int i = 0; i < kMaxBattleFactions; ++i) {
        f.factionEnemyMask[i] = 0ull;
        f.planeIdx[i] = -1;
    }

    const std::size_t cells = f.cell_count();
    f.occMask.assign(cells, std::uint64_t(0));
    if (u.count <= 0) {
        f.count.clear(); f.sumX.clear(); f.sumY.clear();
        f.siteX.clear(); f.siteY.clear(); f.hasSite.clear(); f.alert.clear();
        return;
    }

    // Which factions are actually here (O(N)). Only those get a plane, so the
    // per-frame clear below is proportional to the factions on the field.
    for (int i = 0; i < u.count; ++i) {
        const std::int16_t fi = u.faction[std::size_t(i)];
        if (fi < 0 || fi >= kMaxBattleFactions) continue;
        f.presentMask |= (1ull << fi);
    }
    for (int s = 0; s < kMaxBattleFactions; ++s) {
        if (((f.presentMask >> s) & 1ull) != 0ull)
            f.planeIdx[s] = std::int16_t(f.planeCount++);
    }

    const std::size_t planes = cells * std::size_t(f.planeCount);
    f.count.assign(planes, 0u);
    f.sumX.assign(planes, 0.0f);
    f.sumY.assign(planes, 0.0f);
    f.siteX.assign(planes, 0.0f);
    f.siteY.assign(planes, 0.0f);
    f.hasSite.assign(planes, std::uint8_t(0));
    f.alert.assign(planes, std::uint8_t(0));

    // Scatter: O(N). Each unit contributes presence + centroid to its faction's
    // plane, and OR-s its own enemy mask into its faction's aggregate — so a
    // body with a private grudge (TempHostileToPlayer) drags the player side into
    // its faction's enemy set without any special case downstream.
    for (int i = 0; i < u.count; ++i) {
        const int fi = u.faction[std::size_t(i)];
        if (!f.has_faction(fi)) continue;
        const int cx = f.col_of(u.x[std::size_t(i)]);
        const int cy = f.row_of(u.y[std::size_t(i)]);
        const std::size_t ci = std::size_t(cy) * std::size_t(f.cols) + std::size_t(cx);
        const std::size_t pi = f.plane(fi) + ci;
        ++f.count[pi];
        f.sumX[pi] += u.x[std::size_t(i)];
        f.sumY[pi] += u.y[std::size_t(i)];
        f.occMask[ci] |= (1ull << fi);
        f.factionEnemyMask[fi] |= u.enemyMask[std::size_t(i)];
    }

    // Per present faction: seed every cell that holds one of its enemies with
    // that cell's HOSTILE centroid, then propagate the nearest site with two
    // sweeps (Danielsson-style vector distance transform — approximate in the
    // corners, exact along the sweeps, and far finer than steering needs).
    for (int s = 0; s < kMaxBattleFactions; ++s) {
        if (!f.has_faction(s)) continue;
        const std::uint64_t enemies = f.factionEnemyMask[s];
        if (enemies == 0ull) continue;
        const std::size_t base = f.plane(s);

        for (std::size_t ci = 0; ci < cells; ++ci) {
            const std::uint64_t hit = f.occMask[ci] & enemies;
            if (hit == 0ull) continue;
            float sx = 0.0f, sy = 0.0f;
            std::uint32_t n = 0;
            std::uint64_t rest = hit;
            while (rest != 0ull) {
                const int g = std::countr_zero(rest);   // portable: MSVC is a target
                rest &= rest - 1ull;
                if (!f.has_faction(g)) continue;
                const std::size_t gi = f.plane(g) + ci;
                sx += f.sumX[gi];
                sy += f.sumY[gi];
                n += f.count[gi];
            }
            if (n == 0u) continue;
            f.siteX[base + ci] = sx / float(n);
            f.siteY[base + ci] = sy / float(n);
            f.hasSite[base + ci] = 1u;
        }

        // Distance is measured from a cell's centre to the carried site, so the
        // transform stays consistent while the sites themselves are real
        // positions (no stair-stepping when a unit reads the field).
        auto relax = [&](int cx, int cy, int nx, int ny) {
            if (nx < 0 || ny < 0 || nx >= f.cols || ny >= f.rows) return;
            const std::size_t ni = base + std::size_t(ny) * std::size_t(f.cols)
                                 + std::size_t(nx);
            if (!f.hasSite[ni]) return;
            const std::size_t di = base + std::size_t(cy) * std::size_t(f.cols)
                                 + std::size_t(cx);
            const float ccx = f.cell_centre_x(cx);
            const float ccy = f.cell_centre_y(cy);
            const float ndx = f.siteX[ni] - ccx, ndy = f.siteY[ni] - ccy;
            const float nd2 = ndx * ndx + ndy * ndy;
            if (f.hasSite[di]) {
                const float ddx = f.siteX[di] - ccx, ddy = f.siteY[di] - ccy;
                if (ddx * ddx + ddy * ddy <= nd2) return;
            }
            f.siteX[di] = f.siteX[ni];
            f.siteY[di] = f.siteY[ni];
            f.hasSite[di] = 1u;
        };

        for (int cy = 0; cy < f.rows; ++cy) {
            for (int cx = 0; cx < f.cols; ++cx) {
                relax(cx, cy, cx - 1, cy);
                relax(cx, cy, cx, cy - 1);
                relax(cx, cy, cx - 1, cy - 1);
                relax(cx, cy, cx + 1, cy - 1);
            }
        }
        for (int cy = f.rows - 1; cy >= 0; --cy) {
            for (int cx = f.cols - 1; cx >= 0; --cx) {
                relax(cx, cy, cx + 1, cy);
                relax(cx, cy, cx, cy + 1);
                relax(cx, cy, cx + 1, cy + 1);
                relax(cx, cy, cx - 1, cy + 1);
            }
        }
    }

    // ── Alert chain ────────────────────────────────────────────────────────
    // Seed: a body that can SEE its faction's nearest enemy site alerts its cell.
    // Sight is per-body data, so a hawk and a rat differ without any code.
    for (int i = 0; i < u.count; ++i) {
        const int fi = u.faction[std::size_t(i)];
        if (!f.has_faction(fi)) continue;
        const int cx = f.col_of(u.x[std::size_t(i)]);
        const int cy = f.row_of(u.y[std::size_t(i)]);
        const std::size_t pi = f.plane(fi)
            + std::size_t(cy) * std::size_t(f.cols) + std::size_t(cx);
        if (!f.hasSite[pi] || f.alert[pi]) continue;
        const float dx = f.siteX[pi] - u.x[std::size_t(i)];
        const float dy = f.siteY[pi] - u.y[std::size_t(i)];
        const float s = u.sight[std::size_t(i)];
        if (dx * dx + dy * dy <= s * s) f.alert[pi] = 1u;
    }

    // Spread: flood the alert through cells occupied by the SAME faction. This
    // is the chain of comrades — a rank charges because the rank in front of it
    // saw the enemy, however deep the formation, while a body with no friends
    // between it and the fighting is simply not connected and stays home.
    std::vector<std::uint32_t> queue;
    queue.reserve(cells);
    for (int s = 0; s < kMaxBattleFactions; ++s) {
        if (!f.has_faction(s)) continue;
        const std::size_t base = f.plane(s);
        queue.clear();
        for (std::size_t ci = 0; ci < cells; ++ci) {
            if (f.alert[base + ci]) queue.push_back(std::uint32_t(ci));
        }
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const std::uint32_t ci = queue[head];
            const int cx = int(ci) % f.cols;
            const int cy = int(ci) / f.cols;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) continue;
                    const int nx = cx + ox, ny = cy + oy;
                    if (nx < 0 || ny < 0 || nx >= f.cols || ny >= f.rows) continue;
                    const std::size_t ni = std::size_t(ny) * std::size_t(f.cols)
                                         + std::size_t(nx);
                    const std::size_t pi = base + ni;
                    // Only through comrades, and only where the enemy is known.
                    if (f.alert[pi] || f.count[pi] == 0u || !f.hasSite[pi]) continue;
                    f.alert[pi] = 1u;
                    queue.push_back(std::uint32_t(ni));
                }
            }
        }
    }
}

// ── Deployment ─────────────────────────────────────────────────────────────

bool deploy_army_slot(float centreX, float centreY, int side, int i, int count,
                      float* outXY) {
    if (!outXY || count < 1 || i < 0 || i >= count || side < 0 || side > 1)
        return false;
    // One spacing apart (≈ two body radii + slack) in a roughly square block,
    // the two blocks kFrontGap apart along X, each facing the other.
    constexpr float kSpacing = 3.0f;
    // The two front lines must start INSIDE a soldier's sight (CombatTemplate::
    // sight, 200 by default) or the staged fight never begins: bodies advance
    // because someone saw the enemy, not because a harness told them to.
    constexpr float kFrontGap = 120.0f;
    const int cols = std::max(1, int(std::sqrt(float(count)) + 0.5f));
    const int col = i % cols;
    const int row = i / cols;
    const float ox = (float(col) - float(cols - 1) * 0.5f) * kSpacing;
    const float facing = side == 0 ? -1.0f : 1.0f;
    // Rank 0 is the FRONT rank: depth grows away from the enemy, so both front
    // lines sit at ±kFrontGap/2 whatever the army size.
    outXY[0] = centreX + facing * (kFrontGap * 0.5f + float(row) * kSpacing);
    outXY[1] = centreY + ox;
    return true;
}

// ── The steering pass ──────────────────────────────────────────────────────

void steer_battle(BattleUnits& u, const UnitGrid& fine, const UnitGrid& pick,
                  const InfluenceField& field, const BattleTerrain& terrain,
                  const BattleParams& prm, float dt, BattleStats* stats) {
    if (stats) *stats = BattleStats{};
    if (u.count <= 0 || dt <= 0.0f) return;

    const float bound = terrain.worldMax > 2.0f ? terrain.worldMax - 2.0f : 2.0f;
    std::uint64_t visits = 0;
    std::uint32_t engaged = 0, steered = 0, advancing = 0;

    for (int i = 0; i < u.count; ++i) {
        const std::size_t si = std::size_t(i);
        u.target[si] = -1;
        u.inReach[si] = 0u;

        const float px = u.x[si], py = u.y[si], pz = u.z[si];
        const float ri = u.radius[si];
        const bool pinned = (u.flags[si] & BU_Pinned) != 0u;

        // ── 1. Contact scan ────────────────────────────────────────────────
        // On the PICK grid, whose cell is the crowd's longest reach: a query of
        // that radius therefore touches at most 2×2 cells regardless of whether
        // the fighter is a peasant with a stick or a sorceress with a 25-unit
        // bolt. Occupancy per cell is bounded by body geometry, and the visit
        // ceiling bounds the degenerate tail.
        const float pickR = u.reach[si] + ri + u.maxRadius;
        int best = -1;
        float bestD2 = pickR * pickR;
        if (u.enemyMask[si] != 0ull) {
            const int c0 = pick.col_of(px - pickR), c1 = pick.col_of(px + pickR);
            const int r0 = pick.row_of(py - pickR), r1 = pick.row_of(py + pickR);
            int seen = 0;
            for (int cy = r0; cy <= r1 && seen < prm.maxPickVisits; ++cy) {
                for (int cx = c0; cx <= c1 && seen < prm.maxPickVisits; ++cx) {
                    const std::size_t ci = std::size_t(cy) * std::size_t(pick.cols)
                                         + std::size_t(cx);
                    const std::uint32_t b = pick.begin[ci];
                    const std::uint32_t e = pick.begin[ci + 1u];
                    const std::uint32_t span = e - b;
                    std::uint32_t k = b + bucket_offset(i, span);
                    for (std::uint32_t n = 0; n < span
                             && seen < prm.maxPickVisits; ++n) {
                        const int j = pick.items[k];
                        if (++k >= e) k = b;
                        ++visits;
                        ++seen;
                        if (j == i || !u.hostile(i, j)) continue;
                        const std::size_t sj = std::size_t(j);
                        const float dx = u.x[sj] - px, dy = u.y[sj] - py;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < bestD2) { bestD2 = d2; best = j; }
                    }
                }
            }
        }

        // ── 2. Where this body WANTS to be ─────────────────────────────────
        float seekX = 0.0f, seekY = 0.0f;
        if (best >= 0) {
            const std::size_t sb = std::size_t(best);
            const float dx = u.x[sb] - px, dy = u.y[sb] - py;
            const float d = length2d(dx, dy) + 1.0e-4f;
            // Stand ON the engagement ring, not in the target's centre. Every
            // attacker approaches from its own bearing, so a mob spreads AROUND
            // its victim instead of collapsing onto one point.
            const float ring = prm.ringFactor * u.reach[si] + u.radius[sb];
            const float want = d - ring;
            if (want > prm.arriveEpsilon) {
                seekX = dx / d * want;
                seekY = dy / d * want;
            }
            u.target[si] = best;
            // Reach is judged in 3D — a flier overhead is out of a sword's
            // reach even when its ground shadow is under your feet.
            const float dz = u.z[sb] - pz;
            const float reach3 = u.reach[si] + u.radius[sb];
            if (bestD2 + dz * dz <= reach3 * reach3) {
                u.inReach[si] = 1u;
                ++engaged;
            }
        } else if (u.enemyMask[si] != 0ull && !field.hasSite.empty()) {
            // No contact: read ONE field cell. This is the whole long-range
            // navigation system — no neighbour query, no player special case,
            // and no distance leash. The body advances iff its cell is ALERTED,
            // which is either its own eyesight or a comrade's relayed down the
            // formation. That single rule is what makes a deep army charge as one
            // mass while scattered animals mind their own business.
            const int fi = u.faction[si];
            if (field.has_faction(fi)) {
                const int cx = field.col_of(px);
                const int cy = field.row_of(py);
                const std::size_t pi = field.plane(fi)
                    + std::size_t(cy) * std::size_t(field.cols) + std::size_t(cx);
                if (field.hasSite[pi] && field.alert[pi]) {
                    const float sx = field.siteX[pi], sy = field.siteY[pi];
                    const float selfX = sx - px, selfY = sy - py;
                    const float self2 = selfX * selfX + selfY * selfY;
                    if (self2 <= field.cell * field.cell) {
                        // Last mile: the enemy mass is within one field cell, so
                        // home in on it directly. Converging IS correct here —
                        // this is the melee, and the engagement ring plus body
                        // separation are what keep it from becoming a pile.
                        seekX = selfX;
                        seekY = selfY;
                    } else {
                        // Approach: read the field as a FLOW, not as a
                        // destination. The bearing is taken from the CELL CENTRE
                        // rather than from this body's own position, so every body
                        // in a cell advances along the SAME vector — a slab in
                        // translation, formation width preserved.
                        //
                        // Steering at `site - px` at long range (what shipped
                        // first) makes the site a point attractor: every body whose
                        // transform resolves to the same enemy cell converges onto
                        // one spot, and a thousand-strong deployment squeezes
                        // itself into a ball within seconds. That was visible
                        // in-game as armies standing in tight clumps, and the first
                        // version of this test missed it because a ball at
                        // 1.27-unit spacing still satisfies a nearest-neighbour
                        // check — the crowd's EXTENT is what collapses, and it is
                        // asserted now.
                        seekX = sx - field.cell_centre_x(cx);
                        seekY = sy - field.cell_centre_y(cy);
                    }
                    ++advancing;
                }
            }
        }

        if (pinned) continue;   // a target and an obstacle, but not our body to move

        // ── 3. Bodies push each other apart ────────────────────────────────
        // On the FINE grid, sized to the bodies themselves.
        float sepX = 0.0f, sepY = 0.0f;
        {
            const float qr = (ri + u.maxRadius) * prm.sepRadiusScale;
            const int c0 = fine.col_of(px - qr), c1 = fine.col_of(px + qr);
            const int r0 = fine.row_of(py - qr), r1 = fine.row_of(py + qr);
            int taken = 0, seen = 0;
            for (int cy = r0; cy <= r1 && taken < prm.maxSepNeighbors
                                      && seen < prm.maxSepVisits; ++cy) {
                for (int cx = c0; cx <= c1 && taken < prm.maxSepNeighbors
                                          && seen < prm.maxSepVisits; ++cx) {
                    const std::size_t ci = std::size_t(cy) * std::size_t(fine.cols)
                                         + std::size_t(cx);
                    const std::uint32_t b = fine.begin[ci];
                    const std::uint32_t e = fine.begin[ci + 1u];
                    const std::uint32_t span = e - b;
                    std::uint32_t k = b + bucket_offset(i, span);
                    for (std::uint32_t n = 0; n < span
                             && seen < prm.maxSepVisits; ++n) {
                        const int j = fine.items[k];
                        if (++k >= e) k = b;
                        ++visits;
                        ++seen;
                        if (j == i) continue;
                        const std::size_t sj = std::size_t(j);
                        float dx = px - u.x[sj], dy = py - u.y[sj];
                        const float d = length2d(dx, dy);
                        const float touch = (ri + u.radius[sj]) * prm.sepRadiusScale;
                        if (d >= touch) continue;
                        if (d < 1.0e-3f) tie_break_dir(i, j, dx, dy);
                        else { dx /= d; dy /= d; }
                        const float force = 1.0f - d / touch;
                        sepX += dx * force;
                        sepY += dy * force;
                        if (++taken >= prm.maxSepNeighbors) break;
                    }
                }
            }
        }

        // ── 4. Terrain: ground type from the table, slope from the height ───
        // Slope steering is kept in its OWN accumulator: folding it into `sep`
        // would multiply it by sepWeight and let terrain outvote the advance.
        float terrX = 0.0f, terrY = 0.0f;
        float speedMul = 1.0f;
        if ((u.flags[si] & BU_Flying) == 0u) {
            if (terrain.tiles && terrain.tileSpeed && terrain.tileDim > 0) {
                const int tx = std::clamp(int(px), 0, terrain.tileDim - 1);
                const int ty = std::clamp(int(py), 0, terrain.tileDim - 1);
                const std::uint8_t id = terrain.tiles[std::size_t(ty)
                    * std::size_t(terrain.tileDim) + std::size_t(tx)];
                if (int(id) < terrain.tileSpeedCount)
                    speedMul *= terrain.tileSpeed[id];
            }
            if (terrain.heightAt) {
                const float a = prm.slopeProbe;
                const float hx = terrain.heightAt(terrain.user, px + a, py)
                               - terrain.heightAt(terrain.user, px - a, py);
                const float hy = terrain.heightAt(terrain.user, px, py + a)
                               - terrain.heightAt(terrain.user, px, py - a);
                const float gx = hx / (2.0f * a), gy = hy / (2.0f * a);
                const float grade = length2d(gx, gy);
                const float seekNow = length2d(seekX, seekY);
                // Terrain steers a body that is GOING somewhere; it is not a force
                // acting on one that is standing. Ungated, a slope pushes idle
                // bodies downhill forever and they pool in whatever local
                // depression is nearest — observed in-game as victorious guards
                // collecting in a pit after their enemies were dead.
                if (grade > 1.0e-4f && seekNow > 1.0e-4f) {
                    // Steer away only from what cannot be walked. Everything
                    // gentler is a cost, not an obstacle.
                    const float excess = grade - prm.slopeWalkGrade;
                    if (excess > 0.0f) {
                        const float w = std::min(excess, 1.0f) * prm.slopeAvoid;
                        terrX += -gx / grade * w;
                        terrY += -gy / grade * w;
                    }
                    // Climbing costs speed, with a floor: a charge up a hill is
                    // slower than one across a field, never a standstill.
                    const float uphill = (seekX * gx + seekY * gy) / seekNow;
                    if (uphill > 0.0f) {
                        speedMul *= std::max(prm.slopeSpeedFloor,
                            1.0f / (1.0f + uphill * prm.slopeSpeedDrop));
                    }
                }
            }
        }

        // ── 5. Blend, accelerate, integrate ────────────────────────────────
        const float seekLen = length2d(seekX, seekY);
        float dirX = 0.0f, dirY = 0.0f;
        if (seekLen > 1.0e-4f) { dirX = seekX / seekLen; dirY = seekY / seekLen; }
        dirX += sepX * prm.sepWeight;
        dirY += sepY * prm.sepWeight;
        dirX += terrX;                 // already carries its own (sub-unit) weight
        dirY += terrY;
        const float dirLen = length2d(dirX, dirY);

        float wantVx = 0.0f, wantVy = 0.0f;
        if (dirLen > 1.0e-4f) {
            const float sp = u.speed[si] * speedMul;
            wantVx = dirX / dirLen * sp;
            wantVy = dirY / dirLen * sp;
        }

        // Acceleration limit is what makes a charge read as MASS: no unit flips
        // its velocity in one frame, so lines bend instead of snapping.
        const float amax = u.speed[si] * prm.accelPerSpeed * dt;
        float dvx = wantVx - u.vx[si], dvy = wantVy - u.vy[si];
        const float dvLen = length2d(dvx, dvy);
        if (dvLen > amax && dvLen > 1.0e-6f) {
            dvx = dvx / dvLen * amax;
            dvy = dvy / dvLen * amax;
        }
        u.vx[si] += dvx;
        u.vy[si] += dvy;
        u.x[si] = std::clamp(px + u.vx[si] * dt, 1.0f, bound);
        u.y[si] = std::clamp(py + u.vy[si] * dt, 1.0f, bound);
        ++steered;
    }

    if (stats) {
        stats->neighborVisits = visits;
        stats->engaged = engaged;
        stats->steered = steered;
        stats->advancing = advancing;
    }
}

} // namespace sm::sub

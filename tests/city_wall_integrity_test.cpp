// Locks the two things the owner could see wrong with a town from inside it
// (2026-09-13): walls with HOLES in them, and gates that came in PAIRS.
//
// Neither was visible to any existing assertion. The parity test counted wall
// records (>=40) and the population test checked that citizens stand inside
// the outermost stamped masonry — both of which a ring full of gaps passes
// comfortably, because a gap emits no record to count and no citizen stands in
// one. So this file asks the question a player asks with his feet: is every
// opening in the curtain a GATE, or can you simply walk in?
//
// THE THREE BLIND DETECTORS THIS REPLACED, because each blindness is a lesson
// about testing generated geometry, and the negative control at the bottom is
// what exposed all three:
//
//   1. A flood-fill from the cell border that treated paving as impassable.
//      It passed a city with a wedge knocked out of all three of its curtains,
//      because a city's RING ROADS are paved circles and stopped the flood
//      long before it reached the heart.
//   2. A per-bearing ray asking "is there masonry along here?". At a gateway
//      it got "yes" from an INNER curtain standing behind the outer one's
//      opening, so a multi-ring town could not be found to have a gate at all.
//   3. The same, with a fixed 720 bearings. At a village's 78-tile radius one
//      bearing spans two thirds of a tile, so the ring cannot cover every bin
//      and the audit reported twenty-two "breaches" that were pure sampling
//      aliasing. Resolution is now one bin per tile of perimeter.
//
// What survives asks it exactly: isolate the OUTER curtain by radius, walk its
// gaps, and inspect the chord between the two real masonry tiles bounding each
// gap. A gap with a road across it is a gate; a gap with bare ground across it
// is a breach.

#include "check.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

constexpr float kTwoPi = 6.28318530718f;

struct Town {
    SubworldMapData map;
    int centre = kCellSize / 2;
};

// Build one settlement. `roadDirs` is a bitmask over the 8 kDirOffsets
// directions that carry a road, so both the connected case and the isolated
// one (which must still leave a postern) are reachable.
Town make_town(LandmarkType kind, std::uint32_t seed, int population,
               unsigned roadDirs) {
    CellContext c{};
    c.cx = 64;
    c.cy = 64;
    c.macroHeight = 0.64f;
    c.biome = Meadow;
    c.feature = FT_None;
    c.landmark.id = 101;
    c.landmark.size = population;
    c.landmark.kind = kind;
    c.seed = seed;

    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = 0.64f;
        nbB[i] = Meadow;
        nbF[i] = std::uint8_t(FT_None);
    }
    for (int d = 0; d < 8; ++d) {
        if ((roadDirs & (1u << unsigned(d))) == 0u) continue;
        const int dx = kDirOffsets[d][0];
        const int dy = kDirOffsets[d][1];
        nbF[(dy + 1) * 3 + (dx + 1)] = std::uint8_t(FT_Road);
    }

    Town t{};
    dispatch_generate(c, nbH, nbB, nbF, t.map);
    return t;
}

struct GateAudit {
    int bearings = 0;
    int walled = 0;        // bearings carrying the outer curtain
    int gates = 0;         // gaps with a road across them
    int breaches = 0;      // gaps with nothing across them
};

GateAudit audit_ring(const Town& t) {
    const float cf = float(t.centre);

    // Furthest masonry anywhere — this sets the sampling resolution at one bin
    // per tile of perimeter, so the ring physically cannot miss a bin.
    float reach = 0.0f;
    for (std::size_t i = 0; i < t.map.tiles.size(); ++i) {
        if (t.map.tiles[i] != TILE_WALL) continue;
        const float dx = float(int(i % kCellSize)) - cf;
        const float dy = float(int(i / kCellSize)) - cf;
        reach = std::max(reach, std::sqrt(dx * dx + dy * dy));
    }
    GateAudit a{};
    if (reach <= 0.0f) return a;
    const int bins = std::clamp(int(kTwoPi * reach), 64, 8192);
    a.bearings = bins;

    std::vector<float> prof(std::size_t(bins), -1.0f);
    for (std::size_t i = 0; i < t.map.tiles.size(); ++i) {
        if (t.map.tiles[i] != TILE_WALL) continue;
        const float dx = float(int(i % kCellSize)) - cf;
        const float dy = float(int(i / kCellSize)) - cf;
        float ang = std::atan2(dy, dx);
        if (ang < 0.0f) ang += kTwoPi;
        int b = int(ang / kTwoPi * float(bins));
        if (b >= bins) b = bins - 1;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > prof[std::size_t(b)]) prof[std::size_t(b)] = d;
    }

    // Which bearings carry the OUTERMOST curtain: a bearing whose furthest
    // masonry falls well short of the town's typical reach is one where the
    // outer ring is absent and only an inner ring answered.
    std::vector<float> sorted = prof;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted[std::size_t(bins / 2)];
    // Dilate by one bin. The curtain is laid with a 3×3 brush every half tile,
    // so it is a CONNECTED band three tiles thick — a one-bin gap in it is
    // physically impossible and purely an artefact of binning a discrete curve
    // by angle (two adjacent ring tiles skip a bin whenever the ring steps
    // radially). A real gate is the width of a road, five bins and more, and
    // survives the dilation with room to spare.
    std::vector<char> present(std::size_t(bins), 0);
    for (int b = 0; b < bins; ++b) {
        bool near = false;
        for (int k = -1; k <= 1; ++k) {
            const std::size_t j = std::size_t((b + k + bins) % bins);
            if (prof[j] >= median * 0.8f) { near = true; break; }
        }
        present[std::size_t(b)] = near ? 1 : 0;
        if (near) ++a.walled;
    }
    if (a.walled == 0 || a.walled == bins) return a;   // no ring, or no opening

    // A gap's endpoints must be REAL masonry. `present` is dilated, so a bin
    // can be present because its neighbour carries the curtain while its own
    // radius is still -1 — and taking that at face value put the endpoint at
    // the cell centre, collapsed the chord to nothing, and sampled the PLAZA,
    // which is paved. Every breach then read as a gate. (Found by the negative
    // control, which is the only reason this file is worth anything.)
    auto solid_bin = [&](int b, int dir) {
        for (int k = 0; k < bins; ++k) {
            const int j = ((b + dir * k) % bins + bins) % bins;
            if (prof[std::size_t(j)] >= median * 0.8f) return j;
        }
        return b;
    };
    auto point_at = [&](int b) {
        const float ang = float(b) * kTwoPi / float(bins);
        return std::array<float, 2>{cf + std::cos(ang) * prof[std::size_t(b)],
                                    cf + std::sin(ang) * prof[std::size_t(b)]};
    };

    int start = 0;
    while (!present[std::size_t(start)]) ++start;   // begin on solid masonry
    int b = 0;
    while (b < bins) {
        const int cur = (start + b) % bins;
        if (present[std::size_t(cur)]) { ++b; continue; }
        int len = 0;
        while (len < bins && !present[std::size_t((start + b + len) % bins)]) ++len;
        const std::array<float, 2> A = point_at(solid_bin((start + b - 1 + bins) % bins, -1));
        const std::array<float, 2> B = point_at(solid_bin((start + b + len) % bins, +1));
        const float dx = B[0] - A[0], dy = B[1] - A[1];
        const float span = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(span)));
        bool paved = false;
        for (int s = 0; s <= steps && !paved; ++s) {
            const float tt = float(s) / float(steps);
            const int x = int(std::floor(A[0] + dx * tt));
            const int y = int(std::floor(A[1] + dy * tt));
            if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
            paved = tile_is(t.map.tiles[std::size_t(y) * kCellSize + x], kTilePaved);
        }
        if (paved) ++a.gates; else ++a.breaches;
        b += len;
    }
    return a;
}

int count_tiles(const Town& t, std::uint8_t tile) {
    int n = 0;
    for (std::uint8_t v : t.map.tiles) if (v == tile) ++n;
    return n;
}

// No house solid may swallow a wall tile — a curtain sliced open by a roof is
// exactly what the owner photographed.
int houses_in_masonry(const Town& t) {
    int bad = 0;
    for (const Structure& s : t.map.structures) {
        if (s.kind != Structure::House) continue;
        const float cs = std::cos(s.yaw), sn = std::sin(s.yaw);
        const float ex = std::fabs(s.hx * cs) + std::fabs(s.hy * sn);
        const float ey = std::fabs(s.hx * sn) + std::fabs(s.hy * cs);
        const int x0 = std::max(0, int(std::floor(s.x - ex)));
        const int y0 = std::max(0, int(std::floor(s.y - ey)));
        const int x1 = std::min(kCellSize - 1, int(std::ceil(s.x + ex)));
        const int y1 = std::min(kCellSize - 1, int(std::ceil(s.y + ey)));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float dx = float(x) + 0.5f - s.x;
                const float dy = float(y) + 0.5f - s.y;
                const float lx =  dx * cs + dy * sn;
                const float ly = -dx * sn + dy * cs;
                if (std::fabs(lx) > s.hx || std::fabs(ly) > s.hy) continue;
                if (t.map.tiles[std::size_t(y) * kCellSize + x] == TILE_WALL) ++bad;
            }
        }
    }
    return bad;
}

void check_town(const char* what, LandmarkType kind, std::uint32_t seed,
                int population, unsigned roadDirs) {
    const Town t = make_town(kind, seed, population, roadDirs);
    char msg[256];

    const int wallTiles = count_tiles(t, TILE_WALL);
    std::snprintf(msg, sizeof msg, "%s: raises a wall (%d tiles)", what, wallTiles);
    CHECK(wallTiles > 200, msg);

    const GateAudit a = audit_ring(t);
    std::snprintf(msg, sizeof msg,
                  "%s: every opening is a gate (walled %d, gates %d, BREACHES %d)",
                  what, a.walled, a.gates, a.breaches);
    CHECK(a.breaches == 0, msg);

    std::snprintf(msg, sizeof msg, "%s: has a gateway (%d gates)", what, a.gates);
    CHECK(a.gates > 0, msg);

    std::snprintf(msg, sizeof msg, "%s: is mostly curtain (%d of %d bearings)",
                  what, a.walled, a.bearings);
    CHECK(a.walled > a.bearings / 2, msg);

    // Towers stand where the ring turns — a parameter-free rule, so the count
    // is whatever the shape asks for. It must not be zero (a curtain with no
    // tower does not read as fortified) and must not be every node (that was
    // the old "every other node" lattice, which scaled with the node count
    // rather than with the wall).
    int towers = 0;
    for (const Structure& s : t.map.structures) {
        if (s.kind == Structure::Wall && s.shape == Structure::Cylinder
            && s.radius > 2.5f) ++towers;   // > a gate jamb
    }
    std::snprintf(msg, sizeof msg, "%s: the ring is towered (%d towers)", what, towers);
    CHECK(towers >= 4, msg);

    const int cut = houses_in_masonry(t);
    std::snprintf(msg, sizeof msg, "%s: no house cuts the wall (%d tiles)", what, cut);
    CHECK(cut == 0, msg);
}

// NEGATIVE CONTROL — a green detector proves nothing until it is shown to go
// red. Knock a breach in a sound town's curtain and demand the audit see it.
// The wedge is deliberately aimed BETWEEN the cardinal roads: aimed at one it
// merely widens that gate, and the audit rightly keeps calling it a gate —
// which is exactly how the first version of this control fooled itself.
void check_detector_has_teeth() {
    Town t = make_town(LandmarkType::City, 1234u, 6000, 0b00001111u);
    CHECK(audit_ring(t).breaches == 0, "control: the intact town is sound first");

    // The wedge is cleared of BOTH masonry and paving. Masonry alone is not
    // enough: a field track leaving a gate elsewhere can wander across the
    // wedge, and the audit then rightly reports a road across that gap — which
    // is how the second version of this control fooled itself. What is being
    // asserted is "a wedge of BARE GROUND in the curtain is seen".
    const float cf = float(t.centre);
    int knocked = 0;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t i = std::size_t(y) * kCellSize + x;
            const bool masonry = t.map.tiles[i] == TILE_WALL;
            const bool paving  = tile_is(t.map.tiles[i], kTilePaved);
            if (!masonry && !paving) continue;
            float ang = std::atan2(float(y) - cf, float(x) - cf);
            if (ang < 0.0f) ang += kTwoPi;
            if (ang < 2.00f || ang > 2.21f) continue;  // ~12°, clear of any cardinal
            t.map.tiles[i] = TILE_GRASS;
            t.map.trav[i] = 1;
            if (masonry) ++knocked;
        }
    }
    char msg[192];
    std::snprintf(msg, sizeof msg, "control: the breach removed masonry (%d tiles)",
                  knocked);
    CHECK(knocked > 20, msg);

    const GateAudit a = audit_ring(t);
    std::snprintf(msg, sizeof msg, "control: the audit SEES a breach (%d breaches)",
                  a.breaches);
    CHECK(a.breaches > 0, msg);
}

} // namespace

int main() {
    // Connected towns: roads from four directions, then a stair-stepped pair —
    // the neighbourhood that used to produce two gates a few tiles apart with
    // an empty arch between them.
    check_town("city/cardinals",   LandmarkType::City,    1234u,   6000, 0b00001111u);
    check_town("city/stairstep",   LandmarkType::City,    77u,     2400, 0b00010001u);
    check_town("city/one road",    LandmarkType::City,    9001u,   800,  0b00000001u);
    // Isolated: no road neighbour at all. The postern is the only way in, and
    // the curtain must still close everywhere else.
    check_town("city/isolated",    LandmarkType::City,    424242u, 1200, 0u);
    check_town("village/walled",   LandmarkType::Village, 5150u,   400,  0b00000011u);
    check_town("village/isolated", LandmarkType::Village, 8080u,   300,  0u);
    check_detector_has_teeth();
    return sm::test::report("city_wall_integrity_test");
}

// Is a GATE still a gate on sloping ground?
//
// A lintel is a Box lifted by `kGateClearM` above the terrain under ITS OWN
// CENTRE (sub/height.h kGateClearM; the renderer and the collision index both
// resolve the lift against that one sample). The jambs it bridges stand on
// their own ground, and so does the roadway beneath it. On a flat cell those
// are the same height and the arch is exactly as high as it promises. On a
// hillside they are not, and nothing in the generator ever compared them.
//
// WHY THIS FILE EXISTS AND THE OLD ONE DID NOT SEE IT: every settlement
// fixture in the suite builds its town on a FLAT macro neighbourhood — all
// nine heights equal (city_wall_integrity_test make_town, 0.64 everywhere).
// A seat measured at the centre is trivially right when the ground is level,
// so "seven lintels, zero rejected" was a measurement of the fixture, not of
// the law. This one puts the town on a real hillside and asks what a rider
// asks: can I ride through, anywhere across the opening?
//
// The promise asserted is the gate's own published one (sub/height.h
// kGateClearM), not a number restated here.

#include "check.h"
#include "sub/gens/dispatch.h"
#include "sub/height.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

// A town on a named slice of country. `nbH` is the 3×3 macro height
// neighbourhood — that is the ONLY thing this fixture varies from the flat one
// the rest of the suite uses, because that is the whole question.
SubworldMapData make_town(std::uint32_t seed, int population,
                          const float (&nbH)[9]) {
    CellContext c{};
    c.cx = 64;
    c.cy = 64;
    c.macroHeight = nbH[4];
    c.biome = Meadow;
    c.feature = FT_None;
    c.landmark.id = 101;
    c.landmark.size = population;
    c.landmark.kind = LandmarkType::City;
    c.seed = seed;

    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbB[i] = Meadow;
        nbF[i] = std::uint8_t(FT_None);
    }
    // Roads on the four cardinals: a town people actually leave, so the ring
    // has openings to measure at all.
    for (int d = 0; d < 8; ++d) {
        const int dx = kDirOffsets[d][0];
        const int dy = kDirOffsets[d][1];
        if (dx != 0 && dy != 0) continue;
        nbF[(dy + 1) * 3 + (dx + 1)] = std::uint8_t(FT_Road);
    }

    SubworldMapData map;
    dispatch_generate(c, nbH, nbB, /*nbBiome5*/nullptr, nbF, map);
    return map;
}

// The terrain the town was built on, IN METRES — the space a lintel's zBase
// is stated in. The heightmap itself is normalised [0..2]; metres come from
// the one scale every other reader uses (sub/height.h kHeightScaleM). Mixing
// the two silently turns a 300-metre mountainside into "0.2".
float ground_at(const SubworldMapData& m, float fx, float fy) {
    const int x = std::clamp(int(std::floor(fx)), 0, kCellSize - 1);
    const int y = std::clamp(int(std::floor(fy)), 0, kCellSize - 1);
    return m.heightmap[std::size_t(y) * kCellSize + x] * kHeightScaleM;
}

// The clear a gateway actually offers: from the WORST ground anywhere under
// the bar up to the bar's underside. The worst point is the one that decides —
// an arch whose downhill half is open and whose uphill half is inside the hill
// is not a gate you can ride through, and its midpoint says nothing about that.
float gate_clear(const SubworldMapData& m, const Structure& lintel) {
    const float hl = structure_half_x(lintel);
    const float ux = std::cos(lintel.yaw);
    const float uy = std::sin(lintel.yaw);
    // Subtract the two GROUND samples from each other and take the lift off
    // that difference — never `(ground + lift) - ground`. Terrain sits around
    // a thousand metres, a float holds seven digits, and that spelling loses
    // five millimetres of the answer to cancellation: enough to fail an exact
    // comparison against the lift it is supposed to reproduce.
    const float seat = ground_at(m, lintel.x, lintel.y);
    float rise = 0.0f;              // worst ground under the bar, above seat
    const int steps = std::max(2, int(hl * 2.0f));
    for (int s = 0; s <= steps; ++s) {
        const float t = -hl + 2.0f * hl * float(s) / float(steps);
        rise = std::max(rise, ground_at(m, lintel.x + ux * t,
                                           lintel.y + uy * t) - seat);
    }
    return lintel.zBase - rise;
}

// Is this point inside a building's footprint?
bool inside_a_house(const SubworldMapData& m, float px, float py) {
    for (const Structure& s : m.structures) {
        if (s.kind != Structure::House) continue;
        const float cs = std::cos(s.yaw), sn = std::sin(s.yaw);
        const float dx = px - s.x, dy = py - s.y;
        const float lx =  dx * cs + dy * sn;
        const float ly = -dx * sn + dy * cs;
        if (std::fabs(lx) <= structure_half_x(s)
         && std::fabs(ly) <= structure_half_y(s)) {
            return true;
        }
    }
    return false;
}

// THE MOUTH OF THE GATE. A gateway is a way through, and a way through needs
// room on BOTH sides of the masonry — the road does not begin at the wall.
// Nothing forbade a house from standing in that room, and two gates of the
// upper quarter were photographed with one doing exactly that (owner,
// 2026-09-13). The town now hands its own gateways to its own plot layer as
// ground already spoken for (gens/city.cpp, kit/plots.h KeepOut).
//
// The reach asked for is the GATE'S OWN WIDTH — a way through is at least as
// deep as it is wide — so no distance is invented here either.
int gates_with_a_blocked_mouth(const SubworldMapData& m) {
    int blocked = 0;
    for (const Structure& s : m.structures) {
        if (s.kind != Structure::Wall || s.zBase <= 0.0f) continue;
        const float nx = -std::sin(s.yaw);   // across the masonry: the road
        const float ny =  std::cos(s.yaw);
        const float span = structure_half_x(s) * 2.0f;
        bool bad = false;
        for (int side = -1; side <= 1 && !bad; side += 2) {
            // Start clear of the wall's own thickness; walk out one span.
            for (int d = 2; d <= int(span); ++d) {
                if (inside_a_house(m, s.x + nx * float(side * d),
                                      s.y + ny * float(side * d))) {
                    bad = true;
                    break;
                }
            }
        }
        if (bad) ++blocked;
    }
    return blocked;
}

struct GateReport {
    int lintels = 0;
    float worstClear = 0.0f;
    float worstX = 0.0f, worstY = 0.0f;
    float relief = 0.0f;       // height range across the built-up disk
    int blockedMouths = 0;
};

GateReport audit(const SubworldMapData& m) {
    GateReport r{};
    for (const Structure& s : m.structures) {
        if (s.kind != Structure::Wall || s.zBase <= 0.0f) continue;
        const float c = gate_clear(m, s);
        if (r.lintels == 0 || c < r.worstClear) {
            r.worstClear = c;
            r.worstX = s.x;
            r.worstY = s.y;
        }
        ++r.lintels;
    }
    // The relief of the ground the town was built on — printed so a green run
    // states the country it was green ON. A sweep that turns out to be flat is
    // not evidence about slopes, and that is precisely how the last
    // measurement of this defect misled (see the file header).
    float lo = 1e9f, hi = -1e9f;
    for (float h : m.heightmap) { lo = std::min(lo, h); hi = std::max(hi, h); }
    r.relief = hi - lo;
    r.blockedMouths = gates_with_a_blocked_mouth(m);
    return r;
}

void check_slope(const char* what, std::uint32_t seed, int population,
                 const float (&nbH)[9]) {
    const SubworldMapData m = make_town(seed, population, nbH);
    const GateReport r = audit(m);
    char msg[256];

    std::snprintf(msg, sizeof msg, "%s: the ring has openings (%d lintels)",
                  what, r.lintels);
    CHECK(r.lintels > 0, msg);
    if (r.lintels == 0) return;

    std::snprintf(msg, sizeof msg,
                  "%s: every gate keeps its promised clear "
                  "(worst %.2f m of %.2f at %.0f,%.0f; cell relief %.1f m)",
                  what, double(r.worstClear), double(kGateClearM),
                  double(r.worstX), double(r.worstY), double(r.relief));
    CHECK(r.worstClear >= kGateClearM, msg);

    std::snprintf(msg, sizeof msg,
                  "%s: no house stands in a gateway's mouth (%d of %d gates)",
                  what, r.blockedMouths, r.lintels);
    CHECK(r.blockedMouths == 0, msg);
}

// NEGATIVE CONTROL — the detector must be shown to go red, or a green run is
// only evidence that it cannot see. Drop every lintel onto the roadway (the
// seat a bar would have if nobody lifted it) and demand the audit says so.
void check_detector_has_teeth() {
    float flat[9];
    for (int i = 0; i < 9; ++i) flat[i] = 0.64f;
    SubworldMapData m = make_town(4242u, 6000, flat);
    const GateReport sound = audit(m);
    CHECK(sound.lintels > 0, "control: the town has gates to break");
    CHECK(sound.worstClear >= kGateClearM,
          "control: the flat town's gates are sound first");

    for (Structure& s : m.structures) {
        if (s.kind == Structure::Wall && s.zBase > 0.0f) s.zBase = 0.0f;
    }
    const GateReport broken = audit(m);
    char msg[160];
    std::snprintf(msg, sizeof msg,
                  "control: an unlifted lintel is SEEN (clear %.2f m)",
                  double(broken.worstClear));
    CHECK(broken.worstClear < kGateClearM, msg);
}

} // namespace

int main() {
    // Flat country first: the case the rest of the suite already covers, kept
    // here so a failure elsewhere can be told apart from a failure of slope.
    {
        float flat[9];
        for (int i = 0; i < 9; ++i) flat[i] = 0.64f;
        check_slope("flat city", 12345u, 6000, flat);
    }

    // A hillside. The macro neighbourhood falls from north-west to south-east,
    // so the cell carries a real gradient across its whole width and the ring
    // — which stands far outside the flattened plateau a settlement gets
    // (base_generator.cpp terrain_mod_for) — meets it at every bearing.
    {
        const float slope[9] = {0.90f, 0.82f, 0.74f,
                                0.82f, 0.74f, 0.66f,
                                0.74f, 0.66f, 0.58f};
        check_slope("hillside city", 12345u, 6000, slope);
        check_slope("hillside city (seed 2)", 777u, 6000, slope);
        check_slope("hillside town", 9001u, 1500, slope);
    }

    // A ridge running through the town: the steep case, where one side of an
    // opening can stand metres above the other.
    {
        const float ridge[9] = {0.50f, 0.95f, 0.50f,
                                0.50f, 0.95f, 0.50f,
                                0.50f, 0.95f, 0.50f};
        check_slope("ridge city", 12345u, 6000, ridge);
        check_slope("ridge city (seed 2)", 31337u, 6000, ridge);
    }

    check_detector_has_teeth();
    return sm::test::report("city_gate_lintel_test");
}

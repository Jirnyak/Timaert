// A settlement's people live INSIDE the settlement.
//
// The bug this pins: sub/spawn.cpp's citizen spawner drew a uniform random tile
// out of the WHOLE 1024×1024 macro cell, knowing nothing about where the town
// actually stood. A city walls a disk of ~4–8 % of its cell and a village ~1 %,
// so 92–99 % of every town's population was born in the fields and forests
// outside its own gates while the streets inside stood empty. The counts were
// right, the places were nonsense — which is why the existing spawn tests
// (settlement_faction_test, carried_light_spawn_test) passed throughout: they
// count bodies and components, never asking WHERE a body landed.
//
// The invariants below describe the SHAPE of the distribution, so they hold
// across seeds and populations:
//
//   1. NOT ONE citizen outside the built-up radius (the owner's ruling: no
//      "asymptotic minority" in the fields — strictly inside).
//   2. Inside the real masonry: no citizen further from the centre than the
//      nearest wall tile the generator stamped. This is the assertion that
//      cannot be satisfied by accident — it compares the spawner against the
//      GENERATOR's own output, not against its own formula.
//   3. Still spread, not clumped: area-uniform sampling puts ~75 % of a
//      population beyond half the radius and fills every angular sector. A
//      "fix" that dumped everyone on the market square would fail here.
//   4. Nobody born in water or inside a house/wall footprint.
//   5. The extracted footprint formulas (sub/city_layout.h) still reproduce the
//      literals gen_city / gen_village carried before they were hoisted out —
//      the refactor must not have moved a single wall.
#include "check.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/faction.h"
#include "sub/city_layout.h"
#include "sub/map_data.h"
#include "sub/seamless_manager.h"
#include "sub/spawn.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/city_population_inside_walls_test.cpp", 0);
    return 1;
}

constexpr float kTwoPi = 6.28318530718f;

// A cell resolver that puts ONE settlement of `pop` souls on cell (0,0) and
// leaves the eight neighbours plain meadow — so the composite the manager hands
// the spawner carries a real generated town, walls and all.
sm::sub::CellResolver settlement_resolver(sm::sub::CellLandmarkKind kind,
                                          int pop) {
    return [kind, pop](int cx, int cy) {
        sm::sub::CellContext c{};
        c.cx = cx;
        c.cy = cy;
        c.macroHeight = 0.62f;
        c.macroTemperature = 0.5f;
        c.biome = sm::Biome::Meadow;
        c.feature = sm::FT_None;
        c.landmark.id = -1;
        c.landmark.size = 0;
        c.landmark.kind = sm::sub::CellLandmarkKind::None;
        c.treeCount = 0;
        if (cx == 0 && cy == 0) {
            c.landmark.id = 7;
            c.landmark.size = pop;
            c.landmark.kind = kind;
        }
        c.seed = 0x5E771EDu ^ (std::uint32_t(cx) * 73856093u)
                            ^ (std::uint32_t(cy) * 19349663u);
        return c;
    };
}

struct Spread {
    int   citizens = 0;
    int   outsideRadius = 0;      // beyond the built-up radius
    int   outsideMasonry = 0;     // beyond the nearest stamped wall tile
    int   onSolid = 0;            // born in water / house / wall
    float maxR = 0.0f;
    float fracOuterHalf = 0.0f;   // share beyond half the radius
    int   emptySectors = 0;
};

// Per-azimuth radius of the OUTERMOST wall tile the generator actually stamped,
// binned finely enough that a gate opening (~8 tiles wide, under 3° at city
// scale) never empties a bin. Max, not min: a big city keeps inner rings and
// people rightly live between them — the ring that encloses the town is the
// outer one. This is measured from the generator's OUTPUT, so the assertion
// compares the spawner against the real masonry rather than against its own
// formula. A bin with no wall at all (an unwalled hamlet) reports 0 and is
// skipped by the caller.
constexpr int kWallBins = 64;

std::array<float, kWallBins> outer_wall_profile(
        const std::vector<std::uint8_t>& tiles, float cx, float cy) {
    std::array<float, kWallBins> prof{};
    const int x0 = sm::sub::kCellSize, x1 = sm::sub::kCellSize * 2;
    for (int y = x0; y < x1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (tiles[std::size_t(y) * sm::sub::kFullSize + x]
                != sm::sub::TILE_WALL) {
                continue;
            }
            const float dx = float(x) + 0.5f - cx;
            const float dy = float(y) + 0.5f - cy;
            float a = std::atan2(dy, dx);
            if (a < 0.0f) a += kTwoPi;
            const std::size_t b =
                std::size_t(int(a / kTwoPi * float(kWallBins)) % kWallBins);
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > prof[b]) prof[b] = d;
        }
    }
    return prof;
}

Spread measure(const sm::sub::SeamlessSubworldManager& mgr,
               sm::LandmarkKind landmark, int pop, std::uint32_t seed) {
    Spread s{};
    sm::ecs::World world{};
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             landmark,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             seed,
                             std::uint16_t(sm::faction_index("empire")),
                             pop,
                             /*zoneLevel*/0);

    const bool city = landmark == sm::LandmarkKind::City;
    const float radius = sm::sub::settlement_population_radius(city, pop);
    // The centre window cell (ox=0) occupies [kCellSize, 2·kCellSize); both
    // generators build on that cell's centre.
    const float cx = float(sm::sub::kCellSize) * 1.5f;
    const float cy = cx;
    const auto& tiles = mgr.tiles();
    const std::array<float, kWallBins> wallProfile =
        outer_wall_profile(tiles, cx, cy);

    std::array<int, 8> sector{};
    int outerHalf = 0;
    auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                               sm::ecs::Position>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        if (kind.type >= std::uint16_t(sm::NPCType::Count)) continue;  // fauna
        const auto& p = view.get<sm::ecs::Position>(e);
        ++s.citizens;
        const float dx = p.x - cx;
        const float dy = p.y - cy;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > s.maxR) s.maxR = d;
        if (d > radius + 1.5f) ++s.outsideRadius;   // +1.5: tile-centre rounding
        if (d > radius * 0.5f) ++outerHalf;
        float a = std::atan2(dy, dx);
        if (a < 0.0f) a += kTwoPi;
        ++sector[std::size_t(int(a / kTwoPi * 8.0f) % 8)];
        const float wallHere =
            wallProfile[std::size_t(int(a / kTwoPi * float(kWallBins))
                                    % kWallBins)];
        if (wallHere > 0.0f && d > wallHere) ++s.outsideMasonry;

        const int ix = int(p.x), iy = int(p.y);
        if (ix >= 0 && ix < sm::sub::kFullSize
            && iy >= 0 && iy < sm::sub::kFullSize) {
            const std::uint8_t t =
                tiles[std::size_t(iy) * sm::sub::kFullSize + ix];
            if (t == sm::sub::TILE_WATER || t == sm::sub::TILE_HOUSE
                || t == sm::sub::TILE_WALL) {
                ++s.onSolid;
            }
        }
    }
    if (s.citizens > 0) {
        s.fracOuterHalf = float(outerHalf) / float(s.citizens);
    }
    for (int i = 0; i < 8; ++i) if (sector[std::size_t(i)] == 0) ++s.emptySectors;
    return s;
}

// ── 5. The hoisted formulas still describe the old walls ────────────────────
bool run_footprint_formula_parity() {
    for (int pop : {0, 10, 49, 50, 200, 1000, 2000, 6000, 20000, 100000}) {
        // gen_city's literal, verbatim from before the hoist.
        const int cityPop = pop > 50 ? pop : 50;
        const int oldWallR = int(std::min(360.0f,
            std::max(90.0f, 70.0f + std::sqrt(float(cityPop)) * 3.0f)));
        if (sm::sub::city_wall_radius(pop) != oldWallR) return false;
        if (std::fabs(sm::sub::city_house_radius(pop)
                      - (float(oldWallR) - 10.0f)) > 0.001f) return false;

        // gen_village's literals, verbatim.
        const int vilPop = pop > 10 ? pop : 10;
        const float oldSettleR = std::min(float(sm::sub::kCellSize) * 0.07f,
            30.0f + std::sqrt(float(vilPop)) * 3.0f);
        if (std::fabs(sm::sub::village_core_radius(pop) - oldSettleR) > 0.001f) {
            return false;
        }
        if (sm::sub::village_is_walled(pop) != (vilPop >= 50)) return false;
        const float oldVilWallR = std::max(15.0f, oldSettleR + 6.0f);
        if (std::fabs(sm::sub::village_wall_radius(pop) - oldVilWallR) > 0.001f) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    // ── City ────────────────────────────────────────────────────────────────
    // 1200 souls: one wall ring, wallR = 173, so the built-up disk is 8 % of the
    // cell — under the old spawner 92 % of these people stood in the wilderness.
    {
        sm::sub::clear_saved_subworlds();
        sm::sub::SeamlessSubworldManager mgr;
        mgr.init(0, 0, settlement_resolver(sm::sub::CellLandmarkKind::City, 1200));
        mgr.consume_composite_dirty();
        const Spread s = measure(mgr, sm::LandmarkKind::City, 1200,
                                 0xC17015Eu);
        sm::sub::clear_saved_subworlds();

        if (s.citizens < 100) return fail("a 1200-soul city fielded no crowd");
        if (s.outsideRadius != 0) {
            std::fprintf(stderr, "  %d/%d citizens outside the built-up radius "
                         "(maxR %.1f)\n", s.outsideRadius, s.citizens, s.maxR);
            return fail("city citizens spawned outside the settlement");
        }
        if (s.outsideMasonry != 0) {
            return fail("city citizens spawned at or beyond the town wall");
        }
        if (s.onSolid != 0) return fail("city citizens spawned in water/masonry");
        if (s.fracOuterHalf < 0.55f) {
            return fail("city citizens clumped near the centre "
                        "(radius sampled linearly instead of by area?)");
        }
        if (s.emptySectors != 0) return fail("city has an empty angular sector");
    }

    // ── Village ─────────────────────────────────────────────────────────────
    // The tighter case: a village core is ~1 % of the cell, so the old spawner
    // put ~99 % of the villagers in the woods.
    {
        sm::sub::clear_saved_subworlds();
        sm::sub::SeamlessSubworldManager mgr;
        mgr.init(0, 0,
                 settlement_resolver(sm::sub::CellLandmarkKind::Village, 400));
        mgr.consume_composite_dirty();
        const Spread s = measure(mgr, sm::LandmarkKind::Village, 400,
                                 0x71114Eu);
        sm::sub::clear_saved_subworlds();

        if (s.citizens < 50) return fail("a 400-soul village fielded no crowd");
        if (s.outsideRadius != 0) {
            std::fprintf(stderr, "  %d/%d villagers outside the built-up radius "
                         "(maxR %.1f)\n", s.outsideRadius, s.citizens, s.maxR);
            return fail("villagers spawned outside the village");
        }
        if (s.outsideMasonry != 0) {
            return fail("villagers spawned at or beyond the village wall");
        }
        if (s.onSolid != 0) return fail("villagers spawned in water/masonry");
        if (s.fracOuterHalf < 0.55f) {
            return fail("villagers clumped on the green");
        }
        if (s.emptySectors != 0) return fail("village has an empty angular sector");
    }

    if (!run_footprint_formula_parity()) {
        return fail("hoisting the footprint into sub/city_layout.h moved a wall "
                    "(city_wall_radius / village_core_radius disagree with the "
                    "literals gen_city / gen_village used to carry)");
    }

    std::printf("PASS city_population_inside_walls_test\n");
    CHECK(true, "every gate above held");
    return sm::test::report("city_population_inside_walls_test");
}

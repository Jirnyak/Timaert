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
sm::sub::CellResolver settlement_resolver(sm::LandmarkType kind,
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
        c.landmark.kind = sm::LandmarkType::None;
        c.treeCount = 0;
        if (cx == 0 && cy == 0) {
            c.landmark.id = 7;
            c.landmark.size = pop;
            c.landmark.kind = kind;
        }
        c.seed = sm::sub::cell_seed(0x5E771EDu, cx, cy);
        return c;
    };
}

struct Spread {
    int   citizens = 0;
    int   outsideReach = 0;       // beyond any ground this town could hold
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
               sm::LandmarkType landmark, int pop, std::uint32_t seed,
               const sm::WorldTime& now) {
    Spread s{};
    sm::ecs::World world{};
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             landmark,
                             /*danger*/0,
                             /*depositsNear*/0,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             seed,
                             /*worldSeed*/seed,
                             std::uint16_t(sm::faction_index("empire")),
                             pop,
                             /*landmarkSubjectId*/-1,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount*/-1,
                             /*garrison*/nullptr,
                             now);

    const bool city = landmark == sm::LandmarkType::City;
    const float radius = sm::sub::settlement_population_radius(city, pop);
    // The furthest a place of this size may ever reach (sub/city_layout.h).
    // For a village that is its palisade, which stands OUTSIDE the guaranteed
    // radius by design — the conservative scalar was never the village's edge
    // either, it was only the part of it that needed no terrain to be true.
    const float reach = city
        ? sm::sub::city_max_radius(pop)
        : std::max(radius, sm::sub::village_max_radius(pop)
                               * (sm::sub::village_is_walled(pop)
                                  ? sm::sub::village_wall_radius(pop)
                                    / std::max(1.0f,
                                        sm::sub::village_core_radius(pop))
                                  : 1.0f));
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
        // The GUARANTEED radius is no longer the town's edge — a city's
        // outline is grown over the terrain and runs past it wherever the
        // ground was cheap (sub/gens/kit/growth.h), and the crowd rightly
        // follows into those lobes. What must still hold is that nobody
        // stands beyond any ground this town could possibly have taken.
        if (d > reach + 1.5f) ++s.outsideReach;
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
        // THE CITY'S RADIUS IS NO LONGER PINNED HERE, and deliberately so.
        //
        // This guard was written for a pure HOIST: the footprint moved out of
        // gen_city into the header and must not have shifted a single wall, so
        // it re-derived `70 + 3·√population` clamped to [90, 360] and demanded
        // the header agree. That formula has since been REPLACED (owner,
        // 2026-09-13) because it was a number from nowhere and, being
        // independent of the house count, made a town's density an accident:
        // the same city read dense at one population and half-empty at
        // another. A city's area is now its houses' ground plus its market,
        // and its radius is the circle of that area — so pinning the old
        // literal would pin exactly the defect that was removed.
        //
        // What replaces it as a guard is `city_density_holds` below: the
        // relationship the new law asserts, checked at every size.

        // gen_village's literals, verbatim — the village footprint did NOT
        // change, so its hoist guard still stands.
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

// The new law, in place of the old literal: a city's area IS its houses'
// ground plus its market, at every population. This is the assertion the
// radius formula could never make — density can no longer drift with size.
bool city_density_holds() {
    constexpr float kPi = 3.14159265f;
    for (int pop : {50, 200, 1000, 2000, 6000, 20000, 100000}) {
        const int houses = sm::sub::city_house_target(pop);
        if (houses <= 0) return false;
        const float want = float(houses) * sm::sub::kTownGroundPerHouse
                         + sm::sub::city_market_area(houses);
        if (std::fabs(sm::sub::city_target_area(pop) - want) > 1.0f) return false;
        // …and the radius is that area's circle, unless the CELL bounds it.
        const float r = float(sm::sub::city_wall_radius(pop));
        const float cap = float(sm::sub::kCellSize) * 0.30f;
        if (r < cap - 0.5f) {
            const float area = kPi * r * r;
            // Integer radius, so allow the rounding it costs.
            if (std::fabs(area - want) > 2.0f * kPi * r + 4.0f) return false;
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
        mgr.init(0, 0, settlement_resolver(sm::LandmarkType::City, 1200));
        mgr.consume_composite_dirty();
        // NOON, because the spatial assertions below want the fullest street
        // the town ever has — at midnight the crowd is a handful and a claim
        // about angular coverage would be measuring the hour, not the layout.
        const sm::WorldTime noon = sm::world_time_at(1, 12, 0);
        const Spread s = measure(mgr, sm::LandmarkType::City, 1200,
                                 0xC17015Eu, noon);
        sm::sub::clear_saved_subworlds();

        if (s.citizens < 100) return fail("a 1200-soul city fielded no crowd");
        if (s.outsideReach != 0) {
            std::fprintf(stderr, "  %d/%d citizens beyond the town's reach "
                         "(maxR %.1f)\n", s.outsideReach, s.citizens, s.maxR);
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

        // §42 PARTITION WITNESS (CANON S28: одна душа воплощается один раз).
        // The street crowd plus the hearth reserves behind the doors must
        // sum to the town's population, soul for soul — the double
        // embodiment (a soul on the square AND in a house) is dead.
        // …AND IT HOLDS AT EVERY HOUR. A town's people are a CONSERVED
        // quantity moved between two vessels by the sun (city_layout.h
        // crowd_outdoor_share01): nothing is born at dawn and nothing dies at
        // dusk. So the sum is asserted around the whole clock, and the shape
        // of the pump is asserted with it — fullest street at noon, emptiest
        // at midnight — because a law that conserves the total while never
        // moving anything would pass a sum check in silence.
        int streetAt[24] = {};
        for (int hour = 0; hour < 24; ++hour) {
            const sm::WorldTime t = sm::world_time_at(1, hour, 0);
            const Spread h = measure(mgr, sm::LandmarkType::City, 1200,
                                     0xC17015Eu, t);
            const int keptIn = sm::sub::interior_reserve_for_cell(
                mgr.structures(), sm::LandmarkType::City,
                /*worldSeed*/0xC17015Eu, 0, 0,
                float(sm::sub::kCellSize), float(sm::sub::kCellSize), 1200, t);
            streetAt[hour] = h.citizens;
            if (h.citizens + keptIn != 1200) {
                std::fprintf(stderr, "  %02d:00 street %d + hearths %d != 1200\n",
                             hour, h.citizens, keptIn);
                return fail("the day's pump created or destroyed people");
            }
        }
        std::fprintf(stderr,
                     "  street by the clock: 00h=%d 06h=%d 12h=%d 18h=%d\n",
                     streetAt[0], streetAt[6], streetAt[12], streetAt[18]);
        if (streetAt[12] <= streetAt[6] || streetAt[6] <= streetAt[0]) {
            return fail("the street does not fill as the sun rises");
        }
        if (streetAt[18] >= streetAt[12]) {
            return fail("the street does not empty as the sun sets");
        }
        // Dawn and dusk stand at the same height of sun, so they must field
        // the same crowd — the curve is the sun's, not a hand-drawn day.
        if (streetAt[6] != streetAt[18]) {
            return fail("dawn and dusk disagree though the sun does not");
        }

        // The hearths must actually FILL as the sun goes down — the sum alone
        // would be satisfied by a law that never moved anybody.
        if (1200 - streetAt[0] <= 1200 - streetAt[12]) {
            return fail("the hearths do not fill as the sun goes down");
        }
    }

    // ── Village ─────────────────────────────────────────────────────────────
    // The tighter case: a village core is ~1 % of the cell, so the old spawner
    // put ~99 % of the villagers in the woods.
    {
        sm::sub::clear_saved_subworlds();
        sm::sub::SeamlessSubworldManager mgr;
        mgr.init(0, 0,
                 settlement_resolver(sm::LandmarkType::Village, 400));
        mgr.consume_composite_dirty();
        const sm::WorldTime vnoon = sm::world_time_at(1, 12, 0);
        const Spread s = measure(mgr, sm::LandmarkType::Village, 400,
                                 0x71114Eu, vnoon);
        sm::sub::clear_saved_subworlds();

        if (s.citizens < 50) return fail("a 400-soul village fielded no crowd");
        if (s.outsideReach != 0) {
            std::fprintf(stderr, "  %d/%d villagers beyond the town's reach "
                         "(maxR %.1f)\n", s.outsideReach, s.citizens, s.maxR);
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

        // The same partition witness at village scale — asked at MIDNIGHT,
        // because at noon a place is meant to keep nobody at home and "the
        // hearths are empty" is the correct answer rather than a fault.
        const sm::WorldTime vnight = sm::world_time_at(1, 0, 0);
        const Spread night = measure(mgr, sm::LandmarkType::Village, 400,
                                     0x71114Eu, vnight);
        const int kept = sm::sub::interior_reserve_for_cell(
            mgr.structures(), sm::LandmarkType::Village,
            /*worldSeed*/0x71114Eu, 0, 0,
            float(sm::sub::kCellSize), float(sm::sub::kCellSize), 400, vnight);
        if (kept <= 0) return fail("a 400-soul village kept nobody at home at night");
        if (night.citizens + kept != 400) {
            std::fprintf(stderr, "  night street %d + hearths %d != 400\n",
                         night.citizens, kept);
            return fail("the village's day pump created or destroyed people");
        }
        const int reserve = sm::sub::interior_reserve_for_cell(
            mgr.structures(), sm::LandmarkType::Village,
            /*worldSeed*/0x71114Eu, 0, 0,
            float(sm::sub::kCellSize), float(sm::sub::kCellSize), 400, vnoon);
        if (s.citizens + reserve != 400) {
            std::fprintf(stderr, "  street %d + hearths %d != pop 400\n",
                         s.citizens, reserve);
            return fail("the green and the hearths do not sum to the village");
        }
    }

    if (!city_density_holds()) {
        return fail("a city's area is no longer its houses' ground plus its "
                    "market — the density law broke");
    }

    if (!run_footprint_formula_parity()) {
        return fail("hoisting the footprint into sub/city_layout.h moved a "
                    "VILLAGE wall (village_core_radius / village_wall_radius "
                    "disagree with the literals gen_village used to carry)");
    }

    std::printf("PASS city_population_inside_walls_test\n");
    CHECK(true, "every gate above held");
    return sm::test::report("city_population_inside_walls_test");
}

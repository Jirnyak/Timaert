// R2 — settlement is DERIVED from resources (the owner's causality law).
// Pinned here:
//   · vetoes — no village on water, mountain rock or inside a forest massif;
//   · the PERCENTILE PROPERTY — every village stands at least at the 75th
//     percentile of its city's admissible hinterland scores (the k-best scan
//     guarantees better; 75 keeps the assertion robust, not brittle);
//   · the NEGATIVE CONTROL — the old roulette (blind darts whose only
//     criterion was "land") violates that property on the same world, so a
//     regression back to dice turns this file red, not stale;
//   · count derives from capacity — a lush river hinterland feeds villages,
//     a dry steppe feeds none;
//   · villages actually stand NEXT TO something gatherable (ploughable
//     moisture, a deposit, or a real stand of trees) — the context the
//     score exists to buy;
//   · determinism — one seed, one settled world.
#include "check.h"

#include "core/rng.h"
#include "core/torus.h"
#include "macro/deposit_layer.h"
#include "macro/politik.h"
#include "macro/settlement_score.h"
#include "macro/spawners.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace sm;

constexpr int   kW = 96, kH = 96;
constexpr float kSeaLevel = 0.4f;
constexpr std::uint8_t kSeaLevel8 = std::uint8_t(kSeaLevel * 255.0f);

// A little world with an honest gradient of worth: sea on the left, a river
// column at x=40 wrapped in a moisture bloom (the lush belt), mountains at
// the bottom (deposits live there), and land drying out with distance from
// the river — so "the fattest land" is a fact of the data, not of the test.
TerrainData make_world() {
    TerrainData td;
    td.width = kW;
    td.height = kH;
    td.rgba.assign(std::size_t(kW) * kH * 4u, 0);
    td.riverData.assign(std::size_t(kW) * kH, 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const std::size_t s = std::size_t(y * kW + x) * 4u;
            std::uint8_t height = 140;                    // plain land
            if (x < 6) height = 40;                       // sea
            if (y >= 80) height = 220;                    // mountains (≥0.75)
            if (x == 40 && y < 80) {                      // the river: honest
                height = 40;                              //   water cells
                td.riverData[std::size_t(y * kW + x)] = 255;
            }
            const int dist = std::abs(x - 40);
            const int moisture = std::max(20, 200 - 4 * dist);
            td.rgba[s + 0] = height;
            td.rgba[s + 1] = std::uint8_t(moisture);      // G = fertility
            td.rgba[s + 2] = 128;
            td.rgba[s + 3] = height < kSeaLevel8 ? 0 : 255;
        }
    }
    return td;
}

struct World {
    TerrainData  td;
    TreeLayer    trees;
    DepositLayer deposits;
    GameState    gs;
};

// Cities: A in the lush river belt, B out in the dry steppe. Unowned
// (kingdomIdx -1) so naming needs no kingdoms and allegiance falls to the
// free folk — this test is about the GROUND, not the crown.
void make_settled_world(World& w) {
    w.td = make_world();
    // A forest massif north-east of the river belt: a 6×6 mask blob.
    std::vector<std::uint8_t> mask(std::size_t(kW) * kH, 0);
    for (int y = 14; y < 20; ++y)
        for (int x = 52; x < 58; ++x)
            mask[std::size_t(y) * kW + x] = 1;
    w.trees = build_tree_layer(w.td, mask.data(), mask.size());
    w.deposits = build_deposit_layer(w.td, 777u, kSeaLevel);

    w.gs.worldSeed = 777u;
    w.gs.mapW = kW;
    w.gs.mapH = kH;
    w.gs.politik.mapW = kW;
    w.gs.politik.mapH = kH;
    w.gs.politik.cities.clear();
    City a{};
    a.x = 34; a.y = 20; a.kingdomIdx = -1; a.population = 1000;
    for (int& c : a.connections) c = -1;
    City b{};
    b.x = 84; b.y = 40; b.kingdomIdx = -1; b.population = 1000;
    for (int& c : b.connections) c = -1;
    w.gs.politik.cities.push_back(a);
    w.gs.politik.cities.push_back(b);

    populate_landmarks_from_politik(w.gs, w.td, kSeaLevel8,
                                    w.trees, w.deposits);
}

SettlementSiteContext site_ctx(World& w) {
    SettlementSiteContext ctx{};
    ctx.w.gs       = &w.gs;
    ctx.w.trees    = &w.trees;
    ctx.w.terrain  = &w.td;
    ctx.w.deposits = &w.deposits;
    ctx.seaLevel8  = kSeaLevel8;
    return ctx;
}

// The admissible hinterland scores of one city, sorted ascending — the
// distribution the percentile property is stated against. Mirrors the
// shipping scan: same spacing law, same annulus.
std::vector<int> hinterland_scores(World& w, const City& c) {
    SettlementSiteContext ctx = site_ctx(w);
    const int spacing = derive_city_spacing(&w.td, kSeaLevel8, kW, kH,
                                            int(w.gs.politik.cities.size()));
    const int reach = std::max(4, spacing / 2);
    std::vector<int> scores;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) <= kSettlementReach)
                continue;
            const int score = settlement_site_score(
                ctx, SettlementScoreRow::Village,
                wrapi(c.x + dx, kW), wrapi(c.y + dy, kH));
            if (score >= 0) scores.push_back(score);
        }
    }
    std::sort(scores.begin(), scores.end());
    return scores;
}

int percentile(const std::vector<int>& sorted, int p) {
    if (sorted.empty()) return 0;
    return sorted[std::min(sorted.size() - 1,
                           sorted.size() * std::size_t(p) / 100u)];
}

void test_vetoes_hold() {
    World w;
    make_settled_world(w);
    CHECK_OR_RETURN(!w.gs.villages.empty(), "the lush world settles villages");
    for (const auto& v : w.gs.villages) {
        CHECK(!w.td.is_water(v.x, v.y, kSeaLevel8), "no village on water");
        CHECK(float(w.td.height_at(v.x, v.y)) / 255.0f < 0.75f,
              "no village on mountain rock");
        CHECK(!is_forest_cell(int(w.trees.at(v.x, v.y))),
              "no village inside a forest massif");
    }
}

void test_villages_take_the_best_land() {
    World w;
    make_settled_world(w);
    CHECK_OR_RETURN(!w.gs.villages.empty(), "the lush world settles villages");
    SettlementSiteContext ctx = site_ctx(w);
    for (const auto& v : w.gs.villages) {
        const City& c = w.gs.politik.cities[std::size_t(v.nearestCityId)];
        const std::vector<int> scores = hinterland_scores(w, c);
        const int p75 = percentile(scores, 75);
        const int mine = settlement_site_score(
            ctx, SettlementScoreRow::Village, v.x, v.y);
        CHECK(mine >= p75,
              "a village stands no worse than the 75th percentile of its "
              "hinterland");
    }
}

// The negative control, in-process (never via git checkout): the old
// roulette on the same world violates the percentile property. Eight darts
// so that "all of them landed lucky" cannot happen by accident.
void test_roulette_is_red() {
    World w;
    make_settled_world(w);
    const City& c = w.gs.politik.cities[0];
    const std::vector<int> scores = hinterland_scores(w, c);
    CHECK_OR_RETURN(!scores.empty(), "city A has an admissible hinterland");
    const int p75 = percentile(scores, 75);
    SettlementSiteContext ctx = site_ctx(w);
    Rng rng(w.gs.worldSeed ^ 0xC1A05E1Du);
    int placed = 0, belowP75 = 0;
    while (placed < 8) {
        // The old law, verbatim: a random angle, 4..14 cells, "is it land".
        const float ang = rng.next_f01() * 6.2831853f;
        const int   dist = 4 + int(rng.next_u32() % 11u);
        const int   x = wrapi(c.x + int(std::cos(ang) * float(dist)), kW);
        const int   y = wrapi(c.y + int(std::sin(ang) * float(dist)), kH);
        if (w.td.is_water(x, y, kSeaLevel8)) continue;
        ++placed;
        const int score = settlement_site_score(
            ctx, SettlementScoreRow::Village, x, y);
        if (score < 0 || score < p75) ++belowP75;
    }
    CHECK(belowP75 > 0,
          "the blind roulette violates the percentile property the score "
          "placement holds — the negative control is red");
}

// Villages SCATTER around their town — they do not clump (owner's report
// from the live map: "деревни кластерами через блок вплотную"). The law
// that spreads them is separation = half the hinterland, so two villages
// of one city can never sit a block apart, and no city with admissible
// ground is left hamlet-less.
void test_villages_scatter_around_their_town() {
    World w;
    make_settled_world(w);
    CHECK_OR_RETURN(!w.gs.villages.empty(), "the lush world settles villages");
    const int spacing = derive_city_spacing(&w.td, kSeaLevel8, kW, kH,
                                            int(w.gs.politik.cities.size()));
    const int reach = std::max(4, spacing / 2);
    // Stated in WORLD terms — half the hinterland — deliberately NOT via
    // village_separation(): a property asserted through the function under
    // test is a tautology, and the flat-4 law would slip through it.
    const int mustBeApart = reach / 2;
    CHECK(mustBeApart > kVillageSeparationFloor,
          "the fixture's hinterland is big enough for the floor not to be "
          "the operative law");
    int closest = 1 << 20;
    for (const auto& a : w.gs.villages) {
        for (const auto& b : w.gs.villages) {
            if (&a == &b || a.nearestCityId != b.nearestCityId) continue;
            const int ddx = std::min(std::abs(a.x - b.x), kW - std::abs(a.x - b.x));
            const int ddy = std::min(std::abs(a.y - b.y), kH - std::abs(a.y - b.y));
            closest = std::min(closest, std::max(ddx, ddy));
        }
    }
    if (closest < (1 << 20)) {
        CHECK(closest >= mustBeApart,
              "two villages of one city never stand closer than half their "
              "hinterland — they scatter around the town, never clump");
    }

    // Every city whose hinterland holds ANY admissible ground keeps at
    // least one village. Settlements are built from politik.cities in order,
    // so POSITION pairs them; the id is an ordinal, not an index (v54).
    for (std::size_t si = 0; si < w.gs.settlements.size(); ++si) {
        const auto& s = w.gs.settlements[si];
        const City& c = w.gs.politik.cities[si];
        if (hinterland_scores(w, c).empty()) continue;
        int mine = 0;
        for (const auto& v : w.gs.villages) if (v.nearestCityId == s.id) ++mine;
        CHECK(mine >= 1, "a city with admissible ground is never hamlet-less");
    }
}

void test_count_derives_from_capacity() {
    World w;
    make_settled_world(w);
    int lush = 0, dry = 0;
    // The first-generated city is the river-belt one; its id is whatever the
    // ONE issuer handed it (v54), so ask the settlement, not the number 0.
    const int lushCityId = w.gs.settlements.empty() ? -1
                                                    : w.gs.settlements[0].id;
    for (const auto& v : w.gs.villages) {
        if (v.nearestCityId == lushCityId) ++lush;
        else ++dry;
    }
    CHECK(lush >= 1, "the river belt hinterland feeds at least one village");
    CHECK(lush > dry, "the lush hinterland feeds more villages than the dry "
                      "steppe");
    // Population is the site's own score, never the old 30+rng%90 dice.
    SettlementSiteContext ctx = site_ctx(w);
    for (const auto& v : w.gs.villages) {
        const int score = settlement_site_score(
            ctx, SettlementScoreRow::Village, v.x, v.y);
        CHECK(v.population == std::max(kVillagePopFloor, score),
              "a village's souls ARE its site score");
    }
}

void test_villages_stand_next_to_something() {
    World w;
    make_settled_world(w);
    CHECK_OR_RETURN(!w.gs.villages.empty(), "the lush world settles villages");
    int withContext = 0;
    for (const auto& v : w.gs.villages) {
        bool found = false;
        for (int dy = -kSettlementReach; dy <= kSettlementReach && !found; ++dy) {
            for (int dx = -kSettlementReach; dx <= kSettlementReach && !found;
                 ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int x = wrapi(v.x + dx, kW);
                const int y = wrapi(v.y + dy, kH);
                if (!w.td.is_water(x, y, kSeaLevel8)
                    && int(w.td.moisture_at(x, y)) >= int(kFieldMoistureMin))
                    found = true;
                if (w.deposits.any_at(x, y)) found = true;
                if (int(w.trees.at(x, y)) >= 4096) found = true;
            }
        }
        if (found) ++withContext;
    }
    CHECK(withContext * 2 >= int(w.gs.villages.size()),
          "at least half the villages stand next to something gatherable");
}

// ── Cities read the ground too (R2, second half) ────────────────────────
// Politics decides HOW MANY and WHOSE; the score decides WHERE and HOW
// LARGE. Pinned: land only, the population LAW (souls = per-score rate ×
// capacity, floored — never dice), and the control that scored placement
// actually lifts the ground under the cities against the first-valid old
// law (site = nullptr degrades to exactly that).
long long mean_city_score_x100(World& w, const Politik& p) {
    SettlementSiteContext ctx = site_ctx(w);
    long long sum = 0;
    for (const auto& c : p.cities)
        sum += std::max(0, settlement_site_score(
            ctx, SettlementScoreRow::City, c.x, c.y));
    return p.cities.empty() ? 0
                            : sum * 100 / static_cast<long long>(p.cities.size());
}

void test_cities_read_the_ground() {
    World w;
    w.td = make_world();
    std::vector<std::uint8_t> mask(std::size_t(kW) * kH, 0);
    w.trees = build_tree_layer(w.td, mask.data(), mask.size());
    w.deposits = build_deposit_layer(w.td, 777u, kSeaLevel);
    w.gs.worldSeed = 777u;
    w.gs.mapW = kW;
    w.gs.mapH = kH;
    SettlementSiteContext ctx = site_ctx(w);

    const Politik scored = generate_politik(777u, kW, kH, &w.td, kSeaLevel8,
                                            12, &ctx);
    CHECK_OR_RETURN(!scored.cities.empty(), "the world holds cities");
    for (const auto& c : scored.cities) {
        CHECK(!w.td.is_water(c.x, c.y, kSeaLevel8), "no city on water");
        const int score = settlement_site_score(
            ctx, SettlementScoreRow::City, c.x, c.y);
        const bool isCapital = [&] {
            for (const auto& kg : scored.kingdoms)
                if (kg.capitalCityIdx >= 0
                    && &scored.cities[std::size_t(kg.capitalCityIdx)] == &c)
                    return true;
            return false;
        }();
        CHECK(c.population == (isCapital ? capital_population(score)
                                         : city_population(score)),
              "a city's souls follow the population law, never dice");
    }

    // The control: the same politics WITHOUT the score (the old first-valid
    // law) settles on measurably poorer ground.
    const Politik blind = generate_politik(777u, kW, kH, &w.td, kSeaLevel8,
                                           12, nullptr);
    CHECK(mean_city_score_x100(w, scored) > mean_city_score_x100(w, blind),
          "scored placement stands cities on better ground than the blind "
          "first-valid law");

    // One seed, one politics.
    const Politik again = generate_politik(777u, kW, kH, &w.td, kSeaLevel8,
                                           12, &ctx);
    CHECK_OR_RETURN(again.cities.size() == scored.cities.size(),
                    "two runs raise the same number of cities");
    bool same = true;
    for (std::size_t i = 0; i < scored.cities.size(); ++i)
        same = same && again.cities[i].x == scored.cities[i].x
                    && again.cities[i].y == scored.cities[i].y
                    && again.cities[i].population == scored.cities[i].population;
    CHECK(same, "one seed, one crowned world");
}

void test_determinism() {
    World a, b;
    make_settled_world(a);
    make_settled_world(b);
    CHECK_OR_RETURN(a.gs.villages.size() == b.gs.villages.size(),
                    "two runs settle the same number of villages");
    bool same = true;
    for (std::size_t i = 0; i < a.gs.villages.size(); ++i) {
        const auto& va = a.gs.villages[i];
        const auto& vb = b.gs.villages[i];
        same = same && va.x == vb.x && va.y == vb.y
                    && va.population == vb.population;
    }
    CHECK(same, "one seed, one settled world");
}

} // namespace

int main() {
    test_vetoes_hold();
    test_villages_take_the_best_land();
    test_roulette_is_red();
    test_villages_scatter_around_their_town();
    test_count_derives_from_capacity();
    test_villages_stand_next_to_something();
    test_cities_read_the_ground();
    test_determinism();
    return sm::test::report("settlement_placement_test");
}

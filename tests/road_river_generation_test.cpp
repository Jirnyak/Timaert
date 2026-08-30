#include "check.h"

#include "macro/spawners.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace
{

sm::TerrainData make_terrain(int w, int h, std::uint8_t height)
{
    sm::TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    td.riverData.assign(std::size_t(w) * std::size_t(h), 0);
    for (int i = 0; i < w * h; ++i)
    {
        const std::size_t s = std::size_t(i) * 4;
        td.rgba[s + 0] = height;
        td.rgba[s + 1] = 128;
        td.rgba[s + 2] = 128;
        td.rgba[s + 3] = height < 102 ? 0 : 255;
    }
    return td;
}

void set_cell(sm::TerrainData& td, int x, int y, std::uint8_t height)
{
    const std::size_t s = (std::size_t(y) * td.width + x) * 4;
    td.rgba[s + 0] = height;
    td.rgba[s + 3] = height < 102 ? 0 : 255;
}

sm::City make_city(int x, int y, int connection)
{
    sm::City c{};
    c.x = x;
    c.y = y;
    c.kingdomIdx = 0;
    c.population = 100;
    for (int& v : c.connections)
    {
        v = -1;
    }
    c.connections[0] = connection;
    return c;
}

bool has_connection(const sm::City& c, int target)
{
    for (int v : c.connections)
    {
        if (v == target)
        {
            return true;
        }
    }
    return false;
}

void test_road_prunes_water_only_connection()
{
    sm::TerrainData td = make_terrain(5, 5, 0);
    set_cell(td, 1, 1, 160);
    set_cell(td, 3, 3, 160);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(1, 1, 1));
    p.cities.push_back(make_city(3, 3, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    const int failsBefore = sm::test::failures();
    CHECK(stats.attemptedEdges == 1, "water-only edge should be attempted once");
    CHECK(stats.keptEdges == 0, "water-only edge must not survive");
    CHECK(stats.prunedEdges == 1, "water-only edge must be pruned");
    CHECK(stats.componentPrunedEdges == 1,
          "water-only edge should be rejected before expensive A*");
    CHECK(!has_connection(p.cities[0], 1) && !has_connection(p.cities[1], 0),
          "pruned Politik edge must be removed from both cities");
    for (std::size_t i = 0; i < roads.size(); ++i)
    {
        CHECK(roads[i] == 0, "pruned road mask must stay empty");
        if (sm::test::failures() != failsBefore)
        {
            break;
        }
    }
}

void test_road_survives_land_detour_without_water_cells()
{
    sm::TerrainData td = make_terrain(5, 3, 160);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 2, y, 0);
    }

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(1, 1, 1));
    p.cities.push_back(make_city(3, 1, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    const int failsBefore = sm::test::failures();
    CHECK(stats.attemptedEdges == 1, "detour edge should be attempted once");
    CHECK(stats.keptEdges == 1, "land detour edge should survive");
    CHECK(stats.prunedEdges == 0, "land detour edge should not be pruned");
    CHECK(stats.componentPrunedEdges == 0,
          "land detour edge must still run through road A*");
    CHECK(has_connection(p.cities[0], 1) && has_connection(p.cities[1], 0),
          "surviving Politik edge must remain connected");
    for (int y = 0; y < td.height; ++y)
    {
        const int idx = y * td.width + 2;
        CHECK(roads[std::size_t(idx)] == 0,
              "surviving road mask must not stamp rejected water cells");
        if (sm::test::failures() != failsBefore)
        {
            break;
        }
    }
}

void test_road_uses_a_star_on_open_land_connection()
{
    sm::TerrainData td = make_terrain(8, 8, 160);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(1, 1, 1));
    p.cities.push_back(make_city(6, 6, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    CHECK(stats.attemptedEdges == 1, "open-land edge should be attempted once");
    CHECK(stats.keptEdges == 1, "open-land edge should survive");
    CHECK(stats.prunedEdges == 0, "open-land edge should not be pruned");
    CHECK(stats.expansions > 0,
          "road tracing must use A* for terrain-cost validation");
    CHECK(!roads.empty(), "open-land road mask should be allocated");
}

void test_road_tracing_uses_map_sea_level()
{
    sm::TerrainData td = make_terrain(5, 1, 90);
    for (std::size_t i = 0; i < td.cell_count(); ++i)
    {
        td.rgba[i * 4u + 3] = 255u;
    }

    sm::Politik lowSeaPolitik;
    lowSeaPolitik.mapW = td.width;
    lowSeaPolitik.mapH = td.height;
    lowSeaPolitik.cities.push_back(make_city(0, 0, 1));
    lowSeaPolitik.cities.push_back(make_city(4, 0, 0));
    sm::RoadTraceStats lowSeaStats;
    const std::vector<std::uint8_t> lowSeaRoads =
        sm::trace_roads(td, lowSeaPolitik, &lowSeaStats, 0.30f);

    sm::Politik defaultSeaPolitik;
    defaultSeaPolitik.mapW = td.width;
    defaultSeaPolitik.mapH = td.height;
    defaultSeaPolitik.cities.push_back(make_city(0, 0, 1));
    defaultSeaPolitik.cities.push_back(make_city(4, 0, 0));
    sm::RoadTraceStats defaultSeaStats;
    const std::vector<std::uint8_t> defaultSeaRoads =
        sm::trace_roads(td, defaultSeaPolitik, &defaultSeaStats, 0.40f);

    CHECK(lowSeaStats.keptEdges == 1 && lowSeaStats.prunedEdges == 0,
          "road tracing must use active low sea level for land connectivity");
    CHECK(!lowSeaRoads.empty() && lowSeaRoads[0] == 255u,
          "active low sea road trace must stamp reachable land");
    CHECK(defaultSeaStats.keptEdges == 0 && defaultSeaStats.componentPrunedEdges == 1,
          "road tracing must reject the same cells below active default sea level");
    CHECK(!has_connection(defaultSeaPolitik.cities[0], 1)
              && !has_connection(defaultSeaPolitik.cities[1], 0),
          "default-sea rejected road must prune Politik edges");
    CHECK(!defaultSeaRoads.empty() && defaultSeaRoads[0] == 0u,
          "default-sea rejected road must not stamp water cells");
}

void test_large_road_search_restores_same_land_detour()
{
    sm::TerrainData td = make_terrain(300, 300, 160);
    // TWO water columns: a one-cell wall would be bridgeable now (every wall
    // cell has land on both E/W sides), and this test is about the search
    // BUDGET — the wall must force the long detour, not invite a span.
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 150, y, 0);
        set_cell(td, 151, y, 0);
    }
    set_cell(td, 150, 0, 160);
    set_cell(td, 151, 0, 160);
    set_cell(td, 150, td.height - 1, 160);
    set_cell(td, 151, td.height - 1, 160);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(120, 150, 1));
    p.cities.push_back(make_city(180, 150, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    const int failsBefore = sm::test::failures();
    CHECK(stats.attemptedEdges == 1, "over-budget detour edge should be attempted once");
    CHECK(stats.componentPrunedEdges == 0,
          "over-budget detour is same land component and must not component-prune");
    CHECK(stats.keptEdges == 1,
          "same-land detour must survive the restored native road baseline");
    CHECK(stats.prunedEdges == 0,
          "same-land detour must not be pruned by the large-map cap");
    CHECK(stats.expansions > 4096,
          "test detour must cover the previous too-small large-map cap");
    for (int y = 1; y < td.height - 1; ++y)
    {
        CHECK(roads[std::size_t(y) * td.width + 150] == 0
                  && roads[std::size_t(y) * td.width + 151] == 0,
              "restored road search must not stamp rejected water wall cells");
        if (sm::test::failures() != failsBefore)
        {
            break;
        }
    }
}

// ── Bridges (FT_Bridge, owner 2026-08-29): a road may cross water exactly
// one cell thick, square-on, and that crossing is a stone span. ──────────

// The forced-crossing fixture: on a torus ONE water ring never separates the
// land (the wrap walks around it), so the map carries TWO barriers — a
// one-cell river at x=5 (bridgeable: land on both E/W sides) and a two-cell
// strait at x=13..14 (unbridgeable). The only way between the shores is a
// span at x=5.
sm::TerrainData make_two_barrier_terrain()
{
    sm::TerrainData td = make_terrain(20, 9, 160);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 5, y, 90);   // one-cell river (water: 90 < 102)
        set_cell(td, 13, y, 90);  // two-cell strait
        set_cell(td, 14, y, 90);
    }
    return td;
}

void test_road_bridges_one_cell_river()
{
    sm::TerrainData td = make_two_barrier_terrain();

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(2, 4, 1));
    p.cities.push_back(make_city(8, 4, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    CHECK(stats.componentPrunedEdges == 0,
          "one-cell water joins its shores into ONE road component");
    CHECK(stats.keptEdges == 1 && stats.prunedEdges == 0,
          "the cross-river edge must survive over a bridge");
    CHECK(has_connection(p.cities[0], 1) && has_connection(p.cities[1], 0),
          "the bridged edge must keep its Politik connection");

    int wet = 0, wetY = -1;
    for (int y = 0; y < td.height; ++y)
    {
        if (roads[std::size_t(y) * td.width + 5] != 0)
        {
            ++wet;
            wetY = y;
        }
    }
    CHECK(wet == 1, "the road crosses the river ONCE — one span, no causeway");
    CHECK(wetY >= 0
              && roads[std::size_t(wetY) * td.width + 4] == 255u
              && roads[std::size_t(wetY) * td.width + 6] == 255u,
          "the span meets both banks square-on (cardinal entry and exit)");
    const int failsBefore = sm::test::failures();
    for (int y = 0; y < td.height; ++y)
    {
        CHECK(roads[std::size_t(y) * td.width + 13] == 0
                  && roads[std::size_t(y) * td.width + 14] == 0,
              "the two-cell strait must stay road-free (wide water refused)");
        if (sm::test::failures() != failsBefore)
        {
            break;
        }
    }

    // The stamp law (build_feature_layer): the paid water cell IS a bridge,
    // its banks are stone road.
    const sm::FeatureLayer fl = sm::build_feature_layer(td, roads, nullptr);
    CHECK(fl.at(5, wetY) == sm::FT_Bridge,
          "a road cell on biome water must stamp FT_Bridge");
    CHECK(fl.at(4, wetY) == sm::FT_Road && fl.at(6, wetY) == sm::FT_Road,
          "the bridge's banks must stamp FT_Road");
    int wetFeatures = 0;
    for (int y = 0; y < td.height; ++y)
        if (fl.at(5, y) != sm::FT_None)
            ++wetFeatures;
    CHECK(wetFeatures == 1,
          "exactly the paid crossing carries a feature on the river");
}

void test_two_separating_straits_stay_unbridged()
{
    // The negative control of the bridge law: BOTH barriers two cells wide —
    // nothing is bridgeable, the shores are honest separate components and
    // the edge dies exactly as it always did.
    sm::TerrainData td = make_terrain(20, 9, 160);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 5, y, 90);
        set_cell(td, 6, y, 90);
        set_cell(td, 13, y, 90);
        set_cell(td, 14, y, 90);
    }

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(2, 4, 1));
    p.cities.push_back(make_city(9, 4, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);

    CHECK(stats.keptEdges == 0 && stats.componentPrunedEdges == 1,
          "two-cell water has no one-cell crossing — the edge is pruned");
    CHECK(!has_connection(p.cities[0], 1) && !has_connection(p.cities[1], 0),
          "the unbridgeable edge must lose its Politik connection");
    int wetRoads = 0;
    for (std::size_t i = 0; i < roads.size(); ++i)
        if (roads[i] != 0 && td.rgba[i * 4u + 3] == 0)
            ++wetRoads;
    CHECK(wetRoads == 0, "no water cell may carry road without a span");
}

void test_dirt_lane_lays_a_stone_bridge()
{
    // Every bridge is stone (owner): a dirt lane forced over the one-cell
    // river lands FT_Bridge on the water cell, dirt on the banks.
    sm::TerrainData td = make_two_barrier_terrain();
    sm::FeatureLayer features;
    features.resize(td.width, td.height);

    std::vector<sm::VillageRoadSite> villages(1);
    villages[0].x = 2;
    villages[0].y = 4;
    villages[0].cityX = 8;
    villages[0].cityY = 4;
    villages[0].hasCity = true;

    const int laid = sm::trace_dirt_roads(features, td, villages, {}, 4);

    CHECK(laid > 0, "the cross-river home city must get a lane over a span");
    int wet = 0, wetY = -1;
    for (int y = 0; y < td.height; ++y)
    {
        if (features.at(5, y) != sm::FT_None)
        {
            ++wet;
            wetY = y;
        }
    }
    CHECK(wet == 1 && features.at(5, wetY) == sm::FT_Bridge,
          "the dirt lane's crossing must land FT_Bridge — stone, never dirt");
    CHECK(features.at(4, wetY) == sm::FT_DirtRoad
              && features.at(6, wetY) == sm::FT_DirtRoad,
          "the span's banks carry the lane's own dirt class, square-on");
    const int failsBefore = sm::test::failures();
    for (int y = 0; y < td.height; ++y)
    {
        CHECK(features.at(13, y) == sm::FT_None
                  && features.at(14, y) == sm::FT_None,
              "the two-cell strait must stay lane-free (wide water refused)");
        if (sm::test::failures() != failsBefore)
        {
            break;
        }
    }
}

void test_tree_spawner_respects_river_buffer()
{
    sm::TerrainData dry = make_terrain(64, 64, 150);
    sm::TerrainData river = dry;
    for (int y = 0; y < river.height; ++y)
    {
        river.riverData[std::size_t(y) * river.width + 32] = 255;
    }

    const std::vector<sm::TreePoint> dryTrees = sm::spawn_trees(dry, std::uint32_t{42});
    const std::vector<sm::TreePoint> riverTrees = sm::spawn_trees(river, std::uint32_t{42});

    const int failsBefore = sm::test::failures();
    CHECK(!dryTrees.empty(), "control terrain should spawn at least one tree");
    CHECK(!riverTrees.empty(), "river terrain should still spawn trees away from rivers");
    CHECK(riverTrees.size() < dryTrees.size(),
          "river exclusion should remove some otherwise valid tree cells");

    for (const sm::TreePoint& t : riverTrees)
    {
        int dx = t.x - 32;
        if (dx > river.width / 2)
        {
            dx -= river.width;
        }
        if (dx < -river.width / 2)
        {
            dx += river.width;
        }
        CHECK(dx < -2 || dx > 2,
              "trees must respect TS two-cell river exclusion buffer");
        if (sm::test::failures() != failsBefore)
        {
            break;
        }
    }
}

void test_tree_spawner_uses_map_sea_level()
{
    sm::TerrainData td = make_terrain(64, 64, 90);
    for (std::size_t i = 0; i < td.cell_count(); ++i)
    {
        td.rgba[i * 4u + 3] = 255u;
    }

    const std::vector<sm::TreePoint> lowSeaTrees =
        sm::spawn_trees(td, std::uint32_t{42}, 0.30f);
    const std::vector<sm::TreePoint> defaultSeaTrees =
        sm::spawn_trees(td, std::uint32_t{42}, 0.40f);

    CHECK(!lowSeaTrees.empty(),
          "tree spawner must allow active low-sea shoreline land cells");
    CHECK(defaultSeaTrees.empty(),
          "tree spawner must reject cells below active default sea level");
}

void test_malformed_terrain_fails_closed()
{
    sm::TerrainData td;
    td.width = 4;
    td.height = 4;
    td.rgba.assign(3u, 255u);

    sm::Politik p;
    p.mapW = td.width;
    p.mapH = td.height;
    p.cities.push_back(make_city(0, 0, 1));
    p.cities.push_back(make_city(3, 3, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, p, &stats);
    const std::vector<sm::TreePoint> trees = sm::spawn_trees(td, std::uint32_t{7});

    CHECK(!td.has_rgba_storage(),
          "malformed terrain helper must reject short RGBA storage");
    CHECK(roads.empty(),
          "road tracing must fail closed on malformed terrain storage");
    CHECK(trees.empty(),
          "tree spawning must fail closed on malformed terrain storage");
    CHECK(stats.cityCount == 2 && stats.attemptedEdges == 0
              && stats.keptEdges == 0 && stats.prunedEdges == 0,
          "malformed road tracing must not mutate road stats beyond city count");
}

void test_politik_malformed_terrain_fails_closed()
{
    sm::TerrainData td;
    td.width = 4;
    td.height = 4;
    td.rgba.assign(3u, 255u);

    sm::Politik p = sm::generate_politik(123u, 8, 8, &td, 102u, 12);

    CHECK(!td.has_rgba_storage(),
          "malformed Politik input must be rejected by terrain helper");
    CHECK(p.mapW == 8 && p.mapH == 8,
          "Politik generation should keep requested valid map dimensions");
    CHECK(p.cellOwner.size() == 64u,
          "Politik generation should allocate ownership for valid map dimensions");
    CHECK(!p.cities.empty(),
          "Politik generation should fall back to no-terrain placement instead of failing open");
    for (const sm::City& c : p.cities)
    {
        CHECK(c.x >= 0 && c.x < p.mapW && c.y >= 0 && c.y < p.mapH,
              "Politik fallback cities must stay inside map bounds");
    }

    sm::snap_cities_to_land(p, td, 102u, 8);
    sm::finalize_politik(p, td, 102u);
    CHECK(p.cellOwner.size() == 64u,
          "malformed terrain finalization must not corrupt ownership storage");

    const sm::Politik invalidMap = sm::generate_politik(123u, 0, 8, &td, 102u, 12);
    CHECK(invalidMap.mapW == 0 && invalidMap.mapH == 0
              && invalidMap.cellOwner.empty() && invalidMap.cities.empty(),
          "invalid Politik map dimensions must fail closed");
}

// The dirt law (road-class registry, 2026-08-29): lanes are laid by THE A*
// over the step-cost law — a village reaches its home city and the nearest
// landmark in reach, never overwrites stone, never touches water, and a
// village with no reachable target honestly gets NO lane (the old lerp
// stamped one across anything that was not water).
void test_dirt_roads_lay_a_star_lanes()
{
    sm::TerrainData td = make_terrain(16, 8, 160);
    sm::FeatureLayer features;
    features.resize(td.width, td.height);
    features.set(12, 4, sm::FT_Road); // the city stands on stone already

    std::vector<sm::VillageRoadSite> villages(1);
    villages[0].x = 2 - 16; // out-of-range on purpose: must wrap, not index
    villages[0].y = 4;
    villages[0].cityX = 12;
    villages[0].cityY = 4;
    villages[0].hasCity = true;

    const int laid = sm::trace_dirt_roads(features, td, villages, {}, 4);

    CHECK(laid > 0, "a reachable home city must get a dirt lane");
    CHECK(features.at(2, 4) == sm::FT_DirtRoad,
          "the village cell must carry its road class (wrapped coordinates)");
    CHECK(features.at(12, 4) == sm::FT_Road,
          "a dirt lane must never overwrite stone at its target");
    // The lane is CONTINUOUS ground the A* walked: the torus-shortest route
    // 2 -> 12 is westward (6 steps), so at least that many cells landed.
    CHECK(laid >= 6, "the lane must cover the torus-shortest cell distance");
}

void test_dirt_roads_refuse_unreachable_targets()
{
    // Two islands: land x in [0..5] and [10..13], ocean elsewhere. The old
    // lerp would have stamped a causeway; the law says no road at all.
    sm::TerrainData td = make_terrain(16, 8, 0);
    for (int y = 0; y < td.height; ++y)
    {
        for (int x = 0; x <= 5; ++x) set_cell(td, x, y, 160);
        for (int x = 10; x <= 13; ++x) set_cell(td, x, y, 160);
    }
    sm::FeatureLayer features;
    features.resize(td.width, td.height);

    std::vector<sm::VillageRoadSite> villages(1);
    villages[0].x = 2;
    villages[0].y = 4;
    villages[0].cityX = 12;
    villages[0].cityY = 4;
    villages[0].hasCity = true;

    const int laid = sm::trace_dirt_roads(features, td, villages, {}, 4);

    CHECK(laid == 1,
          "a cross-island city gets no lane — only the village cell stamps");
    CHECK(features.at(2, 4) == sm::FT_DirtRoad,
          "the orphan village still sits on its road class");
    CHECK(features.at(12, 4) == sm::FT_None,
          "no causeway: the far shore must stay untouched");
    int wetDirt = 0;
    for (int y = 0; y < td.height; ++y)
        for (int x = 0; x < td.width; ++x)
            if (td.is_water(x, y, 102) && features.at(x, y) != sm::FT_None)
                ++wetDirt;
    CHECK(wetDirt == 0, "dirt lanes must never stamp water cells");
}

void test_dirt_roads_reach_gates_landmark_lane()
{
    sm::TerrainData td = make_terrain(16, 16, 160);

    std::vector<sm::VillageRoadSite> villages(1);
    villages[0].x = 2;
    villages[0].y = 2;
    villages[0].hasCity = false; // orphan of a city; the landmark row alone
    const std::vector<sm::RoadSite> landmarks{{10, 2}}; // torus distance 8

    sm::FeatureLayer nearFeatures;
    nearFeatures.resize(td.width, td.height);
    const int laidNear =
        sm::trace_dirt_roads(nearFeatures, td, villages, landmarks, 9);
    CHECK(laidNear > 1 && nearFeatures.at(10, 2) == sm::FT_DirtRoad,
          "a landmark within reach must get a lane ending at the landmark");

    sm::FeatureLayer farFeatures;
    farFeatures.resize(td.width, td.height);
    const int laidFar =
        sm::trace_dirt_roads(farFeatures, td, villages, landmarks, 4);
    CHECK(laidFar == 1 && farFeatures.at(10, 2) == sm::FT_None,
          "a landmark beyond reach gets no lane — only the village cell");
}

void test_dirt_roads_fail_closed_on_malformed_inputs()
{
    std::vector<sm::VillageRoadSite> villages(1);
    villages[0].x = 1;
    villages[0].y = 1;
    villages[0].cityX = 3;
    villages[0].cityY = 3;
    villages[0].hasCity = true;

    // Short terrain RGBA storage.
    sm::TerrainData shortTd;
    shortTd.width = 4;
    shortTd.height = 4;
    shortTd.rgba.assign(3u, 255u);
    sm::FeatureLayer features;
    features.resize(4, 4);
    CHECK(sm::trace_dirt_roads(features, shortTd, villages, {}, 4) == 0,
          "dirt-road tracing must fail closed on malformed terrain storage");

    // Feature layer that does not cover the terrain.
    sm::TerrainData td = make_terrain(5, 5, 160);
    sm::FeatureLayer mismatched;
    mismatched.resize(4, 4);
    CHECK(sm::trace_dirt_roads(mismatched, td, villages, {}, 4) == 0,
          "dirt-road tracing must fail closed on a non-covering feature layer");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(mismatched.at(x, y) == sm::FT_None,
                  "failed-closed tracing must leave the feature layer untouched");
}

} // namespace

int main()
{
    test_road_prunes_water_only_connection();
    test_road_survives_land_detour_without_water_cells();
    test_road_uses_a_star_on_open_land_connection();
    test_road_tracing_uses_map_sea_level();
    test_large_road_search_restores_same_land_detour();
    test_road_bridges_one_cell_river();
    test_two_separating_straits_stay_unbridged();
    test_dirt_lane_lays_a_stone_bridge();
    test_tree_spawner_respects_river_buffer();
    test_tree_spawner_uses_map_sea_level();
    test_malformed_terrain_fails_closed();
    test_politik_malformed_terrain_fails_closed();
    test_dirt_roads_lay_a_star_lanes();
    test_dirt_roads_refuse_unreachable_targets();
    test_dirt_roads_reach_gates_landmark_lane();
    test_dirt_roads_fail_closed_on_malformed_inputs();
    return sm::test::report("road_river_generation_test");
}

#include "check.h"

#include "macro/spawners.h"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace
{

// ── ФИКСТУРА АВТОРИТ УРОВНЕМ ПОЛЯ, А НЕ БАЙТОМ КАРТЫ ─────────────────────
// Карта хранит СЛОВО (`kFieldWordMax`), и байтовый литерал в ней компилируется
// молча: `140` раньше значило «суша 0.549», а словом значит 0.002, то есть
// воду. Поэтому двери свидетеля берут АВТОРСКИЙ УРОВЕНЬ 0..1 и переводят его
// единственной дверью записи `field_word_of` — ровно как мир.

// ОБЫЧНАЯ СУША фикстур — середина между плоскостью моря и горной линией,
// выведенная, а не списанная. Здесь стоял байт 160 (0.627): под линией 0.75 это
// была равнина, а когда линия переехала на 0.625 (бескламповый синтез,
// 2026-10-01), ВСЕ эти миры молча стали горными целиком — и дорога, которой
// горы дороже, пошла другим путём и штемпелевала отвергнутую воду.
constexpr float kPlainLand01 =
    0.5f * (sm::kDefaultSeaLevel + sm::kMountainBiomeLevel);

// НИЗКАЯ ПЛОСКОСТЬ МОРЯ свидетеля и уровень МЕЖДУ двумя плоскостями: при 0.30
// это берег, при дефолтной 0.40 — дно. Ровно этим одним числом и различаются
// парные прогоны ниже (M-109). Здесь стоял байт 90.
constexpr float kLowSeaLevel = 0.30f;
constexpr float kBetweenSeaPlanes01 = 90.0f / 255.0f;
static_assert(kBetweenSeaPlanes01 > kLowSeaLevel
                  && kBetweenSeaPlanes01 < sm::kDefaultSeaLevel,
              "уровень обязан лежать МЕЖДУ плоскостями, иначе пара не спорит");

// Середина матрицы климата (ЗАКОН СЛОВАРЯ: биом берётся из каналов, и середина
// даёт Meadow). Здесь стоял байт 128.
constexpr std::uint16_t kClimateMid = sm::field_word_of(128.0f / 255.0f);

// Маска A — уже только канал ТЕКСТУРЫ для шейдера; мир воду спрашивает у
// плоскости. Свидетель всё равно пишет её согласованно, чтобы карта не несла
// двух правописаний одного порога.
constexpr std::uint16_t land_mask(float level01)
{
    return level01 < sm::kDefaultSeaLevel ? std::uint16_t(0)
                                          : std::uint16_t(sm::kFieldWordMax);
}

sm::TerrainData make_terrain(int w, int h, float level01)
{
    sm::TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * std::size_t(h) * 4, 0);
    // Плоскость моря живёт на карте (M-109) — свидетель ставит её сам.
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    const std::uint16_t word = sm::field_word_of(level01);
    const std::uint16_t mask = land_mask(level01);
    for (int i = 0; i < w * h; ++i)
    {
        const std::size_t s = std::size_t(i) * 4;
        td.rgba[s + 0] = word;
        td.rgba[s + 1] = kClimateMid;
        td.rgba[s + 2] = kClimateMid;
        td.rgba[s + 3] = mask;
    }
    // РОЖДЕНИЕ КАРТЫ КОНЧАЕТСЯ ВЫПЕЧКОЙ ПОЛЯ БИОМА (ЗАКОН ПОЛЯ): живой мир
    // читает поле, а не каскад, поэтому карта без выпечки — карта НЕДОРОЖДЁННАЯ,
    // и её биом честно отвечает водой.
    sm::bake_biomes(td);
    return td;
}

// ВЫСОТА — ИСТОЧНИК БИОМА, значит правка мастера кончается перепечкой поля
// (ЗАКОН ПОЛЯ): карта, у которой мастер и поле разошлись, есть карта с двумя
// ответами — ровно то, против чего поле и заведено. Свидетелю это дёшево, а
// закон он охраняет тот же, что мир.
void set_cell(sm::TerrainData& td, int x, int y, float level01)
{
    const std::size_t s = (std::size_t(y) * td.width + x) * 4;
    td.rgba[s + 0] = sm::field_word_of(level01);
    td.rgba[s + 3] = land_mask(level01);
    sm::bake_biomes(td);
}

sm::City make_city(int x, int y, int connection)
{
    sm::City c{};
    c.x = x;
    c.y = y;
    c.factionIdx = 0;
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
    sm::TerrainData td = make_terrain(8, 8, 0.0f);   // ЗАКОН АДРЕСА: квадрат, po2
    set_cell(td, 1, 1, kPlainLand01);
    set_cell(td, 3, 3, kPlainLand01);

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(1, 1, 1));
    cityPlan.push_back(make_city(3, 3, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);

    const int failsBefore = sm::test::failures();
    CHECK(stats.attemptedEdges == 1, "water-only edge should be attempted once");
    CHECK(stats.keptEdges == 0, "water-only edge must not survive");
    CHECK(stats.prunedEdges == 1, "water-only edge must be pruned");
    CHECK(stats.componentPrunedEdges == 1,
          "water-only edge should be rejected before expensive A*");
    CHECK(!has_connection(cityPlan[0], 1) && !has_connection(cityPlan[1], 0),
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
    // ЗАКОН АДРЕСА: квадрат, po2 (была 5×3 — незаконная форма мира; носитель
    // теста — обход водяного столба ЧЕРЕЗ ЗАВОРОТ ТОРА — сохранён).
    sm::TerrainData td = make_terrain(8, 8, kPlainLand01);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 2, y, 0.0f);
    }

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(1, 1, 1));
    cityPlan.push_back(make_city(3, 1, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);

    const int failsBefore = sm::test::failures();
    CHECK(stats.attemptedEdges == 1, "detour edge should be attempted once");
    CHECK(stats.keptEdges == 1, "land detour edge should survive");
    CHECK(stats.prunedEdges == 0, "land detour edge should not be pruned");
    CHECK(stats.componentPrunedEdges == 0,
          "land detour edge must still run through road A*");
    CHECK(has_connection(cityPlan[0], 1) && has_connection(cityPlan[1], 0),
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
    sm::TerrainData td = make_terrain(8, 8, kPlainLand01);

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(1, 1, 1));
    cityPlan.push_back(make_city(6, 6, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);

    CHECK(stats.attemptedEdges == 1, "open-land edge should be attempted once");
    CHECK(stats.keptEdges == 1, "open-land edge should survive");
    CHECK(stats.prunedEdges == 0, "open-land edge should not be pruned");
    CHECK(stats.expansions > 0,
          "road tracing must use A* for terrain-cost validation");
    CHECK(!roads.empty(), "open-land road mask should be allocated");
}

void test_road_tracing_uses_map_sea_level()
{
    // ЗАКОН АДРЕСА: квадрат, po2 (была 5×1 — незаконная форма мира; носитель
    // теста — активный уровень моря решает связность — сохранён).
    sm::TerrainData td = make_terrain(8, 8, kBetweenSeaPlanes01);
    for (std::size_t i = 0; i < td.cell_count(); ++i)
    {
        td.rgba[i * 4u + 3] = std::uint16_t(sm::kFieldWordMax);
    }

    // ОДНО ЧИСЛО ДВИЖЕТ МИР: уровень моря — колонка карты, поэтому два
    // сравниваемых мира различаются ровно им, а не аргументом двери (M-109).
    sm::TerrainData lowSeaTd = td;
    lowSeaTd.seaLevel16 = sm::field_word_of(kLowSeaLevel);
    sm::bake_biomes(lowSeaTd);   // плоскость сдвинута — поле биома за ней
    sm::TerrainData defaultSeaTd = td;
    defaultSeaTd.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    sm::bake_biomes(defaultSeaTd);   // плоскость сдвинута — поле биома за ней

    std::vector<sm::City> lowSeaPlan;
    lowSeaPlan.push_back(make_city(0, 0, 1));
    lowSeaPlan.push_back(make_city(4, 0, 0));
    sm::RoadTraceStats lowSeaStats;
    const std::vector<std::uint8_t> lowSeaRoads =
        sm::trace_roads(lowSeaTd, lowSeaPlan, &lowSeaStats);

    std::vector<sm::City> defaultSeaPlan;
    defaultSeaPlan.push_back(make_city(0, 0, 1));
    defaultSeaPlan.push_back(make_city(4, 0, 0));
    sm::RoadTraceStats defaultSeaStats;
    const std::vector<std::uint8_t> defaultSeaRoads =
        sm::trace_roads(defaultSeaTd, defaultSeaPlan, &defaultSeaStats);

    CHECK(lowSeaStats.keptEdges == 1 && lowSeaStats.prunedEdges == 0,
          "road tracing must use active low sea level for land connectivity");
    CHECK(!lowSeaRoads.empty() && lowSeaRoads[0] == 255u,
          "active low sea road trace must stamp reachable land");
    CHECK(defaultSeaStats.keptEdges == 0 && defaultSeaStats.componentPrunedEdges == 1,
          "road tracing must reject the same cells below active default sea level");
    CHECK(!has_connection(defaultSeaPlan[0], 1)
              && !has_connection(defaultSeaPlan[1], 0),
          "default-sea rejected road must prune Politik edges");
    CHECK(!defaultSeaRoads.empty() && defaultSeaRoads[0] == 0u,
          "default-sea rejected road must not stamp water cells");
}

void test_large_road_search_restores_same_land_detour()
{
    sm::TerrainData td = make_terrain(256, 256, kPlainLand01);   // ЗАКОН АДРЕСА: po2
    // TWO water columns: a one-cell wall would be bridgeable now (every wall
    // cell has land on both E/W sides), and this test is about the search
    // BUDGET — the wall must force the long detour, not invite a span.
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 150, y, 0.0f);
        set_cell(td, 151, y, 0.0f);
    }
    set_cell(td, 150, 0, kPlainLand01);
    set_cell(td, 151, 0, kPlainLand01);
    set_cell(td, 150, td.height - 1, kPlainLand01);
    set_cell(td, 151, td.height - 1, kPlainLand01);

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(120, 150, 1));
    cityPlan.push_back(make_city(180, 150, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);

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
    // ЗАКОН АДРЕСА (владелец, 2026-09-23): мир ВСЕГДА квадрат и степень
    // двойки — здесь стояло 20×9. Ширина ВЫРОСЛА, а не упала: оба барьера
    // (река x=5, пролив x=13..14) и полоса суши между проливом и швом
    // обязаны уцелеть, иначе фикстура проверяла бы другую геометрию.
    sm::TerrainData td = make_terrain(32, 32, kPlainLand01);
    for (int y = 0; y < td.height; ++y)
    {
        // one-cell river (вода: уровень 0.353 ниже плоскости 0.40)
        set_cell(td, 5, y, kBetweenSeaPlanes01);
        set_cell(td, 13, y, kBetweenSeaPlanes01);  // two-cell strait
        set_cell(td, 14, y, kBetweenSeaPlanes01);
    }
    return td;
}

void test_road_bridges_one_cell_river()
{
    sm::TerrainData td = make_two_barrier_terrain();

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(2, 4, 1));
    cityPlan.push_back(make_city(8, 4, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);

    CHECK(stats.componentPrunedEdges == 0,
          "one-cell water joins its shores into ONE road component");
    CHECK(stats.keptEdges == 1 && stats.prunedEdges == 0,
          "the cross-river edge must survive over a bridge");
    CHECK(has_connection(cityPlan[0], 1) && has_connection(cityPlan[1], 0),
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
    // ЗАКОН АДРЕСА (владелец, 2026-09-23): мир ВСЕГДА квадрат и степень
    // двойки — здесь стояло 20×9. Ширина ВЫРОСЛА, а не упала: оба барьера
    // (река x=5, пролив x=13..14) и полоса суши между проливом и швом
    // обязаны уцелеть, иначе фикстура проверяла бы другую геометрию.
    sm::TerrainData td = make_terrain(32, 32, kPlainLand01);
    for (int y = 0; y < td.height; ++y)
    {
        set_cell(td, 5, y, kBetweenSeaPlanes01);
        set_cell(td, 6, y, kBetweenSeaPlanes01);
        set_cell(td, 13, y, kBetweenSeaPlanes01);
        set_cell(td, 14, y, kBetweenSeaPlanes01);
    }

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(2, 4, 1));
    cityPlan.push_back(make_city(9, 4, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);

    CHECK(stats.keptEdges == 0 && stats.componentPrunedEdges == 1,
          "two-cell water has no one-cell crossing — the edge is pruned");
    CHECK(!has_connection(cityPlan[0], 1) && !has_connection(cityPlan[1], 0),
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

// ── ОДНА ДВЕРЬ ШТАМПА, ОДНА КОЛОНКА, ТРИ ЕЁ ЗНАЧЕНИЯ (M-212) ───────────────
//
// Владелец, 2026-10-03: «ну дорога на воде превращается в фичу мост да? надо
// без костылей, а системно». Системно — это когда «нельзя», «всё равно» и
// «превращаюсь» перестают быть тремя механизмами и становятся тремя
// ЗНАЧЕНИЯМИ одной колонки `FeatureDef::onWater`. Здесь судятся ровно эти три.
//
// ТАБЛИЦА МУТАЦИЙ, ПРОГНАНА 2026-10-03 (§8 п.6):
//   · `FT_Road.onWater` = `FT_None` (дорога перестала быть мостом)
//         → КРАСНЫЙ, 3 из 493;
//   · `FT_City.onWater` = `FT_City` (город стал терпеть воду)
//         → КРАСНЫЙ, 2 из 493, отказов 7 → 6 из 7;
//   · дверь перестала спрашивать воду (`got = want`)
//         → КРАСНЫЙ, 7 из 493, отказов 7 → 0;
//   · `FT_Bridge.buildsPerDay` 4 → 0 (ПЕРЕКАЛИБРОВКА чужой колонки)
//         → ЗЕЛЁНЫЙ, 493 — и обязан: этот свидетель судит воду, а не цену.
void test_water_rule_lives_in_the_feature_row()
{
    // Мир из двух клеток: левая мокрая, правая сухая. Свидетель строит себе
    // предусловие сам (§8 п.11) — на везение генератора он не надеется.
    sm::TerrainData td;
    td.width = 2;
    td.height = 1;
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    td.rgba.assign(8u, 0u);
    td.rgba[0] = std::uint16_t(td.seaLevel16 / 2u);        // вода
    td.rgba[4] = std::uint16_t(td.seaLevel16 + 1000u);     // суша
    CHECK(td.is_water(0u) && !td.is_water(1u),
          "фикстура: одна клетка мокрая, одна сухая");

    sm::FeatureLayer fl;
    fl.resize(2, 1);

    // (1) ПРЕВРАЩАЕТСЯ: дорога на воде есть мост — и это ДАННЫЕ строки, а не
    // ветка у звонящего. Обе дорожные строки дают ОДИН пролёт: «every bridge
    // is stone» теперь сказано таблицей, а не комментарием в двух местах.
    CHECK(sm::stamp_feature(fl, td, 0u, sm::FT_Road) == sm::FT_Bridge,
          "дорога на воде становится мостом");
    CHECK(sm::stamp_feature(fl, td, 0u, sm::FT_DirtRoad) == sm::FT_Bridge,
          "грунтовка на воде кладёт тот же пролёт, что шоссе");
    CHECK(fl.at(0u) == sm::FT_Bridge, "и в слое лежит именно мост");

    // (2) ВСЁ РАВНО: мост на воде остаётся мостом — вода ему дом.
    CHECK(sm::stamp_feature(fl, td, 0u, sm::FT_Bridge) == sm::FT_Bridge,
          "мост на воде остаётся мостом");

    // (3) НЕ ВСТАЁТ: пашня, шахта и ПОСЕЛЕНИЕ. Поселение здесь не случайно —
    // оно есть ФИЧА СО СКВАДОМ ПОВЕРХ (владелец), и до этой двери штамп мест
    // воду не проверял ВООБЩЕ.
    const sm::FeatureType dry[] = {sm::FT_Field, sm::FT_IronMine, sm::FT_Pasture,
                                   sm::FT_City, sm::FT_Village, sm::FT_Spire,
                                   sm::FT_Ruin};
    int refused = 0;
    for (sm::FeatureType ft : dry)
        if (sm::stamp_feature(fl, td, 0u, ft) == sm::FT_None) ++refused;
    CHECK(refused == int(std::size(dry)),
          "пашня, шахта и поселение на воде не встают вовсе");
    CHECK(fl.at(0u) == sm::FT_Bridge,
          "отказ НИЧЕГО не пишет в слой — мост остался на месте");

    // (4) НА СУШЕ КОЛОНКА МОЛЧИТ: что просили, то и встало.
    int asAsked = 0;
    for (sm::FeatureType ft : dry) {
        if (sm::stamp_feature(fl, td, 1u, ft) == ft) ++asAsked;
    }
    CHECK(asAsked == int(std::size(dry)),
          "на суше встаёт ровно то, что просили — колонка в дело не лезет");
    CHECK(sm::stamp_feature(fl, td, 1u, sm::FT_Road) == sm::FT_Road,
          "дорога на суше остаётся дорогой");
    std::printf("  [колонка воды] отказов %d из %zu, на суше как просили %d\n",
                refused, std::size(dry), asAsked);
}

void test_tree_spawner_never_plants_on_water()
{
    // ЗАКОН, КОТОРЫЙ ОСТАЛСЯ, И ЕДИНСТВЕННЫЙ (M-211, вердикт владельца:
    // «пусть не отступа ни на сколько, просто минимальное простое бинарное
    // правило — на воде леса нет»). Прежняя редакция этого свидетеля охраняла
    // БУФЕР в две клетки вокруг реки и ставила маску русла руками; маска
    // снесена вместе с буфером, и охранять там больше нечего — но закон «не на
    // воде» жив, и теперь его носитель виден прямо: прокоп в поле высот.
    //
    // Свидетель СТРОИТ СЕБЕ ПРЕДУСЛОВИЕ (§8 п.11): режет русло сам, врезая
    // полосу ниже плоскости моря, а не надеется, что трассер её проложит.
    sm::TerrainData dry = make_terrain(64, 64, 150.0f / 255.0f);
    sm::TerrainData cut = dry;
    const std::uint16_t bed = std::uint16_t(cut.seaLevel16 / 2u);
    for (int y = 0; y < cut.height; ++y)
        cut.rgba[(std::size_t(y) * std::size_t(cut.width) + 32u) * 4u] = bed;

    const std::vector<sm::TreePoint> dryTrees = sm::spawn_trees(dry, std::uint32_t{42});
    const std::vector<sm::TreePoint> cutTrees = sm::spawn_trees(cut, std::uint32_t{42});

    CHECK(!dryTrees.empty(), "контрольный мир обязан вырастить хоть одно дерево");
    CHECK(!cutTrees.empty(), "мир с руслом растит лес везде, кроме самой воды");

    int onWater = 0;
    for (const sm::TreePoint& t : cutTrees)
        if (cut.is_water(sm::cell_of(t.x, t.y, cut.width))) ++onWater;
    CHECK(onWater == 0, "на воде леса нет — единственное правило, и оно бинарное");

    // НЕГАТИВНЫЙ КОНТРОЛЬ СВОЕГО ЖЕ ДЕТЕКТОРА: в сухом мире русла нет, значит
    // те же клетки водой не являются и запрет не срабатывает ни разу. Без этой
    // строки «ноль деревьев на воде» был бы верен и на пустом множестве.
    int dryOnSameColumn = 0;
    for (const sm::TreePoint& t : dryTrees)
        if (t.x == 32) ++dryOnSameColumn;
    CHECK(dryOnSameColumn > 0,
          "без вреза та же колонка лес держит — иначе проверка пуста");
    std::printf("  [лес и вода] деревьев %zu, на воде %d, в сухой колонке 32: %d\n",
                cutTrees.size(), onWater, dryOnSameColumn);
}

void test_tree_spawner_uses_map_sea_level()
{
    // Та же карта, одно различие — ПЛОСКОСТЬ МОРЯ (M-109): при 0.30 уровень
    // 0.353 это берег, при 0.40 — дно. Маска здесь больше ни при чём, поэтому
    // ручная простановка A=суша снята: она и была той самой второй правдой.
    sm::TerrainData lowSeaTd = make_terrain(64, 64, kBetweenSeaPlanes01);
    lowSeaTd.seaLevel16 = sm::field_word_of(kLowSeaLevel);
    sm::bake_biomes(lowSeaTd);   // плоскость сдвинута — поле биома за ней
    sm::TerrainData defaultSeaTd = make_terrain(64, 64, kBetweenSeaPlanes01);
    defaultSeaTd.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    sm::bake_biomes(defaultSeaTd);   // плоскость сдвинута — поле биома за ней

    const std::vector<sm::TreePoint> lowSeaTrees =
        sm::spawn_trees(lowSeaTd, std::uint32_t{42});
    const std::vector<sm::TreePoint> defaultSeaTrees =
        sm::spawn_trees(defaultSeaTd, std::uint32_t{42});

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

    std::vector<sm::City> cityPlan;
    cityPlan.push_back(make_city(0, 0, 1));
    cityPlan.push_back(make_city(3, 3, 0));

    sm::RoadTraceStats stats;
    const std::vector<std::uint8_t> roads = sm::trace_roads(td, cityPlan, &stats);
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

    std::vector<sm::City> cityPlan = sm::generate_politik(123u, 8, 8, &td, 12);

    CHECK(!td.has_rgba_storage(),
          "malformed Politik input must be rejected by terrain helper");
    CHECK(!cityPlan.empty(),
          "Politik generation should fall back to no-terrain placement instead of failing open");
    for (const sm::City& c : cityPlan)
    {
        CHECK(c.x >= 0 && c.x < 8 && c.y >= 0 && c.y < 8,
              "Politik fallback cities must stay inside the REQUESTED map");
    }

    // ПОЛЕ ВЛАДЕНИЯ: ОДИН ПИСАТЕЛЬ, СТОРОНА МИРА, ОТКАЗ В НИЧЕЙНОЕ (M-90).
    // Прежде поле размерялось ДВАЖДЫ — вверху генератора и внизу этой двери,
    // — и сторону брало у терраина, поэтому несовпадение карты и мира молча
    // переразмеряло слой. Утверждается ПАРА: на несовпадении дверь отдаёт
    // сторону МИРА и НИ ОДНОГО владельца, на совпадении — владельца отдаёт.
    // Одиночная половина зеленела бы на двери, которая не работает вовсе.
    std::vector<std::uint8_t> owner(7u, 0u);   // мусор прошлой жизни в поле
    sm::snap_cities_to_land(cityPlan, td, 8);
    sm::finalize_politik(cityPlan, owner, 8, 8, td);
    CHECK(owner.size() == 64u,
          "поле владения размеряется стороной МИРА, а не битого терраина");
    std::size_t owned = 0;
    for (std::uint8_t b : owner) owned += (b != 0xffu) ? 1u : 0u;
    CHECK(owned == 0u,
          "битый терраин обязан оставить мир НИЧЕЙНЫМ, а не прошлой жизнью");

    sm::TerrainData whole = make_terrain(8, 8, kPlainLand01);
    std::vector<sm::City> onePlan;
    onePlan.push_back(make_city(1, 1, -1));
    std::vector<std::uint8_t> wholeOwner;
    sm::finalize_politik(onePlan, wholeOwner, 8, 8, whole);
    CHECK(wholeOwner.size() == 64u, "целый мир получает поле своей стороны");
    std::size_t claimed = 0;
    for (std::uint8_t b : wholeOwner) claimed += (b == 0u) ? 1u : 0u;
    CHECK(claimed == 64u,
          "единственный город целого сухого мира обязан занять его весь");

    // Сторона мира не степень двойки/не задана — план пуст, поле пусто.
    std::vector<sm::City> invalidMap = sm::generate_politik(123u, 0, 8, &td, 12);
    std::vector<std::uint8_t> invalidOwner(9u, 0u);
    sm::finalize_politik(invalidMap, invalidOwner, 0, 8, td);
    CHECK(invalidMap.empty() && invalidOwner.empty(),
          "invalid Politik map dimensions must fail closed");
}

// The dirt law (road-class registry, 2026-08-29): lanes are laid by THE A*
// over the step-cost law — a village reaches its home city and the nearest
// landmark in reach, never overwrites stone, never touches water, and a
// village with no reachable target honestly gets NO lane (the old lerp
// stamped one across anything that was not water).
void test_dirt_roads_lay_a_star_lanes()
{
    sm::TerrainData td = make_terrain(16, 16, kPlainLand01);   // ЗАКОН АДРЕСА: квадрат, po2
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
    sm::TerrainData td = make_terrain(16, 16, 0.0f);  // ЗАКОН АДРЕСА: квадрат, po2
    for (int y = 0; y < td.height; ++y)
    {
        for (int x = 0; x <= 5; ++x) set_cell(td, x, y, kPlainLand01);
        for (int x = 10; x <= 13; ++x) set_cell(td, x, y, kPlainLand01);
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
            if (td.is_water(x, y) && features.at(x, y) != sm::FT_None)
                ++wetDirt;
    CHECK(wetDirt == 0, "dirt lanes must never stamp water cells");
}

void test_dirt_roads_reach_gates_landmark_lane()
{
    sm::TerrainData td = make_terrain(16, 16, kPlainLand01);

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

    // Feature layer that does not cover the terrain. (8×8 против 4×4 —
    // ЗАКОН АДРЕСА: мир po2; носитель теста — НЕСОВПАДЕНИЕ размеров.)
    sm::TerrainData td = make_terrain(8, 8, kPlainLand01);
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
    test_water_rule_lives_in_the_feature_row();
    test_tree_spawner_never_plants_on_water();
    test_tree_spawner_uses_map_sea_level();
    test_malformed_terrain_fails_closed();
    test_politik_malformed_terrain_fails_closed();
    test_dirt_roads_lay_a_star_lanes();
    test_dirt_roads_refuse_unreachable_targets();
    test_dirt_roads_reach_gates_landmark_lane();
    test_dirt_roads_fail_closed_on_malformed_inputs();
    return sm::test::report("road_river_generation_test");
}

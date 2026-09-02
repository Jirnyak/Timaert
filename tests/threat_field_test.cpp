// ПОЛЕ УГРОЗЫ (CANON S10 «хемотаксис по графу округ», владелец 2026-09-02)
// — инварианты, каждый как обещание:
//   · ИСТОЧНИК: Died-факт летописи вносит в округу смерти стоимость душ по
//     строке найма (ни одного нового писателя — поле читает кольцо);
//   · ДИФФУЗИЯ: po2-доля утекает соседям ПО РЁБРАМ графа порталов — округа
//     без мембраны (остров без ребра) не получает ничего;
//   · РАСПАД: threat >>1 раз в kThreatDecayDays, по календарю мира;
//   · РЕПЛЕЙ: поле derived — свежий NavWorld доигрывает кольцо с поправкой
//     на возраст и сходится с полем, жившим все эти дни вживую.
#include "check.h"

#include "macro/chronicle.h"
#include "macro/nav_field.h"
#include "macro/state.h"
#include "macro/threat_field.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 64;

GameState make_world() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    gs.worldSeed = 7u;
    Landmark city{};
    city.type = LandmarkType::City;
    city.id = 9;
    city.x = 10;
    city.y = 10;
    city.population = 500;
    gs.landmarks.push_back(city);
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 40;
    vil.y = 10;
    vil.population = 100;
    vil.nearestCityId = 9;
    gs.landmarks.push_back(vil);
    chronicle_init(gs.chronicle, kMap, kMap);
    return gs;
}

// Рукотворный граф: округа 0 (запад, город 9) ↔ округа 1 (восток, деревня
// 3) одной мембраной; свежесть подогнана под nav_ensure, чтобы боевой путь
// ротации не перепёк рукоделие.
NavWorld make_nav(const GameState& gs, bool withPortal) {
    NavWorld nv{};
    nv.mapW = kMap;
    nv.mapH = kMap;
    nv.bakedSeed = gs.worldSeed;
    std::uint32_t live = 0;
    for (const Landmark& lm : gs.landmarks)
        if (lm.type != LandmarkType::None) ++live;
    nv.bakedLandmarks = live;
    const std::size_t cells = std::size_t(kMap) * std::size_t(kMap);
    nv.regionOf.assign(cells, 0);
    for (int y = 0; y < kMap; ++y)
        for (int x = 0; x < kMap; ++x)
            nv.regionOf[std::size_t(y) * kMap + x] = x < kMap / 2 ? 0 : 1;
    nv.distHome.assign(cells, 16);
    nv.stepHome.assign(cells, 0);
    nv.waterRegionOf.assign(cells, kNavNoRegion);
    nv.regionLandmarkId = {9, 3};
    nv.regionCell = {10 * kMap + 10, 10 * kMap + 40};
    if (withPortal) {
        NavPortal ab{};
        ab.cellFrom = 10 * kMap + 31;
        ab.cellTo = 10 * kMap + 32;
        ab.toRegion = 1;
        NavPortal ba{};
        ba.cellFrom = 10 * kMap + 32;
        ba.cellTo = 10 * kMap + 31;
        ba.toRegion = 0;
        nv.portals = {ab, ba};
        nv.portalBegin = {0, 1};
        nv.portalCount = {1, 1};
    } else {
        nv.portals.clear();
        nv.portalBegin = {0, 0};
        nv.portalCount = {0, 0};
    }
    const std::uint32_t d = 30u * 16u;   // кванты цены — 1/16 клетки
    nv.routeDist = {0u, d, d, 0u};
    nv.routeNext = {0, 1, 0, 1};
    return nv;
}

void test_source_and_diffusion() {
    GameState gs = make_world();
    NavWorld nv = make_nav(gs, /*withPortal*/true);
    MacroWorld mw{.gs = &gs};
    mw.nav = &nv;

    // Осиротевший дом хоронит двоих — та дверь, что пишет Died всегда.
    record_landmark_fact(gs, FactKind::Died, 3, 40, 12, /*amount*/2);
    threat_field_daily(mw, /*day*/1);

    const std::uint32_t price = std::uint32_t(threat_soul_price());
    const std::uint32_t raised = 2u * price;
    const std::uint32_t leak = (raised >> kThreatDiffusionShift) / 1u;
    CHECK(threat_of(nv, 1) == raised - leak,
          "источник: округа смерти держит стоимость душ минус дневную течь");
    CHECK(threat_of(nv, 0) == leak,
          "диффузия: сосед получил po2-долю через мембрану");
    CHECK(threat_of(nv, 0) + threat_of(nv, 1) == raised,
          "мембраны текут, деньги страха не испаряются (распад — отдельно)");

    // Худшая округа маршрута — то, что платит страх артелей.
    CHECK(threat_on_route(nv, 0, 1) == threat_of(nv, 1),
          "маршрутный страх видит худшую округу цепочки routeNext");
}

void test_no_edge_no_flow_and_decay() {
    GameState gs = make_world();
    NavWorld nv = make_nav(gs, /*withPortal*/false);
    MacroWorld mw{.gs = &gs};
    mw.nav = &nv;

    record_landmark_fact(gs, FactKind::Died, 3, 40, 12, /*amount*/2);
    threat_field_daily(mw, /*day*/1);
    const std::uint32_t raised = 2u * std::uint32_t(threat_soul_price());
    CHECK(threat_of(nv, 1) == raised && threat_of(nv, 0) == 0u,
          "через пролив без ребра угроза не течёт (связность честная)");

    // Дни без новостей: поле неподвижно до дня распада…
    for (int day = 2; day < kThreatDecayDays; ++day)
        threat_field_daily(mw, day);
    CHECK(threat_of(nv, 1) == raised,
          "без мембран и новостей поле стоит до дня распада");
    // …и в день распада честно теряет половину.
    threat_field_daily(mw, kThreatDecayDays);
    CHECK(threat_of(nv, 1) == raised >> 1,
          "распад: >>1 раз в kThreatDecayDays, по календарю мира");
}

void test_replay_converges() {
    GameState gs = make_world();
    NavWorld live = make_nav(gs, /*withPortal*/false);
    MacroWorld mw{.gs = &gs};
    mw.nav = &live;

    record_landmark_fact(gs, FactKind::Died, 3, 40, 12, /*amount*/2);
    // Живое поле переживает 17 дней (два распада)…
    for (int day = 1; day <= 17; ++day) threat_field_daily(mw, day);
    const std::uint32_t lived = threat_of(live, 1);

    // …а загрузка приходит на день 17 со свежим графом и доигрывает кольцо
    // с поправкой на возраст — тот же итог, ни байта поля в сейве.
    NavWorld loaded = make_nav(gs, /*withPortal*/false);
    mw.nav = &loaded;
    threat_field_daily(mw, /*day*/17);
    CHECK(threat_of(loaded, 1) == lived,
          "реплей на загрузке сходится с полем, жившим вживую");
    CHECK(lived == (2u * std::uint32_t(threat_soul_price())) >> 2,
          "два горизонта распада = две потерянные половины");
}

} // namespace

int main() {
    test_source_and_diffusion();
    test_no_edge_no_flow_and_decay();
    test_replay_converges();
    return sm::test::report("threat_field_test");
}

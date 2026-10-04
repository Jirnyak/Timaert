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
#include "macro/labour.h"   // settle_souls / souls_flock — двери душ

#include "macro/chronicle.h"
#include "macro/nav_field.h"
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/squad.h"         // record_landmark_fact — дверь дела места
#include "macro/state.h"
#include "macro/store.h"
#include "macro/threat_field.h"

#include <memory>

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 64;

// Мир фикстуры — СТРОКИ ПЛЮС ТЕЛА (M-90 шаг 5): плечо места (склад, души,
// слава) живёт колонками MacroStore, поэтому store приезжает вместе с
// GameState и переживает его ровно столько же.
struct World {
    std::unique_ptr<MacroStore> store;
    GameState gs;
    // ОРДИНАЛ ВЫДАЁТ ЭМИТЕНТ, А НЕ ФИКСТУРА (ломтик F): рукописный `id`
    // умер со строкой места, поэтому мир запоминает, кем родились его двое.
    int cityId = 0;
    int vilId = 0;
};

World make_world() {
    World w{make_macro_store(), GameState{}};
    GameState& gs = w.gs;
    MacroStore& st = *w.store;
    gs.mapW = kMap;
    gs.mapH = kMap;
    gs.worldSeed = 7u;
    {
        const MacroHandle h =
            birth_place(gs, st, SquadType::City, 10, 10);
        w.cityId = int(st.spawnId[h.slot].index);
        // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число
        // фичи, головы в инвентарь — тем же законом, что генезис.
        sm::settle_souls(gs, st, h.slot, 500);
    }
    {
        const MacroHandle h =
            birth_place(gs, st, SquadType::Village, 40, 10);
        w.vilId = int(st.spawnId[h.slot].index);
        sm::settle_souls(gs, st, h.slot, 100);
    }
    chronicle_init(gs.chronicle, kMap, kMap);
    return w;
}

// Рукотворный граф: округа 0 (запад, город) ↔ округа 1 (восток, деревня)
// одной мембраной; свежесть подогнана под nav_ensure, чтобы боевой путь
// ротации не перепёк рукоделие.
NavWorld make_nav(const World& w, bool withPortal) {
    const GameState& gs = w.gs;
    NavWorld nv{};
    nv.mapW = kMap;
    nv.mapH = kMap;
    nv.bakedSeed = gs.worldSeed;
    // Свежесть — ПО СОБЫТИЮ (CANON S9): рукоделие объявляет себя запечённым
    // на текущем составе мест, и боевой nav_ensure его не перепекает.
    nv.bakedNavEpoch = gs.navEpoch;
    const std::size_t cells = std::size_t(kMap) * std::size_t(kMap);
    nv.regionOf.assign(cells, 0);
    for (int y = 0; y < kMap; ++y)
        for (int x = 0; x < kMap; ++x)
            nv.regionOf[std::size_t(y) * kMap + x] = x < kMap / 2 ? 0 : 1;
    nv.distHome.assign(cells, 16);
    nv.stepHome.assign(cells, 0);
    nv.waterRegionOf.assign(cells, kNavNoRegion);
    nv.regionLandmarkId = {w.cityId, w.vilId};
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
    World w = make_world();
    GameState& gs = w.gs;
    NavWorld nv = make_nav(w, /*withPortal*/true);
    MacroWorld mw{.gs = &gs};
    mw.nav = &nv;

    // Осиротевший дом хоронит двоих — та дверь, что пишет Died всегда.
    record_landmark_fact(*w.store, gs, FactKind::Died, w.vilId, 40, 12,
                         /*amount*/2);
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
    World w = make_world();
    GameState& gs = w.gs;
    NavWorld nv = make_nav(w, /*withPortal*/false);
    MacroWorld mw{.gs = &gs};
    mw.nav = &nv;

    record_landmark_fact(*w.store, gs, FactKind::Died, w.vilId, 40, 12,
                         /*amount*/2);
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
    World w = make_world();
    GameState& gs = w.gs;
    NavWorld live = make_nav(w, /*withPortal*/false);
    MacroWorld mw{.gs = &gs};
    mw.nav = &live;

    record_landmark_fact(*w.store, gs, FactKind::Died, w.vilId, 40, 12,
                         /*amount*/2);
    // Живое поле переживает 17 дней (два распада)…
    for (int day = 1; day <= 17; ++day) threat_field_daily(mw, day);
    const std::uint32_t lived = threat_of(live, 1);

    // …а загрузка приходит на день 17 со свежим графом и доигрывает кольцо
    // с поправкой на возраст — тот же итог, ни байта поля в сейве.
    NavWorld loaded = make_nav(w, /*withPortal*/false);
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

// СТРАЖА (CANON S10 «стража + поле угрозы», владелец 2026-09-02) —
// инварианты, каждый как обещание:
//   · ФАКТЫ → ВЫХОД: горячая округа поля дороже похода — ротация поднимает
//     вылазку {Guard, Patrol{округа}} ДУШАМИ ИЗ ГАРНИЗОНА (население города
//     не тронуто), с провиантом на марш и патрульные дни;
//   · ТИШИНА → ГАРНИЗОН: пустое поле — ни одной вылазки, оборона цела
//     (вывод аукциона, как у артелей);
//   · ВЕРНУЛСЯ — ДУШИ НАЗАД: Idle у крыльца растворяется В ГАРНИЗОН, не в
//     население; счёт душ сходится с точностью до лидера;
//   · СТРАХ АРТЕЛЕЙ: threat округи маршрута минусом в скор аукциона —
//     деревня в горячей округе отказывается от рейсов ценой.
#include "check.h"

#include "ecs/components.h"
#include "macro/deposit_layer.h"
#include "macro/nav_field.h"
#include "macro/npc.h"
#include "macro/npc_ai.h"
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
// 3) одной мембраной; свежесть подогнана под nav_ensure — боевой путь
// ротации не перепечёт рукоделие.
NavWorld make_nav(const GameState& gs) {
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
    const std::uint32_t d = 30u * 16u;   // кванты цены — 1/16 клетки
    nv.routeDist = {0u, d, d, 0u};
    nv.routeNext = {0, 1, 0, 1};
    nv.threat.assign(2, 0u);
    return nv;
}

void fill_garrison(Landmark& city, int souls) {
    for (int i = 0; i < souls; ++i) {
        city.garrison.push(make_soldier(
            std::uint16_t(i % 3 == 0 ? NPCType::Guard : NPCType::Peasant),
            /*level*/1 + (i % 2), 1000u + std::uint32_t(i)));
    }
}

entt::entity find_guard(ecs::World& w) {
    for (auto [e, kind] : w.reg.view<ecs::NPCKind>().each()) {
        if (kind.type == std::uint16_t(NPCType::Guard)) return e;
    }
    return entt::null;
}

void test_facts_raise_the_patrol() {
    GameState gs = make_world();
    NavWorld nv = make_nav(gs);
    nv.threat[1] = 100000u;   // горячая округа деревни — дороже любого похода
    gs.landmarks[0].inventory.add("bread", 100);
    fill_garrison(gs.landmarks[0], 10);

    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};
    mw.nav = &nv;

    const int popBefore = gs.landmarks[0].population;
    const int raised = rotate_worker_squads(mw, /*day*/1);
    CHECK(raised >= 1, "горячая округа поднимает вылазку");

    const entt::entity g = find_guard(w);
    CHECK(g != entt::null, "вылазка — сквад типа Guard (строка ростера города)");
    const auto& rt = w.reg.get<ecs::MacroNpcRuntime>(g);
    CHECK(rt.errandVerb == std::uint8_t(ErrandVerb::Patrol),
          "поручение вылазки — глагол Patrol");
    CHECK(rt.errandObject == 3u,
          "объект патруля — ОРДИНАЛ ландмарка горячей округи, не её номер");
    CHECK(rt.homeSettlementId == 9, "дом вылазки — её город");

    // Души из ГАРНИЗОНА: половина ушла (лидер + ростер), население цело.
    CHECK(total_soldiers(gs.landmarks[0].garrison) == 5,
          "вылазка забрала половину гарнизона");
    const auto& roster = w.reg.get<ecs::SquadRoster>(g);
    CHECK(roster.squad.size() == 4,
          "ростер вылазки = взятые души минус лидер");
    CHECK(gs.landmarks[0].population == popBefore,
          "население города вылазка не трогает");

    // Провиант на марш + патрульные дни — тем же законом ломтя.
    const auto& bag = w.reg.get<ecs::NpcInventory>(g);
    CHECK(bag.inv.count("bread") > 0, "вылазка вышла с хлебом со склада");

    // Строка занята живым патрулём — второй вылазки нет, пока эта в поле
    // (поднятый стоит Idle у крыльца — растворился бы; в пути держит строку).
    w.reg.get<ecs::MacroNpcRuntime>(g).state =
        std::uint8_t(NPCState::Traveling);
    const int again = rotate_worker_squads(mw, /*day*/2);
    CHECK(again == 0 && find_guard(w) == g,
          "счёт по типу держит строку — двойной вылазки нет");
}

void test_silence_keeps_the_garrison_home() {
    GameState gs = make_world();
    NavWorld nv = make_nav(gs);   // поле пустое: тишина
    gs.landmarks[0].inventory.add("bread", 100);
    fill_garrison(gs.landmarks[0], 10);

    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};
    mw.nav = &nv;

    rotate_worker_squads(mw, /*day*/1);
    CHECK(find_guard(w) == entt::null,
          "тишина = вывод аукциона: стража сидит в гарнизоне за полцены");
    CHECK(total_soldiers(gs.landmarks[0].garrison) == 10,
          "оборона цела — ни одной души не снято");
}

void test_returned_souls_rejoin_the_garrison() {
    GameState gs = make_world();
    NavWorld nv = make_nav(gs);
    nv.threat[1] = 100000u;
    gs.landmarks[0].inventory.add("bread", 100);
    fill_garrison(gs.landmarks[0], 10);

    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};
    mw.nav = &nv;

    rotate_worker_squads(mw, /*day*/1);
    const entt::entity g = find_guard(w);
    CHECK(g != entt::null, "вылазка поднята (фикстура)");

    // Патруль вернулся: Idle у крыльца, поручение исполнено.
    auto& rt = w.reg.get<ecs::MacroNpcRuntime>(g);
    auto& p = w.reg.get<ecs::Position>(g);
    p.x = 10.0f;
    p.y = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.errandVerb = std::uint8_t(ErrandVerb::None);
    rt.errandObject = 0;

    const int popBefore = gs.landmarks[0].population;
    nv.threat[1] = 0u;   // округа остыла — иначе день 2 поднимет новую вылазку
    rotate_worker_squads(mw, /*day*/2);
    CHECK(find_guard(w) == entt::null, "вылазка у крыльца растворена");
    CHECK(total_soldiers(gs.landmarks[0].garrison) == 10,
          "души вернулись В ГАРНИЗОН — счёт сходится (лидер записью)");
    CHECK(gs.landmarks[0].population == popBefore,
          "растворение вылазки не рождает горожан из воздуха");
}

void test_peasants_pay_the_threat_price() {
    // Деревня в горячей округе: жила в радиусе рук, хлеб на провиант, дань
    // на рейс — но страх съедает каждый скор. Отказ рейса ценой.
    GameState gs = make_world();
    NavWorld nv = make_nav(gs);
    DepositLayer dep{};
    dep.cells[std::size_t(DepositKind::Iron)]
        [10u * std::uint32_t(kMap) + 44u] = 64;   // жила округи деревни
    gs.landmarks[1].inventory.add("bread", 200);
    gs.landmarks[1].titheOwedCoin = 200;

    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent,
                  .deposits = &dep};
    mw.nav = &nv;

    // Контроль: в тихом мире те же цели поднимают артели.
    const int calm = rotate_worker_squads(mw, /*day*/1);
    CHECK(calm > 0, "негативный контроль: тихая округа поднимает артели");
    for (auto [e, kind] : w.reg.view<ecs::NPCKind>().each()) {
        (void)kind;
        w.reg.destroy(e);
    }

    nv.threat[1] = 1u << 20;   // резня в округе деревни
    const int popAfterCalm = gs.landmarks[1].population;
    const int scared = rotate_worker_squads(mw, /*day*/2);
    CHECK(scared == 0,
          "терм опасности: страх съедает скор — отказ рейса ценой");
    CHECK(gs.landmarks[1].population == popAfterCalm,
          "отказ не трогает души деревни");
}

} // namespace

int main() {
    test_facts_raise_the_patrol();
    test_silence_keeps_the_garrison_home();
    test_returned_souls_rejoin_the_garrison();
    test_peasants_pay_the_threat_price();
    return sm::test::report("guard_patrol_test");
}

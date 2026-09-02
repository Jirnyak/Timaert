// АУКЦИОН ЦЕЛЕЙ ротации (CANON S10 «универсальный ИИ сквадов», владелец
// 2026-09-02) — инварианты, каждый как обещание:
//   · у рабочего сквада НЕТ специализации: ротация поднимает КРЕСТЬЯН, и
//     каждый поднятый несёт поручение {глагол, объект}, взятое рулеткой по
//     скору — деньги по закону цены;
//   · рулетка диверсифицирует без координации (с живыми целями разных
//     видов артели одного дня расходятся по разным);
//   · счёт по типу: строки N×Peasant заняты живыми крю — второй ротации
//     нечего поднимать, пока артели в поле;
//   · отказ = вывод аукциона: миру нечего предъявить — деревня не
//     поднимает никого (ноль целей = ноль артелей, ничего не наколдовано);
//   · дань-относ — цель крестьян: один долг, без излишков и жил, уже
//     поднимает рейс сбыта.
#include "check.h"

#include "ecs/components.h"
#include "macro/deposit_layer.h"
#include "macro/npc.h"
#include "macro/npc_ai.h"
#include "macro/resource_field.h"
#include "macro/tree_layer.h"

#include <cstdint>
#include <set>
#include <vector>

namespace {

using namespace sm;

constexpr int kMap = 64;

// Деревня с рынком: дом аукциона всех проверок ниже.
GameState make_world(int villagePop) {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    gs.worldSeed = 7u;
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    vil.population = villagePop;
    vil.nearestCityId = 9;
    gs.landmarks.push_back(vil);
    Landmark city{};
    city.type = LandmarkType::City;
    city.id = 9;
    city.x = 20;
    city.y = 10;
    city.population = 500;
    gs.landmarks.push_back(city);
    return gs;
}

struct Crew {
    std::uint8_t  verb;
    std::uint32_t object;
};

std::vector<Crew> live_crews(ecs::World& w) {
    std::vector<Crew> out;
    for (auto [e, kind, rt]
         : w.reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
        (void)e;
        if (kind.type != std::uint16_t(NPCType::Peasant)) continue;
        out.push_back(Crew{rt.errandVerb, rt.errandObject});
    }
    return out;
}

void test_auction_raises_errand_bearing_peasants() {
    GameState gs = make_world(/*pop*/100);
    gs.landmarks[0].inventory.add("grain", 5000);   // затоваривание — сбыт
    gs.landmarks[0].inventory.add("bread", 200);    // провиант артелей
    gs.landmarks[0].titheOwedCoin = 200;            // и долг дани сверху

    DepositLayer dep{};
    dep.cells[std::size_t(DepositKind::Iron)]
        [10u * std::uint32_t(kMap) + 14u] = 64;     // жила в радиусе рук
    std::vector<TreePoint> trees{{12, 12}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap, 32);

    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent,
                  .deposits = &dep, .treeGrid = &grid};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    const std::vector<Crew> crews = live_crews(w);

    CHECK(raised > 0, "мир с целями поднимает артели");
    CHECK(int(crews.size()) == raised,
          "каждый подъём — крестьянская артель (профессии не поднимаются)");
    CHECK(raised <= 4, "подъём ограничен строками ростера (N×Peasant = 4)");

    const int ironRow = gather_goal_row(ResourceFieldId::Iron);
    const int treeRow = gather_goal_row(ResourceFieldId::Trees);
    CHECK(ironRow >= 0 && treeRow >= 0,
          "таблица целей знает железо и лес по ресурсу, не по индексу");
    bool everyErrandLegal = true;
    for (const Crew& c : crews) {
        const bool gather =
            c.verb == std::uint8_t(ErrandVerb::Gather)
            && (int(c.object) == ironRow || int(c.object) == treeRow);
        const bool sell = c.verb == std::uint8_t(ErrandVerb::Sell)
                          && c.object == 9u;
        if (!gather && !sell) everyErrandLegal = false;
    }
    CHECK(everyErrandLegal,
          "каждое поручение = живая цель этого дома (жила/лес/рейс сбыта)");

    // Рулетка расходится: три вида живых целей; один день может лечь в одну
    // цель честно (скор сбыта при глуте доминирует), поэтому пин — СОЮЗ
    // бросков восьми дней на свежих мирах: рулетка, а не argmax. Сид
    // запинен — исход детерминирован.
    std::set<std::pair<int, int>> distinct;
    for (int day = 1; day <= 8; ++day) {
        GameState gsd = make_world(/*pop*/100);
        gsd.landmarks[0].inventory.add("grain", 5000);
        gsd.landmarks[0].inventory.add("bread", 200);
        gsd.landmarks[0].titheOwedCoin = 200;
        ecs::World wd;
        MacroWorld mwd{.gs = &gsd, .world = &wd, .terrain = &absent,
                       .deposits = &dep, .treeGrid = &grid};
        rotate_worker_squads(mwd, day);
        for (const Crew& c : live_crews(wd))
            distinct.insert({int(c.verb), int(c.object)});
    }
    CHECK(distinct.size() >= 2,
          "рулетка диверсифицирует артели без координации (союз 8 дней)");

    // Счёт по типу: артели в поле занимают строки — второй ротации нечего
    // поднимать (Idle дома растворился бы; в пути — держит строку).
    for (auto [e, kind, rt]
         : w.reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
        (void)e; (void)kind;
        rt.state = std::uint8_t(NPCState::Traveling);
    }
    const int again = rotate_worker_squads(mw, /*day*/2);
    CHECK(again == 0,
          "живые крю типа T занимают строки типа T — двойного подъёма нет");
}

void test_refusal_is_the_auctions_verdict() {
    // Миру нечего предъявить: ни жил, ни леса, ни рынка, пустой склад.
    GameState gs = make_world(/*pop*/100);
    gs.landmarks[0].nearestCityId = -1;
    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    CHECK(raised == 0, "ноль целей с положительным скором = ноль артелей");
    CHECK(live_crews(w).empty(),
          "отказ аукциона не колдует ни одного крестьянина");
    CHECK(gs.landmarks[0].population == 100,
          "невзятая работа не трогает души деревни");
}

void test_tithe_alone_raises_the_sell_run() {
    // Один долг дани — без излишков, жил и леса: рейс сбыта обязан ехать
    // (дань-относ = цель крестьян, вердикт 2026-09-02).
    GameState gs = make_world(/*pop*/100);
    gs.landmarks[0].titheOwedCoin = 300;
    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    const std::vector<Crew> crews = live_crews(w);
    CHECK(raised > 0, "долг дани сам по себе — цель с положительным скором");
    bool allSell = !crews.empty();
    for (const Crew& c : crews) {
        if (c.verb != std::uint8_t(ErrandVerb::Sell) || c.object != 9u)
            allSell = false;
    }
    CHECK(allSell,
          "единственная живая цель — рейс сбыта к своему рынку (объект = "
          "ординал города)");
}

} // namespace

int main() {
    test_auction_raises_errand_bearing_peasants();
    test_refusal_is_the_auctions_verdict();
    test_tithe_alone_raises_the_sell_run();
    return sm::test::report("goal_auction_test");
}

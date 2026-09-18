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
    vil.suzerainLandmarkId = 9;
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
    // УСЛОВИЕ СОЗДАНИЯ (S19.2): при подъёме списывается СЕЗОН содержания —
    // склад обязан держать хлеб на 32 дня каждого рта, иначе артель не
    // поднимается. Сезонный амбар, не «провиант на рейс».
    gs.landmarks[0].inventory.add("bread", 3200);
    gs.landmarks[0].titheOwedCoin = 200;            // и долг дани сверху

    DepositLayer dep{};
    allocate_deposit_fields(dep, kMap, kMap);
    dep.grid(DepositKind::Iron).write(14, 10, 64);  // жила в радиусе рук
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
    // Подъём случается ТОЛЬКО на границе сезона (S19.2), поэтому восемь
    // бросков рулетки — восемь ГРАНИЦ (день 1+32k), не восемь суток.
    for (int k = 0; k < 8; ++k) {
        const int day = 1 + k * kDaysPerSeason;
        GameState gsd = make_world(/*pop*/100);
        gsd.landmarks[0].inventory.add("grain", 5000);
        gsd.landmarks[0].inventory.add("bread", 3200);
        gsd.landmarks[0].titheOwedCoin = 200;
        ecs::World wd;
        MacroWorld mwd{.gs = &gsd, .world = &wd, .terrain = &absent,
                       .deposits = &dep, .treeGrid = &grid};
        rotate_worker_squads(mwd, day);
        for (const Crew& c : live_crews(wd))
            distinct.insert({int(c.verb), int(c.object)});
    }
    CHECK(distinct.size() >= 2,
          "рулетка диверсифицирует артели без координации (союз 8 границ)");

    // Счёт по типу: артели в поле занимают строки — второй ротации нечего
    // поднимать (Idle дома растворился бы; в пути — держит строку).
    for (auto [e, kind, rt]
         : w.reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
        (void)e; (void)kind;
        rt.state = std::uint8_t(NPCState::Traveling);
    }
    // Граница сезона (день 33): контроль честен только там, где подъём
    // вообще возможен — вне границы ноль тривиален.
    const int again = rotate_worker_squads(mw, /*day*/33);
    CHECK(again == 0,
          "живые крю типа T занимают строки типа T — двойного подъёма нет");
}

void test_refusal_is_the_auctions_verdict() {
    // Миру нечего предъявить: ни жил, ни леса, ни рынка, пустой склад.
    GameState gs = make_world(/*pop*/100);
    gs.landmarks[0].suzerainLandmarkId = -1;
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
    // Хлеб — только на условие создания (сезон содержания); целей добычи
    // он не рождает, единственная живая цель остаётся рейсом сбыта.
    gs.landmarks[0].inventory.add("bread", 3200);
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

// ── ДВА РЕГУЛЯТОРА суда границы (S19.2, владелец 2026-09-18): строки —
// СКОЛЬКО сквадов, пул — КАКОГО РАЗМЕРА; стоящая артель на границе ДЫШИТ
// составом (добор из населения / ссадка в население), души не рождаются и
// не испаряются — консервация проверяется суммой.
void test_boundary_court_resizes_standing_crews() {
    GameState gs = make_world(/*pop*/100);
    gs.landmarks[0].inventory.add("grain", 5000);
    gs.landmarks[0].inventory.add("bread", 3200 * 4);   // сезоны впрок
    gs.landmarks[0].titheOwedCoin = 200;
    DepositLayer dep{};
    allocate_deposit_fields(dep, kMap, kMap);
    dep.grid(DepositKind::Iron).write(14, 10, 64);
    std::vector<TreePoint> trees{{12, 12}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap, 32);
    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent,
                  .deposits = &dep, .treeGrid = &grid};
    CHECK(rotate_worker_squads(mw, /*day*/1) > 0, "граница поднимает артели");

    const auto souls_total = [&] {
        int total = gs.landmarks[0].population;
        for (auto [e, kind, rt]
             : w.reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
            (void)kind; (void)rt;
            total += 1;
            if (const auto* ro = w.reg.try_get<ecs::SquadRoster>(e))
                total += ro->squad.size();
        }
        return total;
    };
    const auto crew_sizes = [&] {
        std::vector<int> sizes;
        for (auto [e, kind, rt]
             : w.reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
            (void)kind; (void)rt;
            if (const auto* ro = w.reg.try_get<ecs::SquadRoster>(e))
                sizes.push_back(int(ro->squad.size()));
        }
        return sizes;
    };

    // ПОТЕРЯ В ПОЛЕ: у первой артели гибнут трое (души честно исчезают из
    // мира — сумма падает ровно на троих).
    entt::entity first = entt::null;
    for (auto [e, kind] : w.reg.view<ecs::NPCKind>().each()) {
        (void)kind; first = e; break;
    }
    CHECK(first != entt::null, "есть артель для среза (фикстура)");
    auto& ro = w.reg.get<ecs::SquadRoster>(first);
    CHECK(ro.squad.size() > 3, "ростер больше среза (фикстура)");
    const int cutTo = ro.squad.size() - 3;
    while (ro.squad.size() > cutTo) ro.squad.remove_at(ro.squad.size() - 1);
    const int soulsAfterLoss = souls_total();

    // Граница: суд ДОБИРАЕТ порезанную из населения — все стоящие артели
    // приведены к одному want, сумма душ не изменилась (добор — перенос).
    rotate_worker_squads(mw, /*day*/33);
    CHECK(souls_total() == soulsAfterLoss,
          "добор — перенос населения в ростер: души не рождаются");
    {
        const std::vector<int> sizes = crew_sizes();
        bool equal = !sizes.empty();
        for (const int sz : sizes) equal = equal && sz == sizes[0];
        CHECK(equal, "суд привёл составы стоящих к одному want (пул)");
        CHECK(!sizes.empty() && sizes[0] > cutTo,
              "порезанный ростер добран, дыра не висит до гибели артели");
    }

    // ПЕРЕБОР: той же артели вручную вливают семь лишних душ (модель:
    // домой пришла распухшая) — граница ССАЖИВАЕТ лишних В население.
    const int popBeforeShed = gs.landmarks[0].population;
    const int sizeBeforeShed = int(w.reg.get<ecs::SquadRoster>(first).squad.size());
    for (int k = 0; k < 7; ++k) {
        SoldierRecord rec{};
        rec.entityId = 900000u + std::uint32_t(k);
        rec.kind = std::uint16_t(NPCType::Peasant);
        rec.level = 1;
        w.reg.get<ecs::SquadRoster>(first).squad.push(rec);
    }
    const int soulsInflated = souls_total();
    rotate_worker_squads(mw, /*day*/65);
    CHECK(souls_total() == soulsInflated,
          "ссадка — перенос ростера в население: души не испаряются");
    // Want дня 65 пересчитан от базы, потолстевшей на семь влитых душ, так
    // что он может встать на голову-другую выше прежнего — пин не «равно
    // старому», а «перебор срезан к пулу».
    CHECK(int(w.reg.get<ecs::SquadRoster>(first).squad.size())
              < sizeBeforeShed + 7,
          "перебор ссажен: артель не жиреет мимо пула");
    CHECK(gs.landmarks[0].population > popBeforeShed,
          "ссаженные души вернулись в население");
}

} // namespace

int main() {
    test_auction_raises_errand_bearing_peasants();
    test_refusal_is_the_auctions_verdict();
    test_tithe_alone_raises_the_sell_run();
    test_boundary_court_resizes_standing_crews();
    return sm::test::report("goal_auction_test");
}

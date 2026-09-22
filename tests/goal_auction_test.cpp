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
//   · ДАНЬ — ЗАЯВКА СЮЗЕРЕНА, А НЕ ГРУЗ ДОЛЖНИКА (CANON S4 «сборщик идёт
//     вниз», 2026-09-22): долг вассала поднимает СБОРЩИКА У СЮЗЕРЕНА, и
//     объект поручения есть должник. Прежде этот файл утверждал обратное —
//     что долг поднимает рейс сбыта у самого должника, — и утверждал по
//     снесённому закону.
#include "check.h"

#include "ecs/components.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/npc.h"
#include "macro/nav_field.h"
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
    gs.landmarks.push_back(vil);
    Landmark city{};
    city.type = LandmarkType::City;
    city.id = 9;
    city.x = 20;
    city.y = 10;
    city.population = 500;
    gs.landmarks.push_back(city);
    // Феод ставится ОДНОЙ дверью и только когда оба места в ростере: она
    // пишет ОБА конца (S24), и полуребра в мире не бывает.
    set_suzerain(gs, 3, 9);
    return gs;
}

// Полки комфорта закрыты на сезон вперёд (pop 100): рейс сбыта-закупки
// теперь ценит ОБА конца (вердикт 2026-09-18 «голодный дом едет ПОКУПАТЬ»),
// и голая полка cloth/tools задрала бы его скор на порядки — рулетку было
// бы не разглядеть. Закрытая полка глушит покупной конец, оставляя целям
// дня сопоставимые скоры — ровно как до вердикта.
void stock_comforts(Landmark& lm) {
    for (const NeedDef& n : kNeeds) {
        if (n.popPerUnitDay == 1) continue;   // хлеб фикстуры кладут сами
        const int seasonNeed = (lm.population / n.popPerUnitDay)
                             * kDaysPerSeason;
        if (seasonNeed > 0) lm.inventory.add(n.commodity, seasonNeed);
    }
}

// ОДНА ОКРУГА НА ВСЮ КАРТУ, хозяин — деревня 3. Нав здесь не декорация:
// опись округи (survey_landmark_regions) без запечённой навигации не
// работает вовсе — «своей земли» у места нет, — а цель-ЖИЛА родится только
// из строки описи. Без нава у деревни живёт ровно одна цель добычи (лес),
// и «рулетка не диверсифицирует» читалось бы как дефект закона, тогда как
// это немота фикстуры. Свежесть объявлена по событию (CANON S9), чтобы
// боевой nav_ensure не перепёк рукоделие.
NavWorld make_one_region_nav(const GameState& gs) {
    NavWorld nv{};
    nv.mapW = kMap;
    nv.mapH = kMap;
    nv.bakedSeed = gs.worldSeed;
    nv.bakedNavEpoch = gs.navEpoch;
    const std::size_t cells = std::size_t(kMap) * std::size_t(kMap);
    nv.regionOf.assign(cells, 0);
    nv.distHome.assign(cells, 16);
    nv.stepHome.assign(cells, 0);
    nv.waterRegionOf.assign(cells, kNavNoRegion);
    nv.regionLandmarkId = {3};
    nv.regionCell = {10 * kMap + 10};
    nv.portals.clear();
    nv.portalBegin = {0};
    nv.portalCount = {0};
    nv.routeDist = {0u};
    nv.routeNext = {0};
    return nv;
}

struct Crew {
    std::uint8_t  verb;
    std::uint32_t object;
};

// Артели ОДНОГО дома: с 2026-09-19 крестьянскую строку носит и ГОРОД (артель
// горожан за покупками — рейс к рынку тем же ИИ, ребро вниз), поэтому счёт
// «чьи это артели» обязан спрашивать дом, иначе инварианты деревни судят
// чужие крю.
std::vector<Crew> live_crews_of(ecs::World& w, int homeId) {
    std::vector<Crew> out;
    for (auto [e, kind, rt]
         : w.reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>().each()) {
        (void)e;
        if (kind.type != std::uint16_t(NPCType::Peasant)) continue;
        if (rt.homeSettlementId != homeId) continue;
        out.push_back(Crew{rt.squadType, rt.errandObject});
    }
    return out;
}

std::vector<Crew> live_crews(ecs::World& w) { return live_crews_of(w, 3); }

void test_auction_raises_errand_bearing_peasants() {
    GameState gs = make_world(/*pop*/100);
    // УСЛОВИЕ СОЗДАНИЯ (S19.2): при подъёме списывается СЕЗОН содержания —
    // склад обязан держать хлеб на 32 дня каждого рта, иначе артель не
    // поднимается. Сезонный амбар, не «провиант на рейс».
    // Зернового ГЛУТА в фикстуре нет намеренно: при неттинге производного
    // спроса (полный амбар ⇒ зерно не нужно) любой запас зерна — излишек,
    // и его стоимость глушила бы рулетку; рейс сбыта здесь живёт данью —
    // его скор сопоставим с жилой и лесом, и диверсификация ВИДНА.
    gs.landmarks[0].inventory.add("food", 3200);
    stock_comforts(gs.landmarks[0]);
    gs.landmarks[0].titheOwedValue = 200;            // долг дани — цель сбыта
    // ГОРОДУ ЕСТЬ С ЧЕМ ЕХАТЬ: излишек своего ремесла (город ткёт) — это и
    // товар на продажу, и покупательная способность рейса. Пустому городу
    // аукцион честно откажет: менять нечего, и это правильный отказ.
    gs.landmarks[1].inventory.add(
        "cloth", (500 / 32) * kDaysPerSeason * 2);

    DepositLayer dep{};
    allocate_deposit_fields(dep, kMap, kMap);
    dep.grid(DepositKind::Iron).write(14, 10, 64);  // жила в радиусе рук
    std::vector<TreePoint> trees{{12, 12}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap, 32);

    ecs::World w;
    TerrainData absent{};
    NavWorld nav = make_one_region_nav(gs);
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent,
                  .deposits = &dep, .treeGrid = &grid, .nav = &nav};

    // ОПИСЬ ОКРУГИ — ТЕМ ЖЕ ТАКТОМ, ЧТО В МИРЕ (world_tick: опись и
    // ротация стоят на одной границе сезона). Без неё цель-жила не
    // рождается вовсе — аукциону нечего предъявить, кроме леса, — и
    // «рулетка не диверсифицирует» читалось бы как дефект закона.
    survey_landmark_regions(mw, /*day*/1);
    const int raised = rotate_worker_squads(mw, /*day*/1);
    const std::vector<Crew> crews = live_crews(w);

    const std::vector<Crew> townsfolk = live_crews_of(w, 9);
    CHECK(raised > 0, "мир с целями поднимает артели");
    CHECK(int(crews.size()) + int(townsfolk.size()) == raised,
          "каждый подъём — крестьянская артель (профессии не поднимаются)");
    CHECK(int(crews.size()) <= 4,
          "подъём деревни ограничен строками её ростера (N×Peasant = 4)");
    // КРЮ ГОРОДА ИДЁТ ВНИЗ ПО ФЕОДАЛЬНОМУ РЕБРУ — и с 2026-09-22 это
    // СБОРЩИК: дань перестала ехать попутным грузом чужого рейса, у неё
    // своя заявка и своя машина. Объект поручения — вассал-должник.
    bool townsfolkGoDown = !townsfolk.empty();
    for (const Crew& c : townsfolk) {
        if (c.object != 3u) townsfolkGoDown = false;
        if (c.verb != std::uint8_t(SquadType::Collector)
            && c.verb != std::uint8_t(SquadType::Caravan)) {
            townsfolkGoDown = false;
        }
    }
    CHECK(townsfolkGoDown,
          "город поднял крю ВНИЗ по феодальному ребру, к своему вассалу");
    bool tithesHaveTheirOwnBid = false;
    for (const Crew& c : townsfolk) {
        if (c.verb == std::uint8_t(SquadType::Collector)) {
            tithesHaveTheirOwnBid = true;
        }
    }
    CHECK(tithesHaveTheirOwnBid,
          "долг вассала поднимает СБОРЩИКА у сюзерена — своей заявкой");

    const int ironRow = gather_goal_row(ResourceFieldId::Iron);
    const int treeRow = gather_goal_row(ResourceFieldId::Trees);
    CHECK(ironRow >= 0 && treeRow >= 0,
          "таблица целей знает железо и лес по ресурсу, не по индексу");
    bool everyErrandLegal = true;
    for (const Crew& c : crews) {
        const bool gather =
            c.verb == std::uint8_t(SquadType::Artel)
            && (int(c.object) == ironRow || int(c.object) == treeRow);
        const bool sell = c.verb == std::uint8_t(SquadType::Caravan)
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
        stock_comforts(gsd.landmarks[0]);
        gsd.landmarks[0].titheOwedValue = 200;
        // МИР ПОСЛЕ ГРАНИЦЫ (CANON S10): счёт выставлен и оплачен посевным
        // амбаром — склад держит излишек, не сезонный запас. Былой глут
        // хлеба 3200 при нулевом счёте давил бы рулетку в argmax сбыта:
        // цена глута падает на пол, и сбыт весил бы в сотню раз больше
        // любой жилы — это сломанная под долгом фикстура, не закон.
        econ_debt_boundary(gsd.landmarks[0].inventory,
                           gsd.landmarks[0].needDebt,
                           gsd.landmarks[0].population, nullptr, nullptr);
        ecs::World wd;
        NavWorld navd = make_one_region_nav(gsd);
        MacroWorld mwd{.gs = &gsd, .world = &wd, .terrain = &absent,
                       .deposits = &dep, .treeGrid = &grid, .nav = &navd};
        survey_landmark_regions(mwd, day);
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
    set_suzerain(gs, gs.landmarks[0].id, -1);
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

void test_tithe_raises_the_collector_at_the_suzerain() {
    // ДОЛГ ПОДНИМАЕТ СБОРЩИКА У СЮЗЕРЕНА (CANON S4, 2026-09-22). До этого
    // дня файл утверждал обратное — «один долг дани поднимает рейс сбыта у
    // должника», — и это был закон, который сборщик-идущий-вниз заменил:
    // дань больше не едет попутным грузом чужого рейса.
    GameState gs = make_world(/*pop*/100);
    gs.landmarks[0].titheOwedValue = 300;          // долг лежит на вассале
    // Хлеб обоим: условие создания крю — сезон содержания на складе ДОМА.
    gs.landmarks[0].inventory.add("food", 3200);
    gs.landmarks[1].inventory.add("food", 16000);
    ecs::World w;
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    CHECK(raised > 0, "долг дани — цель с положительным скором");
    const std::vector<Crew> atDebtor = live_crews_of(w, 3);
    for (const Crew& c : atDebtor) {
        CHECK(c.verb != std::uint8_t(SquadType::Collector),
              "должник не снаряжает сборщика сам себе");
    }
    const std::vector<Crew> atSuzerain = live_crews_of(w, 9);
    bool collectorGoesDown = !atSuzerain.empty();
    for (const Crew& c : atSuzerain) {
        if (c.verb != std::uint8_t(SquadType::Collector) || c.object != 3u) {
            collectorGoesDown = false;
        }
    }
    CHECK(collectorGoesDown,
          "сюзерен поднял СБОРЩИКА, и объект поручения — вассал-должник");
}

// ── ДВА РЕГУЛЯТОРА суда границы (S19.2, владелец 2026-09-18): строки —
// СКОЛЬКО сквадов, пул — КАКОГО РАЗМЕРА; стоящая артель на границе ДЫШИТ
// составом (добор из населения / ссадка в население), души не рождаются и
// не испаряются — консервация проверяется суммой.
void test_boundary_court_resizes_standing_crews() {
    GameState gs = make_world(/*pop*/100);
    // Город-сюзерен остаётся РЕБРОМ, но без душ: с 2026-09-19 он поднимает
    // свою артель горожан, а этот тест судит ПУЛ ДЕРЕВНИ — чужие крю с
    // другим пулом сделали бы «все составы равны» ложью о двух законах.
    gs.landmarks[1].population = 0;
    gs.landmarks[0].inventory.add("food", 5000);
    gs.landmarks[0].inventory.add("food", 3200 * 4);   // сезоны впрок
    gs.landmarks[0].titheOwedValue = 200;
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
    while (ro.squad.size() > cutTo) {
        SoldierRecord fallen{};
        CHECK(ro.squad.pop_soul_back(fallen), "срез снимает душу с хвоста");
    }
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

// ── СТАНЦИЯ РЕЙСА — РУЛЕТКА ПО ВЕСУ, А НЕ ПЕРВЫЙ СОСЕД (владелец
// 2026-09-20: «просто ходит по весу случайно к соседям»). Два утверждения, и
// второе — про А, не только про Б (§49): (а) выбор РАСХОДИТСЯ по соседям
// (детерминированный ближний умер), (б) ближний ВЕРОЯТНЕЕ дальнего — вес
// есть, а не просто равномерный жребий.
//
// ПОЧЕМУ КАРТА ШИРОКАЯ: вес = 1/(1 + ДНИ пути, темп 96 клеток в день), и на
// карте 64 все соседи лежат в сотых доли дня — закон там почти равномерен ПО
// ПОСТРОЕНИЮ, разглядеть в нём вес невозможно. Свидетель обязан развести
// станции по ДНЯМ, иначе он подтверждает не закон, а свою фикстуру.
void test_station_is_a_weighted_roulette() {
    constexpr int kWide = 2048;          // дни развести нечем на 64 клетках
    const auto make_three_stations = [&](int day) {
        GameState gs{};
        gs.mapW = kWide;
        gs.mapH = kWide;
        gs.worldSeed = 7u;
        // ДОМ РЕЙСА — ГОРОД. Прежде здесь стояла ДЕРЕВНЯ, поднимавшая
        // рейс сбыта своим долгом дани; оба основания умерли 2026-09-22:
        // строка деревни объявляет только артель добычи, а дань уехала в
        // заявку сюзерена. Рейс сбыта носит ГОРОДСКАЯ строка, и свидетель
        // закона о станции обязан ехать на ней — иначе он судит закон по
        // сквадам, которых в мире не рождается.
        Landmark home{};
        home.type = LandmarkType::City;
        home.id = 3;
        home.x = 100;
        home.y = 100;
        home.population = 100;
        // Живая цель одна — СБЫТ ИЗЛИШКА: ни жил, ни леса, ни вассалов,
        // поэтому объект всякого поручения есть станция.
        home.inventory.add("food", 8000);           // сезон содержания крю
        home.inventory.add("cloth", 4000);          // излишек на вывоз
        home.inventory.add("tools", 4000);
        gs.landmarks.push_back(home);
        // ТРИ СТАНЦИИ, РАЗВЕДЁННЫЕ ПО ДНЯМ ПУТИ: 32 / 600 / 960 клеток.
        const int xs[3] = {132, 700, 1060};
        for (int k = 0; k < 3; ++k) {
            Landmark st{};
            st.type = LandmarkType::City;
            st.id = 9 + k;
            st.x = xs[k];
            st.y = 100;
            // Души станции нужны: гейт кандидата смотрит паству. Своих крю
            // станция не поднимет — ей нечего вывозить, и это честный
            // отказ аукциона, а не немота фикстуры.
            st.population = 50;
            gs.landmarks.push_back(st);
        }
        // Вассалов у дома нет намеренно: заявка сборщика увела бы крю с
        // рейса, и рулетка станции судилась бы по чужому поручению.
        //
        // СТАНЦИИ ПРОШЛИ ГРАНИЦУ СЕЗОНА И ОСТАЛИСЬ ДОЛЖНЫ. Без непокрытой
        // нужды полка станции стоит на ПОЛУ цены, мировое среднее равно
        // единице — и спред рейса равен нулю по построению. Рынок,
        // которому ничего не надо, не рынок.
        for (int k = 0; k < 3; ++k) {
            Landmark& st = gs.landmarks[std::size_t(1 + k)];
            econ_debt_boundary(st.inventory, st.needDebt, st.population,
                               nullptr, nullptr);
        }
        // ВЕДОМОСТИ ПУБЛИКУЮТСЯ, КАК В МИРЕ (world_tick: публикация и
        // ротация стоят на одной границе сезона). Без них яруса 2 знания
        // нет вовсе и цена ТАМ равна нулю — рейс сбыта не рождается ни
        // один. Прежняя редакция фикстуры этого не знала, потому что её
        // рейс держала ДАНЬ полной стоимостью, а дань уехала в заявку
        // сюзерена 2026-09-22.
        publish_landmark_ledgers(gs, day);
        return gs;
    };
    int hits[3] = {0, 0, 0};
    // Подъём случается на границе сезона, поэтому броски — ГРАНИЦЫ.
    constexpr int kDraws = 24;
    for (int k = 0; k < kDraws; ++k) {
        const int day = 1 + k * kDaysPerSeason;
        GameState gs = make_three_stations(day);
        ecs::World w;
        TerrainData absent{};
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};
        rotate_worker_squads(mw, day);
        for (const Crew& c : live_crews_of(w, 3)) {
            if (c.verb != std::uint8_t(SquadType::Caravan)) continue;
            const int idx = int(c.object) - 9;
            if (idx >= 0 && idx < 3) ++hits[idx];
        }
    }
    const int total = hits[0] + hits[1] + hits[2];
    CHECK(total > 0, "рейс сбыта поднят: станция выбрана (фикстура жива)");
    int visited = 0;
    for (const int h : hits) visited += h > 0 ? 1 : 0;
    CHECK(visited >= 2,
          "станция РАСХОДИТСЯ по соседям: детерминированный ближний умер");
    // ПОРОГ ВЫВЕДЕН ИЗ ЗАКОНА, А НЕ ИЗ НАБЛЮДЕНИЯ: 32 клетки = 0.33 дня
    // (вес 0.75), 960 клеток = 10 дней (вес 0.091) — закон предсказывает
    // восемь к одному. Равновероятный жребий предсказывает один к одному.
    // Порог 3:1 отделяет одно от другого с запасом в обе стороны; негативный
    // контроль (вес заменён единицей) даёт 36/28/32 и валит именно его,
    // тогда как «больше» прошло бы случайно.
    CHECK(hits[0] > 3 * hits[2],
          "ближний вероятнее дальнего В РАЗЫ: вес 1/(1+дни), не равный жребий");
}

} // namespace

int main() {
    test_auction_raises_errand_bearing_peasants();
    test_station_is_a_weighted_roulette();
    test_refusal_is_the_auctions_verdict();
    test_tithe_raises_the_collector_at_the_suzerain();
    test_boundary_court_resizes_standing_crews();
    return sm::test::report("goal_auction_test");
}

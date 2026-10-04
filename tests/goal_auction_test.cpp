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
#include "macro/labour.h"   // settle_souls / souls_home — двери душ

#include "ecs/components.h"
#include "macro/deposit_layer.h"
#include "macro/world_row.h"
#include "macro/econ_day.h"
#include "tables/npc.h"
#include "macro/nav_field.h"
#include "macro/npc_ai.h"
#include "macro/place_birth.h"   // birth_landmark — место рождается с ТЕЛОМ
#include "macro/place_body.h"    // place_store / place_slot — плечо места
#include "macro/resource_field.h"
#include "macro/squad.h"         // set_suzerain / tithe_edge_of — роль местом
#include "macro/tree_layer.h"
#include "macro/store.h"

#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace {

using namespace sm;

constexpr int kMap = 64;

// Мир фикстуры — СТРОКИ ПЛЮС ТЕЛА (M-90 шаг 5): плечо места (склад, счёт
// нужд, интересы с феодальным ребром) живёт колонками MacroStore, и ТОТ ЖЕ
// store носит поднятые аукционом артели — он один на макромир.
struct World {
    std::unique_ptr<MacroStore> store;
    GameState gs;
};

// Деревня с рынком: дом аукциона всех проверок ниже.
World make_world(int villagePop) {
    World wld{make_macro_store(), GameState{}};
    GameState& gs = wld.gs;
    MacroStore& st = *wld.store;
    gs.mapW = kMap;
    gs.mapH = kMap;
    gs.worldSeed = 7u;
    {
        Landmark vil{};
        vil.type = LandmarkType::Village;
        vil.id = 3;
        vil.x = 10;
        vil.y = 10;

        // v121: феодальное ребро живёт в строке ФРАКЦИИ сюзерена — безфракцион-
        // ный феод рёбер не ведёт, поэтому фикстура рождает своё предусловие
        // (§8 п.11): оба места несут реестровую фракцию, как всякое место мира.
        vil.factionIdx = std::int16_t(faction_index("timaert"));
        Landmark& row = birth_landmark(gs, st, std::move(vil));
        // Души — ДВЕРЬЮ МИРА (v122): паства в worked-число фичи, головы в
        // инвентарь. Гейт подъёма артелей спрашивает ИМЕННО паству.
        settle_souls(gs, st, row, villagePop);
    }
    {
        Landmark city{};
        city.type = LandmarkType::City;
        city.id = 9;
        city.x = 20;
        city.y = 10;
        city.factionIdx = std::int16_t(faction_index("timaert"));
        Landmark& row = birth_landmark(gs, st, std::move(city));
        settle_souls(gs, st, row, 500);
    }
    // Феод ставится ОДНОЙ дверью и только когда оба места в ростере: она
    // пишет ОБА конца (S24), и полуребра в мире не бывает.
    set_suzerain(gs, st, 3, 9);
    return wld;
}

// Полки комфорта закрыты на сезон вперёд (pop 100): рейс сбыта-закупки
// теперь ценит ОБА конца (вердикт 2026-09-18 «голодный дом едет ПОКУПАТЬ»),
// и голая полка cloth/tools задрала бы его скор на порядки — рулетку было
// бы не разглядеть. Закрытая полка глушит покупной конец, оставляя целям
// дня сопоставимые скоры — ровно как до вердикта.
void stock_comforts(MacroStore& st, Landmark& lm) {
    // Нужда считается ОДНОЙ дверью мира (M-137: доля бюджета горожанина), а не
    // второй копией её арифметики в фикстуре (§8 п.5).
    for (int c = 0; c < kCommodityCount; ++c) {
        const int seasonNeed = season_comfort_units(souls_home(st, lm), c);
        if (seasonNeed > 0) {
            place_store(st, lm).add_of(commodity_item_index(c), seasonNeed);
        }
    }
}

// ОДНА ОКРУГА НА ВСЮ КАРТУ, хозяин — деревня 3. Нав здесь не декорация:
// цель-ЖИЛА рождается дверью find_home_deposit, а она без запечённой
// навигации не отличает свою землю от чужой. Без нава у деревни живёт
// ровно одна цель добычи (лес), и «рулетка не диверсифицирует» читалось бы
// как дефект закона, тогда как это немота фикстуры. Свежесть объявлена по
// событию (CANON S9), чтобы боевой nav_ensure не перепёк рукоделие.
// (Опись округи стояла здесь тем же тактом и уничтожена 2026-09-30,
// ломтик E шаг 2 — CANON S10 «КАРТА ОКРУГИ».)
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
// МЕСТО ТОЖЕ ТЕЛО (M-90 шаг 5), и его слот стоит в том же store: оно живёт
// нулевой строкой существа (`kind.type = 0`, то есть Peasant) и числит домом
// САМО СЕБЯ, поэтому наивный фильтр «Peasant, чей дом 3» зачислил бы деревню
// в собственные артели. Тела мест называются по ИМЕНИ — хэндлом своей строки,
// а не угадываются по колонкам.
bool slot_is_place(const World& wld, std::uint16_t slot) {
    for (const Landmark& lm : wld.gs.landmarks)
        if (place_slot(*wld.store, lm) == slot) return true;
    return false;
}

std::vector<Crew> live_crews_of(const World& wld, ecs::World& w, int homeId) {
    std::vector<Crew> out;
    const sm::MacroStore& st = sm::store_of(w);
    for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32) {
        const std::uint16_t slot = std::uint16_t(s32);
        if (st.alive[slot] == 0) continue;
        if (slot_is_place(wld, slot)) continue;
        const auto& kind = st.kind[slot];
        const auto& rt = st.runtime[slot];
        if (kind.type != std::uint16_t(NPCType::Peasant)) continue;
        if (rt.homeSettlementId != homeId) continue;
        out.push_back(Crew{rt.squadType, rt.errandObject});
    }
    return out;
}

std::vector<Crew> live_crews(const World& wld, ecs::World& w) {
    return live_crews_of(wld, w, 3);
}

void test_auction_raises_errand_bearing_peasants() {
    World wld = make_world(/*pop*/100);
    GameState& gs = wld.gs;
    MacroStore& st = *wld.store;
    // УСЛОВИЕ СОЗДАНИЯ (S19.2): при подъёме списывается СЕЗОН содержания —
    // склад обязан держать хлеб на 32 дня каждого рта, иначе артель не
    // поднимается. Сезонный амбар, не «провиант на рейс».
    // Зернового ГЛУТА в фикстуре нет намеренно: при неттинге производного
    // спроса (полный амбар ⇒ зерно не нужно) любой запас зерна — излишек,
    // и его стоимость глушила бы рулетку; рейс сбыта здесь живёт данью —
    // его скор сопоставим с жилой и лесом, и диверсификация ВИДНА.
    place_store(st, gs.landmarks[0]).add("food", 3200);
    stock_comforts(st, gs.landmarks[0]);
    tithe_edge_of(gs, st, gs.landmarks[0])->owedValue = 200;   // долг дани — на ребре (v121)
    // ГОРОДУ ЕСТЬ С ЧЕМ ЕХАТЬ: излишек своего ремесла (город ткёт) — это и
    // товар на продажу, и покупательная способность рейса. Пустому городу
    // аукцион честно откажет: менять нечего, и это правильный отказ.
    place_store(st, gs.landmarks[1]).add(
        "cloth", (500 / 32) * kDaysPerSeason * 2);

    DepositLayer dep{};
    allocate_deposit_fields(dep, kMap, kMap);
    dep.grid(DepositKind::Iron).write(14, 10, 64);  // жила в радиусе рук
    std::vector<TreePoint> trees{{12, 12}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap, 32);

    ecs::World w;
    // ОДИН store на макромир: тела мест и поднятые артели — его же слоты.
    sm::store_attach(w, wld.store.get());
    TerrainData absent{};
    NavWorld nav = make_one_region_nav(gs);
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent,
                  .deposits = &dep, .treeGrid = &grid, .nav = &nav};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    const std::vector<Crew> crews = live_crews(wld, w);

    const std::vector<Crew> townsfolk = live_crews_of(wld, w, 9);
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
        World wldd = make_world(/*pop*/100);
        GameState& gsd = wldd.gs;
        MacroStore& std_ = *wldd.store;
        stock_comforts(std_, gsd.landmarks[0]);
        tithe_edge_of(gsd, std_, gsd.landmarks[0])->owedValue = 200;
        // МИР ПОСЛЕ ГРАНИЦЫ (CANON S10): счёт выставлен и оплачен посевным
        // амбаром — склад держит излишек, не сезонный запас. Былой глут
        // хлеба 3200 при нулевом счёте давил бы рулетку в argmax сбыта:
        // цена глута падает на пол, и сбыт весил бы в сотню раз больше
        // любой жилы — это сломанная под долгом фикстура, не закон.
        econ_debt_boundary(place_store(std_, gsd.landmarks[0]),
                           std_.roster[place_slot(std_, gsd.landmarks[0])]
                               .needDebt,
                           souls_home(std_, gsd.landmarks[0]), nullptr,
                           nullptr);
        ecs::World wd;
        sm::store_attach(wd, wldd.store.get());
        NavWorld navd = make_one_region_nav(gsd);
        MacroWorld mwd{.gs = &gsd, .world = &wd, .terrain = &absent,
                       .deposits = &dep, .treeGrid = &grid, .nav = &navd};
        rotate_worker_squads(mwd, day);
        for (const Crew& c : live_crews(wldd, wd))
            distinct.insert({int(c.verb), int(c.object)});
    }
    CHECK(distinct.size() >= 2,
          "рулетка диверсифицирует артели без координации (союз 8 границ)");

    // Счёт по типу: артели в поле занимают строки — второй ротации нечего
    // поднимать (Idle дома растворился бы; в пути — держит строку).
    {
        sm::MacroStore& stq = sm::store_of(w);
        for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32)
            if (stq.alive[s32] != 0
                && !slot_is_place(wld, std::uint16_t(s32)))
                stq.runtime[s32].state = std::uint8_t(NPCState::Traveling);
    }
    // Граница сезона (день 33): контроль честен только там, где подъём
    // вообще возможен — вне границы ноль тривиален.
    const int again = rotate_worker_squads(mw, /*day*/33);
    CHECK(again == 0,
          "живые крю типа T занимают строки типа T — двойного подъёма нет");
}

void test_refusal_is_the_auctions_verdict() {
    // Миру нечего предъявить: ни жил, ни леса, ни рынка, пустой склад.
    World wld = make_world(/*pop*/100);
    GameState& gs = wld.gs;
    MacroStore& st = *wld.store;
    set_suzerain(gs, st, gs.landmarks[0].id, -1);
    ecs::World w;
    sm::store_attach(w, wld.store.get());
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    CHECK(raised == 0, "ноль целей с положительным скором = ноль артелей");
    CHECK(live_crews(wld, w).empty(),
          "отказ аукциона не колдует ни одного крестьянина");
    CHECK(souls_flock(gs, st, gs.landmarks[0]) == 100,
          "невзятая работа не трогает души деревни");
}

void test_tithe_raises_the_collector_at_the_suzerain() {
    // ДОЛГ ПОДНИМАЕТ СБОРЩИКА У СЮЗЕРЕНА (CANON S4, 2026-09-22). До этого
    // дня файл утверждал обратное — «один долг дани поднимает рейс сбыта у
    // должника», — и это был закон, который сборщик-идущий-вниз заменил:
    // дань больше не едет попутным грузом чужого рейса.
    World wld = make_world(/*pop*/100);
    GameState& gs = wld.gs;
    MacroStore& st = *wld.store;
    tithe_edge_of(gs, st, gs.landmarks[0])->owedValue = 300;   // долг на ребре вассала
    // Хлеб обоим: условие создания крю — сезон содержания на складе ДОМА.
    place_store(st, gs.landmarks[0]).add("food", 3200);
    place_store(st, gs.landmarks[1]).add("food", 16000);
    ecs::World w;
    sm::store_attach(w, wld.store.get());
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};

    const int raised = rotate_worker_squads(mw, /*day*/1);
    CHECK(raised > 0, "долг дани — цель с положительным скором");
    const std::vector<Crew> atDebtor = live_crews_of(wld, w, 3);
    for (const Crew& c : atDebtor) {
        CHECK(c.verb != std::uint8_t(SquadType::Collector),
              "должник не снаряжает сборщика сам себе");
    }
    const std::vector<Crew> atSuzerain = live_crews_of(wld, w, 9);
    // УТВЕРЖДАЕТСЯ НАЛИЧИЕ СБОРЩИКА, А НЕ ОТСУТСТВИЕ СОСЕДЕЙ ПО РУЛЕТКЕ.
    // До 2026-09-30 здесь стояло «ВСЕ крю сюзерена — сборщики», и это было
    // верно лишь потому, что фикстура была НЕМА: без опубликованной
    // ведомости цена чужого рынка равнялась нулю, и рейс сбыта не набирал
    // положительного скора ни разу. Со сносом яруса 2 (ломтик E шаг 2)
    // цена «там» есть абсолютная стоимость строки и известна ВСЕГДА —
    // сюзерен с 16 000 хлеба честно поднимает рядом и корованов (замер:
    // 1 сборщик + 20 корованов). Закон, который этот свидетель охраняет,
    // — «долг дани поднимает СБОРЩИКА У СЮЗЕРЕНА, и объект поручения —
    // вассал-должник», — к числу соседей по рулетке не относится.
    int collectorsDown = 0;
    for (const Crew& c : atSuzerain) {
        if (c.verb != std::uint8_t(SquadType::Collector)) continue;
        CHECK(c.object == 3u,
              "объект поручения сборщика — вассал-должник, и только он");
        ++collectorsDown;
    }
    CHECK(collectorsDown > 0, "долг дани поднял СБОРЩИКА у сюзерена");
}

// ── ДВА РЕГУЛЯТОРА суда границы (S19.2, владелец 2026-09-18): строки —
// СКОЛЬКО сквадов, пул — КАКОГО РАЗМЕРА; стоящая артель на границе ДЫШИТ
// составом (добор из населения / ссадка в население), души не рождаются и
// не испаряются — консервация проверяется суммой.
void test_boundary_court_resizes_standing_crews() {
    World wld = make_world(/*pop*/100);
    GameState& gs = wld.gs;
    MacroStore& st = *wld.store;
    // Город-сюзерен остаётся РЕБРОМ, но без душ: с 2026-09-19 он поднимает
    // свою артель горожан, а этот тест судит ПУЛ ДЕРЕВНИ — чужие крю с
    // другим пулом сделали бы «все составы равны» ложью о двух законах.
    {
        // Город-сюзерен обезлюжен ЦЕЛИКОМ — оба носителя: паства (worked) и
        // головы. Обнулить один значило бы оставить место, которое по
        // одной двери живо, а по другой мертво.
        Landmark& suz = gs.landmarks[1];
        Inventory& suzStore = place_store(st, suz);
        bleed_heads(suzStore, creature_heads(suzStore));
        worked_write(gs, suz.x, suz.y, 0);
    }
    place_store(st, gs.landmarks[0]).add("food", 5000);
    place_store(st, gs.landmarks[0]).add("food", 3200 * 4);   // сезоны впрок
    tithe_edge_of(gs, st, gs.landmarks[0])->owedValue = 200;
    DepositLayer dep{};
    allocate_deposit_fields(dep, kMap, kMap);
    dep.grid(DepositKind::Iron).write(14, 10, 64);
    std::vector<TreePoint> trees{{12, 12}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap, 32);
    ecs::World w;
    sm::store_attach(w, wld.store.get());
    TerrainData absent{};
    MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent,
                  .deposits = &dep, .treeGrid = &grid};
    CHECK(rotate_worker_squads(mw, /*day*/1) > 0, "граница поднимает артели");

    // СЧЁТ ДУШ: дома у места (его склад) плюс каждая АРТЕЛЬ — лидер и
    // ростер. Тела мест из обхода исключены по имени: их склад и ЕСТЬ
    // «дома», и сложить его дважды значило бы объявить перенос души в
    // ростер её исчезновением.
    const auto souls_total = [&] {
        int total = souls_home(st, gs.landmarks[0]);
        const sm::MacroStore& stq = sm::store_of(w);
        for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32) {
            if (stq.alive[s32] == 0) continue;
            if (slot_is_place(wld, std::uint16_t(s32))) continue;
            total += 1 + creature_heads(stq.inventory[s32].inv);
        }
        return total;
    };
    const auto crew_sizes = [&] {
        std::vector<int> sizes;
        const sm::MacroStore& stq = sm::store_of(w);
        for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32) {
            if (stq.alive[s32] == 0) continue;
            if (slot_is_place(wld, std::uint16_t(s32))) continue;
            sizes.push_back(creature_heads(stq.inventory[s32].inv));
        }
        return sizes;
    };

    // ПОТЕРЯ В ПОЛЕ: у первой артели гибнут трое (души честно исчезают из
    // мира — сумма падает ровно на троих).
    sm::MacroHandle first{};
    {
        const sm::MacroStore& stq = sm::store_of(w);
        for (std::size_t s32 = 0; s32 < sm::kMacroEntityCap; ++s32)
            if (stq.alive[s32] != 0
                && !slot_is_place(wld, std::uint16_t(s32))) {
                first = sm::handle_at(stq, std::uint16_t(s32));
                break;
            }
    }
    CHECK(sm::store_of(w).valid(first), "есть артель для среза (фикстура)");
    auto& fbag = sm::store_of(w).inventory[first.slot];
    CHECK(creature_heads(fbag.inv) > 3, "ростер больше среза (фикстура)");
    const int cutTo = creature_heads(fbag.inv) - 3;
    while (creature_heads(fbag.inv) > cutTo) {
        SoldierRecord fallen{};
        CHECK(creatures_pop_back(fbag.inv, fallen),
              "срез снимает душу с хвоста");
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
    const int popBeforeShed = souls_home(st, gs.landmarks[0]);
    const int sizeBeforeShed =
        creature_heads(sm::store_of(w).inventory[first.slot].inv);
    for (int k = 0; k < 7; ++k) {
        SoldierRecord rec{};
        rec.entityId = 900000u + std::uint32_t(k);
        rec.kind = std::uint16_t(NPCType::Peasant);
        rec.level = 1;
        creatures_push(sm::store_of(w).inventory[first.slot].inv, rec);
    }
    const int soulsInflated = souls_total();
    rotate_worker_squads(mw, /*day*/65);
    CHECK(souls_total() == soulsInflated,
          "ссадка — перенос ростера в население: души не испаряются");
    // Want дня 65 пересчитан от базы, потолстевшей на семь влитых душ, так
    // что он может встать на голову-другую выше прежнего — пин не «равно
    // старому», а «перебор срезан к пулу».
    CHECK(creature_heads(sm::store_of(w).inventory[first.slot].inv)
              < sizeBeforeShed + 7,
          "перебор ссажен: артель не жиреет мимо пула");
    CHECK(souls_home(st, gs.landmarks[0]) > popBeforeShed,
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
    const auto make_three_stations = [&]() {
        World wld{make_macro_store(), GameState{}};
        GameState& gs = wld.gs;
        MacroStore& st = *wld.store;
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
        Landmark& homeRow = birth_landmark(gs, st, std::move(home));

        // Живая цель одна — СБЫТ ИЗЛИШКА: ни жил, ни леса, ни вассалов,
        // поэтому объект всякого поручения есть станция.
        Inventory& homeStore = place_store(st, homeRow);
        homeStore.add("food", 8000);                // сезон содержания крю
        homeStore.add("cloth", 4000);               // излишек на вывоз
        homeStore.add("tools", 4000);
        settle_souls(gs, st, homeRow, 100);
        // ТРИ СТАНЦИИ, РАЗВЕДЁННЫЕ ПО ДНЯМ ПУТИ: 32 / 600 / 960 клеток.
        const int xs[3] = {132, 700, 1060};
        for (int k = 0; k < 3; ++k) {
            Landmark station{};
            station.type = LandmarkType::City;
            station.id = 9 + k;
            station.x = xs[k];
            station.y = 100;
            // Души станции нужны: гейт кандидата смотрит паству. Своих крю
            // станция не поднимет — ей нечего вывозить, и это честный
            // отказ аукциона, а не немота фикстуры.
            Landmark& row = birth_landmark(gs, st, std::move(station));
            settle_souls(gs, st, row, 50);
        }
        // Вассалов у дома нет намеренно: заявка сборщика увела бы крю с
        // рейса, и рулетка станции судилась бы по чужому поручению.
        //
        // СТАНЦИИ ПРОШЛИ ГРАНИЦУ СЕЗОНА И ОСТАЛИСЬ ДОЛЖНЫ. Без непокрытой
        // нужды полка станции стоит на ПОЛУ цены, мировое среднее равно
        // единице — и спред рейса равен нулю по построению. Рынок,
        // которому ничего не надо, не рынок.
        for (int k = 0; k < 3; ++k) {
            Landmark& station = gs.landmarks[std::size_t(1 + k)];
            econ_debt_boundary(place_store(st, station),
                               st.roster[place_slot(st, station)].needDebt,
                               souls_home(st, station), nullptr, nullptr);
        }
        // (ЗДЕСЬ ПУБЛИКОВАЛИСЬ ВЕДОМОСТИ — уничтожены 2026-09-30, ломтик
        // E шаг 2. Публиковать больше нечего: цена ТАМ есть абсолютная
        // стоимость строки и известна всегда, поэтому рейс сбыта больше не
        // зависит от того, успел ли мир дожить до границы сезона.)
        return wld;
    };
    int hits[3] = {0, 0, 0};
    // Подъём случается на границе сезона, поэтому броски — ГРАНИЦЫ.
    constexpr int kDraws = 24;
    for (int k = 0; k < kDraws; ++k) {
        const int day = 1 + k * kDaysPerSeason;
        World wld = make_three_stations();
        GameState& gs = wld.gs;
        ecs::World w;
        sm::store_attach(w, wld.store.get());
        TerrainData absent{};
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &absent};
        rotate_worker_squads(mw, day);
        for (const Crew& c : live_crews_of(wld, w, 3)) {
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

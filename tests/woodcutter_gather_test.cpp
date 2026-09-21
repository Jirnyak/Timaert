// The first honest work-loop (W2b): a village woodcutter CHOPS the world and
// HAULS it home. Pinned:
//   · the chop leaves through the registry's Trees carrier row — the layer
//     count really falls and the revision moves (the grid rides the save
//     whole, v36, so a save remembers the stump);
//   · the haul rides in the woodcutter's OWN bag and lands in his HOME
//     village's universal inventory (the home-link fix: a village man works
//     for the village, not for the nearest city);
//   · CONSERVATION — wood gained by the store == wood lost by the layer,
//     and the bag is empty after delivery: nothing minted, nothing dropped.
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/agent_memory.h"
#include "macro/chronicle.h"
#include "macro/econ_day.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/deposit_layer.h"
#include "macro/tree_layer.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 32;

// Count the chronicle's facts of one kind around a cell — the witcher's own
// question, asked by the tests that pin what the work-loops write (and, by
// the negative controls, what they must NOT write).
struct FactTally {
    int n = 0;
    WorldFact last{};
};
FactTally tally_facts(const Chronicle& c, FactKind kind, int x, int y) {
    struct Ctx { FactKind kind; FactTally out; } ctx{kind, {}};
    chronicle_near(c, x, y, /*radiusCells*/1, /*sinceDay*/0,
                   [](void* u, const WorldFact& f) {
                       Ctx& t = *static_cast<Ctx*>(u);
                       if (f.kind != std::uint16_t(t.kind)) return;
                       ++t.out.n;
                       if (t.out.n == 1) t.out.last = f;
                   }, &ctx);
    return ctx.out;
}

entt::entity make_woodcutter(ecs::World& w, float x, float y,
                             int homeVillageId) {
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(int(x), int(y), kMap));
    reg.emplace<ecs::MacroVisual>(e, x, y, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = homeVillageId;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    ecs::Pools pools{};
    const CharacterSheet sheet = make_character_sheet(
        NPCType::Peasant, 3, leader_sheet_seed(11u));
    refresh_body_from_sheet(pools, &rt, sheet, NPCType::Peasant);
    pools.sp = pools.maxSp;
    // Работа именуется ПОРУЧЕНИЕМ, не типом (аукцион, CANON S10): рубка =
    // Gather над строкой целей Trees — то, что рулетка ротации выдала бы.
    rt.errandVerb = std::uint8_t(ErrandVerb::Gather);
    rt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Trees));
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 11u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    pools.hp = pools.maxHp = 30;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);
    return e;
}

void test_the_chop_is_real_and_the_haul_comes_home() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    vil.population = 40;
    gs.landmarks.push_back(vil);

    // A little forest cell four cells east of the village — small enough to
    // be felled to BARE within the run, so the chronicle negative control
    // below is a real condition and not a vacuous one.
    TreeLayer layer;
    layer.width = kMap;
    layer.height = kMap;
    layer.data.assign(std::size_t(kMap) * kMap, 0);
    layer.data[10 * kMap + 14] = 16;
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);

    ecs::World w;
    const entt::entity wc = make_woodcutter(w, 10.0f, 10.0f, vil.id);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);

    // Live a while: idle -> travel -> WORK (the chop) -> return (the haul).
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .trees = &layer, .world = &w,
                      .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle=*/true);
    }

    const int layerLost = 16 - int(layer.at(14, 10));
    const int storeGained = gs.landmarks[0].inventory.count("wood");
    const int inBag =
        w.reg.get<ecs::NpcInventory>(wc).inv.count("wood");

    CHECK(layerLost > 0, "the chop really fell trees in the layer");
    CHECK(storeGained > 0, "the haul reached the village store");
    CHECK(layerLost == storeGained + inBag,
          "CONSERVATION: layer loss == store gain + what still rides the bag");
    CHECK(layer.revision > 0,
          "the chop moved the grid revision - the map sprite and the save "
          "(which carries the grid whole) both see the stump");
    CHECK(gs.landmarks[0].inventory.count("wood") > 0
              && gs.landmarks.size() == 1,
          "the village man hauls for the VILLAGE (no city even exists here)");

    // NEGATIVE CONTROL for the vein writer: the forest cell was felled to
    // bare ground, and the chronicle stays SILENT — the forest regrows by its
    // own law (resource_fields_daily_growth), so an emptied cell is weather,
    // not the irreversible loss FactKind::Drained records.
    CHECK(layer.at(14, 10) == 0, "the control condition fired: bare cell");
    CHECK(tally_facts(gs.chronicle, FactKind::Drained, 14, 10).n == 0,
          "a felled forest writes NO Drained fact - a forest is not a vein");
}

void test_the_farmer_works_the_field() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.landmarks.push_back(vil);

    // A field two cells east — where stamp_field_features would put one.
    FeatureLayer features;
    features.resize(kMap, kMap);
    features.set(12, 10, FT_Field);

    // The terrain master: fertile land everywhere. Without it the reap is
    // fail-closed (Field Inc F4), so the honest test wires it.
    TerrainData terrain;
    terrain.width = kMap;
    terrain.height = kMap;
    terrain.rgba.assign(std::size_t(kMap) * kMap * 4u, 128);
    for (std::size_t i = 1; i < terrain.rgba.size(); i += 4) {
        terrain.rgba[i] = 160;   // G = fertility
    }

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(10, 10, kMap));
    reg.emplace<ecs::MacroVisual>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = vil.id;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.errandVerb = std::uint8_t(ErrandVerb::Gather);
    prt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Wheat));
    reg.emplace<ecs::MacroNpcRuntime>(e, prt);
    reg.emplace<ecs::MacroSpawnId>(e, 12u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    pools.hp = pools.maxHp = 20;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &terrain,
                      .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle=*/true);
    }
    const int grain = gs.landmarks[0].inventory.count("food");
    const int inBag = w.reg.get<ecs::NpcInventory>(e).inv.count("food");
    CHECK(grain > 0, "the farmer's grain reached the village store");
    // THE BATCH LAW THIS USED TO PIN IS GONE (owner, 2026-09-16). It read
    // `grain % kGatherPerCycle == 0` — "the haul arrives in whole cycle
    // yields" — and it was true only while a take was a declared batch of
    // eight. A take is now ONE object per hand, at exactly the price the
    // player pays for one, and the TRIP emerges from the backs and the bar
    // instead of from a constant. Nothing is pinned in its place because
    // nothing quantised survives: the honest statement left is conservation,
    // below, and it is the one that was load-bearing all along.
    //
    // CONSERVATION (Field Inc F4): every grain that reached anybody left the
    // world — the field's scar is exactly as deep as store PLUS bag. The bag
    // half is new and it is not bookkeeping: the farmer can now be caught
    // mid-trip with grain on his back, where the old single-take cycle always
    // ended at the door.
    const sm::ResourceGrid& wheatScars =
        gs.resourceScarCells[std::size_t(sm::ResourceFieldId::Wheat)];
    CHECK(wheatScars.at(12, 10) == grain + inBag,
          "grain gained by store AND bag == stands the field lost");
    CHECK(wheatScars.liveCells == 1,
          "the farmer scars only the field he works");
}

void test_farmer_without_terrain_conjures_nothing() {
    // The fail-closed half of the same law (mirrors no-layer-no-chop): an
    // unwired terrain means no ledger to settle against, so NOTHING is
    // gathered — grain from thin air died with Field Inc F4.
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.landmarks.push_back(vil);
    FeatureLayer features;
    features.resize(kMap, kMap);
    features.set(12, 10, FT_Field);

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(10, 10, kMap));
    reg.emplace<ecs::MacroVisual>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = vil.id;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.errandVerb = std::uint8_t(ErrandVerb::Gather);
    prt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Wheat));
    reg.emplace<ecs::MacroNpcRuntime>(e, prt);
    reg.emplace<ecs::MacroSpawnId>(e, 12u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    pools.hp = pools.maxHp = 20;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle=*/true);
    }
    CHECK(gs.landmarks[0].inventory.count("food") == 0,
          "no terrain wired: nothing to reap against, nothing conjured");
    CHECK(gs.resourceScarCells[std::size_t(sm::ResourceFieldId::Wheat)].liveCells == 0,
          "no terrain wired: no scar appears either");
}

void test_no_layer_no_chop() {
    // Without a live tree layer the behaviour must not invent wood — the
    // old walk-to-forest-and-back pantomime, unchanged.
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.landmarks.push_back(vil);
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);
    ecs::World w;
    make_woodcutter(w, 10.0f, 10.0f, vil.id);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(gs.landmarks[0].inventory.count("wood") == 0,
          "no layer => no honest wood, and none minted from nothing");
}

} // namespace

// THE SAME MINE, THROUGH THE OTHER DRIVER. There are two macro-AI drivers and
// the game picks between them by WHERE THE PLAYER IS: the budgeted one runs
// while he is in a subworld (main.cpp), the full one while he is on the map.
// The budgeted one was assembling its own copy of the tick context and had
// silently lost one pointer of the seventeen — the deposit layer — so every
// miner, quarryman and clay-digger in the world stopped digging the moment the
// player walked through a door, and resumed when he came out. The world must
// live the same with him and without him (CANON.md S2), so the two drivers
// have to agree about the same mine.
void test_the_mine_runs_while_the_player_is_away() {
    using namespace sm;
    constexpr int kMap = 32;
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.landmarks.push_back(vil);

    DepositLayer deposits;
    allocate_deposit_fields(deposits, kMap, kMap);
    const std::uint32_t veinIdx = 10u * std::uint32_t(kMap) + 14u;
    deposits.grid(DepositKind::Iron).write(14, 10, 20);

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(10, 10, kMap));
    reg.emplace<ecs::MacroVisual>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = vil.id;
    rt.targetSettlementId = -1;
    rt.targetX = 10.0f;
    rt.targetY = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt, make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    rt.errandVerb = std::uint8_t(ErrandVerb::Gather);
    rt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Iron));
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 13u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    pools.hp = pools.maxHp = 30;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .deposits = &deposits};
        tick_macro_npc_ai_budgeted(mw, art, kAiTicks,
                                   /*max_npc_ticks=*/64,
                                   /*allowAutoBattle=*/true);
    }

    // Annihilation (v55): a vein worked all the way out within the run has
    // LEFT the map — absent reads as 0, exactly what "worked" means here.
    const int veinLeft =
        int(deposits.grid(DepositKind::Iron).at_index(veinIdx));
    CHECK(20 - veinLeft > 0,
          "the mine is worked while the player is underground, exactly as it "
          "is worked while he is on the map");
}

void test_agent_memory_is_bounded_and_current() {
    AgentMemory m{};
    MemoryEntry e{};
    e.kind = std::uint8_t(AgentMemoryKind::MarketSnapshot);
    e.subject = 5;
    e.day = 10;
    e.payload[0] = 0x21;
    remember(m, e);
    CHECK(m.count == 1 && recall(m, AgentMemoryKind::MarketSnapshot, 5),
          "a memory can be recalled by (kind, subject)");
    e.day = 20;
    e.payload[0] = 0x33;
    remember(m, e);
    CHECK(m.count == 1
              && recall(m, AgentMemoryKind::MarketSnapshot, 5)->day == 20,
          "the same (kind, subject) OVERWRITES - one current belief");
    for (int i = 0; i < kAgentMemorySlots + 3; ++i) {
        MemoryEntry x{};
        x.kind = std::uint8_t(AgentMemoryKind::MarketSnapshot);
        x.subject = std::uint16_t(100 + i);
        x.day = std::uint32_t(30 + i);
        remember(m, x);
    }
    CHECK(int(m.count) == kAgentMemorySlots,
          "a bounded head never grows past its slots");
    CHECK(recall(m, AgentMemoryKind::MarketSnapshot, 5) == nullptr,
          "past the cap the OLDEST memory is forgotten");

    Inventory store;
    store.add("food", 2000);   // plenty
    store.add("wood", 100);     // stocked
    store.add("iron", 10);      // scarce
    const MemoryEntry snap = pack_market_snapshot(store, 7, 40);
    CHECK(market_stock_class(snap, commodity_index("food")) == 3
              && market_stock_class(snap, commodity_index("wood")) == 2
              && market_stock_class(snap, commodity_index("iron")) == 1
              && market_stock_class(snap, commodity_index("clay")) == 0,
          "the snapshot packs stock classes per commodity");
}

void test_the_vendor_sells_at_the_nearest_city() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    Landmark city{};
    city.type = LandmarkType::City;
    city.id = 1;   // landmark ids are ordinals from 1 (v54): 0 = "no place"
    city.x = 10;
    city.y = 10;
    city.population = 100;
    city.inventory.add("food", 2000);   // plenty: the export
    // The deal PAYS now (owner 2026-08-30): a coinless fixture is the
    // deadlock the payment law exists to refuse. The purse covers the
    // grain lot at the SEASONAL famine price (corridor died 2026-09-18) —
    // a thin purse would pay the vendor in its own bread by value density,
    // and the return leg would waddle home under a tonne of payment.
    city.inventory.add("coin_timaert_copper", 40000);
    gs.landmarks.push_back(city);      // grain: NONE — the import
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 16;
    vil.y = 10;
    vil.suzerainLandmarkId = 1;
    vil.population = 50;
    // A GENUINE surplus: the loading law keeps the seasonal larder home
    // (S19.2 + verdict 2026-09-18 «дома дешевле базы» decides the load),
    // and 50 souls bake through 50 grain a day — 1600 a season. Only what
    // stands ABOVE that rides to market.
    vil.inventory.add("food", 4000);
    vil.inventory.add("coin_timaert_copper", 50 * 2);
    // ДОМ ГОЛОДЕН СЧЁТОМ (CANON S10): «дома нет хлеба» = непогашенный
    // сезонный счёт — из него и читается нужда, которую вендор едет
    // закрывать покупкой.
    vil.needDebt[commodity_index("food")] =
        vil.population * kDaysPerSeason;
    gs.landmarks.push_back(vil);
    // МИР ПУБЛИКУЕТ ВЕДОМОСТЬ (CANON S10, ярус 2), и только потом крю
    // торгует: что везти домой, судит прейскурант дома, а не память крю.
    // В живом мире это делает генезис и каждая граница сезона; фикстура
    // поднимает мир руками — значит и публикует руками.
    CHECK(publish_landmark_ledgers(gs, /*day=*/1) == 2,
          "fixture: both places published their ledgers");

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(16, 10, kMap));
    reg.emplace<ecs::MacroVisual>(e, 16.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime crt{};
    crt.homeSettlementId = 3;   // the VILLAGE: vendors are the village's arm
    // РЫНОК — ИЗ ПОРУЧЕНИЯ (2026-09-19): рейс к рынку читает errandObject,
    // а не феодальное ребро, — потому что тем же рейсом горожане едут
    // закупаться В ДЕРЕВНЮ. Фикстура называет рынок так же, как его назвал
    // бы аукцион.
    crt.errandVerb = std::uint8_t(ErrandVerb::Sell);
    crt.errandObject = 1u;      // the city's ordinal
    crt.targetSettlementId = -1;
    crt.targetX = 10.0f;
    crt.targetY = 10.0f;
    crt.state = std::uint8_t(NPCState::Idle);
    crt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &crt, make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, crt);
    reg.emplace<ecs::MacroSpawnId>(e, 13u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    pools.hp = pools.maxHp = 25;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);
    reg.emplace<AgentMemory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < 600; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const auto& bag = reg.get<ecs::NpcInventory>(e).inv;
    const int cityGrain = gs.landmarks[0].inventory.count("food");
    const int vilBread = gs.landmarks[1].inventory.count("food");
    CHECK(cityGrain > 0,
          "the vendor sold the village surplus at the nearest city");
    // ПОД ДОЛГОМ (CANON S10) «купил домой хлеб» видно СЧЁТОМ: привезённое
    // гасит его в дверях прихода и съедается — полка держит только излишек.
    const int vilBreadDebtPaid = 50 * kDaysPerSeason
        - gs.landmarks[1].needDebt[commodity_index("food")];
    CHECK(vilBreadDebtPaid > 0,
          "the earnings FED the home's lack — the bread bill fell");
    CHECK(recall(reg.get<AgentMemory>(e),
                 AgentMemoryKind::MarketSnapshot, 3) != nullptr,
          "the departure snapshot of the vendor's OWN home lives in memory");
    // КОНСЕРВАЦИЯ ОДНОЙ ПИЩЕЙ (2026-09-20, снос хлеба): до этого дня в мире
    // было ДВЕ съедобные строки — зерно и хлеб, — и сумма считалась по каждой
    // отдельно. Теперь поток один: всё, что не лежит на полках и не едет в
    // спине, ОПЛАТИЛО СЧЁТ и съедено в дверях прихода (CANON S10).
    const int foodOnShelves = gs.landmarks[0].inventory.count("food")
                              + gs.landmarks[1].inventory.count("food")
                              + bag.count("food");
    CHECK(foodOnShelves + vilBreadDebtPaid == 2000 + 4000,
          "CONSERVATION: cargo moves or pays the bill — never dropped");
    (void)vilBread;
    // ...and the deal's other half obeys the same law: coin travels between
    // the three purses (city, village, hold) and is never minted or burned.
    const int coinTotal = gs.landmarks[0].inventory.count("coin_timaert_copper")
                          + gs.landmarks[1].inventory.count("coin_timaert_copper")
                          + bag.count("coin_timaert_copper");
    CHECK(coinTotal == 40000 + 50 * 2,
          "CONSERVATION: coin moves through the deal, never minted");
    CHECK(gs.landmarks[1].inventory.count("coin_timaert_copper") > 0,
          "the village EARNED coin for its raw — the payment is real");

    // The DEAL is a fact of the world (S20.1): filed at the village the
    // moment the exchange happened — a transition by nature, so every visit
    // may file one — naming both parties and what the goods were worth on
    // the ONE price table.
    const FactTally traded = tally_facts(gs.chronicle, FactKind::Traded,
                                         16, 10);
    CHECK(traded.n >= 1, "a completed exchange left a Traded fact");
    CHECK(traded.last.subject == 3u
              && traded.last.subjectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact's subject is the home village whose vendor dealt");
    CHECK(traded.last.object == 1u
              && traded.last.objectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact's object is the city it traded AT");
    CHECK(traded.last.amount > 0,
          "the deal's worth is real: table value of what changed hands");
}

// The miner: the SAME gatherer row-loop as the chop above, pointed at a
// deposit (resources.md — a profession per resource, a row not a branch).
// Pinned: the ore leaves through the Iron carrier row, hauls home into the
// village store, CONSERVES, the drained vein stays a VISIBLE cell at 0 and
// a dry world gives the miner nothing further; no deposit layer wired = no
// ore conjured (the shared fail-closed rule).
void test_the_miner_works_the_vein() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.landmarks.push_back(vil);

    DepositLayer deposits;
    allocate_deposit_fields(deposits, kMap, kMap);
    const std::uint32_t veinIdx = 10u * std::uint32_t(kMap) + 14u;
    deposits.grid(DepositKind::Iron).write(14, 10, 20);

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(10, 10, kMap));
    reg.emplace<ecs::MacroVisual>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = vil.id;
    rt.targetSettlementId = -1;
    rt.targetX = 10.0f;
    rt.targetY = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt, make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    rt.errandVerb = std::uint8_t(ErrandVerb::Gather);
    rt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Iron));
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 13u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    pools.hp = pools.maxHp = 30;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .deposits = &deposits};
        tick_macro_npc_ai(mw, art, kAiTicks, /*allowAutoBattle=*/true);
    }

    const auto& ironCells = deposits.grid(DepositKind::Iron);
    const int veinLeft = int(ironCells.at_index(veinIdx));
    const int veinLost = 20 - veinLeft;
    const int storeGained = gs.landmarks[0].inventory.count("iron");
    const int inBag = w.reg.get<ecs::NpcInventory>(e).inv.count("iron");

    CHECK(veinLost > 0, "the dig really drained the vein");
    CHECK(storeGained > 0, "the haul reached the village store");
    CHECK(veinLost == storeGained + inBag,
          "CONSERVATION: vein loss == store gain + what still rides the bag");
    // The world ran dry: 20 units at kGatherPerWorkerDay per trip is gone
    // within the 400 thinks — and by the ANNIHILATION law (owner,
    // 2026-08-28) the worked-out vein is a vein that no longer exists: the
    // cell leaves the map, the counter keeps the scarcity baseline, and the
    // chronicle (below) is the only record of what stood here.
    CHECK(ironCells.at_index(veinIdx) == 0,
          "the worked-out vein is ANNIHILATED - in a field, that is 0");
    CHECK(storeGained + inBag == 20,
          "everything the vein ever held is accounted for");

    // The worked-out vein is a FACT of the world (S20.1): filed ONCE — the
    // transition is the story, thirty daily hauls are weather — by the home
    // village, at the vein's cell, naming WHAT ran dry by its registry row.
    const FactTally drained = tally_facts(gs.chronicle, FactKind::Drained,
                                          14, 10);
    CHECK(drained.n == 1,
          "one dead vein = ONE Drained fact, not one per haul");
    CHECK(drained.last.subject == 3u
              && drained.last.subjectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact names the village whose man worked the vein out");
    CHECK(drained.last.x == 14 && drained.last.y == 10,
          "the fact stands on the vein's own cell");
    CHECK(drained.last.amount == int(ResourceFieldId::Iron) + 1,
          "the fact says WHAT ran dry: the resource registry row, +1");

    // No deposit layer wired = no ore conjured (the shared fail-closed rule).
    GameState gs2{};
    gs2.mapW = kMap;
    gs2.mapH = kMap;
    gs2.landmarks.push_back(vil);
    ecs::World w2;
    const auto e2 = w2.reg.create();
    w2.reg.emplace<ecs::MacroCell>(e2, ecs::cell_index(10, 10, kMap));
    w2.reg.emplace<ecs::MacroVisual>(e2, 10.0f, 10.0f, 0.0f);
    w2.reg.emplace<ecs::NPCKind>(e2, std::uint16_t(NPCType::Peasant),
                                 std::uint16_t(faction_index("timaert")));
    w2.reg.emplace<ecs::MacroNpcRuntime>(e2, rt);
    w2.reg.emplace<ecs::MacroSpawnId>(e2, 14u);
    w2.reg.emplace<ecs::NpcLevel>(e2, std::int16_t(3));
    {
        ecs::Pools p2{};
        p2.hp = p2.maxHp = 30;
        p2.sp = p2.maxSp = 100;
        w2.reg.emplace<ecs::Pools>(e2, p2);
    }
    w2.reg.emplace<ecs::SquadRoster>(e2);
    w2.reg.emplace<ecs::NpcInventory>(e2);
    MacroNpcAiRuntime art2{};
    reset_macro_npc_ai_runtime(art2, 71u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw2{.gs = &gs2, .world = &w2};
        tick_macro_npc_ai(mw2, art2, kAiTicks);
    }
    CHECK(gs2.landmarks[0].inventory.count("iron") == 0,
          "no deposit layer => no honest ore, and none minted from nothing");
}

// ЛОШАДЬ — ЮНИТ, А НЕ ПРЕДМЕТ (CANON S10, владелец 2026-09-19). Пинится
// ровно новая колонка закона — «выход ложится в РОСТЕР, а не в сумку» — и
// её сохранение: поле теряет ровно столько голов, сколько встало в отряд,
// сумка при этом пуста, а обоз растёт спинами пойманных (haulMult 8).
// Бутстрап тот же, что у шахты над жилой: первый день артель поднимает
// ПАСТБИЩЕ, ловля — следующим днём.
void test_the_catch_lands_in_the_roster() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Landmark vil{};
    vil.type = LandmarkType::Village;
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    vil.population = 40;
    gs.landmarks.push_back(vil);

    FeatureLayer features;
    features.resize(kMap, kMap);

    // Fertile ground everywhere: the herd baseline is the wheat row's own
    // fertility read as mouths, so grass is what a pasture needs.
    TerrainData terrain;
    terrain.width = kMap;
    terrain.height = kMap;
    terrain.rgba.assign(std::size_t(kMap) * kMap * 4u, 128);
    for (std::size_t i = 1; i < terrain.rgba.size(); i += 4) {
        terrain.rgba[i] = 255;   // G = fertility
    }

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::MacroCell>(e, ecs::cell_index(10, 10, kMap));
    reg.emplace<ecs::MacroVisual>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = vil.id;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt,
        make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(77u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.errandVerb = std::uint8_t(ErrandVerb::Gather);
    prt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Horses));
    prt.carryPerSoul = 40.0f;
    prt.carryCap = 40.0f;
    reg.emplace<ecs::MacroNpcRuntime>(e, prt);
    reg.emplace<ecs::MacroSpawnId>(e, 77u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    pools.hp = pools.maxHp = 20;
    reg.emplace<ecs::Pools>(e, pools);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 77u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &terrain,
                      .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle=*/true);
    }

    const SoldierSquad& roster = reg.get<ecs::SquadRoster>(e).squad;
    const int caught = count_soldiers_of_kind(
        roster, std::uint16_t(NPCType::Horse));
    const ResourceGrid& herdScars =
        gs.resourceScarCells[std::size_t(ResourceFieldId::Horses)];
    int lost = 0;
    herdScars.for_each_live([&](std::uint32_t, std::int32_t scar) {
        lost += int(scar);
    });

    const int stabled = count_soldiers_of_kind(
        gs.landmarks[0].garrison.squad, std::uint16_t(NPCType::Horse));
    CHECK(caught + stabled > 0, "the catch landed as SOULS, not as cargo");
    CHECK(lost == caught + stabled,
          "CONSERVATION через два контейнера: упряжка + стойло == голов, "
          "которых лишилось поле");
    CHECK(stabled > 0,
          "ТАКТ 1: отряд сдал табун ДОМОЙ — стойло места, не карман артели");
    CHECK(gs.landmarks[0].inventory.count("food") == 0
              && reg.get<ecs::NpcInventory>(e).inv.count("food") == 0,
          "a creature yield rides NO bag: nothing landed in the store");
    CHECK(is_mount_kind(std::uint16_t(NPCType::Horse)),
          "строка лошади несёт тег Mount — закон спрашивает ТЕГ, не род");
    CHECK(caught <= mount_allowance(roster),
          "ТАКТ 2: отряд ведёт не больше ездовых, чем душ (закон упряжки)");
    // A pasture rose first — the crew fences before it catches (S10 «фичи
    // создаются сквадами»), and the world remembers it as a Built row.
    int pastures = 0;
    for (const std::uint8_t f : features.data)
        if (f == FT_Pasture) ++pastures;
    CHECK(pastures == 1, "the crew fenced exactly ONE pasture to work");
    CHECK(!gs.builtFeatures.empty(),
          "the fence is WORLD TRUTH — it rides the save as a Built row");
    // ТАКТ 2 ОТДЕЛЬНО: стойло снаряжает уходящую артель. Дверь зовётся
    // из суда ротации, здесь — прямо, чтобы свидетель судил ЗАКОН, а не
    // расписание дня: место выдаёт по коню на душу и ни одного сверх.
    {
        auto& roMut = reg.get<ecs::SquadRoster>(e).squad;
        while (roMut.slot_count() > 0) {          // пешая артель
            SoldierRecord off{};
            if (!roMut.pop_soul_back(off)) break;
        }
        Landmark& home = gs.landmarks[0];
        const int stall = count_soldiers_of_kind(
            home.garrison.squad, std::uint16_t(NPCType::Horse));
        CHECK(stall >= 2, "фикстура: в стойле есть из чего снаряжать");
        // Лидер без членов — одна душа, значит ровно один конь.
        const int given = outfit_crew_mounts(w, home, e);
        CHECK(given == 1 && count_mount_souls(roMut) == 1,
              "ТАКТ 2: дом выдал по ездовому на душу — одному лидеру коня");
        CHECK(count_soldiers_of_kind(home.garrison.squad,
                                     std::uint16_t(NPCType::Horse))
                  == stall - given,
              "CONSERVATION такта 2: сколько вышло из стойла, столько и "
              "встало в упряжку");
        const int twice = outfit_crew_mounts(w, home, e);
        CHECK(twice == 0,
              "мера — потолок, а не запрос: снаряжённый отряд второго коня "
              "не берёт, даже когда стойло полно");
        // И обоз вырос ровно на спину коня — та же дверь, что у добора.
        const auto& rtNow = reg.get<ecs::MacroNpcRuntime>(e);
        CHECK(rtNow.carryCap >= rtNow.carryPerSoul * (1.0f + 8.0f) - 0.5f,
              "выданный конь — восемь спин в обозе (haulMult)");
    }
}

int main() {
    test_the_chop_is_real_and_the_haul_comes_home();
    // Was DEFINED and never CALLED — found the day -Wunused-function came
    // on (С3): a silently never-running test, the exact family check.h's
    // zero-checks rule hunts, hidden one scope deeper.
    test_no_layer_no_chop();
    test_the_farmer_works_the_field();
    test_farmer_without_terrain_conjures_nothing();
    test_the_miner_works_the_vein();
    test_the_mine_runs_while_the_player_is_away();
    test_agent_memory_is_bounded_and_current();
    test_the_vendor_sells_at_the_nearest_city();
    test_the_catch_lands_in_the_roster();
    return sm::test::report("woodcutter_gather_test");
}

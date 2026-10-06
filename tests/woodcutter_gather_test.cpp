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
#include "macro/labour.h"   // settle_souls — двери душ
#include "macro/upkeep_window.h"   // upkeep_bill — счёт по таблице

#include "macro/npc_ai.h"
#include "macro/landmark_iter.h"  // for_each_place — перепись мест
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/world_row.h"
#include "macro/agent_memory.h"
#include "macro/chronicle.h"
#include "macro/econ_day.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/deposit_layer.h"
#include "macro/tree_layer.h"
#include "macro/store.h"

#include <cmath>
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

sm::MacroHandle make_woodcutter(ecs::World& w, float x, float y,
                                int homeVillageId) {
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(int(x), int(y), kMap)};
    st.visual[h.slot] = ecs::MacroVisual{x, y, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = homeVillageId;
    rt.targetSettlementId = 0;
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
    rt.squadType = std::uint8_t(SquadType::Artel);
    rt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Trees));
    st.runtime[h.slot] = rt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{11u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(3)};
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;
    return h;
}

void test_the_chop_is_real_and_the_haul_comes_home() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);

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

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store: склад
    // — его колонка, ординал выдаёт эмитент ВНУТРИ двери рождения.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    const int vilId = int(sm::store_of(w).spawnId[vil.slot].index);
    // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число
    // фичи, головы в инвентарь ТЕЛА — тем же законом, что генезис.
    sm::settle_souls(gs, sm::store_of(w), vil.slot, 40);
    const sm::MacroHandle wc = make_woodcutter(w, 10.0f, 10.0f, vilId);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);

    // Live a while: idle -> travel -> WORK (the chop) -> return (the haul).
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .trees = &layer, .world = &w,
                      .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const int layerLost = 16 - int(layer.at(14, 10));
    const int storeGained =
        sm::store_of(w).inventory[vil.slot].inv.count("wood");
    const int inBag =
        (*sm::body_state<ecs::NpcInventory>(sm::store_of(w), wc)).inv.count("wood");

    CHECK(layerLost > 0, "the chop really fell trees in the layer");
    CHECK(storeGained > 0, "the haul reached the village store");
    CHECK(layerLost == storeGained + inBag,
          "CONSERVATION: layer loss == store gain + what still rides the bag");
    CHECK(layer.revision > 0,
          "the chop moved the grid revision - the map sprite and the save "
          "(which carries the grid whole) both see the stump");
    int placeCount = 0;
    sm::for_each_place(sm::store_of(w),
                       [&](std::uint16_t) { ++placeCount; });
    CHECK(sm::store_of(w).inventory[vil.slot].inv.count("wood") > 0
              && placeCount == 1,
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
    // A field two cells east — where stamp_field_features would put one.
    FeatureLayer features;
    features.resize(kMap, kMap);
    features.set(12, 10, FT_Field);

    // The terrain master: fertile land everywhere. Without it the reap is
    // fail-closed (Field Inc F4), so the honest test wires it.
    TerrainData terrain;
    terrain.width = kMap;
    terrain.height = kMap;
    // УРОВНИ, а не байты: карта хранит слово (`kFieldWordMax`), и байтовый
    // литерал 160 означал бы фертильность 0.002 — то есть пустошь, молча.
    terrain.rgba.assign(std::size_t(kMap) * kMap * 4u,
                        sm::field_word_of(128.0f / 255.0f));
    for (std::size_t i = 1; i < terrain.rgba.size(); i += 4) {
        terrain.rgba[i] = sm::field_word_of(160.0f / 255.0f);   // G = fertility
    }

    ecs::World w;

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    const sm::MacroHandle e = h;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.visual[h.slot] = ecs::MacroVisual{10.0f, 10.0f, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = int(st.spawnId[vil.slot].index);
    prt.targetSettlementId = 0;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.squadType = std::uint8_t(SquadType::Artel);
    prt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Wheat));
    st.runtime[h.slot] = prt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{12u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(2)};
    pools.hp = pools.maxHp = 20;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &terrain,
                      .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    const int foodUnits = st.inventory[vil.slot].inv.count("food");
    const int inBag = (*sm::body_state<ecs::NpcInventory>(sm::store_of(w), e)).inv.count("food");
    CHECK(foodUnits > 0, "the farmer's haul reached the village store");
    // THE BATCH LAW THIS USED TO PIN IS GONE (owner, 2026-09-16). It read
    // `foodUnits % kGatherPerCycle == 0` — "the haul arrives in whole cycle
    // yields" — and it was true only while a take was a declared batch of
    // eight. A take is now ONE object per hand, at exactly the price the
    // player pays for one, and the TRIP emerges from the backs and the bar
    // instead of from a constant. Nothing is pinned in its place because
    // nothing quantised survives: the honest statement left is conservation,
    // below, and it is the one that was load-bearing all along.
    //
    // CONSERVATION (Field Inc F4): every unit that reached anybody left the
    // world — the field's scar is exactly as deep as store PLUS bag. The bag
    // half is new and it is not bookkeeping: the farmer can now be caught
    // mid-trip with the haul on his back, where the old single-take cycle always
    // ended at the door.
    const sm::ResourceGrid& wheatScars =
        gs.resourceScarCells[std::size_t(sm::ResourceFieldId::Wheat)];
    CHECK(wheatScars.at(12, 10) == foodUnits + inBag,
          "food gained by store AND bag == stands the field lost");
    CHECK(wheatScars.liveCells == 1,
          "the farmer scars only the field he works");
}

void test_farmer_without_terrain_conjures_nothing() {
    // The fail-closed half of the same law (mirrors no-layer-no-chop): an
    // unwired terrain means no ledger to settle against, so NOTHING is
    // gathered — food from thin air died with Field Inc F4.
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    FeatureLayer features;
    features.resize(kMap, kMap);
    features.set(12, 10, FT_Field);

    ecs::World w;

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.visual[h.slot] = ecs::MacroVisual{10.0f, 10.0f, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = int(st.spawnId[vil.slot].index);
    prt.targetSettlementId = 0;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.squadType = std::uint8_t(SquadType::Artel);
    prt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Wheat));
    st.runtime[h.slot] = prt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{12u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(2)};
    pools.hp = pools.maxHp = 20;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(st.inventory[vil.slot].inv.count("food") == 0,
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
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    make_woodcutter(w, 10.0f, 10.0f,
                    int(sm::store_of(w).spawnId[vil.slot].index));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(sm::store_of(w).inventory[vil.slot].inv.count("wood") == 0,
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
    DepositLayer deposits;
    allocate_deposit_fields(deposits, kMap, kMap);
    const std::uint32_t veinIdx = 10u * std::uint32_t(kMap) + 14u;
    deposits.grid(DepositKind::Iron).write(14, 10, 20);

    ecs::World w;

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.visual[h.slot] = ecs::MacroVisual{10.0f, 10.0f, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = int(st.spawnId[vil.slot].index);
    rt.targetSettlementId = 0;
    rt.targetX = 10.0f;
    rt.targetY = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt, make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    rt.squadType = std::uint8_t(SquadType::Artel);
    rt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Iron));
    st.runtime[h.slot] = rt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{13u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(3)};
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .deposits = &deposits};
        tick_macro_npc_ai_budgeted(mw, art, kAiTicks,
                                   /*max_npc_ticks=*/64);
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
    // ЖИЛЕЦ ПАМЯТИ СЕГОДНЯ ОДИН — ДОЛГ. Снимок рынка (первый жилец) вырезан
    // 2026-09-22: он писался каждым выездом и не читался ни одной строкой
    // мира. Законы САМОГО ВМЕСТИЛИЩА от этого не изменились, и свидетель
    // обязан проверять их на живом роде, а не на снесённом.
    AgentMemory m{};
    remember(m, make_debt_fact(kDebtToSettlement, 5, 100, 10));
    const MemoryEntry* d = recall(m, AgentMemoryKind::Debt, 5,
                                  kDebtToSettlement);
    CHECK(m.count == 1 && d && memory_amount(*d) == 100,
          "a memory can be recalled by (kind, subject)");
    remember(m, make_debt_fact(kDebtToSettlement, 5, 40, 20));
    d = recall(m, AgentMemoryKind::Debt, 5, kDebtToSettlement);
    CHECK(m.count == 1 && d && memory_amount(*d) == 140 && d->day == 20,
          "the same (kind, subject) FOLDS by its own law - a debt SUMS");
    CHECK(recall(m, AgentMemoryKind::Debt, 5, kDebtToFaction) == nullptr,
          "the key is (kind, subject, SPACE): owing a town is not owing a "
          "crown");
    for (int i = 0; i < kAgentMemorySlots + 3; ++i) {
        remember(m, make_debt_fact(kDebtToSettlement,
                                   std::uint16_t(100 + i), 1,
                                   std::uint32_t(30 + i)));
    }
    CHECK(int(m.count) == kAgentMemorySlots,
          "a bounded head never grows past its slots");
    CHECK(recall(m, AgentMemoryKind::Debt, 5, kDebtToSettlement) == nullptr,
          "past the cap the OLDEST memory is forgotten");
}

void test_the_vendor_sells_at_the_nearest_city() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);

    ecs::World w;

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    sm::MacroStore& st = sm::store_of(w);

    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store: склад
    // И счёт нужд — его колонки, ординал выдаёт эмитент. Хэндл слота живёт
    // сколько нужно: следующее рождение его не двигает (вектор строк, чьё
    // движение протухало ссылкой, умер).
    const sm::MacroHandle city =
        sm::birth_place(gs, st, SquadType::City, 10, 10);
    const int cityId = int(st.spawnId[city.slot].index);
    {
        sm::Inventory& row = st.inventory[city.slot].inv;
        row.add("food", 2000);   // plenty: the export
        // The deal PAYS now (owner 2026-08-30): a coinless fixture is the
        // deadlock the payment law exists to refuse. The purse covers the
        // food lot at the SEASONAL famine price (corridor died 2026-09-18) —
        // a thin purse would pay the vendor in its own goods by value
        // density, and the return leg would waddle home under a tonne of
        // payment.
        row.add("coin_timaert_copper", 40000);
    }
    const sm::MacroHandle vil =
        sm::birth_place(gs, st, SquadType::Village, 16, 10);
    const int vilId = int(st.spawnId[vil.slot].index);
    {
        sm::Inventory& row = st.inventory[vil.slot].inv;
        // A GENUINE surplus: the loading law keeps the seasonal larder home
        // (S19.2 + verdict 2026-09-18 «дома дешевле базы» decides the load),
        // and 50 souls EAT 50 food a day — 1600 a season. Only what
        // stands ABOVE that rides to market.
        row.add("food", 4000);
        row.add("coin_timaert_copper", 50 * 2);
        // ДОМ ГОЛОДЕН СЧЁТОМ (CANON S10): «дома нет хлеба» = непогашенный
        // сезонный счёт — из него и читается нужда, которую вендор едет
        // закрывать покупкой.
        // СЧЁТ ЕДЫ — ПО ТАБЛИЦЕ (v122): сезонная нужда есть `upkeep_bill` по
        // головам этого места, а не «душа × сезон» литералом.
        sm::settle_souls(gs, st, vil.slot, 50);
        st.upkeep[vil.slot].needDebt[commodity_index("food")] =
            sm::upkeep_bill(row).board;
    }
    // (ЗДЕСЬ ФИКСТУРА ПУБЛИКОВАЛА ВЕДОМОСТИ — уничтожены 2026-09-30,
    // ломтик E шаг 2: что везти домой, судит сам дом, читаемый живьём в
    // точке сделки, а цена чужого рынка есть абсолютная стоимость строки.
    // Руками поднимать больше нечего.)

    const sm::MacroHandle h = sm::store_birth(st);
    const sm::MacroHandle e = h;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(16, 10, kMap)};
    st.visual[h.slot] = ecs::MacroVisual{16.0f, 10.0f, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime crt{};
    crt.homeSettlementId = vilId;   // the VILLAGE: vendors are its arm
    // РЫНОК — ИЗ ПОРУЧЕНИЯ (2026-09-19): рейс к рынку читает errandObject,
    // а не феодальное ребро, — потому что тем же рейсом горожане едут
    // закупаться В ДЕРЕВНЮ. Фикстура называет рынок так же, как его назвал
    // бы аукцион.
    crt.squadType = std::uint8_t(SquadType::Caravan);
    crt.errandObject = std::uint32_t(cityId);   // the city's ordinal
    crt.targetSettlementId = 0;
    crt.targetX = 10.0f;
    crt.targetY = 10.0f;
    crt.state = std::uint8_t(NPCState::Idle);
    crt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &crt, make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    st.runtime[h.slot] = crt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{13u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(3)};
    pools.hp = pools.maxHp = 25;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < 600; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const auto& bag = (*sm::body_state<ecs::NpcInventory>(sm::store_of(w), e)).inv;
    const int cityFood = st.inventory[city.slot].inv.count("food");
    const int vilFood = st.inventory[vil.slot].inv.count("food");
    CHECK(cityFood > 0,
          "the vendor sold the village surplus at the nearest city");
    // ПОД ДОЛГОМ (CANON S10) «купил домой хлеб» видно СЧЁТОМ: привезённое
    // гасит его в дверях прихода и съедается — полка держит только излишек.
    const int vilFoodDebtPaid = 50 * kDaysPerSeason
        - st.upkeep[vil.slot].needDebt[commodity_index("food")];
    CHECK(vilFoodDebtPaid > 0,
          "the earnings FED the home's lack — the food bill fell");
    // КОНСЕРВАЦИЯ ОДНОЙ ПИЩЕЙ (2026-09-20, снос хлеба): до этого дня в мире
    // было ДВЕ съедобные строки — зерно и хлеб, — и сумма считалась по каждой
    // отдельно. Теперь поток один: всё, что не лежит на полках и не едет в
    // спине, ОПЛАТИЛО СЧЁТ и съедено в дверях прихода (CANON S10).
    const int foodOnShelves = st.inventory[city.slot].inv.count("food")
                              + st.inventory[vil.slot].inv.count("food")
                              + bag.count("food");
    CHECK(foodOnShelves + vilFoodDebtPaid == 2000 + 4000,
          "CONSERVATION: cargo moves or pays the bill — never dropped");
    (void)vilFood;
    // ...and the deal's other half obeys the same law: coin travels between
    // the three purses (city, village, hold) and is never minted or burned.
    const int coinTotal =
        st.inventory[city.slot].inv.count("coin_timaert_copper")
        + st.inventory[vil.slot].inv.count("coin_timaert_copper")
        + bag.count("coin_timaert_copper");
    CHECK(coinTotal == 40000 + 50 * 2,
          "CONSERVATION: coin moves through the deal, never minted");
    // КОШЕЛЁК ДЕРЕВНИ — ЭТО ПОЛКА ПЛЮС ТРЮМ ЕЁ СОБСТВЕННОЙ КРЮ. С
    // 2026-09-22 крю грузит в рейс ВСЁ, включая КАЗНУ (владелец: «всё в
    // инвентаре — товар»), и у сезонного резерва дома нет товарной строки
    // для монеты — стак уезжает целиком. Поэтому «монета на полке» в
    // произвольный тик читает ФАЗУ РЕЙСА, а не заработок: на 600-м думе
    // вся казна деревни законно едет в обозе, и прежняя редакция этой
    // проверки падала на мире, который работает правильно.
    const int vilPurse =
        st.inventory[vil.slot].inv.count("coin_timaert_copper")
        + bag.count("coin_timaert_copper");
    CHECK(vilPurse > 50 * 2,
          "the village EARNED coin for its raw — the payment is real "
          "(purse = shelf + its own crew's hold: a run is not a loss)");

    // The DEAL is a fact of the world (S20.1): filed at the village the
    // moment the exchange happened — a transition by nature, so every visit
    // may file one — naming both parties and what the goods were worth on
    // the ONE price table.
    const FactTally traded = tally_facts(gs.chronicle, FactKind::Traded,
                                         16, 10);
    CHECK(traded.n >= 1, "a completed exchange left a Traded fact");
    CHECK(traded.last.subject == std::uint32_t(vilId)
              && traded.last.subjectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact's subject is the home village whose vendor dealt");
    CHECK(traded.last.object == std::uint32_t(cityId)
              && traded.last.objectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact's object is the city it traded AT");
    CHECK(traded.last.amount > 0,
          "the deal's worth is real: table value of what changed hands");
}

// The miner: the SAME gatherer row-loop as the chop above, pointed at a
// deposit (CANON S10 (бывший resources.md) — a profession per resource, a row not a branch).
// Pinned: the ore leaves through the Iron carrier row, hauls home into the
// village store, CONSERVES, the drained vein stays a VISIBLE cell at 0 and
// a dry world gives the miner nothing further; no deposit layer wired = no
// ore conjured (the shared fail-closed rule).
void test_the_miner_works_the_vein() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    DepositLayer deposits;
    allocate_deposit_fields(deposits, kMap, kMap);
    const std::uint32_t veinIdx = 10u * std::uint32_t(kMap) + 14u;
    deposits.grid(DepositKind::Iron).write(14, 10, 20);

    ecs::World w;

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    sm::MacroStore& st = sm::store_of(w);
    const int vilId = int(st.spawnId[vil.slot].index);
    const sm::MacroHandle h = sm::store_birth(st);
    const sm::MacroHandle e = h;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.visual[h.slot] = ecs::MacroVisual{10.0f, 10.0f, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = vilId;
    rt.targetSettlementId = 0;
    rt.targetX = 10.0f;
    rt.targetY = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt, make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    rt.squadType = std::uint8_t(SquadType::Artel);
    rt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Iron));
    st.runtime[h.slot] = rt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{13u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(3)};
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .deposits = &deposits};
        tick_macro_npc_ai(mw, art, kAiTicks);
    }

    const auto& ironCells = deposits.grid(DepositKind::Iron);
    const int veinLeft = int(ironCells.at_index(veinIdx));
    const int veinLost = 20 - veinLeft;
    const int storeGained = st.inventory[vil.slot].inv.count("iron");
    const int inBag = (*sm::body_state<ecs::NpcInventory>(sm::store_of(w), e)).inv.count("iron");

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
    CHECK(drained.last.subject == std::uint32_t(vilId)
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
    ecs::World w2;
    auto w2Store_ = sm::make_macro_store();
    sm::store_attach(w2, w2Store_.get());
    sm::MacroStore& st2 = sm::store_of(w2);
    // У ВТОРОГО МИРА — СВОЁ ТЕЛО МЕСТА: место ЕСТЬ слот своего store
    // (ломтик F), и одно тело на два мира невыразимо. Деревня рождается
    // здесь заново, своим эмитентом.
    const sm::MacroHandle vil2 =
        sm::birth_place(gs2, st2, SquadType::Village, 10, 10);
    const sm::MacroHandle h2 = sm::store_birth(st2);
    st2.cell[h2.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st2.visual[h2.slot] = ecs::MacroVisual{10.0f, 10.0f, 0.0f};
    st2.kind[h2.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                     std::uint16_t(faction_index("timaert"))};
    st2.runtime[h2.slot] = rt;
    st2.spawnId[h2.slot] = ecs::MacroSpawnId{14u};
    st2.level[h2.slot] = ecs::NpcLevel{std::int16_t(3)};
    {
        ecs::Pools p2{};
        p2.hp = p2.maxHp = 30;
        p2.sp = p2.maxSp = 100;
        st2.pools[h2.slot] = p2;
    }
    MacroNpcAiRuntime art2{};
    reset_macro_npc_ai_runtime(art2, 71u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw2{.gs = &gs2, .world = &w2};
        tick_macro_npc_ai(mw2, art2, kAiTicks);
    }
    CHECK(st2.inventory[vil2.slot].inv.count("iron") == 0,
          "no deposit layer => no honest ore, and none minted from nothing");
}

// ЛОШАДЬ — ЮНИТ, А НЕ ПРЕДМЕТ (CANON S10, владелец 2026-09-19). Пинится
// ровно колонка закона — «выход ложится СУЩЕСТВОМ в контейнер, а не в
// сумку» — и её сохранение: поле теряет ровно столько голов, сколько встало
// в отряд, сумка при этом пуста, а обоз растёт спинами пойманных
// (haulMult 8). Бутстрап тот же, что у шахты над жилой: первый день артель
// поднимает ПАСТБИЩЕ, ловля — следующим днём.
//
// ЗАКОН УПРЯЖКИ СНЕСЁН ЦЕЛИКОМ (M-230, вердикт владельца 2026-10-06), и
// здесь стояли ШЕСТЬ его свидетелей — такт 1 (сдача в стойло), тег Mount,
// мера «по лошадке на душу», такт 2 с его сохранением и потолком. Они сняты
// ВМЕСТЕ С НОСИТЕЛЕМ, а не подогнаны (AGENTS §5 п.6): закона, который они
// охраняли, больше нет. Что осталось от них живым, то и проверяется ниже —
// одна колонка haulMult и сохранение улова, теперь через ОДИН контейнер.
// Проверка «у дома ноль голов» и есть негативный контроль сноса: вернись
// двухтактный обоз — она краснеет первой.
void test_the_catch_lands_as_creatures() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    FeatureLayer features;
    features.resize(kMap, kMap);

    // Fertile ground everywhere: the herd baseline is the wheat row's own
    // fertility read as mouths, so grass is what a pasture needs.
    TerrainData terrain;
    terrain.width = kMap;
    terrain.height = kMap;
    // УРОВНИ, а не байты (карта на слове, `kFieldWordMax`): полная
    // фертильность — это ВЕРХ словаря, а не байт 255, который в слове значил
    // бы 0.004.
    terrain.rgba.assign(std::size_t(kMap) * kMap * 4u,
                        sm::field_word_of(128.0f / 255.0f));
    for (std::size_t i = 1; i < terrain.rgba.size(); i += 4) {
        terrain.rgba[i] = std::uint16_t(sm::kFieldWordMax);   // G = fertility
    }

    ecs::World w;

    auto wStore_ = sm::make_macro_store();

    sm::store_attach(w, wStore_.get());
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_place(gs, sm::store_of(w), SquadType::Village, 10, 10);
    sm::MacroStore& st = sm::store_of(w);
    // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число
    // фичи, головы в инвентарь ТЕЛА — тем же законом, что генезис.
    sm::settle_souls(gs, st, vil.slot, 40);
    const sm::MacroHandle h = sm::store_birth(st);
    const sm::MacroHandle e = h;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.visual[h.slot] = ecs::MacroVisual{10.0f, 10.0f, 0.0f};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = int(st.spawnId[vil.slot].index);
    prt.targetSettlementId = 0;
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
    prt.squadType = std::uint8_t(SquadType::Artel);
    prt.errandObject = std::uint32_t(gather_goal_row(ResourceFieldId::Horses));
    prt.carryPerSoul = 40.0f;
    prt.carryCap = 40.0f;
    st.runtime[h.slot] = prt;
    st.spawnId[h.slot] = ecs::MacroSpawnId{77u};
    st.level[h.slot] = ecs::NpcLevel{std::int16_t(2)};
    pools.hp = pools.maxHp = 20;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 77u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &terrain,
                      .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const Inventory& creatures = (*sm::body_state<ecs::NpcInventory>(sm::store_of(w), e)).inv;
    const int caught = creature_count_of(creatures, NPCType::Horse);
    const ResourceGrid& herdScars =
        gs.resourceScarCells[std::size_t(ResourceFieldId::Horses)];
    int lost = 0;
    herdScars.for_each_live([&](std::uint32_t, std::int32_t scar) {
        lost += int(scar);
    });

    const int athome =
        creature_count_of(st.inventory[vil.slot].inv, NPCType::Horse);
    CHECK(caught > 0, "the catch landed as SOULS, not as cargo");
    CHECK(lost == caught,
          "CONSERVATION ОДНИМ контейнером: поле лишилось ровно того, что "
          "встало в отряд");
    CHECK(athome == 0,
          "НЕГАТИВНЫЙ КОНТРОЛЬ СНОСА (M-230): табун не принадлежит месту — "
          "дом не получает ни одной головы, потому что обоза больше нет");
    CHECK(st.inventory[vil.slot].inv.count("food") == 0
              && (*sm::body_state<ecs::NpcInventory>(sm::store_of(w), e)).inv.count("food") == 0,
          "a creature yield rides NO bag: nothing landed in the store");
    // (ЗДЕСЬ СТОЯЛИ ДВА СВИДЕТЕЛЯ ЗАКОНА «артель ставит загон ПЕРЕД ловлей».
    // Закон отменён вердиктом владельца 2026-09-22 — «СТРОИТЕЛЬСТВО
    // КРЕСТЬЯНАМИ НЕ РАБОТАЕТ НА НЕГО НЕЛЬЗЯ ПОЛАГАТЬСЯ», — поэтому
    // свидетели сняты ВМЕСТЕ С ЗАКОНОМ, а не подогнаны под новый ответ.
    // Ловля от этого не изменилась: проверка сохранения выше зелена —
    // артель берёт с клетки поля, как лесоруб с клетки леса.
    // Прегенерацию загонов миром несёт наряд M-79 реестра.)
    // ЕДИНСТВЕННАЯ ВЫЖИВШАЯ КОЛОНКА ЛОШАДИ — СПИНА, и считает её живая
    // дверь состава (squad.h refresh_squad_carry), которую зовёт сама
    // ловля. Лидер — крестьянин (haulMult 1.0), поэтому каждая пойманная
    // голова добавляет РОВНО восемь его спин, и обоз есть точное
    // carryPerSoul × (1 + 8 × голов): закон вместимости, а не пересказанное
    // число (AGENTS §8 п.4).
    {
        const auto& rtNow =
            (*sm::body_state<ecs::MacroNpcRuntime>(sm::store_of(w), e));
        const float horseHaul = npc_def(NPCType::Horse).haulMult;
        const float leaderHaul = npc_def(NPCType::Peasant).haulMult;
        const float backs = 1.0f + horseHaul / leaderHaul * float(caught);
        CHECK(std::fabs(rtNow.carryCap - rtNow.carryPerSoul * backs) <= 0.5f,
              "обоз = сумма спин по строкам состава: пойманный конь несёт "
              "восемь крестьянских (haulMult)");
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
    test_the_catch_lands_as_creatures();
    return sm::test::report("woodcutter_gather_test");
}

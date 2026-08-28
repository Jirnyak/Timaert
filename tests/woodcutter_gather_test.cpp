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
    reg.emplace<ecs::Position>(e, x, y, 0.0f);
    reg.emplace<ecs::VisualPos>(e, x, y, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Woodcutter),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = homeVillageId;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    const CharacterSheet sheet = make_character_sheet(
        NPCType::Woodcutter, 3, leader_sheet_seed(11u));
    refresh_leader_travel_stats(rt, sheet, NPCType::Woodcutter);
    rt.sp = rt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 11u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    reg.emplace<ecs::Health>(e, 30.0f, 30.0f);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);
    return e;
}

void test_the_chop_is_real_and_the_haul_comes_home() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    vil.population = 40;
    gs.villages.push_back(vil);

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
    const int storeGained = gs.villages[0].inventory.count("wood");
    const int inBag =
        w.reg.get<ecs::NpcInventory>(wc).inv.count("wood");

    CHECK(layerLost > 0, "the chop really fell trees in the layer");
    CHECK(storeGained > 0, "the haul reached the village store");
    CHECK(layerLost == storeGained + inBag,
          "CONSERVATION: layer loss == store gain + what still rides the bag");
    CHECK(layer.revision > 0,
          "the chop moved the grid revision - the map sprite and the save "
          "(which carries the grid whole) both see the stump");
    CHECK(gs.villages[0].inventory.count("wood") > 0
              && gs.settlements.empty(),
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
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);

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
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::VisualPos>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = vil.id;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    refresh_leader_travel_stats(
        prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)),
        NPCType::Peasant);
    prt.sp = prt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, prt);
    reg.emplace<ecs::MacroSpawnId>(e, 12u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    reg.emplace<ecs::Health>(e, 20.0f, 20.0f);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .terrain = &terrain,
                      .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle=*/true);
    }
    const int grain = gs.villages[0].inventory.count("grain");
    CHECK(grain > 0, "the farmer's grain reached the village store");
    CHECK(grain % kGatherPerWorkerDay == 0,
          "grain arrives in whole trip-yields - one law of labour");
    // CONSERVATION (Field Inc F4): every grain in the store left the world —
    // the field cell carries a harvest scar exactly as deep as the haul.
    const std::uint32_t fieldIdx = 10u * std::uint32_t(kMap) + 12u;
    const auto scar = gs.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].find(fieldIdx);
    CHECK(scar != gs.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].end() && int(scar->second) == grain,
          "grain gained by the store == stands the field lost");
    CHECK(gs.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].size() == 1,
          "the farmer scars only the field he works");
}

void test_farmer_without_terrain_conjures_nothing() {
    // The fail-closed half of the same law (mirrors no-layer-no-chop): an
    // unwired terrain means no ledger to settle against, so NOTHING is
    // gathered — grain from thin air died with Field Inc F4.
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);
    FeatureLayer features;
    features.resize(kMap, kMap);
    features.set(12, 10, FT_Field);

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::VisualPos>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Peasant),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime prt{};
    prt.homeSettlementId = vil.id;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    refresh_leader_travel_stats(
        prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)),
        NPCType::Peasant);
    prt.sp = prt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, prt);
    reg.emplace<ecs::MacroSpawnId>(e, 12u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(2));
    reg.emplace<ecs::Health>(e, 20.0f, 20.0f);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .features = &features};
        tick_macro_npc_ai(mw, rt, kAiTicks, /*allowAutoBattle=*/true);
    }
    CHECK(gs.villages[0].inventory.count("grain") == 0,
          "no terrain wired: nothing to reap against, nothing conjured");
    CHECK(gs.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].empty(),
          "no terrain wired: no scar appears either");
}

void test_no_layer_no_chop() {
    // Without a live tree layer the behaviour must not invent wood — the
    // old walk-to-forest-and-back pantomime, unchanged.
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);
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
    CHECK(gs.villages[0].inventory.count("wood") == 0,
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
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);

    DepositLayer deposits;
    deposits.width = kMap;
    deposits.height = kMap;
    const std::uint32_t veinIdx = 10u * std::uint32_t(kMap) + 14u;
    deposits.cells[std::size_t(DepositKind::Iron)][veinIdx] = 20;

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::VisualPos>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Miner),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = vil.id;
    rt.targetSettlementId = -1;
    rt.targetX = 10.0f;
    rt.targetY = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    refresh_leader_travel_stats(
        rt, make_character_sheet(NPCType::Miner, 3, leader_sheet_seed(13u)),
        NPCType::Miner);
    rt.sp = rt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 13u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    reg.emplace<ecs::Health>(e, 30.0f, 30.0f);
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
    const auto& mineIron = deposits.cells[std::size_t(DepositKind::Iron)];
    const int veinLeft =
        mineIron.count(veinIdx) ? int(mineIron.at(veinIdx)) : 0;
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
    store.add("bread", 2000);   // plenty
    store.add("wood", 100);     // stocked
    store.add("iron", 10);      // scarce
    const MemoryEntry snap = pack_market_snapshot(store, 7, 40);
    CHECK(market_stock_class(snap, commodity_index("bread")) == 3
              && market_stock_class(snap, commodity_index("wood")) == 2
              && market_stock_class(snap, commodity_index("iron")) == 1
              && market_stock_class(snap, commodity_index("grain")) == 0,
          "the snapshot packs stock classes per commodity");
}

void test_the_caravan_trades_on_its_memory() {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    Settlement city{};
    city.id = 1;   // landmark ids are ordinals from 1 (v54): 0 = "no place"
    city.x = 10;
    city.y = 10;
    city.population = 100;
    city.inventory.add("bread", 2000);   // plenty: the export
    gs.settlements.push_back(city);      // grain: NONE — the import
    Village vil{};
    vil.id = 3;
    vil.x = 16;
    vil.y = 10;
    vil.nearestCityId = 1;
    vil.inventory.add("grain", 500);
    gs.villages.push_back(vil);

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::VisualPos>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Caravan),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime crt{};
    crt.homeSettlementId = 1;
    crt.targetSettlementId = -1;
    crt.targetX = 10.0f;
    crt.targetY = 10.0f;
    crt.state = std::uint8_t(NPCState::Idle);
    crt.stateTimer = 0;
    refresh_leader_travel_stats(
        crt, make_character_sheet(NPCType::Caravan, 3, leader_sheet_seed(13u)),
        NPCType::Caravan);
    crt.sp = crt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, crt);
    reg.emplace<ecs::MacroSpawnId>(e, 13u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    reg.emplace<ecs::Health>(e, 25.0f, 25.0f);
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
    const int cityGrain = gs.settlements[0].inventory.count("grain");
    const int vilBread = gs.villages[0].inventory.count("bread");
    CHECK(cityGrain > 0,
          "the caravan hauled the city what its snapshot said it LACKED");
    CHECK(vilBread > 0, "the caravan delivered the city's surplus bread");
    CHECK(recall(reg.get<AgentMemory>(e),
                 AgentMemoryKind::MarketSnapshot, 1) != nullptr,
          "the departure snapshot lives in the caravan's memory");
    const int grainTotal = cityGrain + bag.count("grain")
                           + gs.villages[0].inventory.count("grain");
    const int breadTotal = vilBread + bag.count("bread")
                           + gs.settlements[0].inventory.count("bread");
    CHECK(grainTotal == 500 && breadTotal == 2000,
          "CONSERVATION: cargo moves, it is never minted or dropped");

    // The DEAL is a fact of the world (S20.1): filed at the village the
    // moment the exchange happened — a transition by nature, so every visit
    // may file one — naming both parties and what the goods were worth on
    // the ONE price table.
    const FactTally traded = tally_facts(gs.chronicle, FactKind::Traded,
                                         16, 10);
    CHECK(traded.n >= 1, "a completed exchange left a Traded fact");
    CHECK(traded.last.subject == 1u
              && traded.last.subjectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact's subject is the home city whose caravan dealt");
    CHECK(traded.last.object == 3u
              && traded.last.objectKind
                     == std::uint8_t(FactSubject::Landmark),
          "the fact's object is the village it traded WITH");
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
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    gs.villages.push_back(vil);

    DepositLayer deposits;
    deposits.width = kMap;
    deposits.height = kMap;
    const std::uint32_t veinIdx = 10u * std::uint32_t(kMap) + 14u;
    deposits.cells[std::size_t(DepositKind::Iron)][veinIdx] = 20;

    ecs::World w;
    auto& reg = w.reg;
    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::VisualPos>(e, 10.0f, 10.0f, 0.0f);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Miner),
                              std::uint16_t(faction_index("timaert")));
    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId = vil.id;
    rt.targetSettlementId = -1;
    rt.targetX = 10.0f;
    rt.targetY = 10.0f;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    refresh_leader_travel_stats(
        rt, make_character_sheet(NPCType::Miner, 3, leader_sheet_seed(13u)),
        NPCType::Miner);
    rt.sp = rt.maxSp;
    reg.emplace<ecs::MacroNpcRuntime>(e, rt);
    reg.emplace<ecs::MacroSpawnId>(e, 13u);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
    reg.emplace<ecs::Health>(e, 30.0f, 30.0f);
    reg.emplace<ecs::SquadRoster>(e);
    reg.emplace<ecs::NpcInventory>(e);

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .world = &w, .deposits = &deposits};
        tick_macro_npc_ai(mw, art, kAiTicks, /*allowAutoBattle=*/true);
    }

    const auto& ironCells = deposits.cells[std::size_t(DepositKind::Iron)];
    const int veinLeft =
        ironCells.count(veinIdx) ? int(ironCells.at(veinIdx)) : 0;
    const int veinLost = 20 - veinLeft;
    const int storeGained = gs.villages[0].inventory.count("iron");
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
    CHECK(ironCells.count(veinIdx) == 0,
          "the worked-out vein is ANNIHILATED - no dead cell lingers");
    CHECK(deposits.drainedCells[std::size_t(DepositKind::Iron)] == 1u,
          "the annihilation is counted once");
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
    gs2.villages.push_back(vil);
    ecs::World w2;
    const auto e2 = w2.reg.create();
    w2.reg.emplace<ecs::Position>(e2, 10.0f, 10.0f, 0.0f);
    w2.reg.emplace<ecs::VisualPos>(e2, 10.0f, 10.0f, 0.0f);
    w2.reg.emplace<ecs::NPCKind>(e2, std::uint16_t(NPCType::Miner),
                                 std::uint16_t(faction_index("timaert")));
    w2.reg.emplace<ecs::MacroNpcRuntime>(e2, rt);
    w2.reg.emplace<ecs::MacroSpawnId>(e2, 14u);
    w2.reg.emplace<ecs::NpcLevel>(e2, std::int16_t(3));
    w2.reg.emplace<ecs::Health>(e2, 30.0f, 30.0f);
    w2.reg.emplace<ecs::SquadRoster>(e2);
    w2.reg.emplace<ecs::NpcInventory>(e2);
    MacroNpcAiRuntime art2{};
    reset_macro_npc_ai_runtime(art2, 71u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw2{.gs = &gs2, .world = &w2};
        tick_macro_npc_ai(mw2, art2, kAiTicks);
    }
    CHECK(gs2.villages[0].inventory.count("iron") == 0,
          "no deposit layer => no honest ore, and none minted from nothing");
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
    test_the_caravan_trades_on_its_memory();
    return sm::test::report("woodcutter_gather_test");
}

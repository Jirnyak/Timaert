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
#include "macro/econ_day.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/squad.h"
#include "macro/tree_layer.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 32;

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
    rt.homeIsVillage = 1;
    rt.targetSettlementId = -1;
    rt.targetX = x;
    rt.targetY = y;
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    const CharacterSheet sheet = make_character_sheet(
        NPCType::Woodcutter, 3, leader_sheet_seed(11u));
    refresh_leader_travel_stats(rt, sheet);
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
    Village vil{};
    vil.id = 3;
    vil.x = 10;
    vil.y = 10;
    vil.population = 40;
    gs.villages.push_back(vil);

    // A little forest cell four cells east of the village.
    TreeLayer layer;
    layer.width = kMap;
    layer.height = kMap;
    layer.data.assign(std::size_t(kMap) * kMap, 0);
    layer.data[10 * kMap + 14] = 100;
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);

    ecs::World w;
    const entt::entity wc = make_woodcutter(w, 10.0f, 10.0f, vil.id);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);

    // Live a while: idle -> travel -> WORK (the chop) -> return (the haul).
    for (int i = 0; i < 400; ++i) {
        tick_macro_npc_ai(gs, w, &grid, rt, kAiTicks,
                          /*allowAutoBattle=*/true, /*pathCost=*/nullptr,
                          &layer);
    }

    const int layerLost = 100 - int(layer.at(14, 10));
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
    prt.homeIsVillage = 1;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    refresh_leader_travel_stats(
        prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)));
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
        tick_macro_npc_ai(gs, w, nullptr, rt, kAiTicks,
                          /*allowAutoBattle=*/true, nullptr, nullptr,
                          &features, &terrain);
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
    prt.homeIsVillage = 1;
    prt.targetSettlementId = -1;
    prt.targetX = 10.0f;
    prt.targetY = 10.0f;
    prt.state = std::uint8_t(NPCState::Idle);
    prt.stateTimer = 0;
    refresh_leader_travel_stats(
        prt, make_character_sheet(NPCType::Peasant, 2, leader_sheet_seed(12u)));
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
        tick_macro_npc_ai(gs, w, nullptr, rt, kAiTicks,
                          /*allowAutoBattle=*/true, nullptr, nullptr,
                          &features);
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
        tick_macro_npc_ai(gs, w, &grid, rt, kAiTicks);
    }
    CHECK(gs.villages[0].inventory.count("wood") == 0,
          "no layer => no honest wood, and none minted from nothing");
}

} // namespace

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
    Settlement city{};
    city.id = 0;
    city.x = 10;
    city.y = 10;
    city.population = 100;
    city.inventory.add("bread", 2000);   // plenty: the export
    gs.settlements.push_back(city);      // grain: NONE — the import
    Village vil{};
    vil.id = 3;
    vil.x = 16;
    vil.y = 10;
    vil.nearestCityId = 0;
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
    crt.homeSettlementId = 0;
    crt.targetSettlementId = -1;
    crt.targetX = 10.0f;
    crt.targetY = 10.0f;
    crt.state = std::uint8_t(NPCState::Idle);
    crt.stateTimer = 0;
    refresh_leader_travel_stats(
        crt, make_character_sheet(NPCType::Caravan, 3, leader_sheet_seed(13u)));
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
        tick_macro_npc_ai(gs, w, nullptr, rt, kAiTicks);
    }

    const auto& bag = reg.get<ecs::NpcInventory>(e).inv;
    const int cityGrain = gs.settlements[0].inventory.count("grain");
    const int vilBread = gs.villages[0].inventory.count("bread");
    CHECK(cityGrain > 0,
          "the caravan hauled the city what its snapshot said it LACKED");
    CHECK(vilBread > 0, "the caravan delivered the city's surplus bread");
    CHECK(recall(reg.get<AgentMemory>(e),
                 AgentMemoryKind::MarketSnapshot, 0) != nullptr,
          "the departure snapshot lives in the caravan's memory");
    const int grainTotal = cityGrain + bag.count("grain")
                           + gs.villages[0].inventory.count("grain");
    const int breadTotal = vilBread + bag.count("bread")
                           + gs.settlements[0].inventory.count("bread");
    CHECK(grainTotal == 500 && breadTotal == 2000,
          "CONSERVATION: cargo moves, it is never minted or dropped");
}

int main() {
    test_the_chop_is_real_and_the_haul_comes_home();
    test_the_farmer_works_the_field();
    test_farmer_without_terrain_conjures_nothing();
    test_agent_memory_is_bounded_and_current();
    test_the_caravan_trades_on_its_memory();
    return sm::test::report("woodcutter_gather_test");
}

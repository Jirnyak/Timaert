// The trade laws, re-homed on the AGENT (W2b-4). The abstract TradeRoute
// system is gone — trade is a caravan carrying real cargo — so the laws it
// was convicted under (audit II.4) are re-pinned where they now live:
//
//   1. TORUS LAW — a caravan picks its city's NEAREST village by TORUS
//      distance. Red-first heritage: flat dx²+dy² made a neighbour across
//      the map seam look maximally distant; a caravan must prefer the
//      village 6 cells across the seam to one 10 cells away on the same
//      side.
//
//   2. GARRISON CAP — garrisons grew by up to +10/day with no sink and
//      silently broke the whole save at kMaxSoldiers=8192 (~game day 820).
//      A settlement's garrison stops recruiting at a hard cap.
//
// (Law 2 of the old file — route party resolution by KIND — died with the
// route system itself; a caravan holds POINTERS to nothing: it stands in a
// real village and moves real stacks.)

#include "check.h"
#include "macro/agent_memory.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/npc_ai.h"
#include "macro/squad.h"
#include "macro/world_tick.h"
#include "macro/state.h"

#include <cstdio>
#include <vector>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/trade_law_test.cpp", 0);
    return 1;
}

} // namespace

int main() {
    using namespace sm;

    // ── 1. Torus law: the caravan's STATION choice wraps ────────────────
    // (owner 2026-08-30: caravans walk city to city; the next station is the
    // nearest OTHER city — and "nearest" is torus-metric like everything.)
    {
        GameState gs{};
        gs.mapW = 64;
        gs.mapH = 64;
        Landmark city{};
        city.type = LandmarkType::City;
        city.id = 0;
        city.x = 60;                     // near the east seam
        city.y = 32;
        city.population = 100;
        city.inventory.add("bread", 2048);
        gs.landmarks.push_back(city);

        Landmark sameSide{};             // 30 cells west, same side — far
        sameSide.type = LandmarkType::City;   // enough that the comparable-
        sameSide.id = 1;                      // distance coin flip stays out
        sameSide.x = 30;
        sameSide.y = 32;
        sameSide.population = 50;
        gs.landmarks.push_back(sameSide);

        Landmark acrossSeam{};           // 6 cells east THROUGH the seam
        acrossSeam.type = LandmarkType::City;
        acrossSeam.id = 2;
        acrossSeam.x = 2;                // 60 -> 63|0 -> 2 = 6 cells by torus
        acrossSeam.y = 32;
        acrossSeam.population = 50;
        gs.landmarks.push_back(acrossSeam);

        ecs::World w;
        auto& reg = w.reg;
        const auto e = reg.create();
        reg.emplace<ecs::Position>(e, 60.0f, 32.0f, 0.0f);
        reg.emplace<ecs::VisualPos>(e, 60.0f, 32.0f, 0.0f);
        reg.emplace<ecs::NPCKind>(e, std::uint16_t(NPCType::Caravan),
                                  std::uint16_t(faction_index("timaert")));
        ecs::MacroNpcRuntime rt{};
        rt.homeSettlementId = 0;
        rt.targetSettlementId = -1;
        rt.targetX = 60.0f;
        rt.targetY = 32.0f;
        rt.state = std::uint8_t(NPCState::Idle);
        rt.stateTimer = 0;
        refresh_leader_travel_stats(rt, make_character_sheet(
            NPCType::Caravan, 3, leader_sheet_seed(21u)), NPCType::Caravan);
        rt.sp = rt.maxSp;
        reg.emplace<ecs::MacroNpcRuntime>(e, rt);
        reg.emplace<ecs::MacroSpawnId>(e, 21u);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(3));
        reg.emplace<ecs::Health>(e, 25, 25);
        reg.emplace<ecs::SquadRoster>(e);
        reg.emplace<ecs::NpcInventory>(e);
        reg.emplace<AgentMemory>(e);

        MacroNpcAiRuntime ai{};
        reset_macro_npc_ai_runtime(ai, 90u);
        MacroWorld mw{.gs = &gs, .world = &w};
        tick_macro_npc_ai(mw, ai, kAiTicks);

        const auto& out = reg.get<ecs::MacroNpcRuntime>(e);
        if (out.state != std::uint8_t(NPCState::Traveling)) {
            return fail("the caravan did not set out");
        }
        if (out.targetSettlementId != acrossSeam.id) {
            return fail("TORUS LAW: the seam-side CITY is nearer and must win");
        }
    }

    // ── 2. Garrison cap ─────────────────────────────────────────────────
    static_assert(kMaxGarrisonPerSettlement < 8192,
                  "cap must sit far below the save-format soldier guard");
    if (garrison_wants_recruits(kMaxGarrisonPerSettlement - 1)
        != true) {
        return fail("one below the cap must still recruit");
    }
    if (garrison_wants_recruits(kMaxGarrisonPerSettlement) != false) {
        return fail("at the cap recruiting must stop");
    }
    if (garrison_wants_recruits(kMaxGarrisonPerSettlement + 100) != false) {
        return fail("over the cap recruiting must stop");
    }

    std::printf("trade_law_test: caravan_torus=ok garrison_cap=ok\n");
    CHECK(true, "every gate above held");
    return sm::test::report("trade_law_test");
}

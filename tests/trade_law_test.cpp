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
#include "macro/store.h"

#include <cstdio>
#include <vector>

namespace {

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
        city.inventory.add("food", 2048);
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

        // ЗАКОН ПИНАЕТСЯ ПРЯМО В СВОЮ ДВЕРЬ (2026-09-21): прежде его
        // водил ИИ каравана, а род каравана снесён — караван оказался
        // сквадом, притворившимся видом существа. Дверь та же, что ведёт
        // рейсы сбыта артелей сегодня.
        ecs::World w;
        auto wStore_ = sm::make_macro_store();
        sm::store_attach(w, wStore_.get());
        MacroWorld mw{.gs = &gs, .world = &w};
        TickContext ctx{};
        ctx.mw = mw;
        ctx.mapW = gs.mapW;
        ctx.mapH = gs.mapH;
        Rng roll(90u);
        ctx.rng = &roll;
        float sx = 0.0f, sy = 0.0f;
        const int pick = pick_next_station_(ctx, MacroPos{60.0f, 32.0f},
                                            /*currentId*/0, /*prevId*/-1,
                                            sx, sy);
        CHECK(pick >= 0, "the trade door named a station at all");
        CHECK(pick == acrossSeam.id,
              "ЗАКОН АДРЕСА: the city 6 cells away THROUGH the seam beats the "
              "one 30 cells away on the same side — the world is a connected "
              "torus, so flat dx²+dy² is the wrong metric for it");
    }

    // ── 2. Garrison target (§42 Инк 7: population >> the registry shift;
    // the flat 64-cap died with the tavern-pool law) ─────────────────────
    {
        const int pop = 1200;
        const int target =
            garrison_target_strength(LandmarkType::City, pop);   // 1200>>3
        // The expectation is DERIVED from the same law the code reads
        // (population >> the registry shift), never a remembered number.
        CHECK(target == pop >> 3,
              "the garrison target IS the registry law applied to population");
        CHECK(garrison_wants_recruits(LandmarkType::City, pop, target - 1),
              "one below the target, the place still recruits");
        CHECK(!garrison_wants_recruits(LandmarkType::City, pop, target),
              "AT the target recruiting stops — the cap binds, and it binds "
              "through the row, not through a clamp somewhere downstream");
        // NEGATIVE CONTROL of the row itself: a kind whose column says NONE
        // keeps no garrison at any population. Without it the three checks
        // above would also pass for a rule that just multiplies population.
        CHECK(garrison_target_strength(LandmarkType::Spire, 1000) == 0,
              "a kind whose column says none keeps no garrison at all");
    }

    return sm::test::report("trade_law_test");
}

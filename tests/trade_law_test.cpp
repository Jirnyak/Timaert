// Locks the three proven trade-network defects from audit.md II.4:
//
//   1. TORUS LAW — trade distance wrapped like every other system's distance.
//      The old code computed flat dx²+dy² on a toroidal map: a neighbour
//      across the map seam looked maximally distant and never traded.
//      Red-first proof: with the flat math this test's cross-seam
//      destination LOSES to a farther same-side one; with the torus math it
//      wins (it is physically closer).
//
//   2. PARTY RESOLUTION — settlements and villages number their ids from
//      zero in SEPARATE lists, and the old arrival resolver searched
//      settlements first: a village-origin route with id=k bound its origin
//      to settlements[k], so village export revenue was credited to an
//      unrelated city forever. Routes now carry origin/dest KIND and the
//      resolver dispatches by it.
//
//   3. GARRISON CAP — garrisons grew by up to +10/day with no sink and
//      silently broke the whole save at kMaxSoldiers=8192 (~game day 820).
//      A settlement's garrison now stops recruiting at a hard cap.

#include "macro/economy.h"
#include "macro/world_tick.h"
#include "macro/state.h"

#include <cstdio>
#include <vector>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL trade_law_test: %s\n", msg);
    return 1;
}

} // namespace

int main() {
    using namespace sm;

    // ── 1. Torus law ────────────────────────────────────────────────────
    {
        const int mapW = 100, mapH = 100;
        EconomyState originEco = create_economy_state({ResourceId::Grain});
        EconomyState nearAcrossSeam = create_economy_state();
        EconomyState farSameSide = create_economy_state();
        originEco.resources[std::size_t(ResourceId::Grain)] = 100.0f;
        originEco.resourcePrices[std::size_t(ResourceId::Grain)] = 1.0f;
        // Identical demand on both destinations — distance is the tiebreaker.
        nearAcrossSeam.resourcePrices[std::size_t(ResourceId::Grain)] = 2.0f;
        farSameSide.resourcePrices[std::size_t(ResourceId::Grain)] = 2.0f;

        TradeOrigin origin{/*id*/ 0, 5.0f, 50.0f, &originEco};
        std::vector<TradeDest> dests{
            {/*id*/ 1, 25.0f, 50.0f, &farSameSide},    // 20 cells away, no wrap
            {/*id*/ 2, 95.0f, 50.0f, &nearAcrossSeam}, // 10 cells across the seam
        };
        const TradeRoute route = find_best_trade_route(
            origin, dests, /*isVillage=*/true, /*day*/ 10, /*lastTrade*/ 0,
            mapW, mapH);
        if (!route.valid) return fail("profitable route not found");
        if (route.destId != 2) {
            std::fprintf(stderr, "chose destId=%d (flat distance?)\n",
                         route.destId);
            return fail("cross-seam neighbour must win: it is CLOSER on the torus");
        }
        // Travel time uses the wrapped distance too: 10 cells / 50 => 1 day.
        if (route.arrivalDay != 11) {
            std::fprintf(stderr, "arrivalDay=%d\n", route.arrivalDay);
            return fail("travel days must use the wrapped distance");
        }
    }

    // ── 2. Party resolution by KIND, not by bare id ─────────────────────
    {
        GameState gs{};
        gs.mapW = 64;
        gs.mapH = 64;
        Settlement city{};
        city.id = 0;
        city.eco = create_economy_state();
        gs.settlements.push_back(city);
        Village village{};
        village.id = 0;  // SAME number as the city — the collision in the wild
        village.eco = create_economy_state({ResourceId::Grain});
        gs.villages.push_back(village);

        TradeRoute route{};
        route.originId = 0;
        route.originIsVillage = true;
        route.destId = 0;
        route.destIsVillage = false;
        route.valid = true;

        const RouteParties parties = resolve_route_parties(gs, route);
        if (parties.origin != &gs.villages[0].eco) {
            return fail("village-origin route bound to a settlement's economy");
        }
        if (parties.dest != &gs.settlements[0].eco) {
            return fail("city-dest route did not bind to the settlement");
        }

        TradeRoute back{};
        back.originId = 0;
        back.originIsVillage = false;
        back.destId = 0;
        back.destIsVillage = true;
        back.valid = true;
        const RouteParties parties2 = resolve_route_parties(gs, back);
        if (parties2.origin != &gs.settlements[0].eco
            || parties2.dest != &gs.villages[0].eco) {
            return fail("reverse route bound to the wrong parties");
        }
    }

    // ── 3. Garrison cap ─────────────────────────────────────────────────
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

    std::printf("trade_law_test: torus=ok parties=ok garrison_cap=ok\n");
    return 0;
}

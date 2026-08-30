// The caravan↔village package deal (npc_ai.h trade_caravan_at_village,
// owner 2026-08-30): the caravan BUYS the city's lacks for coin and SELLS
// its exports for coin through the ONE stock-price law. The laws held here:
//
//   1. CONSERVATION — coin and every commodity are moved, never minted or
//      burned, across the two bags.
//   2. DIRECTION — raw flows village→hold, exports flow hold→village, and
//      coin settles exactly to (bought − sold) on the village side.
//   3. THE CLAMP — every unit changed hands inside the price law's own
//      [base/4 .. 4×base] corridor (economy.h stock_scarcity); relations,
//      not retuned literals.
//   4. NEGATIVE CONTROL — strip both wallets and the same deal moves
//      NOTHING: no coin, no confiscation. That control is the whole point
//      of the increment — the old loop hauled cargo without payment.
#include "check.h"

#include "macro/agent_memory.h"
#include "macro/commodity.h"
#include "macro/currency.h"
#include "macro/econ_day.h"
#include "macro/items.h"
#include "macro/npc_ai.h"
#include "macro/state.h"

namespace {

long long commodity_total(const sm::Inventory& a, const sm::Inventory& b,
                          const char* id) {
    return (long long)a.count(id) + b.count(id);
}

}  // namespace

int main() {
    // A starving village rich in grain, a caravan hold rich in bread and
    // coin — the baseline deadlock this deal exists to break.
    sm::Landmark village{};
    village.type = sm::LandmarkType::Village;
    village.population = 64;
    CHECK(village.inventory.add("grain", 500), "fixture: village grain");
    CHECK(village.inventory.add("coin_empire", 40),
          "fixture: village purse");

    sm::Inventory hold;
    CHECK(hold.add("bread", 50), "fixture: hold bread");
    CHECK(hold.add("coin_empire", 1000), "fixture: hold purse");

    // The home snapshot: a city with NOTHING — every commodity class 0, so
    // the buy half wants raw the honest way (through the snapshot door, not
    // through a test-only flag).
    sm::Inventory emptyCity;
    const sm::MemoryEntry snap =
        sm::pack_market_snapshot(emptyCity, 7, /*day=*/1);

    const long long coinBefore =
        sm::wallet_value(hold) + sm::wallet_value(village.inventory);
    const long long grainBefore =
        commodity_total(hold, village.inventory, "grain");
    const long long breadBefore =
        commodity_total(hold, village.inventory, "bread");
    const int villageCoinBefore = sm::wallet_value(village.inventory);
    const int villageGrainBefore = village.inventory.count("grain");
    const int holdBreadBefore = hold.count("bread");

    const sm::CaravanDeal deal = sm::trade_caravan_at_village(
        hold, /*capacityKg=*/1e6f, village, &snap);

    // 1. Conservation — the deal moved, it never created.
    CHECK(sm::wallet_value(hold) + sm::wallet_value(village.inventory)
              == coinBefore,
          "coin is conserved across the deal");
    CHECK(commodity_total(hold, village.inventory, "grain") == grainBefore,
          "grain is conserved across the deal");
    CHECK(commodity_total(hold, village.inventory, "bread") == breadBefore,
          "bread is conserved across the deal");

    // 2. Direction and settlement.
    const int grainMoved = villageGrainBefore - village.inventory.count("grain");
    const int breadMoved = holdBreadBefore - hold.count("bread");
    CHECK(grainMoved > 0, "the caravan bought raw from the village");
    CHECK(hold.count("grain") == grainMoved, "the raw landed in the hold");
    CHECK(breadMoved > 0, "the caravan sold bread to the village");
    CHECK(village.inventory.count("bread") == breadMoved,
          "the bread landed in the village");
    CHECK(deal.boughtValue > 0 && deal.soldValue > 0,
          "both halves of the deal paid");
    CHECK(sm::wallet_value(village.inventory) - villageCoinBefore
              == deal.boughtValue - deal.soldValue,
          "the village purse settled to bought minus sold exactly");

    // 3. The clamp corridor, derived from the same tables the code reads.
    const int grainBase = sm::item_def("grain")->value;
    const int breadBase = sm::item_def("bread")->value;
    CHECK(deal.boughtValue <= 4 * grainBase * grainMoved,
          "raw paid at most the 4x scarcity ceiling");
    CHECK(deal.boughtValue >= grainMoved * std::max(1, grainBase / 4),
          "raw paid at least the 1/4 abundance floor");
    CHECK(deal.soldValue <= 4 * breadBase * breadMoved,
          "bread paid at most the 4x scarcity ceiling");
    CHECK(deal.soldValue >= breadMoved * std::max(1, breadBase / 4),
          "bread paid at least the 1/4 abundance floor");

    // 4. Negative control: no money on either side → NOTHING moves. The old
    // confiscating loop fails exactly this check.
    sm::Landmark poor{};
    poor.type = sm::LandmarkType::Village;
    poor.population = 64;
    CHECK(poor.inventory.add("grain", 500), "fixture: poor village grain");
    sm::Inventory brokeHold;
    CHECK(brokeHold.add("bread", 50), "fixture: broke hold bread");
    const sm::CaravanDeal none = sm::trade_caravan_at_village(
        brokeHold, 1e6f, poor, &snap);
    CHECK(none.boughtValue == 0 && none.soldValue == 0,
          "no coin, no payment");
    CHECK(poor.inventory.count("grain") == 500,
          "no coin, no confiscation of raw");
    CHECK(brokeHold.count("bread") == 50,
          "no coin, no confiscation of bread");

    return sm::test::report("caravan_deal_test");
}

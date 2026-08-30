// Trading at a market (npc_ai.h, owner 2026-08-30): the caravan's STATION
// stop and the village VENDOR run — locality as law, both through the ONE
// stock-price law. Held here:
//
//   1. CONSERVATION — coin and every commodity move, never minted or burned.
//   2. THE STATION'S OWN BOUNDS — it sells only into a shortage (never past
//      the market's daily demand) and buys only the surplus (never the
//      market's living stock below its demand).
//   3. THE VENDOR — sells the whole load, then buys the home's lacks.
//   4. THE CLAMP — every unit priced inside [base/4 .. 4×base].
//   5. NEGATIVE CONTROL — a coinless market can buy nothing, and nothing is
//      confiscated from it either.
#include "check.h"

#include "macro/agent_memory.h"
#include "macro/commodity.h"
#include "macro/currency.h"
#include "macro/econ_day.h"
#include "macro/economy.h"
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
    using sm::EconSite;

    // ── The station stop ─────────────────────────────────────────────────
    // A city short of bread (none in store, pop 64 demands 64) and glutted
    // with wood (far above its demand). The caravan holds bread and coin.
    sm::Landmark city{};
    city.type = sm::LandmarkType::City;
    city.population = 64;
    CHECK(city.inventory.add("wood", 2000), "fixture: city wood glut");
    CHECK(city.inventory.add("coin_empire", 600), "fixture: city purse");

    sm::Inventory hold;
    CHECK(hold.add("bread", 200), "fixture: hold bread");
    CHECK(hold.add("coin_empire", 4000), "fixture: hold purse");

    const long long coinBefore =
        sm::wallet_value(hold) + sm::wallet_value(city.inventory);
    const long long woodBefore = commodity_total(hold, city.inventory, "wood");
    const long long breadBefore =
        commodity_total(hold, city.inventory, "bread");
    const int breadDemand = sm::daily_demand_for("bread", city.population,
                                                 EconSite::City);
    const int woodDemand = sm::daily_demand_for("wood", city.population,
                                                EconSite::City);

    // Charisma 0 here: the corridor checks below stay the raw price law's;
    // the sheet edge is asserted separately at the end.
    const sm::CaravanDeal st = sm::trade_caravan_at_station(
        hold, /*capacityKg=*/1e6f, city, /*charisma=*/0, /*bargaining=*/0);

    CHECK(sm::wallet_value(hold) + sm::wallet_value(city.inventory)
              == coinBefore,
          "station: coin is conserved");
    CHECK(commodity_total(hold, city.inventory, "wood") == woodBefore
              && commodity_total(hold, city.inventory, "bread")
                     == breadBefore,
          "station: goods are conserved");
    CHECK(st.soldValue > 0, "station: the shortage was sold into");
    CHECK(city.inventory.count("bread") > 0
              && city.inventory.count("bread") <= breadDemand,
          "station: sells only up to the market's own daily demand");
    CHECK(st.boughtValue > 0, "station: the surplus was bought");
    CHECK(city.inventory.count("wood") >= woodDemand,
          "station: never buys below the market's own demand");
    CHECK(hold.count("wood") > 0, "station: the surplus rode away");
    // The clamp corridor, derived from the same tables the code reads.
    const int woodBase = sm::item_def("wood")->value;
    const int breadBase = sm::item_def("bread")->value;
    const int woodMoved = hold.count("wood");
    const int breadMoved = city.inventory.count("bread");
    CHECK(st.boughtValue <= 4 * woodBase * woodMoved
              && st.boughtValue >= woodMoved * std::max(1, woodBase / 4),
          "station: surplus paid inside the price-law corridor");
    CHECK(st.soldValue <= 4 * breadBase * breadMoved
              && st.soldValue >= breadMoved * std::max(1, breadBase / 4),
          "station: shortage paid inside the price-law corridor");

    // ── The vendor run ───────────────────────────────────────────────────
    // A village crew brings grain to a town that lacks it; home lacks tools
    // (snapshot class 0), the town holds them.
    sm::Landmark town{};
    town.type = sm::LandmarkType::City;
    town.population = 64;
    CHECK(town.inventory.add("tools", 50), "fixture: town tools");
    CHECK(town.inventory.add("coin_empire", 2000), "fixture: town purse");

    sm::Inventory homeStore;   // the village store: grain-rich, tool-less
    CHECK(homeStore.add("grain", 5000), "fixture: home grain");
    const sm::MemoryEntry snap =
        sm::pack_market_snapshot(homeStore, 7, /*day=*/1);

    sm::Inventory bag;
    CHECK(bag.add("grain", 300), "fixture: vendor grain");

    const long long vCoinBefore =
        sm::wallet_value(bag) + sm::wallet_value(town.inventory);
    const sm::CaravanDeal vd = sm::trade_vendor_at_market(
        bag, 1e6f, town, &snap, /*homePopulation=*/50,
        EconSite::Village, /*charisma=*/0, /*bargaining=*/0);

    CHECK(sm::wallet_value(bag) + sm::wallet_value(town.inventory)
              == vCoinBefore,
          "vendor: coin is conserved");
    CHECK(bag.count("grain") == 0 && town.inventory.count("grain") == 300,
          "vendor: the whole load was sold");
    CHECK(vd.soldValue > 0, "vendor: the sale paid real coin");
    CHECK(bag.count("tools") > 0,
          "vendor: the earnings bought the home's lack");
    CHECK(vd.boughtValue > 0 && vd.boughtValue <= vd.soldValue
              + 0 /* the crew carried no purse of its own */,
          "vendor: purchases are funded by the sale alone");

    // ── Negative control: a coinless market buys nothing, loses nothing ──
    sm::Landmark broke{};
    broke.type = sm::LandmarkType::City;
    broke.population = 64;
    sm::Inventory bag2;
    CHECK(bag2.add("bread", 50), "fixture: control bread");
    const sm::CaravanDeal none = sm::trade_caravan_at_station(
        bag2, 1e6f, broke, 0, 0);
    CHECK(none.soldValue == 0 && bag2.count("bread") == 50,
          "no coin, no sale, no confiscation");

    // ── The sheet edge: a charismatic trader closes the same deal better ──
    // (owner 2026-08-30: «караван торгует выгоднее, потому что у него в
    // таблице выше уровень и харизма» — the deal reads the sheet, so two
    // identical fixtures differing ONLY in charisma must settle differently,
    // in the trader's favour on both halves.)
    const auto run_fixture = [](int cha) {
        sm::Landmark m{};
        m.type = sm::LandmarkType::City;
        m.population = 64;
        m.inventory.add("wood", 2000);
        m.inventory.add("coin_empire", 600);
        sm::Inventory h;
        h.add("bread", 200);
        h.add("coin_empire", 4000);
        return sm::trade_caravan_at_station(h, 1e6f, m, cha, 0);
    };
    const sm::CaravanDeal plain = run_fixture(0);
    const sm::CaravanDeal silver = run_fixture(10);
    CHECK(silver.soldValue >= plain.soldValue,
          "charisma never sells for less");
    CHECK(silver.boughtValue <= plain.boughtValue,
          "charisma never buys for more");
    CHECK(silver.soldValue > plain.soldValue
              || silver.boughtValue < plain.boughtValue,
          "the sheet edge is real: the same deal settles in the trader's favour");

    return sm::test::report("caravan_deal_test");
}

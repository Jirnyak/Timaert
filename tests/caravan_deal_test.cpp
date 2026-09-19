// Trading at a market (npc_ai.h, owner 2026-08-30): the caravan's STATION
// stop and the village VENDOR run — locality as law, both through the ONE
// stock-price law. Held here:
//
//   1. CONSERVATION — coin and every commodity move, never minted or burned.
//   2. THE STATION'S OWN BOUNDS — it sells only into a shortage (never past
//      the market's SEASONAL need, the price law's own horizon) and buys
//      only the surplus (never the market's living stock below that need).
//   3. THE VENDOR — sells the whole load, then buys the home's lacks.
//   4. THE PRICE BOUNDS — every lot priced between the floor (1/unit,
//      «ничто не бесплатно») and the empty-shelf ceiling of the SAME law
//      (the [0.25…4] corridor died 2026-09-18).
//   5. NEGATIVE CONTROL — a coinless market can buy nothing, and nothing is
//      confiscated from it either.
#include "macro/characters.h"   // landmark_sheet — руки места
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

    // ── The station stop ─────────────────────────────────────────────────
    // A city short of bread and glutted with wood. Под долгом (CANON S10)
    // «не хватает хлеба» = НЕПОГАШЕННЫЙ СЧЁТ: спрос кривой читает его
    // остаток, а проданный в город хлеб гасит счёт и СЪЕДАЕТСЯ на месте —
    // на полку ложится только излишек сверх счёта.
    sm::Landmark city{};
    city.type = sm::LandmarkType::City;
    city.population = 64;
    city.needDebt[sm::commodity_index("bread")] =
        city.population * sm::kDaysPerSeason;
    CHECK(city.inventory.add("wood", 2000), "fixture: city wood glut");
    // КОШЕЛЁК ФИКСТУРЫ ПОДНЯТ ДО НОВЫХ ЦЕН (S25, тот же переезд, что у
    // вендора и лесоруба на снятии коридора): без «домашней маржи» ×0.7
    // сделка стоит полную цену кривой, и тонкая казна доплачивала ТОВАРОМ
    // (pay_value_dense) — город отдавал назад только что купленный хлеб и
    // проваливался ниже собственной сезонной нужды по дровам. Свидетель
    // сторожит ТОРГОВЛЮ, поэтому его рынок обязан быть платёжеспособным;
    // сама находка — «оплата натурой ест сезонный амбар» — записана хвостом
    // в NEXT_SESSION, она старше этой правки и ею не лечится.
    CHECK(city.inventory.add("coin_empire_copper", 6000), "fixture: city purse");

    sm::Inventory hold;
    CHECK(hold.add("bread", 200), "fixture: hold bread");
    // Кошелёк кроет закупку дров МОНЕТОЙ (весь глут 2000 дров стоит 10000
    // по после-сделочной полке): оплата идёт по плотности ценности, и
    // тонкая монета доплачивала бы ХЛЕБОМ — город съедал бы его как платёж
    // (долг, S10) раньше, чем дойдёт хлебная строка сделки. Свидетель
    // сторожит ПРОДАЖУ в нужду, поэтому платёжное плечо — монета.
    CHECK(hold.add("coin_empire_copper", 12000), "fixture: hold purse");

    const long long coinBefore =
        sm::coin_census_value(hold) + sm::coin_census_value(city.inventory);
    const long long woodBefore = commodity_total(hold, city.inventory, "wood");
    const long long breadBefore =
        commodity_total(hold, city.inventory, "bread");
    const int breadDebtBefore =
        city.needDebt[sm::commodity_index("bread")];
    const int breadDemand = sm::season_demand_for(
        "bread", city.needDebt, city.population,
        sm::landmark_sheet(sm::LandmarkType::City).skills, &city.inventory);
    const int woodDemand = sm::season_demand_for(
        "wood", city.needDebt, city.population,
        sm::landmark_sheet(sm::LandmarkType::City).skills, &city.inventory);

    // РАВНЫЕ АНКЕТЫ (S25): обе стороны называют одну торговую силу, значит
    // наценки нет и границы ниже — границы САМОЙ кривой цены, без примеси
    // чьего-то преимущества. Перевес судится отдельной фикстурой в конце.
    const sm::CaravanDeal st = sm::trade_caravan_at_station(
        hold, /*capacityKg=*/1e6f, city,
        /*myTradePct=*/0, /*theirTradePct=*/0);

    CHECK(sm::coin_census_value(hold) + sm::coin_census_value(city.inventory)
              == coinBefore,
          "station: coin is conserved");
    // ХЛЕБ СОХРАНЯЕТСЯ СКВОЗЬ СЧЁТ: проданное в место гасит долг и
    // съедается (CANON S10) — исчезнувшее с полок равно погашенному,
    // единица в единицу.
    const int breadDebtPaid = breadDebtBefore
        - city.needDebt[sm::commodity_index("bread")];
    CHECK(commodity_total(hold, city.inventory, "wood") == woodBefore
              && commodity_total(hold, city.inventory, "bread")
                         + breadDebtPaid
                     == breadBefore,
          "station: goods are conserved (bread — through the bill)");
    CHECK(st.soldValue > 0, "station: the shortage was sold into");
    const int soldBread = int(breadBefore) - hold.count("bread");
    CHECK(soldBread > 0 && soldBread <= breadDemand,
          "station: sells only up to the market's own unpaid need");
    CHECK(breadDebtPaid > 0,
          "station: the sold bread paid the bill on the spot");
    CHECK(st.boughtValue > 0, "station: the surplus was bought");
    CHECK(city.inventory.count("wood") >= woodDemand,
          "station: never buys below the market's own need");
    CHECK(hold.count("wood") > 0, "station: the surplus rode away");
    // Price bounds derived from the SAME law the code reads: no unit is
    // free (floor 1) and no unit costs more than the empty-shelf price of
    // its own curve — a bound read off the door, never a recomputation.
    const int breadBase = sm::item_def("bread")->value;
    const int woodMoved = hold.count("wood");
    CHECK(st.boughtValue >= woodMoved,
          "station: even a glut lot is never free (floor 1/unit)");
    // Границы — той же кривой: ни одна единица не бесплатна (пол 1) и ни
    // одна не дороже цены ПУСТОЙ полки своей кривой. Прежний потолок нёс в
    // себе ×0.7 «домашней маржи» — она умерла вместе с домом у сделки
    // (S25), и при равных анкетах продажа доходит ровно до цены кривой.
    CHECK(st.soldValue <= (long long)soldBread
                              * sm::stock_price(breadBase, 0, breadDemand)
              && st.soldValue >= soldBread,
          "station: shortage paid inside the law's own bounds");

    // ── The vendor run ───────────────────────────────────────────────────
    // A village crew brings grain to a town that lacks it; home lacks tools
    // (snapshot class 0), the town holds them.
    sm::Landmark town{};
    town.type = sm::LandmarkType::City;
    town.population = 64;
    // Хлебный счёт не погашен — из него производный спрос на зерно (город
    // печёт); счёт по инструментам оплачен, полка с ними — ИЗЛИШЕК.
    town.needDebt[sm::commodity_index("bread")] =
        town.population * sm::kDaysPerSeason;
    CHECK(town.inventory.add("tools", 50), "fixture: town tools");
    // The purse covers the load at the SEASONAL famine price (the corridor
    // died 2026-09-18): a starving shelf prices near base × seasonal need,
    // and a fixture purse tuned to the old 4× ceiling could afford nothing —
    // which is the affordability law working, not the vendor failing.
    CHECK(town.inventory.add("coin_empire_copper", 20000), "fixture: town purse");

    sm::Inventory homeStore;   // the village store: grain-rich, tool-less
    CHECK(homeStore.add("food", 5000), "fixture: home grain");
    const sm::MemoryEntry snap =
        sm::pack_market_snapshot(homeStore, 7, /*day=*/1);

    sm::Inventory bag;
    CHECK(bag.add("food", 300), "fixture: vendor grain");

    const long long vCoinBefore =
        sm::coin_census_value(bag) + sm::coin_census_value(town.inventory);
    const sm::CaravanDeal vd = sm::trade_vendor_at_market(
        bag, 1e6f, town, &snap, /*homeDebt=*/nullptr,
        /*homePopulation=*/50,
        sm::landmark_sheet(sm::LandmarkType::Village).skills,
        /*myTradePct=*/0, /*theirTradePct=*/0);

    CHECK(sm::coin_census_value(bag) + sm::coin_census_value(town.inventory)
              == vCoinBefore,
          "vendor: coin is conserved");
    CHECK(bag.count("food") == 0 && town.inventory.count("food") == 300,
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
    // Фикстура сузилась до ПРОДАЖИ в нужду: под долгом (S10) дровяная
    // покупка платилась бы хлебом по плотности ценности, и хлеб съедался
    // бы платежом раньше своей строки — обе половины сделки по отдельности
    // сторожит станция выше, эдж-пин держит продажу.
    const auto run_fixture = [](int edge) {
        sm::Landmark m{};
        m.type = sm::LandmarkType::City;
        m.population = 64;
        m.needDebt[sm::commodity_index("bread")] =
            m.population * sm::kDaysPerSeason;
        // Казна с запасом НАД честной ценой лота (полный лот 200 хлеба в
        // голодный счёт ≈ 20 400): упрись оба варианта в одну и ту же
        // казну — эдж стал бы невидим (оба заплатили бы всё, что есть).
        m.inventory.add("coin_empire_copper", 30000);
        sm::Inventory h;
        h.add("bread", 200);
        // Перевес — РАЗНИЦА сил (S25): рынок назван нулём, караван — edge.
        return sm::trade_caravan_at_station(h, 1e6f, m, edge, 0);
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

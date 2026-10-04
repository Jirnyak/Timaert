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
#include "macro/labour.h"           // souls_home — рты места
#include "macro/roster_window.h"   // roster_bill — счёт по таблице

#include "macro/agent_memory.h"
#include "tables/commodity.h"
#include "macro/currency.h"
#include "macro/econ_day.h"
#include "macro/economy.h"
#include "macro/anketa.h"
#include "macro/npc_ai.h"
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/state.h"
#include "macro/store.h"

namespace {

long long commodity_total(const sm::Inventory& a, const sm::Inventory& b,
                          const char* id) {
    return (long long)a.count(id) + b.count(id);
}

// ── ФИКСТУРНОЕ МЕСТО = ТЕЛО, И ДРУГОГО МЕСТА НЕТ (ломтик F) ──────────────
// Плечо места — склад, счёт содержания, род, ординал — КОЛОНКИ его слота в
// MacroStore. «Половинчатого» места (строка без тела) больше не существует,
// поэтому все рынки этого свидетеля идут одной дверью рождения; сделке при
// этом нужен только store, мир — затем, чтобы тело знало свою клетку.
std::uint16_t fixture_place(sm::MacroStore& st, sm::GameState& gs,
                            sm::SquadType type) {
    return sm::birth_place(gs, st, type, 0, 0).slot;
}

// Счёт содержания места — колонка ростера его тела (roster.h): одна
// лестница на место и на отряд, байт в байт.
std::int32_t* need_debt(sm::MacroStore& st, std::uint16_t slot) {
    return st.roster[slot].needDebt;
}

sm::Inventory& shelf(sm::MacroStore& st, std::uint16_t slot) {
    return st.inventory[slot].inv;
}

}  // namespace

int main() {
    // ТЕЛА ФИКСТУРНЫХ МЕСТ (M-90 шаг 5): один store на весь свидетель,
    // мир — только затем, чтобы тело знало свою клетку.
    auto storePtr = sm::make_macro_store();
    sm::MacroStore& mstore = *storePtr;
    sm::GameState gsFix{};
    gsFix.mapW = 64;
    gsFix.mapH = 64;

    // ── The station stop ─────────────────────────────────────────────────
    // A city short of food and glutted with wood. Под долгом (CANON S10)
    // «не хватает хлеба» = НЕПОГАШЕННЫЙ СЧЁТ: спрос кривой читает его
    // остаток, а проданный в город хлеб гасит счёт и СЪЕДАЕТСЯ на месте —
    // на полку ложится только излишек сверх счёта.
    const std::uint16_t city = fixture_place(mstore, gsFix, sm::SquadType::City);
    // Души — ГОЛОВАМИ в инвентарь записи (v122; запись живёт вне мира, так
    // что паства-worked ей не нужна: торговля спрашивает РТЫ). Счёт еды —
    // `roster_bill` по таблице, а не «душа × сезон» литералом.
    sm::raise_flock_into_roster(shelf(mstore, city), 64);
    need_debt(mstore, city)[sm::commodity_index("food")] =
        sm::roster_bill(shelf(mstore, city)).board;
    CHECK(shelf(mstore, city).add("wood", 2000), "fixture: city wood glut");
    // КОШЕЛЁК ФИКСТУРЫ ПОДНЯТ ДО НОВЫХ ЦЕН (S25, тот же переезд, что у
    // вендора и лесоруба на снятии коридора): без «домашней маржи» ×0.7
    // сделка стоит полную цену кривой, и тонкая казна доплачивала ТОВАРОМ
    // (pay_value_dense) — город отдавал назад только что купленный хлеб и
    // проваливался ниже собственной сезонной нужды по дровам. Свидетель
    // сторожит ТОРГОВЛЮ, поэтому его рынок обязан быть платёжеспособным;
    // сама находка — «оплата натурой ест сезонный амбар» — записана хвостом
    // в NEXT_SESSION, она старше этой правки и ею не лечится.
    CHECK(shelf(mstore, city).add("coin_empire_copper", 6000), "fixture: city purse");

    sm::Inventory hold;
    CHECK(hold.add("food", 200), "fixture: hold food");
    // Кошелёк кроет закупку дров МОНЕТОЙ (весь глут 2000 дров стоит 10000
    // по после-сделочной полке): оплата идёт по плотности ценности, и
    // тонкая монета доплачивала бы ХЛЕБОМ — город съедал бы его как платёж
    // (долг, S10) раньше, чем дойдёт хлебная строка сделки. Свидетель
    // сторожит ПРОДАЖУ в нужду, поэтому платёжное плечо — монета.
    CHECK(hold.add("coin_empire_copper", 12000), "fixture: hold purse");

    const long long coinBefore =
        sm::coin_census_value(hold) + sm::coin_census_value(shelf(mstore, city));
    const long long woodBefore = commodity_total(hold, shelf(mstore, city), "wood");
    const long long foodBefore =
        commodity_total(hold, shelf(mstore, city), "food");
    const int foodDebtBefore =
        need_debt(mstore, city)[sm::commodity_index("food")];
    const int foodDemand = sm::season_demand_for(
        sm::item_index("food"), need_debt(mstore, city), sm::souls_home(mstore, city),
        sm::landmark_sheet(sm::SquadType::City).skills, &shelf(mstore, city));
    const int woodDemand = sm::season_demand_for(
        sm::item_index("wood"), need_debt(mstore, city), sm::souls_home(mstore, city),
        sm::landmark_sheet(sm::SquadType::City).skills, &shelf(mstore, city));

    // РАВНЫЕ АНКЕТЫ (S25): обе стороны называют одну торговую силу, значит
    // наценки нет и границы ниже — границы САМОЙ кривой цены, без примеси
    // чьего-то преимущества. Перевес судится отдельной фикстурой в конце.
    const sm::CaravanDeal st = sm::trade_caravan_at_station(
        mstore, hold, /*capacityKg=*/1e6f, city,
        /*myTradePct=*/0, /*theirTradePct=*/0);

    CHECK(sm::coin_census_value(hold) + sm::coin_census_value(shelf(mstore, city))
              == coinBefore,
          "station: coin is conserved");
    // ХЛЕБ СОХРАНЯЕТСЯ СКВОЗЬ СЧЁТ: проданное в место гасит долг и
    // съедается (CANON S10) — исчезнувшее с полок равно погашенному,
    // единица в единицу.
    const int foodDebtPaid = foodDebtBefore
        - need_debt(mstore, city)[sm::commodity_index("food")];
    CHECK(commodity_total(hold, shelf(mstore, city), "wood") == woodBefore
              && commodity_total(hold, shelf(mstore, city), "food")
                         + foodDebtPaid
                     == foodBefore,
          "station: goods are conserved (food — through the bill)");
    CHECK(st.soldValue > 0, "station: the shortage was sold into");
    const int soldFood = int(foodBefore) - hold.count("food");
    CHECK(soldFood > 0 && soldFood <= foodDemand,
          "station: sells only up to the market's own unpaid need");
    CHECK(foodDebtPaid > 0,
          "station: the sold food paid the bill on the spot");
    CHECK(st.boughtValue > 0, "station: the surplus was bought");
    CHECK(shelf(mstore, city).count("wood") >= woodDemand,
          "station: never buys below the market's own need");
    CHECK(hold.count("wood") > 0, "station: the surplus rode away");
    // Price bounds derived from the SAME law the code reads: no unit is
    // free (floor 1) and no unit costs more than the empty-shelf price of
    // its own curve — a bound read off the door, never a recomputation.
    const int foodBase = sm::item_def("food")->value;
    const int woodMoved = hold.count("wood");
    CHECK(st.boughtValue >= woodMoved,
          "station: even a glut lot is never free (floor 1/unit)");
    // Границы — той же кривой: ни одна единица не бесплатна (пол 1) и ни
    // одна не дороже цены ПУСТОЙ полки своей кривой. Прежний потолок нёс в
    // себе ×0.7 «домашней маржи» — она умерла вместе с домом у сделки
    // (S25), и при равных анкетах продажа доходит ровно до цены кривой.
    CHECK(st.soldValue <= (long long)soldFood
                              * sm::stock_price(foodBase, 0, foodDemand)
              && st.soldValue >= soldFood,
          "station: shortage paid inside the law's own bounds");

    // ── The vendor run ───────────────────────────────────────────────────
    // A village crew brings food to a town that lacks it; home lacks tools
    // (snapshot class 0), the town holds them.
    const std::uint16_t town = fixture_place(mstore, gsFix, sm::SquadType::City);
    sm::raise_flock_into_roster(shelf(mstore, town), 64);
    // Хлебный счёт не погашен — из него производный спрос на зерно (город
    // печёт); счёт по инструментам оплачен, полка с ними — ИЗЛИШЕК.
    need_debt(mstore, town)[sm::commodity_index("food")] =
        sm::roster_bill(shelf(mstore, town)).board;
    CHECK(shelf(mstore, town).add("tools", 50), "fixture: town tools");
    // The purse covers the load at the SEASONAL famine price (the corridor
    // died 2026-09-18): a starving shelf prices near base × seasonal need,
    // and a fixture purse tuned to the old 4× ceiling could afford nothing —
    // which is the affordability law working, not the vendor failing.
    CHECK(shelf(mstore, town).add("coin_empire_copper", 20000), "fixture: town purse");

    // ДОМ — САМО МЕСТО: зерна навалом, инструментов нет. Ведомость-кэш
    // уничтожена 2026-09-30 (ломтик E шаг 2), и сделка читает дом теми же
    // двумя дверьми, которыми ведомость и выписывалась.
    sm::GameState hgs{};
    hgs.mapW = 64;
    hgs.mapH = 64;
    // Дом идёт ТОЙ ЖЕ дверью, что рынки: ломтиком F второй двери рождения
    // места не существует вовсе.
    const sm::MacroHandle home =
        sm::birth_place(hgs, mstore, sm::SquadType::Village, 0, 0);
    sm::raise_flock_into_roster(shelf(mstore, home.slot), 50);
    CHECK(shelf(mstore, home.slot).add("food", 5000), "fixture: home food");

    sm::Inventory bag;
    CHECK(bag.add("food", 300), "fixture: vendor food");

    const long long vCoinBefore =
        sm::coin_census_value(bag) + sm::coin_census_value(shelf(mstore, town));
    const sm::CaravanDeal vd = sm::trade_vendor_at_market(
        mstore, bag, 1e6f, town, home,
        /*myTradePct=*/0, /*theirTradePct=*/0);

    CHECK(sm::coin_census_value(bag) + sm::coin_census_value(shelf(mstore, town))
              == vCoinBefore,
          "vendor: coin is conserved");
    // Проданное в место с непогашенным счётом гасится и СЪЕДАЕТСЯ в дверях
    // прихода (CANON S10), поэтому «весь груз продан» читается суммой полки
    // и оплаченного счёта — с 2026-09-20 голодная строка и есть пища, и
    // привезённое зерно ложится ровно в тот счёт, который город не покрыл.
    // Счёт, который город НЕ покрыл, читается той же дверью, что его
    // выставила (roster_bill по головам) — ни одного пересказанного числа.
    const int townDebtPaid = sm::roster_bill(shelf(mstore, town)).board
        - need_debt(mstore, town)[sm::commodity_index("food")];
    CHECK(bag.count("food") == 0
              && shelf(mstore, town).count("food") + townDebtPaid == 300,
          "vendor: the whole load was sold");
    CHECK(vd.soldValue > 0, "vendor: the sale paid real coin");
    CHECK(bag.count("tools") > 0,
          "vendor: the earnings bought the home's lack");
    CHECK(vd.boughtValue > 0 && vd.boughtValue <= vd.soldValue
              + 0 /* the crew carried no purse of its own */,
          "vendor: purchases are funded by the sale alone");

    // ── НАСОС ВЫКЛЮЧЕН: дом, тонущий в хлебе, хлеба НЕ покупает ─────────
    // Закон, ради которого ярус 2 и строился (CANON S10, 2026-09-20). Пока
    // запас дома читался 4-битным КЛАССОМ памяти крю с потолком «много =
    // 4096», город с 45 млн хлеба при сезонной нужде 80 640 выглядел
    // голодным (цена дома ≈ база × 19.7), и крю честно скупало хлеб, чтобы
    // везти ДОМОЙ, — мир качал хлеб ВВЕРХ. Закон пережил снос ведомости:
    // точный склад дома теперь читается живьём в точке сделки.
    //
    // СВИДЕТЕЛЬ — ПАРА, А НЕ ОДИНОЧНАЯ ПРОВЕРКА, и это принципиально: «крю
    // не купило хлеба» верно и у немой фикстуры (дома нет, рынок пуст,
    // кошелёк пуст), то есть одиночная проверка зеленела бы по любой из
    // трёх посторонних причин. Поэтому тот же крю на ТОМ ЖЕ рынке с ТЕМ ЖЕ
    // грузом сводится с двумя домами, и утверждается РАЗНИЦА.
    {
        const auto buy_home_food = [&](int homeFood) {
            const std::uint16_t hm =
                fixture_place(mstore, gsFix, sm::SquadType::City);
            sm::raise_flock_into_roster(shelf(mstore, hm), 2520);
            // Счёт сезона ВЫСТАВЛЕН целиком у ОБОИХ — довод «у него же есть
            // нужда» снят заранее: нужда есть, и гора всё равно делает хлеб
            // дешёвым дома.
            need_debt(mstore, hm)[sm::commodity_index("food")] =
                sm::roster_bill(shelf(mstore, hm)).board;
            if (homeFood > 0)
                CHECK(shelf(mstore, hm).add("food", homeFood),
                      "fixture: the home's shelf");

            const std::uint16_t mkt =
                fixture_place(mstore, gsFix, sm::SquadType::City);
            sm::raise_flock_into_roster(shelf(mstore, mkt), 64);
            CHECK(shelf(mstore, mkt).add("food", 4000), "fixture: market food");
            CHECK(shelf(mstore, mkt).add("coin_empire_copper", 20000),
                  "fixture: market purse");

            sm::Inventory crew;
            CHECK(crew.add("food", 300), "fixture: crew load");
            sm::trade_vendor_at_market(mstore, crew, 1e6f, mkt,
                                       sm::handle_at(mstore, hm),
                                       /*myTradePct=*/0,
                                       /*theirTradePct=*/0);
            return crew.count("food");
        };
        const int boughtStarving = buy_home_food(0);
        const int boughtGlutted  = buy_home_food(45000000);
        CHECK(boughtStarving > 0,
              "негативный контроль: голодный дом хлеб ПОКУПАЕТ — значит "
              "рынок, кошелёк и сделка в этой фикстуре живы");
        CHECK(boughtGlutted == 0,
              "the food mountain buys no food: the pump is off");
    }

    // ── БЕЗ ДОМА КРЮ НЕ ГАДАЕТ ───────────────────────────────────────────
    // У крю нет дома — значит нет и того, чьи нужды оно бы закрывало. Оно
    // обязано продать и уехать с выручкой, а не покупать вслепую (CANON
    // S10, ярус 1 — торговля стоит и без знания).
    {
        const std::uint16_t mkt =
            fixture_place(mstore, gsFix, sm::SquadType::City);
        sm::raise_flock_into_roster(shelf(mstore, mkt), 64);
        CHECK(shelf(mstore, mkt).add("tools", 50), "fixture: unlit market tools");
        CHECK(shelf(mstore, mkt).add("coin_empire_copper", 20000),
              "fixture: unlit market purse");
        sm::Inventory bag4;
        CHECK(bag4.add("food", 300), "fixture: unlit crew load");
        const sm::CaravanDeal d = sm::trade_vendor_at_market(
            mstore, bag4, 1e6f, mkt, sm::MacroHandle{},
            /*myTradePct=*/0, /*theirTradePct=*/0);
        CHECK(d.soldValue > 0 && d.boughtValue == 0
                  && bag4.count("tools") == 0,
              "no home, no guessing: the crew sells and rides home");
    }

    // ── Negative control: a coinless market buys nothing, loses nothing ──
    const std::uint16_t broke = fixture_place(mstore, gsFix, sm::SquadType::City);
    sm::raise_flock_into_roster(shelf(mstore, broke), 64);
    sm::Inventory bag2;
    CHECK(bag2.add("food", 50), "fixture: control food");
    const sm::CaravanDeal none = sm::trade_caravan_at_station(
        mstore, bag2, 1e6f, broke, 0, 0);
    CHECK(none.soldValue == 0 && bag2.count("food") == 50,
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
    const auto run_fixture = [&](int edge) {
        const std::uint16_t m = fixture_place(mstore, gsFix, sm::SquadType::City);
        sm::raise_flock_into_roster(shelf(mstore, m), 64);
        need_debt(mstore, m)[sm::commodity_index("food")] =
            sm::roster_bill(shelf(mstore, m)).board;
        // Казна с запасом НАД честной ценой лота (полный лот 200 хлеба в
        // голодный счёт ≈ 20 400): упрись оба варианта в одну и ту же
        // казну — эдж стал бы невидим (оба заплатили бы всё, что есть).
        shelf(mstore, m).add("coin_empire_copper", 30000);
        sm::Inventory h;
        h.add("food", 200);
        // Перевес — РАЗНИЦА сил (S25): рынок назван нулём, караван — edge.
        return sm::trade_caravan_at_station(mstore, h, 1e6f, m, edge, 0);
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

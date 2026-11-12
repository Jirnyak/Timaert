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
#include "macro/characters.h"   // squad_birth — руки рода
#include <memory>
#include "check.h"
#include "macro/labour.h"           // souls_home — рты места
#include "macro/upkeep_window.h"   // upkeep_bill — счёт по таблице

#include "tables/commodity.h"
#include "macro/currency.h"
#include "macro/econ_day.h"
#include "macro/economy.h"
#include "bus/anketa.h"
#include "macro/deal.h"   // kNoCarryLimitG — «предела нет» живёт у двери deal
#include "macro/npc_ai.h"
#include "macro/birth_squad.h"   // birth_squad — место родится ТЕЛОМ
#include "macro/state.h"
#include "bus/store.h"

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
std::uint16_t fixture_rooted(sm::MacroStore& st, sm::GameState& gs,
                            sm::SquadType type) {
    return sm::birth_squad(gs, nullptr, st, type, 0, 0).slot;
}

// Счёт содержания места — колонка контейнера его тела (creatures.h): одна
// лестница на место и на отряд, байт в байт.
std::int32_t* need_debt(sm::MacroStore& st, std::uint16_t slot) {
    return st.upkeep[slot].needDebt;
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
    std::unique_ptr<sm::GameState> gsFixOwn = sm::make_game_state();
    sm::GameState& gsFix = *gsFixOwn;
    gsFix.mapW = 64;
    gsFix.mapH = 64;

    // ── The station stop ─────────────────────────────────────────────────
    // A city short of food and glutted with wood. Под долгом (CANON S10)
    // «не хватает хлеба» = НЕПОГАШЕННЫЙ СЧЁТ: спрос кривой читает его
    // остаток, а проданный в город хлеб гасит счёт и СЪЕДАЕТСЯ на месте —
    // на полку ложится только излишек сверх счёта.
    const std::uint16_t city = fixture_rooted(mstore, gsFix, sm::SquadType::City);
    // Души — ГОЛОВАМИ в инвентарь записи (v122; запись живёт вне мира, так
    // что паства числом фичи ей не нужна: торговля спрашивает РТЫ). Счёт еды —
    // `upkeep_bill` по таблице, а не «душа × сезон» литералом.
    sm::raise_flock_into_container(shelf(mstore, city), 64);
    need_debt(mstore, city)[sm::commodity_index("food")] =
        sm::upkeep_bill(shelf(mstore, city)).board;
    CHECK(shelf(mstore, city).add("wood", 2000), "fixture: city wood glut");
    // КОШЕЛЁК ФИКСТУРЫ ПОДНЯТ ДО НОВЫХ ЦЕН (S25, тот же переезд, что у
    // вендора и лесоруба на снятии коридора): без «домашней маржи» ×0.7
    // сделка стоит полную цену кривой, и тонкая казна доплачивала ТОВАРОМ
    // (pay_value_dense) — город отдавал назад только что купленный хлеб и
    // проваливался ниже собственной сезонной нужды по дровам. Свидетель
    // сторожит ТОРГОВЛЮ, поэтому его рынок обязан быть платёжеспособным;
    // сама находка — «оплата натурой ест сезонный амбар» — записана хвостом
    // в NEXT_SESSION, она старше этой правки и ею не лечится.
    // КАЗНА С ЗАПАСОМ НАД ЦЕНОЙ ЛОТА: упрись сделка в кошелёк рынка, размер
    // лота назвал бы кошелёк, а не закон купца, — а сторожится здесь закон.
    CHECK(shelf(mstore, city).add("coin_empire_copper", 600000), "fixture: city purse");

    // Трейдеры фикстур — высокие слоты стора (места рождаются снизу
    // эмитентом): сделка адресуется СЛОТАМИ (deal.h, ломоть 2 M-72).
    const std::uint16_t holdSlot = 16000;
    sm::Inventory& hold = mstore.inventory[holdSlot].inv;
    CHECK(hold.add("food", 200), "fixture: hold food");
    // Кошелёк кроет закупку дров МОНЕТОЙ (весь глут 2000 дров стоит 10000
    // по после-сделочной полке): оплата идёт по плотности ценности, и
    // тонкая монета доплачивала бы ХЛЕБОМ — город съедал бы его как платёж
    // (долг, S10) раньше, чем дойдёт хлебная строка сделки. Свидетель
    // сторожит ПРОДАЖУ в нужду, поэтому платёжное плечо — монета.
    CHECK(hold.add("coin_empire_copper", 12000), "fixture: hold purse");

    const int holdValueBefore = sm::inventory_value(hold);
    const long long coinBefore =
        sm::coin_census_value(hold) + sm::coin_census_value(shelf(mstore, city));
    const long long woodBefore = commodity_total(hold, shelf(mstore, city), "wood");
    const long long foodBefore =
        commodity_total(hold, shelf(mstore, city), "food");
    const int foodDebtBefore =
        need_debt(mstore, city)[sm::commodity_index("food")];
    const int foodDemand = sm::season_demand_for(
        sm::item_index("food"), need_debt(mstore, city), sm::souls_home(mstore, city),
        sm::squad_birth(sm::SquadType::City).sheet.skills, &shelf(mstore, city));
    const int woodDemand = sm::season_demand_for(
        sm::item_index("wood"), need_debt(mstore, city), sm::souls_home(mstore, city),
        sm::squad_birth(sm::SquadType::City).sheet.skills, &shelf(mstore, city));

    // РАВНЫЕ АНКЕТЫ (S25): обе стороны называют одну торговую силу, значит
    // наценки нет и границы ниже — границы САМОЙ кривой цены, без примеси
    // чьего-то преимущества. Перевес судится отдельной фикстурой в конце.
    const sm::CaravanDeal st = sm::trade_caravan_at_station(
        mstore, holdSlot, /*capacityG=*/sm::kNoCarryLimitG, city,
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
    CHECK(soldFood > 0 && soldFood < std::min(200, foodDemand),
          "station: ЛОТ КУПЦА — продано меньше и груза, и непогашенной нужды: "
          "лот «до самой нужды» прибыли не несёт");
    // ЦЕЛЬ СДЕЛКИ — СТОИМОСТЬ ТРЮМА (владелец 2026-11-12): она выросла.
    CHECK(sm::inventory_value(hold) > holdValueBefore,
          "station: абсолютная стоимость трюма купца ВЫРОСЛА — продал дороже "
          "базы, взял дешевле");
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

    // ── ЗАКОН ЛОТА КУПЦА (`trader_lot@src/macro/economy.h`) ───────────────
    // Здесь стоял «рейс сбыта деревни» — вторая сделка, читавшая склад и счёт
    // ДОМА; она снесена 2026-11-12 (M-478), и с ней ушли её свидетели. Законы,
    // которые они держали сверх своей сделки, — сохранение монеты и товара —
    // держит станция выше.
    //
    // ЯКОРЬ: лот купца есть ЛОТ НАИБОЛЬШЕЙ ПРИБЫЛИ, и проверяется это НЕ его
    // же формулой, а полным перебором через дверь цены: прибыль лота n есть
    // n × (цена единицы после сделки − база), и никакой другой размер лота в
    // пределах дефицита (завала) не приносит больше.
    {
        const auto gain = [](int base, int have, int demand, int n,
                             bool selling) {
            const long long unit = sm::lot_unit_price(base, have, demand, n,
                                                      selling, 0, 0);
            return selling ? n * (unit - base) : n * (base - unit);
        };
        constexpr int kBase = 1000;   // крупная база: округление цены до
                                      // монеты не размывает максимум
        // Продажа в дефицит: полка 99, нужда 399.
        const int sellLot = sm::trader_lot(kBase, 99, 399, /*selling=*/true,
                                           0, 0, /*cap=*/1 << 20);
        long long bestSell = 0;
        int rangeSell = 0;
        for (int n = 1; n <= 399 - 99; ++n, ++rangeSell)
            bestSell = std::max(bestSell, gain(kBase, 99, 399, n, true));
        CHECK(rangeSell == 300 && sellLot > 0
                  && gain(kBase, 99, 399, sellLot, true) == bestSell,
              "ЛОТ КУПЦА (продажа): из всех лотов в пределах дефицита он несёт "
              "наибольшую прибыль — перебором через дверь цены");
        CHECK(gain(kBase, 99, 399, 399 - 99, true) <= 0,
              "лот «до самой нужды» прибыли не несёт: его цена после сделки "
              "равна базе");
        CHECK(99 + sellLot < 399,
              "и полку до нужды он не доводит — остаток достаётся следующему");
        // Покупка из завала: полка 399, нужда 99.
        const int buyLot = sm::trader_lot(kBase, 399, 99, /*selling=*/false,
                                          0, 0, /*cap=*/1 << 20);
        long long bestBuy = 0;
        int rangeBuy = 0;
        for (int n = 1; n <= 399 - 99; ++n, ++rangeBuy)
            bestBuy = std::max(bestBuy, gain(kBase, 399, 99, n, false));
        CHECK(rangeBuy == 300 && buyLot > 0
                  && gain(kBase, 399, 99, buyLot, false) == bestBuy,
              "ЛОТ КУПЦА (покупка): наибольшая прибыль среди всех лотов завала");
        CHECK(399 - buyLot > 99,
              "и ниже нужды рынка полку он не опускает");
        // Кап держится, и сквозь него лот остаётся выгодным.
        CHECK(sm::trader_lot(kBase, 99, 399, true, 0, 0, 7) == 7,
              "кап лота (груз, трюм) режет лот, а не отменяет его");
        // ВСЯКИЙ ЛОТ СТРОГО ВЫГОДЕН И НЕ ПЕРЕЛЕТАЕТ РАВНОВЕСИЕ — по сетке
        // полок, нужд, баз (включая копеечную, где цена округляется до базы)
        // и разниц торговых сил.
        int samples = 0, lots = 0, bad = 0;
        for (int base : {1, 3, 10, 1000}) {
            for (int have = 0; have <= 40; ++have) {
                for (int demand = 0; demand <= 120; ++demand) {
                    for (int edge : {-20, 0, 20}) {
                        for (bool selling : {true, false}) {
                            ++samples;
                            const int n = sm::trader_lot(
                                base, have, demand, selling, edge, 0, 1 << 20);
                            if (n < 0) ++bad;
                            if (n <= 0) continue;
                            ++lots;
                            const int unit = sm::lot_unit_price(
                                base, have, demand, n, selling, edge, 0);
                            if (selling ? unit <= base : unit >= base) ++bad;
                            if (!selling && n > have) ++bad;
                            // При равных силах торгуют только в дефицит
                            // (завал), и равновесие не перелетают. Перевес
                            // сил сдвигает оптимум за нужду — границы на это
                            // нет нарочно (вывод — у `trader_lot`).
                            if (edge == 0
                                && (selling ? have + n > demand
                                            : have - n < demand))
                                ++bad;
                        }
                    }
                }
            }
        }
        CHECK(samples > 0 && lots > 0 && bad == 0,
              "сетка лотов: каждый строго выгоден, не больше полки, а при "
              "равных силах не перелетает нужду рынка");
    }

    // ── СТРОКА В ОДНОМ ВИЗИТЕ ИДЁТ В ОДНУ СТОРОНУ ────────────────────────
    // Насос монеты, найденный ревью: перевес торговых сил даёт сильному
    // отрицательный спред против слабого рынка — он продаёт строку сверх
    // нужды и той же сделкой выкупает её дешевле. Проявиться он может только
    // на строке, приход которой НЕ СЪЕДАЕТСЯ счётом (иначе полка не растёт):
    // это строка с ПРОИЗВОДНЫМ спросом — сырьё рецепта, чей выход рынку
    // нужен. Свидетель её не угадывает, а находит: рынку выставлен счёт на
    // инструмент, и берётся первая строка без своего счёта, на которую от
    // этого лёг спрос.
    {
        const std::uint16_t m = fixture_rooted(mstore, gsFix, sm::SquadType::City);
        sm::raise_flock_into_container(shelf(mstore, m), 64);
        const sm::Skills& hands =
            sm::squad_birth(sm::SquadType::City).sheet.skills;
        need_debt(mstore, m)[sm::commodity_index("tools")] = 400;
        int rawIdx = -1, rawNeed = 0;
        for (int c = 0; c < sm::kCommodityCount && rawIdx < 0; ++c) {
            if (need_debt(mstore, m)[c] != 0) continue;   // свой счёт — съест
            const int id = sm::commodity_item_index(c);
            const int need = sm::season_demand_for(
                id, need_debt(mstore, m), sm::souls_home(mstore, m), hands,
                &shelf(mstore, m));
            if (need > 16) { rawIdx = id; rawNeed = need; }
        }
        CHECK(rawIdx >= 0,
              "предусловие: у рынка нашлась строка с производным спросом — "
              "сырьё, приход которого счётом не съедается");
        if (rawIdx >= 0) {
            const int shelfBefore = rawNeed - 8;   // недостаёт восьми
            CHECK(shelf(mstore, m).add_of(rawIdx, shelfBefore),
                  "fixture: raw shelf");
            CHECK(shelf(mstore, m).add("coin_empire_copper", 600000),
                  "fixture: market purse");
            const std::uint16_t hSlot = 16006;
            sm::Inventory& h = mstore.inventory[hSlot].inv;
            constexpr int kHoldRaw = 400;
            CHECK(h.add_of(rawIdx, kHoldRaw), "fixture: hold raw");
            const sm::CaravanDeal d = sm::trade_caravan_at_station(
                mstore, hSlot, sm::kNoCarryLimitG, m,
                /*myTradePct=*/20, /*theirTradePct=*/0);
            const int soldRaw = kHoldRaw - h.count_of(rawIdx);
            CHECK(d.soldValue > 0 && soldRaw > 8,
                  "предусловие насоса встретилось: сильный купец продал "
                  "БОЛЬШЕ, чем рынку недоставало, — полка стала завалом");
            CHECK(shelf(mstore, m).count_of(rawIdx) == shelfBefore + soldRaw,
                  "ОДНА СТОРОНА ЗА ВИЗИТ: проданное осталось у рынка — в ту "
                  "же сделку оно обратно не выкуплено");
            CHECK(d.boughtValue == 0,
                  "и монета назад не ушла: брать у этого рынка было нечего, "
                  "кроме только что проданного");
        }
    }

    // ── Negative control: a coinless market buys nothing, loses nothing ──
    const std::uint16_t broke = fixture_rooted(mstore, gsFix, sm::SquadType::City);
    sm::raise_flock_into_container(shelf(mstore, broke), 64);
    const std::uint16_t bag2Slot = 16004;
    sm::Inventory& bag2 = mstore.inventory[bag2Slot].inv;
    CHECK(bag2.add("food", 50), "fixture: control food");
    const sm::CaravanDeal none = sm::trade_caravan_at_station(
        mstore, bag2Slot, sm::kNoCarryLimitG, broke, 0, 0);
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
        const std::uint16_t m = fixture_rooted(mstore, gsFix, sm::SquadType::City);
        sm::raise_flock_into_container(shelf(mstore, m), 64);
        need_debt(mstore, m)[sm::commodity_index("food")] =
            sm::upkeep_bill(shelf(mstore, m)).board;
        // Казна с запасом НАД честной ценой лота (полный лот 200 хлеба в
        // голодный счёт ≈ 20 400): упрись оба варианта в одну и ту же
        // казну — эдж стал бы невидим (оба заплатили бы всё, что есть).
        shelf(mstore, m).add("coin_empire_copper", 30000);
        const std::uint16_t hSlot = 16005;
        sm::Inventory& h = mstore.inventory[hSlot].inv;
        h = sm::Inventory{};   // лямбда зовётся дважды — слот чистится
        h.add("food", 200);
        // Перевес — РАЗНИЦА сил (S25): рынок назван нулём, караван — edge.
        return sm::trade_caravan_at_station(mstore, hSlot, sm::kNoCarryLimitG,
                                            m, edge, 0);
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

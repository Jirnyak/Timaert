// The price-from-stock law (starcluster, W2d final) and the death of
// arbitrage. Pinned:
//   · scarcity shape — absence is dear, glut is cheap, NO corridor (the
//     [0.25…4] clamp died 2026-09-18: it did not guard against the generous
//     merchant, it CREATED him — slippage vanishes only at saturation), and
//     the horizon is the SEASON (S19.2: the world eats once a season, so
//     stock == a season's need ⇔ price == base — the readable equilibrium);
//   · demand comes from the ONE needs ladder;
//   · ARBITRAGE IS EXTINCT: a buy-then-sell round trip can never profit,
//     for any stock, amount, charisma and context multipliers — including
//     the generous-merchant pair (0.9 buy / 1.2 sell) that once yielded
//     buy-81/sell-92 infinite money (audit III.2). The killer is the
//     SLIPPAGE both sides pay: a buy prices the shelf it leaves behind
//     (scarcer, dearer), a sell prices the glut it creates (cheaper).
#include "macro/characters.h"   // landmark_sheet — руки места
#include "check.h"

#include "macro/economy.h"
#include "macro/econ_day.h"
#include "macro/currency.h"

namespace {
// Руки города и деревни — анкета их рода (characters.h), а не вид места.
static const sm::Skills& CITY =
    sm::landmark_sheet(sm::LandmarkType::City).skills;

using namespace sm;

void test_scarcity_shape() {
    CHECK(stock_scarcity(0, 0) == 1.0f, "empty shelf, no demand = base");
    CHECK(stock_scarcity(1000, 8) < stock_scarcity(10, 8),
          "glut is cheaper than a modest stock");
    // РАВНОВЕСИЕ БЕЗ КАЛЕНДАРЯ (CANON S10, долг 2026-09-19): спрос приходит
    // сезонным ЧИСЛОМ (остаток счёта), и «склад == непокрытая нужда» даёт
    // базу без единого множителя горизонта в этой двери.
    CHECK(stock_scarcity(100, 100) == 1.0f,
          "stock == the unpaid need = base price (the world's equilibrium)");
    CHECK(stock_scarcity(0, 100) == float(100 + 1),
          "hungry absence prices the whole unpaid need — no ceiling");
    CHECK(stock_scarcity(1 << 20, 0) < stock_scarcity(1 << 10, 0),
          "a deeper glut keeps getting cheaper — no floor on the curve");
    CHECK(stock_price(10, 0, 100) == 10 * (100 + 1),
          "price = base x scarcity");
    CHECK(stock_price(10, 1 << 20, 0) >= 1,
          "a price never reaches zero — «ничто не бесплатно» is the floor");
    // Без счёта (nullptr) прямая часть — лестница населения × сезон; со
    // счётом она читала бы ОСТАТОК долга (закон мира, юниты те же).
    CHECK(season_demand_for(hunger_item_id(), nullptr, 128, CITY, nullptr)
                  == 128 * kDaysPerSeason
              && season_demand_for("cloth", nullptr, 128, CITY, nullptr)
                  == 4 * kDaysPerSeason
              && season_demand_for("wpn_dagger", nullptr, 128, CITY, nullptr)
                  == 0,
          "demand reads the ONE needs ladder");

    // НИ ОДНО БЛАГО НЕ ВАРИТСЯ ИЗ ПИЩИ — сквозной закон по ВСЕЙ таблице
    // составов (владелец, 2026-09-20; ради него и заведено ВОЛОКНО со своей
    // парцеллой). ПОЧЕМУ ЭТО ЗАКОН, А НЕ ВКУС: пока ткань пряли из зерна,
    // любое давление труда в сторону благ съедало хлеб мира. Измерено в день,
    // когда хлеб вырезали и городские руки остались без дела: ткани ×40,
    // съедено ртами 13.8 М → 11.2 М при ВДВОЕ большей добыче, голодавших в
    // городах 306 → 92 901. Свидетель стоит на ВСЕЙ таблице, потому что
    // следующая заглушка («мясо в клей», «зерно в бумагу») придёт другой
    // строкой, и поймать её должен закон, а не проверка про ткань.
    {
        const int hungerIdx = item_index(hunger_item_id());
        bool anyGoods = false, foodFree = true;
        for (int c = 0; c < kCommodityCount; ++c) {
            const int idx = item_index(kCommodities[c].id);
            const ItemDef* d = item_def_at(idx);
            if (!d || d->type != ItemType::Goods) continue;
            anyGoods = true;
            for (const ItemPart& part : item_parts(idx)) {
                if (int(part.def) == hungerIdx) foodFree = false;
            }
        }
        CHECK(anyGoods, "the sweep actually saw goods rows (negative control)");
        CHECK(foodFree,
              "no comfort good is spun from the hunger row — the world must "
              "not be able to weave its bread into shirts");
    }
    // СПРОС ЧИТАЕТ ДОЛГ: полупогашенный счёт хлеба — и спрос ровно он.
    {
        std::int32_t debt[kCommodityCount] = {};
        debt[commodity_index("food")] = 777;
        CHECK(season_demand_for("food", debt, 128, CITY, nullptr) == 777,
              "the direct demand IS the unpaid bill");
    }
}

void test_arbitrage_dies_two_ways() {
    // INFINITE money is dead by two independent walls, and both are pinned.
    //
    // WALL 1 — SLIPPAGE: a full-shelf round trip (buy the lot, sell it
    // back) prices the buy against the EMPTIED shelf and the sell against
    // the restocked one, and the gap eats the round. Контекстные множители
    // (настроение места, нрав купца) вырезаны 2026-09-19 — свип по ним
    // умер вместе с ними: у цены остались кривая и разница анкет.
    {
        int trips = 0;
        for (int s = 2; s <= 512; s *= 2) {
            // РАВНЫЕ АНКЕТЫ (S25): обе стороны называют одну торговую силу,
            // наценки нет, и единственное, что решает круг, — СЛИППЕДЖ.
            // Свип по силе остаётся, но обе стороны идут одним числом:
            // закон теперь про РАЗНИЦУ, и «моя сила против нуля» — это уже
            // не прокрутка, а грабёж слабого (см. пин ниже).
            for (int cha = 0; cha <= 200; cha += 50) {
                for (int base : {5, 10, 100}) {
                    for (int demand : {0, 8, 64}) {
                        // A famine market (demand >= the whole shelf) pins
                        // BOTH ends — no slippage exists there and WALL 2
                        // (the purse) is the guard. Slippage needs headroom.
                        if (demand >= s) continue;
                        const int buyUnit = trade_price(
                            stock_price(base, 0, demand),
                            cha, cha, /*buying=*/true);
                        const int sellUnit = trade_price(
                            stock_price(base, s, demand),
                            cha, cha, /*buying=*/false);
                        ++trips;
                        CHECK(sellUnit <= buyUnit,
                              "full-shelf round trips never profit");
                        if (sellUnit > buyUnit) return;
                    }
                }
            }
        }
        CHECK(trips > 100, "the sweep actually swept");
    }

    // ...И ПЕРЕВЕС ЧЕСТНО ПРИБЫЛЕН — это ЗАКОН, а не дыра в нём (S25:
    // «у кого больше, тот и наценивает»). Прокачанный торговец обирает
    // слабую сторону на разницу анкет; сторожит это не кламп, которого
    // больше нет, а WALL 2 ниже — КОШЕЛЁК контрагента конечен.
    {
        const int shelf = 64, base = 10, demand = 8;
        const int strongBuys = trade_price(stock_price(base, 0, demand),
                                           /*mine=*/60, /*theirs=*/0,
                                           /*buying=*/true);
        const int strongSells = trade_price(stock_price(base, shelf, demand),
                                            /*mine=*/60, /*theirs=*/0,
                                            /*buying=*/false);
        const int evenBuys = trade_price(stock_price(base, 0, demand),
                                         0, 0, true);
        const int evenSells = trade_price(stock_price(base, shelf, demand),
                                          0, 0, false);
        CHECK(strongBuys < evenBuys && strongSells > evenSells,
              "перевес анкеты двигает ОБА конца в пользу сильного");
        CHECK(evenSells <= evenBuys,
              "негативный контроль: при равных анкетах тот же круг не "
              "приносит ничего — работает только слиппедж");
    }

    // WALL 2 — THE PURSE: прибыль в мире ЕСТЬ, и это закон (S25: сильная
    // анкета обирает слабую). Стережёт её не кламп, которого нет, а КОШЕЛЁК
    // контрагента: каждый прибыльный круг его осушает, и ферма встаёт.
    // Simulated as the panels settle it: real coin transfers, all-or-nothing.
    {
        Inventory player;
        Inventory merchant;
        player.add("coin_empire_copper", 1000);
        merchant.add("coin_empire_copper", 200);   // his whole purse
        merchant.add("food", 64);
        const int myStart = coin_census_value(player);
        int rounds = 0;
        bool farmDied = false;
        // ФЕРМА УМИРАЕТ ДВУМЯ СПОСОБАМИ, и оба надо узнавать: круг перестаёт
        // быть выгодным (слиппедж) ЛИБО контрагенту нечем платить. Второй
        // выглядит не как отказ, а как СТОЯЧИЙ круг: обчищенный купец
        // отдаёт по крохе, и счётчик прибыли замирает.
        int stale = 0;
        for (; rounds < 10000; ++rounds) {
            const int purseBefore = coin_census_value(player);
            const int supply = merchant.count("food");
            // Сильная анкета против слабой: покупаю со скидкой, продаю с
            // наценкой — прибыльный круг, который и должен упереться в его
            // кошелёк.
            const int buyUnit = trade_price(
                stock_price(100, supply - 1, 0), 60, 0, true);
            const int sellUnit = trade_price(
                stock_price(100, supply, 0), 60, 0, false);
            if (sellUnit <= buyUnit) { farmDied = true; break; }   // profitless
            if (coin_census_value(player) < buyUnit) break;
            if (!transfer_value_dense(player, merchant, buyUnit)) break;
            merchant.remove("food", 1);
            player.add("food", 1);
            if (!transfer_value_dense(merchant, player, sellUnit)) {
                // He cannot pay: the deal does not happen — put it back.
                player.remove("food", 1);
                merchant.add("food", 1);
                farmDied = true;   // he cannot pay: the purse wall
                break;
            }
            player.remove("food", 1);
            merchant.add("food", 1);
            if (coin_census_value(player) <= purseBefore) {
                if (++stale >= 3) { farmDied = true; break; }
            } else {
                stale = 0;
            }
        }
        const int profit = coin_census_value(player) - myStart;
        CHECK(rounds < 10000 && farmDied,
              "the farm DIES - profitless or purse-broke, never infinite");
        // Прибыль сильного — не дефект (S25), поэтому пин здесь не «ноль»,
        // а КОНЕЧНОСТЬ: сколько бы кругов ни прошло, взять больше, чем было
        // у контрагента, невозможно — денег в мире не печатается.
        CHECK(profit <= 200, "the profit can never exceed his whole purse");
        CHECK(coin_census_value(player) + coin_census_value(merchant) == 1200,
              "coin CONSERVES across every round - nothing was minted");
        // (The metric is the COIN CENSUS, not the bags' whole value: since
        // verdict №1 a debit may travel as goods when they are denser, and
        // what this law is about is that no coin was minted.)
    }
}

// ── ТОРГОВАЯ СИЛА — ОДНА ДВЕРЬ, И ОБЕ СТОРОНЫ НАЗЫВАЮТ ЕЮ СЕБЯ ────────────
// Свидетель заведён 2026-09-21 на дефект, проживший в панели ИГРОКА всё
// время существования двусторонней цены: `trade_price(база, МОЯ сила, ЕГО
// сила)` звалась как `trade_price(база, харизма, ранг Торговли)`. Два
// симптома, оба видны игроку, оба пинятся здесь:
//   1. ранг Торговли стоял на месте ЧУЖОЙ силы ⇒ прокачка скилла ухудшала
//      цену (наценка выходила «моя харизма минус мой же скилл»);
//   2. анкета контрагента не спрашивалась вовсе ⇒ торг со столицей и с
//      нищим бродягой стоил одинаково.
// Пинится СВОЙСТВО, а не число: монотонность силы по обеим осям листа и то,
// что чужая анкета цену двигает. Литералов из таблицы здесь нет — лист
// собирается тут же, и правка балансных чисел анкет тест не уронит.
void test_trade_power_is_one_door() {
    // 1. Сила растёт по ОБЕИМ осям листа: атрибут — природная мощь, скилл её
    //    МНОЖИТ (attributes.h). Инверсия по второй оси и была дефектом.
    CharacterSheet weak{};
    weak.attributes[AttributeId::Cha] = 10;
    weak.skills[SkillId::Trade] = 0;
    CharacterSheet skilled = weak;
    skilled.skills[SkillId::Trade] = 30;
    CharacterSheet charming = weak;
    charming.attributes[AttributeId::Cha] = 30;
    CHECK(trade_power_of(skilled) > trade_power_of(weak),
          "ранг Торговли УСИЛИВАЕТ торговую силу, а не ослабляет её");
    CHECK(trade_power_of(charming) > trade_power_of(weak),
          "харизма усиливает торговую силу");

    // 2. Цену двигает РАЗНИЦА анкет, значит контрагент обязан быть спрошен:
    //    у сильного места слабый покупатель платит больше, чем у слабого.
    const int base = 100;
    const int atStrong = trade_price(base, trade_power_of(weak),
                                     trade_power_of(skilled), /*buying=*/true);
    const int atWeak = trade_price(base, trade_power_of(weak),
                                   trade_power_of(weak), /*buying=*/true);
    CHECK(atStrong > atWeak,
          "у сильной стороны тот же товар дороже — анкета контрагента "
          "участвует в цене");

    // 3. Прокачка Торговли улучшает цену покупателю при ТОМ ЖЕ контрагенте.
    //    Это прямой пин симптома №1.
    const int skilledBuys = trade_price(base, trade_power_of(skilled),
                                        trade_power_of(weak), true);
    const int weakBuys = trade_price(base, trade_power_of(weak),
                                     trade_power_of(weak), true);
    CHECK(skilledBuys < weakBuys,
          "прокачанный торговец покупает ДЕШЕВЛЕ — скилл работает НА игрока");

    // 4. Негативный контроль: равные анкеты — наценки нет вовсе. Без него
    //    проверки выше прошли бы и на законе «цена всегда в пользу того, кто
    //    спросил» (CANON S25: «равные стороны торгуют ровно по цене»).
    CHECK(trade_price(base, trade_power_of(skilled),
                      trade_power_of(skilled), true) == base
          && trade_price(base, trade_power_of(skilled),
                         trade_power_of(skilled), false) == base,
          "негативный контроль: равные анкеты дают ровно базу с обоих концов");
}

} // namespace

int main() {
    test_scarcity_shape();
    test_arbitrage_dies_two_ways();
    test_trade_power_is_one_door();
    return sm::test::report("price_law_test");
}

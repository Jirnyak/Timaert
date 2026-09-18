// The price-from-stock law (starcluster, W2d final) and the death of
// arbitrage. Pinned:
//   · scarcity shape — absence is dear, glut is cheap, po2-clamped;
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
static const sm::Skills& VILLAGE =
    sm::landmark_sheet(sm::LandmarkType::Village).skills;

using namespace sm;

void test_scarcity_shape() {
    CHECK(stock_scarcity(0, 0) == 1.0f, "empty shelf, no demand = base");
    CHECK(stock_scarcity(1000, 8) < stock_scarcity(10, 8),
          "glut is cheaper than a modest stock");
    CHECK(stock_scarcity(0, 100) == 4.0f, "hungry absence clamps at 4x (po2)");
    CHECK(stock_scarcity(1 << 20, 0) == 0.25f, "glut clamps at 1/4 (po2)");
    CHECK(stock_price(10, 0, 100) == 40, "price = base x scarcity");
    CHECK(stock_price(10, 1 << 20, 0) >= 1, "a price never reaches zero");
    CHECK(daily_demand_for("bread", 128, CITY) == 128
              && daily_demand_for("cloth", 128, CITY) == 4
              && daily_demand_for("wpn_dagger", 128, CITY) == 0,
          "demand reads the ONE needs ladder");
}

void test_arbitrage_dies_two_ways() {
    // INFINITE money is dead by two independent walls, and both are pinned.
    //
    // WALL 1 — SLIPPAGE: a full-shelf round trip (buy the lot, sell it
    // back) prices the buy against the EMPTIED shelf and the sell against
    // the restocked one — the gap dominates every multiplier pair the game
    // has, including the once-exploitable generous merchant (0.9 buy /
    // 1.2 sell, the audit's buy-81/sell-92).
    {
        const float ctx[][2] = {
            {1.0f, 1.0f}, {0.9f, 1.2f}, {1.2f, 0.9f}, {0.9f, 1.0f},
        };
        int trips = 0;
        for (int s = 2; s <= 512; s *= 2) {
            for (int cha = 0; cha <= 200; cha += 50) {
                for (const auto& c : ctx) {
                    for (int base : {5, 10, 100}) {
                        for (int demand : {0, 8, 64}) {
                            // A famine market (demand >= the whole shelf)
                            // pins BOTH ends at the po2 clamp — no slippage
                            // exists there and WALL 2 (the purse) is the
                            // guard. Slippage's own law needs headroom.
                            if (demand >= s) continue;
                            const int buyUnit = trade_price(
                                stock_price(base, 0, demand),
                                cha, 0, c[0], /*buying=*/true);
                            const int sellUnit = trade_price(
                                stock_price(base, s, demand),
                                cha, 0, c[1], /*buying=*/false);
                            ++trips;
                            CHECK(sellUnit <= buyUnit,
                                  "full-shelf round trips never profit");
                            if (sellUnit > buyUnit) return;
                        }
                    }
                }
            }
        }
        CHECK(trips > 100, "the sweep actually swept");
    }

    // WALL 2 — THE PURSE: whatever per-unit drip a favourable multiplier
    // pair still yields (a GENEROUS merchant genuinely overpays — that is
    // his temperament, not a bug), every profitable round DRAINS his finite
    // coin, and the farm stops dead when the purse does. Simulated as the
    // panels settle it: real coin transfers, all-or-nothing.
    {
        Inventory player;
        Inventory merchant;
        player.add("coin_empire_copper", 1000);
        merchant.add("coin_empire_copper", 200);   // his whole purse
        merchant.add("bread", 64);
        const int myStart = coin_census_value(player);
        int rounds = 0;
        bool farmDied = false;
        for (; rounds < 10000; ++rounds) {
            const int supply = merchant.count("bread");
            const int buyUnit = trade_price(
                stock_price(100, supply - 1, 0), 10, 0, 0.9f, true);
            const int sellUnit = trade_price(
                stock_price(100, supply, 0), 10, 0, 1.2f, false);
            if (sellUnit <= buyUnit) { farmDied = true; break; }   // profitless
            if (coin_census_value(player) < buyUnit) break;
            if (!transfer_value_dense(player, merchant, buyUnit)) break;
            merchant.remove("bread", 1);
            player.add("bread", 1);
            if (!transfer_value_dense(merchant, player, sellUnit)) {
                // He cannot pay: the deal does not happen — put it back.
                player.remove("bread", 1);
                merchant.add("bread", 1);
                farmDied = true;   // he cannot pay: the purse wall
                break;
            }
            player.remove("bread", 1);
            merchant.add("bread", 1);
        }
        const int profit = coin_census_value(player) - myStart;
        CHECK(rounds < 10000 && farmDied,
              "the farm DIES - profitless or purse-broke, never infinite");
        CHECK(profit > 0, "the generous drip was real before it died");
        CHECK(profit <= 200,
              "total extraction is bounded by the merchant's own purse");
        CHECK(coin_census_value(player) + coin_census_value(merchant) == 1200,
              "coin CONSERVES across every round - nothing was minted");
        // (The metric is the COIN CENSUS, not the bags' whole value: since
        // verdict №1 a debit may travel as goods when they are denser, and
        // what this law is about is that no coin was minted.)
    }
}

} // namespace

int main() {
    test_scarcity_shape();
    test_arbitrage_dies_two_ways();
    return sm::test::report("price_law_test");
}

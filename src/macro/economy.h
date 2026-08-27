// The player's trade-price laws — all that remains of the old abstract
// economy (W2b-4, owner's ruling: rip it whole).
//
// What used to live here — EconomyState's float arrays that never produced
// a good (every recipe demanded two resources while every village gathered
// only grain), update_prices nobody could pay, TradeRoutes that minted money
// on arrival — is GONE. The honest economy is macro/econ_day.h (the day's
// laws over the landmark's universal Inventory) plus AGENTS: woodcutters
// and farmers gather (macro/npc_ai.cpp), caravans carry real cargo in their
// own bags. Prices from STOCK arrive with the treasury increment (W2d) —
// until then the ONE player price law below serves every trade screen.
//
// Trade itself is UNIVERSAL (owner): an exchange between two Inventories —
// a settlement's, an NPC's, one day a chest's — priced by this law with the
// caller's ONE context multiplier (settlement mood / merchant temperament).
#pragma once
#include <cstdint>

namespace sm {

// ── Context contributions to the one price law (CANON S6, 2026-08-24) ─────
// The named multiplier columns live HERE, beside the law they feed — they
// used to be private helpers of two different screens (the settlement's mood
// in overlays.cpp, the merchant's temperament in macro_overlay.cpp): the
// same law with two homes, one of them buy-side only by accident of
// location. 1.0 is a system with nothing to say. New context (war, season,
// route danger) joins as another named column, never a screen helper.
enum class SettlementMood : std::uint8_t;
namespace ecs { struct NpcTraits; }
// Both context columns answer the same shape — WHICH SIDE of the deal is being
// priced — because a context that only knows how to make things dearer prices
// one direction and forgets the other. The settlement's mood used to take no
// `buying` flag, so a town in revolt charged the player a premium and then
// paid him the ordinary price for his goods: the context applied to half the
// trade by accident of its signature.
float mood_price_mult(SettlementMood mood, bool buying);
float trait_price_mult(const ecs::NpcTraits* traits, bool buying);

// THE player trade price — ONE law (owner ruling 2026-08-05). Base = the
// canonical charisma+bargaining pricing; context — the settlement's mood or
// the merchant's temperament — enters as ONE multiplier column resolved by
// the caller. `bargaining` is the future trade skill's column, 0 today.
int player_trade_price(int baseValue, int charisma, int bargaining,
                       float contextMult, bool buying);

int player_buy_price (int basePrice, int charisma, int bargaining);
int player_sell_price(int basePrice, int charisma, int bargaining);

// ── Price FROM STOCK (the starcluster law, owner-approved) ───────────────
//
//     scarcity = (demand + 1) / (supply + 1),  clamped to [1/4, 4] (po2)
//     price    = base × scarcity
//
// `supply` is the counterparty's stock of the item; `demand` its DAILY
// demand (the needs ladder × population; 0 for goods nobody eats — then
// abundance discounts and absence returns base). The caller passes the
// POST-TRADE supply — what remains after a buy, what piles up after a
// sell — so every deal pays its own SLIPPAGE: buying leaves the shelf
// scarcer and dearer, selling gluts it cheaper. That slippage is what
// extinguishes arbitrage: a buy-then-sell round trip can never profit,
// whatever the charisma and context multipliers say (price_law_test pins
// it across the once-exploitable generous-merchant pair).
float stock_scarcity(int supply, int demandPerDay);
int stock_price(int baseValue, int supply, int demandPerDay);

// A settlement's daily demand for an item — the needs ladder over its
// population; 0 for anything the table does not consume.
int daily_demand_for(const char* itemId, int population);

} // namespace sm

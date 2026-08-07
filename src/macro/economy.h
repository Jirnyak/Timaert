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

namespace sm {

// THE player trade price — ONE law (owner ruling 2026-08-05). Base = the
// canonical charisma+bargaining pricing; context — the settlement's mood or
// the merchant's temperament — enters as ONE multiplier column resolved by
// the caller. `bargaining` is the future trade skill's column, 0 today.
int player_trade_price(int baseValue, int charisma, int bargaining,
                       float contextMult, bool buying);

int player_buy_price (int basePrice, int charisma, int bargaining);
int player_sell_price(int basePrice, int charisma, int bargaining);

} // namespace sm

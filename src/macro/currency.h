// Faction currencies (owner's design, W2d): money is not a field, it is a
// COMMODITY. Every realm MINTS its own coin — a light item of the one
// catalog — and trade is honest BARTER: exchange anything for anything as
// long as the VALUE matches. Internally every formula converts to universal
// VALUE (inventory_value below); the coins are just the goods most often
// carried to balance a deal, which is what a currency IS.
//
// v1 keeps all coins at value 1 — no exchange rates yet. Rates, the
// currency exchange, and minting coins FROM gold/silver arrive with the
// wider economy (owner: «биржа валют потом»); adding a currency is a row
// here and a row in the item catalog.
#pragma once
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "macro/items.h"

namespace sm {

struct CurrencyDef {
    const char* itemId;
    const char* factionId;   // the minting realm (macro/faction.h row id)
};

inline constexpr CurrencyDef kCurrencyDefs[] = {
    {"coin_empire",  "empire"},
    {"coin_magika",  "magika"},
    {"coin_timaert", "timaert"},
    {"coin_barbar",  "barbarian_north"},
};
inline constexpr int kCurrencyCount =
    int(sizeof(kCurrencyDefs) / sizeof(kCurrencyDefs[0]));

inline bool is_currency_item(const char* id) {
    for (const CurrencyDef& c : kCurrencyDefs) {
        if (std::strcmp(c.itemId, id) == 0) return true;
    }
    return false;
}

// The mint that serves a faction. Culture groups fold onto their realm's
// coin (the Magica splinters and the Lake Duchy strike the Magika sigil;
// the chargen's "barbarians" is the northern kingdoms' ring), and everyone
// without a mint of their own — bandits, cults, the free folk — trades in
// imperial coin, the de-facto reserve currency of v1.
inline const char* currency_for_faction_id(const char* factionId) {
    if (factionId) {
        for (const CurrencyDef& c : kCurrencyDefs) {
            if (std::strcmp(c.factionId, factionId) == 0) return c.itemId;
        }
        if (std::strcmp(factionId, "old_magica") == 0
            || std::strcmp(factionId, "northern_magica") == 0
            || std::strcmp(factionId, "lower_magica") == 0
            || std::strcmp(factionId, "lake_duchy") == 0
            || std::strcmp(factionId, "cults") == 0) {
            return "coin_magika";
        }
        if (std::strcmp(factionId, "barbarians") == 0) return "coin_barbar";
    }
    return "coin_empire";
}

// ── The wallet: universal value accounting over coin stacks ──────────────

// What the coins in this bag are WORTH (coin value × count, all rows).
inline int wallet_value(const Inventory& inv) {
    int total = 0;
    for (const CurrencyDef& c : kCurrencyDefs) {
        const ItemDef* def = item_def(c.itemId);
        total += inv.count(c.itemId) * (def ? def->value : 1);
    }
    return total;
}

// Move coin stacks worth exactly `value` between bags — the SETTLEMENT half
// of a barter: real coins travel, nothing is minted in a deal. Greedy over
// the currency rows; all-or-nothing (a short wallet moves nothing).
inline bool transfer_value(Inventory& from, Inventory& to, int value) {
    if (value <= 0) return true;
    if (wallet_value(from) < value) return false;
    int left = value;
    for (const CurrencyDef& c : kCurrencyDefs) {
        if (left <= 0) break;
        const ItemDef* def = item_def(c.itemId);
        const int unit = def && def->value > 0 ? def->value : 1;
        const int have = from.count(c.itemId);
        const int take = have < (left + unit - 1) / unit
            ? have : (left + unit - 1) / unit;
        if (take <= 0) continue;
        from.remove(c.itemId, take);
        to.add(c.itemId, take);
        left -= take * unit;
    }
    return left <= 0;
}

// ── The package deal: two bundles swap whole or not at all ───────────────

// One side of a deal: lines of (catalog id, count) leaving a bag. Ids may
// repeat; validation sums them.
using BarterPackage = std::vector<std::pair<std::string, int>>;

// The GENERAL settlement of a barter (owner ruling 2026-08-07): the trade
// screens stage a package on EACH side and ONE button settles both, all-or-
// nothing — counts are checked against the PRE-DEAL bags, then everything
// travels. Coin is not special here: a currency row is just another line,
// which is how a deal balances. VALUE fairness (given covers taken) is the
// caller's law — the screens price both sides and gate the button.
inline bool barter_swap(Inventory& a, Inventory& b,
                        const BarterPackage& fromA,
                        const BarterPackage& fromB) {
    const auto covered = [](const Inventory& bag, const BarterPackage& pkg) {
        for (const auto& line : pkg) {
            if (line.second <= 0) return false;
            int need = 0;
            for (const auto& l : pkg)
                if (l.first == line.first) need += l.second;
            if (bag.count(line.first) < need) return false;
        }
        return true;
    };
    if (!covered(a, fromA) || !covered(b, fromB)) return false;
    for (const auto& line : fromA) {
        a.remove(line.first, line.second);
        b.add(line.first, line.second);
    }
    for (const auto& line : fromB) {
        b.remove(line.first, line.second);
        a.add(line.first, line.second);
    }
    return true;
}

// Spend value INTO a counterparty-less sink (an upkeep, a fee): as much as
// the wallet holds, capped by `value`. Returns what was actually paid —
// the caller decides what an unpaid remainder means.
inline int wallet_spend_up_to(Inventory& inv, int value) {
    int left = value < 0 ? 0 : value;
    int paid = 0;
    for (const CurrencyDef& c : kCurrencyDefs) {
        if (left <= 0) break;
        const ItemDef* def = item_def(c.itemId);
        const int unit = def && def->value > 0 ? def->value : 1;
        const int have = inv.count(c.itemId);
        const int take = have < left / unit ? have : left / unit;
        if (take <= 0) continue;
        inv.remove(c.itemId, take);
        left -= take * unit;
        paid += take * unit;
    }
    return paid;
}

// The whole bag in universal VALUE — what every formula converts to
// (owner: «внутри всё равно учитывается стоимость»).
inline int inventory_value(const Inventory& inv) {
    int total = 0;
    for (const ItemRef& s : inv.slots) {
        if (s.empty()) continue;
        const ItemDef* def = item_def_at(int(s.def));
        total += (def ? def->value : 0) * s.count;
    }
    return total;
}

} // namespace sm

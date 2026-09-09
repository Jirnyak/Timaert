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

#include "macro/faction.h"
#include "macro/items.h"

namespace sm {

// The coins themselves — the currency rows of the one item catalog. WHO
// trades in which coin is the faction registry's own column (FactionDef::
// mint), not a second mapping here to drift against it.
struct CurrencyDef {
    const char* itemId;
};

inline constexpr CurrencyDef kCurrencyDefs[] = {
    {"coin_empire"},
    {"coin_magika"},
    {"coin_timaert"},
    {"coin_barbar"},
};
inline constexpr int kCurrencyCount =
    int(sizeof(kCurrencyDefs) / sizeof(kCurrencyDefs[0]));

inline bool is_currency_item(const char* id) {
    for (const CurrencyDef& c : kCurrencyDefs) {
        if (std::strcmp(c.itemId, id) == 0) return true;
    }
    return false;
}

// The mint that serves a faction: its registry row's own `mint` column
// (macro/faction.h). Everyone without a mint of their own — beasts, bandits,
// the free folk, an id the registry does not know — trades in imperial coin,
// the de-facto reserve currency of v1. The strcmp chain that lived here was
// an if-by-kind in general code (CANON S16); its "barbarians" branch named a
// row that does not exist, so three of the four barbarian realms quietly
// traded imperial.
inline const char* currency_for_faction_id(const char* factionId) {
    const int fi = faction_index(factionId);
    if (fi >= 0 && kFactionDefs[fi].mint && kFactionDefs[fi].mint[0] != '\0') {
        return kFactionDefs[fi].mint;
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

// Move coin stacks worth `value` between bags — the SETTLEMENT half of a
// barter: real coins travel, nothing is minted in a deal — and nothing is
// BURNED either: the credit lands before the debit, so a stack the receiver
// refuses (a full bag) simply STAYS with the payer (CANON S5). Greedy over
// the currency rows; a short wallet moves nothing. Returns the value
// actually moved — callers compare it against `value` to know the deal
// settled whole.
inline int transfer_value(Inventory& from, Inventory& to, int value) {
    if (value <= 0) return 0;
    if (wallet_value(from) < value) return 0;
    int left = value;
    int moved = 0;
    for (const CurrencyDef& c : kCurrencyDefs) {
        if (left <= 0) break;
        const ItemDef* def = item_def(c.itemId);
        const int unit = def && def->value > 0 ? def->value : 1;
        const int have = from.count(c.itemId);
        const int take = have < (left + unit - 1) / unit
            ? have : (left + unit - 1) / unit;
        if (take <= 0) continue;
        if (!to.add(c.itemId, take)) continue;   // refused: stack stays put
        from.remove(c.itemId, take);
        left -= take * unit;
        moved += take * unit;
    }
    return moved;
}

// ── The package deal: two bundles swap whole or not at all ───────────────

// One staged line of a deal: the SLOT of the source shelf, not an id — a
// rolled sword and its bare twin are two stacks of one id, and an id-keyed
// line priced the plain while shipping whichever the bag found first (the
// exact class Inventory::remove_at was built against, owner verdict
// 2026-09-07 «торговля пер-стак»). `def` is the row the slot held when the
// line was staged — a shelf slot re-filled by the economy day under an open
// panel refuses the stale line instead of shipping strangers.
struct BarterLine {
    int           slot  = -1;
    int           count = 0;
    std::uint16_t def   = 0;
};
using BarterPackage = std::vector<BarterLine>;

// The GENERAL settlement of a barter (owner ruling 2026-08-07): the trade
// screens stage a package on EACH side and ONE button settles both, all-or-
// nothing — lines are checked against the PRE-DEAL bags, then everything
// travels. What travels is the slot's WHOLE identity (seed, affixes,
// material, quality) — add_ref re-stacks by the one stacking law, so a
// rolled instance arrives as itself and never merges into a plain pile.
// Coin is not special here: a currency row is just another line, which is
// how a deal balances. VALUE fairness (given covers taken) is the caller's
// law — the screens price both sides and gate the button.
inline bool barter_swap(Inventory& a, Inventory& b,
                        const BarterPackage& fromA,
                        const BarterPackage& fromB) {
    const auto covered = [](const Inventory& bag, const BarterPackage& pkg) {
        for (const BarterLine& line : pkg) {
            if (line.count <= 0) return false;
            if (line.slot < 0 || line.slot >= kMaxInventorySlots) return false;
            const ItemRef& s = bag.slots[std::size_t(line.slot)];
            if (s.empty() || s.def != line.def) return false;
            int need = 0;
            for (const BarterLine& l : pkg)
                if (l.slot == line.slot) need += l.count;
            if (s.count < need) return false;
        }
        return true;
    };
    if (!covered(a, fromA) || !covered(b, fromB)) return false;
    // Settle on COPIES and commit whole: `add_ref` can refuse a full bag
    // mid-deal, and a half-settled swap would burn the goods already
    // removed. All-or-nothing stays literal — a refused deal leaves both
    // pre-deal bags untouched (CANON S5: nothing evaporates).
    Inventory na = a, nb = b;
    const auto ship = [](Inventory& from, Inventory& to,
                         const BarterPackage& pkg) {
        for (const BarterLine& line : pkg) {
            ItemRef payload = from.slots[std::size_t(line.slot)];
            payload.count = line.count;
            if (!from.remove_at(line.slot, line.count)) return false;
            if (!to.add_ref(payload)) return false;
        }
        return true;
    };
    if (!ship(na, nb, fromA) || !ship(nb, na, fromB)) return false;
    a = na;
    b = nb;
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
// (owner: «внутри всё равно учитывается стоимость»). Per stack through THE
// contextual price (value_of — row + affixes; material/quality wake here
// with their tables): a bandit hauling a rolled blade is worth hunting for
// exactly what the blade would fetch, not for its bare row.
inline int inventory_value(const Inventory& inv) {
    int total = 0;
    for (const ItemRef& s : inv.slots) {
        if (s.empty()) continue;
        total += value_of(s) * s.count;
    }
    return total;
}

} // namespace sm

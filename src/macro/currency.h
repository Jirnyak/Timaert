// VALUE settlement over the one item catalog — the barter machinery.
//
// A COIN IS NOT A CONCEPT HERE (owner verdict №1 of the second audit,
// 2026-09-17, дословно: «монета это просто товар со стоимостью 10 для
// серебра 100 для золота 1 для медяка — никаких особых механик и ворот,
// всё через бартер»). The kCurrencyDefs list, the is_currency_item gate and
// the wallet trio (wallet_value / transfer_value / wallet_spend_up_to) that
// walked that list died 2026-09-18. What a faction mints is the faction
// registry's own mint columns (macro/faction.h faction_coins); everything
// in THIS header prices and moves stacks by the one contextual value law,
// and a coin wins purely by arithmetic — minimal weight at maximal value.
#pragma once
#include <string>
#include <vector>

#include "macro/faction.h"
#include "macro/items.h"

namespace sm {

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

// ── Payment by value — the ONE law of every debit ────────────────────────

// The densest stack of the bag: the slot the value law reaches for first.
// Плотнее — раньше: v/w больше ⇔ v·bestW > bestV·w (перекрёстно, без
// деления; вес 0 бесконечно плотен и выигрывает всегда). -1 = nothing of
// value left to pay with.
inline int densest_value_slot(const Inventory& inv, int& outUnitValue) {
    int best = -1;
    int bestV = 0;
    float bestW = 0.0f;
    for (int i = 0; i < int(inv.slots.size()); ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (s.empty()) continue;
        const int v = value_of(s);
        if (v <= 0) continue;
        const ItemDef* def = item_def_at(int(s.def));
        const float w = def && def->weight > 0.0f ? def->weight : 0.0f;
        const bool denser = best < 0
            || float(v) * bestW > float(bestV) * w
            || (float(v) * bestW == float(bestV) * w && v > bestV);
        if (denser) { best = i; bestV = v; bestW = w; }
    }
    outUnitValue = bestV;
    return best;
}

// ОПЛАТА СТОИМОСТЬЮ В СТОК (владелец, 2026-09-18, дословно: «оплата может
// списываться по единой системе стоимости — натурой, по механике бартера…
// и в чём особенны монеты? никакого хардкода: у них минимальный вес при
// макс стоимости, поэтому дефолт — списание в монетах, но если монет нет,
// списывается что-то другое, что выгодно по весу»). Никакого списка валют:
// стаки уходят в порядке ПЛОТНОСТИ ЦЕННОСТИ (value/kg), и монета выигрывает
// арифметикой веса, не веткой. Последняя единица может переплатить —
// бартерный закон: отданное покрывает взятое, излишек — щедрость
// плательщика. Возвращает уплаченную стоимость; на тощем контейнере платит
// сколько есть — судья «покрыто/не покрыто» остаётся у вызывающего
// (окно сравнивает inventory_value ДО платежа).
inline int pay_value_dense(Inventory& inv, int value) {
    int left = value < 0 ? 0 : value;
    int paid = 0;
    while (left > 0) {
        int unitV = 0;
        const int best = densest_value_slot(inv, unitV);
        if (best < 0) break;   // платить больше нечем
        const ItemRef& s = inv.slots[std::size_t(best)];
        const int want = (left + unitV - 1) / unitV;
        const int take = want < int(s.count) ? want : int(s.count);
        if (take <= 0 || !inv.remove_at(best, take)) break;
        paid += take * unitV;
        left -= take * unitV;
    }
    return paid;
}

// ОПЛАТА СТОИМОСТЬЮ КОНТРАГЕНТУ — the same density law with a receiver: the
// stacks TRAVEL (whole identity, add_ref), nothing is minted and nothing is
// burned. Credit-before-debit per stack: what the receiver's full bag
// refuses simply STAYS with the payer (CANON S5). Returns the value actually
// moved — callers compare it against `value` to know the deal settled whole.
inline int transfer_value_dense(Inventory& from, Inventory& to, int value) {
    int left = value < 0 ? 0 : value;
    int moved = 0;
    while (left > 0) {
        int unitV = 0;
        const int best = densest_value_slot(from, unitV);
        if (best < 0) break;
        const ItemRef& s = from.slots[std::size_t(best)];
        const int want = (left + unitV - 1) / unitV;
        const int take = want < int(s.count) ? want : int(s.count);
        if (take <= 0) break;
        ItemRef payload = s;
        payload.count = std::uint16_t(take);
        if (!to.add_ref(payload)) break;   // refused: the stack stays put
        if (!from.remove_at(best, take)) break;
        moved += take * unitV;
        left -= take * unitV;
    }
    return moved;
}

// ── Granting value as coin — change-making over the mint family ──────────

// Add `value` worth of a faction's OWN coin to a bag, largest nominal first
// (gold → silver → copper; copper is nominal 1, so nothing is dropped).
// This is where rewards, purses and treasury seeds are MINTED into rows —
// plain change-making arithmetic over the registry's mint columns, never a
// mechanic: the coins land as ordinary stacks and trade as ordinary goods.
// A full bag refuses what it refuses; returns the value actually added.
inline int add_value_in_coins(Inventory& inv, int factionIdx, int value) {
    int left = value < 0 ? 0 : value;
    int added = 0;
    const char* const* coins = faction_coins(factionIdx);
    for (int i = 2; i >= 0 && left > 0; --i) {
        const ItemDef* def = item_def(coins[i]);
        const int unit = def && def->value > 0 ? def->value : 1;
        const int n = left / unit;
        if (n <= 0) continue;
        if (!inv.add(coins[i], n)) continue;   // full bag: try smaller coin
        left -= n * unit;
        added += n * unit;
    }
    return added;
}

// VALUE of the COIN rows of a bag — a census over the faction registry's
// mint columns (deduped: culture groups fold onto their realm's family).
// This is ANALYTICS for assessments that are defined over coin — the tithe's
// coin half averages it so it never double-counts the goods half it stands
// beside. No payment law branches on it: debits go by density, always.
inline int coin_census_value(const Inventory& inv) {
    struct CoinRow { int idx; int value; };
    static const std::vector<CoinRow> rows = [] {
        std::vector<CoinRow> r;
        for (const FactionDef& f : kFactionDefs) {
            for (const char* c : f.mint) {
                if (!c || !c[0]) continue;
                const int idx = item_index(c);
                if (idx < 0) continue;
                bool seen = false;
                for (const CoinRow& e : r) seen = seen || e.idx == idx;
                if (seen) continue;
                const ItemDef* def = item_def_at(idx);
                r.push_back({idx, def && def->value > 0 ? def->value : 1});
            }
        }
        return r;
    }();
    int total = 0;
    for (const CoinRow& e : rows) total += inv.count_of(e.idx) * e.value;
    return total;
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

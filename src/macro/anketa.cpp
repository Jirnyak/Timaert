// ДВЕРИ АНКЕТЫ НАД ЕДИНЫМ КОНТЕЙНЕРОМ (наряд M-181, разрез items):
// тела дверей НАД СОСТОЯНИЕМ — Inventory анкеты (craft/scrap/use/вес).
// Каталог и его тела уехали в tables/items.{h,cpp}; направление
// одностороннее — эти двери ЧИТАЮТ каталог (item_parts, item_yield,
// value_of, item_def), каталог о них не знает (AGENTS §11).

#include "macro/anketa.h"

#include "tables/commodity.h"

#include <limits>
#include <string>

namespace sm {

bool craft_item(Inventory& inv, int defIdx, int n) {
    if (n <= 0) return false;
    const auto parts = item_parts(defIdx);
    if (parts.empty()) return false;             // terminal: nothing composes it
    // All-or-nothing on a copy (the barter_swap idiom): remove_of can succeed
    // partially across stacks before a later part runs short, and add_of can
    // refuse a full bag after the materials already left it.
    Inventory work = inv;
    for (const ItemPart& part : parts) {
        if (!work.remove_of(int(part.def), n * int(part.count))) return false;
    }
    // n batches make n × yield items — white base: seed 0, plain.
    if (!work.add_of(defIdx, n * item_yield(defIdx))) return false;
    inv = work;
    return true;
}

bool scrap_at(Inventory& inv, int slot, int n) {
    if (slot < 0 || slot >= kMaxInventorySlots || n <= 0) return false;
    const ItemRef ref = inv.slots[std::size_t(slot)];
    if (ref.empty() || ref.count < n) return false;
    const auto parts = item_parts(int(ref.def));
    if (parts.empty()) return false;             // terminal: no reverse
    Inventory work = inv;
    if (!work.remove_at(slot, n)) return false;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        int mat = int(parts[i].def);
        // The instance's material byte substitutes part 0 (ItemRef.material:
        // 1 + raw commodity row) — a steel sword returns steel.
        if (i == 0 && ref.material != 0
            && int(ref.material) <= kRawCommodityCount) {
            const int sub = item_index(kCommodities[ref.material - 1].id);
            if (sub >= 0) mat = sub;
        }
        // POOLED entropy (owner 2026-09-12): half the TOTAL matter of the n
        // units, floored — one sword still pays 1 iron, a lone dagger's
        // handle still burns whole, two daggers pool into 1 iron, and 64
        // coins (yield 32: each is 1/32 silver) melt to exactly 1 silver.
        // Affix cells are simply never read: they burn.
        const int back = n * int(parts[i].count) / (2 * item_yield(int(ref.def)));
        if (back > 0 && !work.add_of(mat, back)) return false;
    }
    inv = work;
    return true;
}

int auto_scrap_overflow(Inventory& inv) {
    int scrapped = 0;
    while (inv.used_slots() > kAutoScrapSlots) {
        // The cheapest non-fungible stack that HAS a reverse. Plain rows are
        // never candidates: they merge into one slot and cannot clog. A
        // linear pick per freed slot is the honest cost of a once-a-day tick
        // over the 1024 fixed slots.
        int best = -1;
        long bestValue = 0;
        for (int s = 0; s < kMaxInventorySlots; ++s) {
            const ItemRef& r = inv.slots[std::size_t(s)];
            if (r.empty()) continue;
            const bool fungible = r.seed == 0 && r.material == 0
                && r.level == 0 && r.entityId == 0u && affix_count(r) == 0;
            if (fungible) continue;
            if (item_parts(int(r.def)).empty()) continue;
            const long v = long(value_of(r)) * r.count;
            if (best < 0 || v < bestValue) {
                best = s;
                bestValue = v;
            }
        }
        if (best < 0) break;   // nothing scrappable: the law falls silent
        if (!scrap_at(inv, best, inv.slots[std::size_t(best)].count)) break;
        ++scrapped;
    }
    return scrapped;
}

float inventory_weight(const Inventory& inv) noexcept {
    float total = 0.0f;
    for (const ItemRef& s : inv.slots) {
        if (s.empty()) continue;
        if (const ItemDef* d = item_def_at(int(s.def))) {
            total += d->weight * static_cast<float>(s.count);
        }
    }
    return total;
}

std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc) {
    // Find stack
    ItemRef* stack = nullptr;
    const int idx = item_index(itemId);
    for (ItemRef& s : inv.slots) {
        if (!s.empty() && s.def == std::uint16_t(idx)) { stack = &s; break; }
    }
    if (!stack || stack->count <= 0) return {};

    const ItemDef* def = item_def(itemId);
    if (!def) return {};
    if (!item_type_consumable(def->type)) return {};

    std::string msg;
    auto append = [&](const std::string& part) {
        if (!msg.empty()) msg += ", ";
        msg += part;
    };

    // Every cell of the row goes through the ONE instant door (macro/bonus.h),
    // which reports what actually MOVED rather than what was asked for — so
    // the line the player reads says 5 when only 5 fitted. The three
    // hand-written clamp-and-append blocks that stood here were the same
    // arithmetic three times, and they could only ever speak about the three
    // pools they happened to name.
    PoolSlice pools{};
    pools.current[int(PoolId::Hp)] = &pc.currentHp;
    pools.maximum[int(PoolId::Hp)] = pc.maxHp;
    pools.current[int(PoolId::Mp)] = &pc.currentMp;
    pools.maximum[int(PoolId::Mp)] = pc.maxMp;
    pools.current[int(PoolId::Sp)] = &pc.currentSp;
    pools.maximum[int(PoolId::Sp)] = pc.maxSp;

    for (const Bonus& b : def->bonus) {
        if (b.row == 0) continue;
        const int moved = apply_instant(pools, b);
        // A zero move is REPORTED, not swallowed: "+0 HP" is how the player
        // learns the potion he just drank on a full bar was wasted.
        std::string part = moved >= 0 ? "+" : "";
        part += std::to_string(moved);
        part += " ";
        part += bonus_def(BonusId(b.row)).label;
        append(part);
    }

    inv.remove(itemId, 1);

    std::string head = "Used ";
    head += def->name;
    if (msg.empty()) return head;
    return head + ": " + msg;
}

} // namespace sm

// Wearing things: the flat-array half of macro/anatomy.h.
#include "macro/anatomy.h"

namespace sm {

namespace {

// A cell occupied BY ANOTHER item's blocks mask, rather than by an item of its
// own. It is a reserved catalog ordinal rather than a second container or a
// parallel bitset, because "what is in this cell" already has a home and a
// second store for the same question is what CANON S26 forbids.
//
// 0xFFFF cannot collide with a real row: the catalog is checked against it by
// the contract test, and `item_def_at` refuses it like any other bad ordinal.
constexpr std::uint16_t kBlockedByDef = 0xFFFFu;

bool cell_blocked(const ItemRef& r) {
    return r.def == kBlockedByDef && r.count != 0;
}

// Every cell a worn item would ALSO occupy, given where it sits.
void mark_blocks(Equipment& eq, int origin, std::uint64_t blocks) {
    if (blocks == 0) return;
    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        if (i == origin) continue;
        if (!eq.worn[std::size_t(i)].empty()) continue;
        if ((blocks & part_bit(eq.part_at(i))) == 0) continue;
        ItemRef& cell = eq.worn[std::size_t(i)];
        cell = ItemRef{};
        cell.def = kBlockedByDef;
        cell.count = 1;
        // The origin rides in `seed` so unequip can release exactly the cells
        // ONE item blocked, and not those another two-hander blocked too.
        cell.seed = std::uint32_t(origin) + 1u;
    }
}

void release_blocks(Equipment& eq, int origin) {
    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        ItemRef& cell = eq.worn[std::size_t(i)];
        if (cell_blocked(cell) && cell.seed == std::uint32_t(origin) + 1u) {
            cell = ItemRef{};
        }
    }
}

} // namespace

bool item_fits_cell(const Equipment& eq, int cell, const ItemDef& def) {
    if (cell < 0 || cell >= eq.cells()) return false;
    if (def.slotMask == 0) return false;          // not a wearable row at all
    const BodyPartId part = eq.part_at(cell);
    if (part >= BodyPartId::Count) return false;  // past this body
    return (def.slotMask & part_bit(part)) != 0;
}

int equip(Equipment& eq, const ItemRef& item) {
    if (item.empty()) return -1;
    const ItemDef* def = item_def_at(int(item.def));
    if (!def || def->slotMask == 0) return -1;

    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        if (!item_fits_cell(eq, i, *def)) continue;
        if (!eq.worn[std::size_t(i)].empty()) continue;   // taken, or blocked

        // A two-hander needs its blocked cells EMPTY, not merely present: it
        // may not shove a shield off an arm the wearer chose to use.
        if (def->blocksMask != 0) {
            bool clear = true;
            for (int j = 0; j < n && clear; ++j) {
                if (j == i) continue;
                if ((def->blocksMask & part_bit(eq.part_at(j))) == 0) continue;
                if (!eq.worn[std::size_t(j)].empty()) clear = false;
            }
            if (!clear) continue;   // try another cell before refusing
        }

        ItemRef one = item;
        one.count = 1;              // a body wears ONE, not a stack
        eq.worn[std::size_t(i)] = one;
        mark_blocks(eq, i, def->blocksMask);
        return i;
    }
    return -1;   // nothing on this body can take it — refusal, never a drop
}

ItemRef unequip(Equipment& eq, int cell) {
    if (cell < 0 || cell >= eq.cells()) return {};
    ItemRef& slot = eq.worn[std::size_t(cell)];
    if (slot.empty() || cell_blocked(slot)) return {};
    const ItemRef out = slot;
    slot = ItemRef{};
    release_blocks(eq, cell);
    return out;
}

BonusTotals worn_bonuses(const Equipment& eq) {
    BonusTotals t{};
    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        const ItemRef& r = eq.worn[std::size_t(i)];
        if (r.empty() || cell_blocked(r)) continue;
        // The ROW's own bonuses and the INSTANCE's rolled affixes are the same
        // kind of thing and are summed by the same accumulator — that is what
        // it bought to make an affix and a catalog bonus one type.
        if (const ItemDef* def = item_def_at(int(r.def))) {
            accumulate(t, def->bonus, kMaxItemBonuses);
        }
        accumulate(t, r.affix, kMaxItemAffixes);
    }
    return t;
}

int worn_armor(const Equipment& eq) {
    int sum = 0;
    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        const ItemRef& r = eq.worn[std::size_t(i)];
        if (r.empty() || cell_blocked(r)) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            sum += def->armor;
        }
    }
    return sum > 0 ? sum : 0;
}

} // namespace sm

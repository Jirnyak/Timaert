// Wearing things: the flat-array half of macro/anatomy.h.
#include "macro/anatomy.h"

#include <algorithm>
#include <cmath>

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

namespace {

// The one per-cell attempt both equip doors walk: fit, emptiness, a blocker's
// cells clear — then wear. Returns whether cell `i` took the item.
bool try_equip_cell(Equipment& eq, const ItemRef& item, const ItemDef& def,
                    int i) {
    if (!item_fits_cell(eq, i, def)) return false;
    if (!eq.worn[std::size_t(i)].empty()) return false;   // taken, or blocked

    // A two-hander needs its blocked cells EMPTY, not merely present: it
    // may not shove a shield off an arm the wearer chose to use.
    if (def.blocksMask != 0) {
        const int n = eq.cells();
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            if ((def.blocksMask & part_bit(eq.part_at(j))) == 0) continue;
            if (!eq.worn[std::size_t(j)].empty()) return false;
        }
    }

    ItemRef one = item;
    one.count = 1;              // a body wears ONE, not a stack
    eq.worn[std::size_t(i)] = one;
    mark_blocks(eq, i, def.blocksMask);
    return true;
}

} // namespace

int equip(Equipment& eq, const ItemRef& item) {
    if (item.empty()) return -1;
    const ItemDef* def = item_def_at(int(item.def));
    if (!def || def->slotMask == 0) return -1;

    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        if (try_equip_cell(eq, item, *def, i)) return i;
    }
    return -1;   // nothing on this body can take it — refusal, never a drop
}

int equip_at(Equipment& eq, const ItemRef& item, int cell) {
    if (item.empty()) return -1;
    const ItemDef* def = item_def_at(int(item.def));
    if (!def || def->slotMask == 0) return -1;
    return try_equip_cell(eq, item, *def, cell) ? cell : -1;
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

void remark_equipment_blocks(Equipment& eq) {
    const int n = eq.cells();
    // Clear whatever markers are there, then re-mark from the rows: a load
    // restored only the ITEMS, and the blocks follow from them.
    for (int i = 0; i < n; ++i) {
        if (cell_blocked(eq.worn[std::size_t(i)])) eq.worn[std::size_t(i)] = ItemRef{};
    }
    for (int i = 0; i < n; ++i) {
        const ItemRef& r = eq.worn[std::size_t(i)];
        if (r.empty() || cell_blocked(r)) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            mark_blocks(eq, i, def->blocksMask);
        }
    }
}

int worn_cells(const Equipment& eq) {
    int n = 0;
    const int cells = eq.cells();
    for (int i = 0; i < cells; ++i) {
        const ItemRef& r = eq.worn[std::size_t(i)];
        if (!r.empty() && !cell_blocked(r)) ++n;
    }
    return n;
}

const ItemDef* weapon_in_hand(const Equipment& eq) {
    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        const ItemRef& r = eq.worn[std::size_t(i)];
        if (r.empty() || cell_blocked(r)) continue;
        const BodyPartId part = eq.part_at(i);
        if (part != BodyPartId::Grip && part != BodyPartId::OffGrip) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            if (def->type == ItemType::Weapon) return def;
        }
    }
    return nullptr;
}

StrikeFields hand_strike_fields(const Attributes& attributes,
                                const Skills& skills, const Equipment* eq) {
    const ItemDef* w = eq ? weapon_in_hand(*eq) : nullptr;
    StrikeFields out{};
    out.dice    = w ? w->dice : kFistDice;
    out.dmgType = w ? w->dmgType : DamageType::Blunt;
    const SkillId skill =
        (w && w->skill != SkillId::Count) ? w->skill : SkillId::Unarmed;
    out.multPct = std::int16_t(skill_mult_pct(skills, skill));
    const DerivedBonuses d = calculate_derived(attributes, skills);
    // What the gear itself says about the blow (bonus.h Derived rows — the
    // affix track): a flat add beside the attribute's, and a whole-percent
    // verdict over the mass law's tempo. Summed from EVERYTHING worn, not
    // just the weapon — a striker's ring drives the same fist.
    const BonusTotals worn = eq ? worn_bonuses(*eq) : BonusTotals{};
    // A cursed sum below zero is a wound refused, not a heal: floor at 0.
    const int flat = int(std::floor(d.rawPhysDamage))
                   + worn.derived_of(DerivedModId::DmgFlat);
    out.flatAdd = std::int16_t(flat < 0 ? 0 : flat);
    out.luck    = std::uint8_t(attributes.of(AttributeId::Lck));
    // The TEMPO half (CANON S14 «один рычаг», 2026-09-07): the MASS law's
    // base (weight → seconds, weapon_swing_seconds; the bare hand is the
    // massless fastest) through the recovery door — Spd asymptote ×
    // Armsmaster, the arms' generic, never the typed skill that already
    // multiplied the dice above (one handle, one lever). The worn SwingPct
    // rows speak LAST, clamped to ×4 either way (po2), so a curse cannot
    // freeze the arm and a stack of hastes cannot divide time by zero.
    const int steps = recovery_steps(weapon_swing_seconds(w),
                                     attributes, skills,
                                     SkillId::Armsmaster);
    const int swingPct = 100 + std::clamp(
        worn.derived_of(DerivedModId::SwingPct),
        kDerivedPctFloor, kDerivedPctCeil);
    const int paced = steps * 100 / swingPct;
    out.recoverySteps = paced < 1 ? 1 : paced;
    return out;
}

ArmorProfile worn_armor(const Equipment& eq) {
    ArmorProfile sum{};
    const int n = eq.cells();
    for (int i = 0; i < n; ++i) {
        const ItemRef& r = eq.worn[std::size_t(i)];
        if (r.empty() || cell_blocked(r)) continue;
        if (const ItemDef* def = item_def_at(int(r.def))) {
            for (std::size_t t = 0; t < kDamageTypeCount; ++t) {
                const int v = int(sum.v[t]) + int(def->armor.v[t]);
                sum.v[t] = std::uint8_t(v > 255 ? 255 : v);
            }
        }
    }
    return sum;
}

} // namespace sm

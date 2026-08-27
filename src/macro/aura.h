// Leader aura — how a leader's sheet reaches the troops (owner rulings,
// macrosim.md "Three rulings", №2): the buff is a SET of modifiers sourced
// from the leader's own sheet and gear, applied INTO a member's sheet when
// its body is born — never a second multiplier standing beside the sheet.
// A second source of strength next to the sheet is exactly the split this
// project keeps closing: everything the buff grants must be expressible as
// the attributes and skills the one combat projection already reads.
//
// SOURCES are the extension axis (owner, 2026-08-06): perks today; the
// leader's charisma, skills and carried items later. Each source is one
// function appending into the same AuraMods, called from the one collector
// — adding a source never touches a consumer, and a consumer (body birth,
// the auto-resolve) never knows where a modifier came from.
//
// Header-only, macro layer, no ECS: pure sheet math, testable everywhere.
#pragma once

#include "macro/character_sheet.h"

#include "macro/bonus.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace sm {

// An aura is a handful of BONUSES — rows of the one registry (macro/bonus.h),
// not a private encoding of the same idea. It used to carry its own
// `AuraMod{target, id, delta}`, which was the honest half of the five
// half-doors: it named an address in the sheet and it was already ordinal. Its
// vocabulary is now everyone's, so the same row that reads "+1 Vitality" on a
// leader's perk reads "+1 Vitality" on a sword.
//
// Small and flat on purpose (po2): a body birth must not allocate to receive
// one.
inline constexpr std::size_t kMaxAuraMods = 8;

struct AuraMods {
    std::array<Bonus, kMaxAuraMods> mods{};
    std::uint8_t count = 0;
};

inline void aura_add(AuraMods& a, Bonus m) {
    if (a.count >= kMaxAuraMods) return;   // full aura drops the tail, loudly
                                           // bounded rather than quietly UB
    a.mods[a.count++] = m;
}

// ── Source: perks ──────────────────────────────────────────────────────────
// A perk that buffs the carrier's squad is a ROW here, never a branch in a
// spawner. The Leader perk is the owner's own example made real: "+10 HP to
// every soldier" is +1 vit through the one formula (attributes.h: a vit point
// is 10 max HP), so the buff lands in the member's sheet and nowhere else.
struct PerkAuraRow {
    PerkID perk;
    Bonus  mod;
};

inline constexpr PerkAuraRow kPerkAuras[] = {
    {PerkID::Leader, {std::uint8_t(BonusId::Vit), +1}},
};

inline void aura_from_perks(const Perks& perks, AuraMods& out) {
    for (const PerkAuraRow& row : kPerkAuras) {
        if (has_perk(perks, row.perk)) aura_add(out, row.mod);
    }
}

// ── The one collector ──────────────────────────────────────────────────────
// Everything a leader's sheet says about its troops, gathered once per squad
// and handed to every member birth. Future sources — the leader's charisma,
// its skills, the items it carries — are one function each, appended here.
inline AuraMods collect_leader_aura(const CharacterSheet& leader) {
    AuraMods out;
    aura_from_perks(leader.perks, out);
    return out;
}

// ── The one applier ────────────────────────────────────────────────────────
// Summed into BonusTotals and read back through `effective_sheet` — the ONE
// application every standing modifier in the game goes through, whatever put
// it there.
//
// It still WRITES the member's sheet, and that is a deliberate, bounded
// exception with its own reason: a projected body is born, fights and dies
// inside one subworld session, its sheet is derived on the spot from
// (type, level, seed) and thrown away, and nothing ever needs to take the
// aura off it. What matters is that the SUM is no longer computed here —
// `effective_sheet` owns the clamps (a score floors at 1, a rank stays inside
// [0, kMaxSkillRank]) and owns them for items and perks too. When a body
// wants a modifier it can LOSE, the answer is to keep the totals beside the
// sheet and call effective_sheet at the moment of reading, which is precisely
// what this function no longer prevents.
inline void apply_aura(CharacterSheet& sheet, const AuraMods& aura) {
    if (aura.count == 0) return;
    BonusTotals totals{};
    accumulate(totals, aura.mods.data(), int(aura.count));
    sheet = effective_sheet(sheet, totals);
}

} // namespace sm

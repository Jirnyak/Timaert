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

#include <algorithm>
#include <array>
#include <cstdint>

namespace sm {

// One modifier: "+delta to this attribute/skill of every member".
struct AuraMod {
    enum Target : std::uint8_t { Attribute = 0, Skill = 1 };
    Target       target;
    std::uint8_t id;      // AttributeId / SkillId, matching `target`
    std::int16_t delta;   // signed: a curse is the same row as a blessing
};

// Small and flat on purpose (po2): an aura is a handful of modifiers, and a
// body birth must not allocate to receive one.
inline constexpr std::size_t kMaxAuraMods = 8;

struct AuraMods {
    std::array<AuraMod, kMaxAuraMods> mods{};
    std::uint8_t count = 0;
};

inline void aura_add(AuraMods& a, AuraMod m) {
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
    PerkID  perk;
    AuraMod mod;
};

inline constexpr PerkAuraRow kPerkAuras[] = {
    {PerkID::Leader,
     {AuraMod::Attribute, std::uint8_t(AttributeId::Vit), +1}},
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
// Deltas land in the member's OWN sheet, before combat is projected from it,
// clamped at the same doors every legitimate point spend already respects:
// an attribute never drops below its base of 1, a skill rank stays within
// [0, kMaxSkillRank]. The member's sheet then IS its strength — the aura
// leaves no residue a second formula could disagree about.
inline void apply_aura(CharacterSheet& sheet, const AuraMods& aura) {
    for (std::uint8_t i = 0; i < aura.count; ++i) {
        const AuraMod& m = aura.mods[i];
        if (m.target == AuraMod::Attribute) {
            if (int* v = attribute_value(sheet.attributes,
                                         AttributeId(m.id))) {
                *v = std::max(1, *v + int(m.delta));
            }
        } else {
            if (int* v = skill_value(sheet.skills, SkillId(m.id))) {
                *v = std::clamp(*v + int(m.delta), 0, kMaxSkillRank);
            }
        }
    }
}

} // namespace sm

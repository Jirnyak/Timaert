// THE 9×9 symmetry of damage and armour (CANON S13, owner verdicts
// 2026-09-03/05): as many armour types as damage types, ONE enum for both —
// a second vocabulary here would be the faction-registry mistake all over
// again. A blow carries a DamageType; a body's defence is an ArmorProfile —
// nine columns indexed by the same enum; the two meet in exactly one law,
// mitigate_amount(), read by the damage door (sub/damage.cpp) and inverted
// by the auto-resolve (macro/auto_battle.h). The point of the symmetry is
// player CHOICE: know what the monster deals, dress and ward against that
// column.
//
// Integer arithmetic — combat laws are integer by law (CANON S13); the float
// halving formula died here 2026-09-05.
#pragma once

#include "core/table_guard.h"

#include <array>
#include <cstdint>

namespace sm {

// Three physical + six elemental. The elemental six ARE the magic schools of
// S15 (Fire/Water/Air/Earth/Arcane/Void) — a fire spell deals Fire, a fire
// ward armours Fire, one vocabulary end to end.
enum class DamageType : std::uint8_t {
    Slash = 0,
    Pierce,
    Blunt,
    Fire,
    Water,
    Air,
    Earth,
    Arcane,
    Void,
    Count,
};
inline constexpr std::size_t kDamageTypeCount = std::size_t(DamageType::Count);

struct DamageTypeDef {
    DamageType  type;
    const char* key;    // machine id for content files / console
    const char* label;  // what a panel prints
};

inline constexpr DamageTypeDef kDamageTypeDefs[kDamageTypeCount] = {
    {DamageType::Slash,  "slash",  "Slashing"},
    {DamageType::Pierce, "pierce", "Piercing"},
    {DamageType::Blunt,  "blunt",  "Bludgeoning"},
    {DamageType::Fire,   "fire",   "Fire"},
    {DamageType::Water,  "water",  "Water"},
    {DamageType::Air,    "air",    "Air"},
    {DamageType::Earth,  "earth",  "Earth"},
    {DamageType::Arcane, "arcane", "Arcane"},
    {DamageType::Void,   "void",   "Void"},
};
static_assert(rows_in_enum_order(kDamageTypeDefs, &DamageTypeDef::type),
              "kDamageTypeDefs must mirror DamageType ordinals");

// The armour at which a blow is HALVED by the percent branch — and therefore
// the whole scale on which every armour number in the game reads. It is not
// picked, it is the game's own plain blow: `kPlayerBaseMeleeDamage`
// (sub/engine.h), what an untrained man does with his bare hands. So
// "armour 10" says «this body halves a plain blow» — and, since the hybrid
// law below took over, «...and shrugs anything up to a plain blow off
// entirely».
inline constexpr int kArmorHalving = 10;

// A body's defence: one column per DamageType, same units as damage because
// the two meet in mitigate_amount(). uint8 by ЗАКОН ТИПА: armour is a count
// that is never negative, and at the 255 ceiling the percent branch already
// keeps a blow to kArmorHalving/265 ≈ 4% while the threshold branch blocks
// anything up to 255 outright — a wider column would buy no design room.
struct ArmorProfile {
    std::array<std::uint8_t, kDamageTypeCount> v{};

    constexpr int of(DamageType t) const { return int(v[std::size_t(t)]); }
};

// The mechanical translation for a scalar-era row (owner verdict 2026-09-05:
// existing content converts mechanically, authored per-column layouts are
// content-stage work): one number becomes all nine columns, preserving the
// old "armour mitigates every kind the door lets it" behaviour exactly.
constexpr ArmorProfile uniform_armor(int x) {
    ArmorProfile p{};
    const std::uint8_t c = std::uint8_t(x < 0 ? 0 : x > 255 ? 255 : x);
    for (std::size_t i = 0; i < kDamageTypeCount; ++i) p.v[i] = c;
    return p;
}

// THE mitigation law — the HYBRID (owner verdict 2026-09-05): armour A cuts
// the LARGER of
//   · A itself       (the threshold: a blow no bigger than the plate cannot
//                     find flesh at all — 100% reduction is real, rare, and
//                     countered by crits, big dice and the right type), and
//   · dmg·A/(A+10)   (the percent: a big blow is softened by the old halving
//                     fraction — armour never zeroes what overwhelms it).
// The two branches cross at dmg = A + kArmorHalving, so each regime owns the
// side of the scale where it reads naturally. One home, two readers: the
// damage door subtracts this, the auto-resolve credits the identical
// protection as effective HP.
constexpr int mitigate_amount(int dmg, int armour) {
    if (dmg <= 0) return 0;
    if (armour <= 0) return dmg;
    const int pctCut = dmg * armour / (armour + kArmorHalving);
    const int cut = armour > pctCut ? armour : pctCut;
    return dmg > cut ? dmg - cut : 0;
}

} // namespace sm

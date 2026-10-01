// THE 8×8 symmetry of damage and armour (CANON S13, owner verdicts
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
//
// EIGHT, AND THE DEBT IS PAID (наряд M-193, 2026-10-01). Owner's verdict,
// verbatim: «НЕТ! 8 типов уронов и 8 типов брони это на всю игру! ни больше
// не меньше» (CANON S15/S26), and on finally doing it: «да это большой долг
// надо раз и навсегда избавиться от него». Slash is GONE — folded into Pierce,
// which is now the one SHARP PHYSICAL type (arrows, daggers, spears, sword
// edges, fangs: point pressure that cuts flesh and binds in plate). What
// bought the fold is a power of two: 8 columns is one SIMD register for the
// whole defence of a body, and 8 types is a one-byte mask. The symmetry «as
// many armour types as damage types, one enum for both» is what is permanent;
// the number is now settled at 8 for the life of the game.
#pragma once

#include "core/dice.h"
#include "core/table_guard.h"

#include <array>
#include <cstdint>

namespace sm {

// Two physical + six elemental. The elemental six ARE the magic schools of
// S15 (Fire/Water/Air/Earth/Arcane/Void) — a fire spell deals Fire, a fire
// ward armours Fire, one vocabulary end to end.
enum class DamageType : std::uint8_t {
    // SHARP PHYSICAL — the one cutting/stabbing type. Slash died into it
    // 2026-10-01 (M-193): a sword edge and an arrow head argue with armour
    // the same way, and two columns for one argument was the ninth column.
    Pierce = 0,
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
// EIGHT IS THE NUMBER, AND THE COMPILER HOLDS IT (owner, CANON S13/S15/S26:
// «8 типов уронов и 8 типов брони это на всю игру! ни больше не меньше»). It
// is not taste: 8 columns of a byte each is ONE 128-bit SIMD register for the
// whole defence of a body (armour + block, tables above), and 8 types is a
// one-byte mask. A ninth row added here stops COMPILING rather than quietly
// costing a second register in the hottest loop of the game.
static_assert(kDamageTypeCount == 8,
              "CANON S13: ровно 8 типов урона на всю игру — и это по-двойка, "
              "покупающая один SIMD-регистр на всю защиту тела");

struct DamageTypeDef {
    DamageType  type;
    const char* key;    // machine id for content files / console
    const char* label;  // what a panel prints
};

inline constexpr DamageTypeDef kDamageTypeDefs[kDamageTypeCount] = {
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
// the whole scale on which every armour number in the game reads. 10 is the
// historical plain blow (the pre-dice player's bare-handed 10, the anchor
// every creature and armour row was tuned against), so "armour 10" says
// «this body halves the historical plain blow» — and, since the hybrid law
// below took over, «...and shrugs anything up to that off entirely». A bare
// FIST is now the honest 1d2 of the fist's own row: useless against plate,
// which is what the threshold branch is for.
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

// ── THE strike assembly (CANON S13/S14) ────────────────────────────────────
// One algebra for every blow, so no attack site can grow a private law:
//     wound = (roll NdM + attribute add) · skill percent, then the crit.
// Attributes ADD (flatAdd = the sheet's raw damage, floored to the int
// house), skills MULTIPLY (multPct in whole percent: 100 = untrained,
// 100 + rank·pctPerRank for the weapon in hand), and LCK asks the crit door
// once per strike — a crit is the blade finding the ARMOUR GAP, so it rides
// to the damage door as "mitigation does not apply", never as a multiplier.
struct StrikeRoll {
    int amount = 0;
    bool critical = false;
};

inline StrikeRoll roll_strike(Rng& rng, Dice dice, int flatAdd, int multPct,
                              int luck) {
    StrikeRoll out{};
    const int raw = roll_dice(rng, dice) + flatAdd;
    out.amount = raw > 0 ? raw * multPct / 100 : 0;
    out.critical = crit_procs(rng, luck);
    return out;
}

// What the auto-resolve credits per swing — the strike's exact expectation
// (dice_mean_x2 keeps the half-point without floats: ×2 throughout, halved
// once at the end). Crit expectation is deliberately uncredited, like the
// threshold branch: it depends on the victim's armour, which a per-fighter
// scalar does not know (see auto_battle.h fighter_power).
constexpr int strike_mean_x2(Dice dice, int flatAdd, int multPct) {
    const int rawX2 = dice_mean_x2(dice) + flatAdd * 2;
    return rawX2 > 0 ? rawX2 * multPct / 100 : 0;
}

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла, и это не небрежность:
// заголовок пользуется контуром только в блоке ниже, а вставленная
// наверху строка сдвинула бы ВСЁ содержимое на единицу и сгноила бы
// каждую ссылку документов в этот файл (замер: одна такая вставка в 17
// заголовков сломала 10 ссылок в SKELETON.md и промтах). Номер строки —
// производное (§13 п.5), и дешевле не двигать его вовсе.
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА ────────────────────────────────────────────────
// Судит КОМПИЛЯТОР, а не ревью: строка мира, в которую заложили вектор, строку,
// карту, умный указатель, функтор или виртуальный метод, отсюда НЕ СОБЕРЁТСЯ, и
// сообщение назовёт тип поимённо (`core/row_law.h`, наряд M-169).
// Новая структура в этом файле обязана появиться и в этом списке — за полнотой
// списка следит `arch_guard_test`, иначе стену обходили бы молча, новым типом.
TIMAERT_ROW(sm::DamageTypeDef);
TIMAERT_ROW(sm::ArmorProfile);
TIMAERT_ROW(sm::StrikeRoll);

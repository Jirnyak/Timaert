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

// THE SCALE EVERY DEFENCE NUMBER IN THE GAME READS ON. 10 is the historical
// plain blow (the pre-dice player's bare-handed 10, the anchor every creature
// and armour row was tuned against), so "armour 10" says «this body halves the
// historical plain blow», and "block 10" says «this plate eats one plain blow
// whole». A bare FIST is the honest 1d2 of the fist's own row: useless against
// plate, which is what the block column is for.
inline constexpr int kArmorHalving = 10;

// ── A BODY'S DEFENCE: TWO COLUMNS, AND THE PAIR IS THE WHOLE ANSWER ────────
// CANON S13 «ЗАКОН ЗАЩИТЫ — ДВЕ КОЛОНКИ, ОДНО ВЫРАЖЕНИЕ» (вердикт владельца
// 2026-09-30, дословно: «НИКАКИХ ГАВЕН! В ЖОПУ У НАС ПРОЦЕНТНАЯ БРОНЯ + БЛОК»).
//
// WHY TWO. The old single column carried TWO jobs through a max(): a threshold
// («a blow no bigger than the plate finds no flesh») and a percent («a big blow
// is softened, never zeroed»). Two jobs is two columns, and the max() died with
// its reason. They are not interchangeable: the percent column is
// self-limiting by construction, the flat one is linear — which is why only
// one of them ever goes offline (M-194) and why their authored scales differ.
//
// WHY THESE WIDTHS. 8 + 8 = exactly 16 bytes = ONE 128-bit SIMD register, so a
// body's whole defence is one load with no padding holes — that is what the
// po2 count of damage types buys (static_assert above). The STORED width is
// the AUTHORING width; the effective sum (row + worn × ranks) is `int` and
// never comes back to a byte — a sum squeezed into the authoring type would
// hit its ceiling on accumulated junk, which is exactly what the old
// `uniform_armor` clamp at 255 did.
struct Defense {
    // БРОНЯ — ПРОЦЕНТ. Signed by ЗАКОН ТИПА, and the sign is a MECHANIC, not a
    // guard: negative armour is VULNERABILITY (owner: «-127 это иммунитет
    // наоборот ну то есть удвоение урона формула остаётся гладкой»). Range is
    // −127…127 and deliberately SYMMETRIC: −128 is not used, because «нет
    // брони» is 0 and a value carrying a second meaning would be a sentinel
    // nobody asked for. The authored range IS the type's range; a second
    // «authored ceiling» number would have no derivation (CANON S13, уточнение
    // владельца 2026-10-01).
    //
    // И ЗДЕСЬ НЕТ ИММУНИТЕТА: колонка чисто процентная и удар не обнуляет
    // НИКОГДА (127 пропускает 10/137 = 7.3%). Блок его тоже не даёт — он
    // плоский с капом типа 255 при лейт-уроне в тысячи. Абсолютного иммунитета
    // в этом законе НЕТ НИГДЕ, и это ОТКРЫТЫЙ ВОПРОС владельцу (CANON S13,
    // поправка 2026-10-01), а не свойство, которое кто-то здесь заложил.
    std::array<std::int8_t, kDamageTypeCount> armor{};
    // БЛОК — ПЛОСКО, и он НИКОГДА не уходит в простой (M-194). Unsigned: a
    // negative flat column would mean «+127 to every poke», which would drown
    // the dice — the vulnerability axis lives in `armor` alone, one axis per
    // question. 255 is the TYPE's ceiling; the AUTHORED ceiling is
    // 2·kArmorHalving = 20 («лучшая плита каталога глушит два плоских удара»),
    // because a flat column is multiplied LINEARLY by rank and would otherwise
    // become a wall under the whole late-game damage band (CANON S13).
    std::array<std::uint8_t, kDamageTypeCount> block{};

    constexpr int armor_of(DamageType t) const {
        return int(armor[std::size_t(t)]);
    }
    constexpr int block_of(DamageType t) const {
        return int(block[std::size_t(t)]);
    }
};
static_assert(sizeof(Defense) == 16,
              "защита тела обязана лечь в ОДИН 128-битный регистр: 8 колонок "
              "брони по байту + 8 колонок блока по байту, без дыр паддинга");

// What the EFFECTIVE defence of a body is, for ONE damage type: the row, the
// worn pieces and the ranks, already summed. Deliberately `int` and
// deliberately NOT `Defense` — see the width note above.
struct DefenseSum {
    int armor = 0;
    int block = 0;
};

// The mechanical translation for a row authored as one number: the same value
// in every armour column, block left to the row's own authoring (a hide has
// armour and no block; rigid plate and a golem's shell are what block is for).
constexpr Defense uniform_armor(int x) {
    Defense d{};
    const std::int8_t c = std::int8_t(x < -127 ? -127 : x > 127 ? 127 : x);
    for (std::size_t i = 0; i < kDamageTypeCount; ++i) d.armor[i] = c;
    return d;
}

// ...and the same for a row that also has a flat block (uniform in both).
constexpr Defense uniform_defense(int armour, int block) {
    Defense d = uniform_armor(armour);
    const std::uint8_t b =
        std::uint8_t(block < 0 ? 0 : block > 255 ? 255 : block);
    for (std::size_t i = 0; i < kDamageTypeCount; ++i) d.block[i] = b;
    return d;
}

// ── THE DEFENCE LAW — ONE EXPRESSION, NO BRANCH ON THE SIGN ────────────────
// Sequential by owner's verdict («да давай последовательно»): the flat block
// comes off first, and what is left argues with the percent.
//
//     out = (dmg − B) · (d − 2·min(A,0)) / (d + |A|),    d = kArmorHalving
//
//   · A ≥ 0 → dmg·d/(d+A): percent armour, self-limiting — twice the armour
//     never gives half the damage again;
//   · A < 0 → dmg·(d−2A)/(d−A): VULNERABILITY, asymptote ×2 (at −127 it is
//     ×1.927). Only the percent half is mirrored, never the flat one: a flat
//     mirror would add |A| to every poke and drown the dice;
//   · A = 0 → dmg. Ноль — ЗНАЧЕНИЕ, а не отсутствие закона.
//
// SMOOTH IS LITERAL, not a figure of speech: at A = 0 both halves give ×1 AND
// the same derivative (−0.1 per point of armour), so the curve has no kink
// where protection turns into vulnerability.
//
// RANGE: the worst multiplier is 264/137 at A = −127, so the product is honest
// up to dmg ≈ 8.1 million — three orders above this world's damage band (late
// game is thousands by CANON S13), and `int` is never at risk.
//
// ONE HOME, TWO READERS: the damage door subtracts this, and the auto-resolve
// credits the SAME law inverted as effective HP (auto_battle.h) — two answers
// to «сколько держит тело» would be two laws of battle (S13).
constexpr int mitigate_amount(int dmg, int armour, int block) {
    if (dmg <= 0) return 0;
    const int afterBlock = dmg - block;
    if (afterBlock <= 0) return 0;
    const int lo  = armour < 0 ? armour : 0;        // min(A, 0)
    const int mag = armour < 0 ? -armour : armour;  // |A|
    return afterBlock * (kArmorHalving - 2 * lo) / (kArmorHalving + mag);
}

// The same law as a MULTIPLIER on effective HP — what a per-fighter scalar
// needs (auto_battle.h). It is `1/M` of the percent half exactly: at A ≥ 0 this
// is the historical (d+A)/d, and at A < 0 it honestly falls below 1. The flat
// block is NOT credited here and cannot be: its worth depends on the size of
// the incoming blow, which a scalar does not know — the same blindness the old
// threshold branch had, named out loud rather than papered over.
constexpr int armor_hp_mult_num(int armour) {
    return kArmorHalving + (armour < 0 ? -armour : armour);
}
constexpr int armor_hp_mult_den(int armour) {
    return kArmorHalving - 2 * (armour < 0 ? armour : 0);
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
TIMAERT_ROW(sm::Defense);
TIMAERT_ROW(sm::DefenseSum);
TIMAERT_ROW(sm::StrikeRoll);

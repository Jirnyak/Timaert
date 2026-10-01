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

// ПЛОСКИЙ УДАР — единица, в которой мерится БЛОК. 10 это исторический простой
// удар (безоружная десятка до-кубиковой эпохи, анкер, по которому выверялись
// строки), поэтому «блок 10» читается как «эта плита съедает один плоский удар
// целиком». Голый КУЛАК — честные 1d2 своей строки: против плиты бесполезен,
// ровно для этого колонка блока и есть.
//
// ИМЯ ИСПРАВЛЕНО 2026-10-01 (M-197): константа звалась `kArmorHalving` и ВРАЛА
// именем — «про половину» она не была никогда, она была РАЗМЕРОМ УДАРА. Колонке
// брони она больше не нужна вовсе: та стала процентом и мерится в процентах.
inline constexpr int kPlainBlow = 10;

// ПОЛНАЯ ЗАЩИТА — СТО ПРОЦЕНТОВ, и это не калибровка, а ОПРЕДЕЛЕНИЕ процента
// (вердикт владельца 2026-10-01, дословно: «давай процентами просто 100% это и
// есть 100 а всё что выше это сверх»). Единственное число в законе защиты, и
// менять его нельзя не потому, что так решил автор, а потому, что процентов
// больше ста не бывает.
inline constexpr int kArmorFull = 100;

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
    // БРОНЯ — ЭТО СРАЗУ ПРОЦЕНТ СНЯТОГО УРОНА, И ЧИСЛО ЕСТЬ МЕХАНИКА (вердикт
    // владельца 2026-10-01: «давай процентами просто 100% это и есть 100 а всё
    // что выше это сверх»). 40 читается как «сорок процентов срезано», без
    // формулы в голове у читателя и у балансировщика.
    //
    // ОТСЮДА ИММУНИТЕТ ВЫПАДАЕТ САМ: `kArmorFull` = 100 и есть «не берёт
    // вовсе», и ему не нужны ни сентинел, ни битовая маска, ни ветка. А ВСЁ,
    // ЧТО ВЫШЕ СТА, — ЗАПАС: диспел, снимающий 30 у элементаля на 130,
    // оставляет его иммунным, а на 120 — уже нет. Запас бесплатен (тип и так
    // его несёт) и читателя ждёт в будущей магии снятия иммунитетов.
    //
    // ЗНАК — МЕХАНИКА, А НЕ СТРАЖ: −100 это РОВНО удвоение урона (владелец:
    // «-127 это иммунитет наоборот»), и теперь это точное равенство, а не
    // асимптота. Диапазон симметричен, −128 не используется: «нет брони» есть
    // 0, и значение со вторым смыслом здесь никому не нужно.
    std::array<std::int8_t, kDamageTypeCount> armor{};
    // БЛОК — ПЛОСКО, в единицах `kPlainBlow`, и он НИКОГДА не уходит в простой
    // (M-194). Unsigned: отрицательная плоская колонка значила бы «+127 к каждой
    // тычке» и утопила бы кубы — ось уязвимости живёт в `armor` одна. 255 это
    // предел ТИПА; балансная верхушка — 2·kPlainBlow = 20 («лучшая плита
    // каталога глушит два плоских удара»), потому что плоскую колонку ранг
    // разгоняет ЛИНЕЙНО (CANON S13).
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

// The mechanical translation for a row authored as one number: the same PERCENT
// in every armour column, block left to the row's own authoring (a hide absorbs
// a share and blocks nothing; rigid plate and a golem's shell are what the block
// column is for).
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

// ── THE DEFENCE LAW — ОДНО ВЫРАЖЕНИЕ, И ВЕТКИ ПО ЗНАКУ БОЛЬШЕ НЕТ ─────────
// Sequential by owner's verdict («да давай последовательно»): плоский блок
// снимается ПЕРВЫМ и в простой не уходит, остаток спорит с процентом.
//
//     out = (dmg − B) · (kArmorFull − A) / kArmorFull
//
//   · A = 0   → dmg. Ноль — ЗНАЧЕНИЕ, а не отсутствие закона;
//   · A = 50  → ровно половина;
//   · A ≥ 100 → НОЛЬ. Иммунитет, выпавший из шкалы, без сентинела и без маски;
//               и всё, что выше ста, есть ЗАПАС под снятие иммунитета;
//   · A = −100 → РОВНО удвоение; A = −127 → ×2.27. Точные равенства вместо
//               прежней асимптоты, и ОТДЕЛЬНОЙ ВЕТКИ ДЛЯ МИНУСА НЕ НУЖНО:
//               (100 − (−100))/100 = 2 выпадает из того же выражения.
//
// ЭТО ЗАМЕНИЛО ГИПЕРБОЛУ `d/(d+A)` 2026-10-01 (M-197, вердикт владельца «давай
// процентами просто»). Что потеряно и названо вслух: у линейного процента НЕТ
// ЗАТУХАНИЯ, поэтому эффективное HP = 100/(100−A) растёт к бесконечности у
// сотни (×2 при 50, ×10 при 90, ×100 при 99). Это не дефект закона, а его
// природа, и лечится он ДИСЦИПЛИНОЙ КОНТЕНТА, записанной в CANON S13: игрок
// живёт в полосе 0…60, а 100+ отдан строкам, которые ОБЯЗАНЫ быть иммунны.
//
// Кламп в ноль — страж ФОРМУЛЫ (урон не бывает отрицательным), а не костыль в
// динамике: за сотней процентов нет «ещё больше защиты», там нет урона вовсе.
constexpr int mitigate_amount(int dmg, int armour, int block) {
    if (dmg <= 0) return 0;
    const int afterBlock = dmg - block;
    if (afterBlock <= 0) return 0;
    const int pct = kArmorFull - armour;
    if (pct <= 0) return 0;          // 100 % и выше: не берёт вовсе
    return afterBlock * pct / kArmorFull;
}

// Тот же закон как МНОЖИТЕЛЬ эффективного HP — то, что нужно скалярному
// резолверу (auto_battle.h): hp · kArmorFull / (kArmorFull − A).
//
// СТРАЖ У СОТНИ НАЗВАН ВСЛУХ: при A ≥ 100 тело не берёт этот тип вовсе, то есть
// эффективное HP БЕСКОНЕЧНО, а скаляр бесконечности не выражает — поэтому
// знаменатель упирается в единицу (A = 99). Смещение названо, как названы блок и
// крит: скалярный резолвер недооценивает иммунного бойца. Умрёт вместе с ним —
// владелец назвал будущую форму прямо: «авторезолв мы будем честно симулить так
// что не будет такого там будет 2д плоскость виртуальная и прям сим сражения».
constexpr int armor_hp_mult_num(int /*armour*/) { return kArmorFull; }
constexpr int armor_hp_mult_den(int armour) {
    const int den = kArmorFull - armour;
    return den < 1 ? 1 : den;
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

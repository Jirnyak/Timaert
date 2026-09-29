// СЛОВАРЬ ТИПОВ ЧАСТЕЙ ТЕЛА — каталог, общий по обе стороны границы миров
// (наряд M-181; выделен из macro/anatomy.h, потому что строку каталога
// предметов — slotMask как OR масок part_bit — авторит ИМЕННО он, а слой
// tables/ не включает macro/ никогда). Это та часть анатомии, которая
// ПЕРЕЖИВЁТ M-183: система планов тела умирает, словарь ТИПОВ остаётся —
// «есть ли у тела такой слот» станет битовой маской на строке существа, и
// битам этой маски имена даёт этот enum.
//
// Owner's rulings, 2026-08-27:
//   · «ЭКИПИРОВКА = DOD-МАССИВ, НЕ ГРАФ: массив, где ячейка = тип слота; у
//     строки предмета — маска слота, и тогда будет надевание»;
//   · «маска предмета — по ТИПАМ частей, а не по индексам».
//
// If a mask named INDICES, an octopus would need eight enum values for eight
// tentacles and every ring in the game would have to list all of them;
// because it names TYPES, a ring says "Finger" once.
#pragma once

#include "core/table_guard.h"

#include <cstddef>
#include <cstdint>

namespace sm {

// ── The parts a body can have ────────────────────────────────────────────
// ~30 types in four groups, the owner's own proposal. These are TYPES, not
// places: "Finger" is one row whether a body has two of them or twenty.
enum class BodyPartId : std::uint8_t {
    // Armour and clothing — what a blow lands on.
    Head, Face, Neck, Torso, Back, Shoulder, Arm, Hand, Waist, Leg, Foot,
    Wing, Tail,
    // Weapons and the business of fighting.
    Grip,        // a hand free to hold something — where a sword goes
    OffGrip,     // the other one: a shield, a torch, a second blade
    Mouth,       // a beast's bite, a mask that changes it
    Horn,
    Stinger,
    // Ornament — where a bonus rides without covering anything.
    Finger, Ear, Eye, Brow, Pendant, Charm,
    // Utility — carried rather than worn.
    Pack, Belt, Quiver, Pocket, Familiar, Mount,
    Count
};

struct BodyPartDef {
    // MUST equal the row's index in kBodyPartDefs (guard below the table).
    BodyPartId  id;
    const char* key;     // authoring id; runtime addresses by ordinal
    const char* label;
};

inline constexpr BodyPartDef kBodyPartDefs[] = {
    {BodyPartId::Head,     "head",     "Head"},
    {BodyPartId::Face,     "face",     "Face"},
    {BodyPartId::Neck,     "neck",     "Neck"},
    {BodyPartId::Torso,    "torso",    "Torso"},
    {BodyPartId::Back,     "back",     "Back"},
    {BodyPartId::Shoulder, "shoulder", "Shoulder"},
    {BodyPartId::Arm,      "arm",      "Arm"},
    {BodyPartId::Hand,     "hand",     "Hand"},
    {BodyPartId::Waist,    "waist",    "Waist"},
    {BodyPartId::Leg,      "leg",      "Leg"},
    {BodyPartId::Foot,     "foot",     "Foot"},
    {BodyPartId::Wing,     "wing",     "Wing"},
    {BodyPartId::Tail,     "tail",     "Tail"},
    {BodyPartId::Grip,     "grip",     "Hand (main)"},
    {BodyPartId::OffGrip,  "offgrip",  "Hand (off)"},
    {BodyPartId::Mouth,    "mouth",    "Mouth"},
    {BodyPartId::Horn,     "horn",     "Horn"},
    {BodyPartId::Stinger,  "stinger",  "Stinger"},
    {BodyPartId::Finger,   "finger",   "Finger"},
    {BodyPartId::Ear,      "ear",      "Ear"},
    {BodyPartId::Eye,      "eye",      "Eye"},
    {BodyPartId::Brow,     "brow",     "Brow"},
    {BodyPartId::Pendant,  "pendant",  "Pendant"},
    {BodyPartId::Charm,    "charm",    "Charm"},
    {BodyPartId::Pack,     "pack",     "Pack"},
    {BodyPartId::Belt,     "belt",     "Belt"},
    {BodyPartId::Quiver,   "quiver",   "Quiver"},
    {BodyPartId::Pocket,   "pocket",   "Pocket"},
    {BodyPartId::Familiar, "familiar", "Familiar"},
    {BodyPartId::Mount,    "mount",    "Mount"},
};
static_assert(sizeof(kBodyPartDefs) / sizeof(kBodyPartDefs[0])
                  == std::size_t(BodyPartId::Count),
              "kBodyPartDefs must carry one row per BodyPartId");
static_assert(rows_in_enum_order(kBodyPartDefs, &BodyPartDef::id),
              "kBodyPartDefs rows must stand in BodyPartId order");
// An item's slot mask is a BITMASK over these types, so the count is capped by
// the width of that mask. 64 is the ceiling, and it is stated rather than
// discovered by a silently-dropped bit on the 65th part.
static_assert(std::size_t(BodyPartId::Count) <= 64,
              "a part type must fit the slot mask's bit width");

inline constexpr const BodyPartDef& body_part_def(BodyPartId id) {
    return kBodyPartDefs[std::size_t(id)];
}

// A mask naming one part type. `slotMask` on an item row is an OR of these.
inline constexpr std::uint64_t part_bit(BodyPartId id) {
    return std::uint64_t(1) << std::uint64_t(id);
}

// ── ГЛОБАЛЬНАЯ РАСКЛАДКА СЛОТОВ ЭКИПИРОВКИ (M-183, вердикты владельца
// 2026-09-28) ────────────────────────────────────────────────────────────
// Слот адресуется `тип × 16 + n` ОДИНАКОВО для всех тел: план тела как
// отдельная сущность (AnatomyDef) умер — «есть ли у тела такой слот» стало
// БИТОМ маски. 16 — вердикт владельца («да согласен» на 16 против 10), и
// вывод у числа физический: при по-двойке адрес слота — СДВИГ (t<<4 | n),
// не умножение; запас честный — «восемь щупалец — одна строка» влезает.
inline constexpr int kSlotsPerPartType = 16;
inline constexpr int kEquipCells = int(BodyPartId::Count) * kSlotsPerPartType;
inline constexpr int kSlotMaskWords = (kEquipCells + 63) / 64;   // 480 бит → 8×u64

inline constexpr int equip_cell(BodyPartId t, int n) {
    return (int(t) << 4) | (n & (kSlotsPerPartType - 1));
}
inline constexpr BodyPartId equip_cell_part(int cell) {
    return BodyPartId(std::uint8_t(cell >> 4));
}
static_assert((kSlotsPerPartType & (kSlotsPerPartType - 1)) == 0,
              "адрес слота — сдвиг, значит 16 обязано быть степенью двойки");
static_assert(equip_cell(BodyPartId::Mount, kSlotsPerPartType - 1)
                  == kEquipCells - 1,
              "последний слот последнего типа замыкает раскладку без дыр");

// Маска «какие слоты у этого тела ЕСТЬ». На строке существа — дефолт вида;
// в анкете носителя — ЕГО тело (редактор тел: отрастить конечность =
// поставить бит, В РАНТАЙМЕ, без новой строки контента — вердикт владельца:
// «за немного пустой памяти бесплатный редактор тел, эмерджентное
// отращивание конечностей»).
struct SlotMask {
    std::uint64_t bits[kSlotMaskWords]{};

    constexpr bool has(int cell) const {
        return cell >= 0 && cell < kEquipCells
            && ((bits[std::size_t(cell >> 6)] >> (cell & 63)) & 1u) != 0u;
    }
    constexpr void set(int cell) {
        if (cell >= 0 && cell < kEquipCells)
            bits[std::size_t(cell >> 6)] |= std::uint64_t(1) << (cell & 63);
    }
    constexpr void clear(int cell) {
        if (cell >= 0 && cell < kEquipCells)
            bits[std::size_t(cell >> 6)] &= ~(std::uint64_t(1) << (cell & 63));
    }
    constexpr int count() const {
        int n = 0;
        for (std::uint64_t w : bits)
            for (; w; w &= w - 1) ++n;
        return n;
    }
};
static_assert(sizeof(SlotMask) == 64, "маска тела = 8 слов ровно");

// Авторская линия плана: «столько-то частей такого типа». Восемь щупалец —
// одна линия, не восемь имён (закон прежней анатомии, переживший её).
struct PartCount {
    BodyPartId   part;
    std::uint8_t count;
};

template <std::size_t N>
constexpr SlotMask make_slots(const PartCount (&lines)[N]) {
    SlotMask m{};
    for (const PartCount& l : lines) {
        const int cap = int(l.count) < kSlotsPerPartType ? int(l.count)
                                                         : kSlotsPerPartType;
        for (int n = 0; n < cap; ++n) m.set(equip_cell(l.part, n));
    }
    return m;
}

// Четыре плана прежней kAnatomyDefs — те же линии, выраженные масками.
inline constexpr SlotMask kHumanoidSlots = make_slots<24>(
    {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Neck, 1},
     {BodyPartId::Torso, 1}, {BodyPartId::Back, 1}, {BodyPartId::Shoulder, 2},
     {BodyPartId::Arm, 2}, {BodyPartId::Hand, 2}, {BodyPartId::Waist, 1},
     {BodyPartId::Leg, 2}, {BodyPartId::Foot, 2},
     {BodyPartId::Grip, 1}, {BodyPartId::OffGrip, 1}, {BodyPartId::Mouth, 1},
     {BodyPartId::Finger, 10}, {BodyPartId::Ear, 2}, {BodyPartId::Eye, 2},
     {BodyPartId::Brow, 1}, {BodyPartId::Pendant, 1}, {BodyPartId::Charm, 2},
     {BodyPartId::Pack, 1}, {BodyPartId::Belt, 1}, {BodyPartId::Quiver, 1},
     {BodyPartId::Pocket, 2}});
inline constexpr SlotMask kQuadrupedSlots = make_slots<13>(
    {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Neck, 1},
     {BodyPartId::Torso, 1}, {BodyPartId::Back, 1}, {BodyPartId::Leg, 4},
     {BodyPartId::Foot, 4}, {BodyPartId::Tail, 1}, {BodyPartId::Mouth, 1},
     {BodyPartId::Horn, 2}, {BodyPartId::Ear, 2}, {BodyPartId::Eye, 2},
     {BodyPartId::Pack, 1}});
inline constexpr SlotMask kSerpentSlots = make_slots<7>(
    {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Torso, 1},
     {BodyPartId::Tail, 1}, {BodyPartId::Mouth, 1}, {BodyPartId::Eye, 2},
     {BodyPartId::Stinger, 1}});
inline constexpr SlotMask kAvianSlots = make_slots<12>(
    {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Neck, 1},
     {BodyPartId::Torso, 1}, {BodyPartId::Back, 1}, {BodyPartId::Wing, 2},
     {BodyPartId::Leg, 2}, {BodyPartId::Foot, 2}, {BodyPartId::Tail, 1},
     {BodyPartId::Mouth, 1}, {BodyPartId::Eye, 2}, {BodyPartId::Charm, 1}});

// Гуманоид несёт 42 слота (сумма линий выше) — если число сдвинулось,
// сдвинулась либо таблица линий, либо раскладка; оба сдвига обязаны быть
// НАЗВАННЫМИ.
static_assert(kHumanoidSlots.count() == 42,
              "линии гуманоида дают 42 слота — сверь таблицу выше");

} // namespace sm

// Контур строк файла — судит компилятор (`core/row_law.h`, наряд M-169):
// строка каталога с вектором, строкой или виртуальным методом отсюда не
// соберётся. Включение стоит внизу, чтобы не сдвигать номера строк (§13 п.5).
#include "core/row_law.h"
TIMAERT_ROW(sm::BodyPartDef);
TIMAERT_ROW(sm::SlotMask);
TIMAERT_ROW(sm::PartCount);

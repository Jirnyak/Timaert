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

} // namespace sm

// Контур строк файла — судит компилятор (`core/row_law.h`, наряд M-169):
// строка каталога с вектором, строкой или виртуальным методом отсюда не
// соберётся. Включение стоит внизу, чтобы не сдвигать номера строк (§13 п.5).
#include "core/row_law.h"
TIMAERT_ROW(sm::BodyPartDef);

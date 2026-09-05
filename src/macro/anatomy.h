// ANATOMY IS A ROW, AND EQUIPMENT IS A FLAT ARRAY OVER IT.
//
// Owner's rulings, 2026-08-27:
//   · «ЭКИПИРОВКА = DOD-МАССИВ, НЕ ГРАФ: массив, где ячейка = тип слота; у
//     строки предмета — маска слота, и тогда будет надевание»;
//   · «АНАТОМИЯ СТРОКОЙ, 128 ячеек (как в Caves of Qud и Elin): тело = массив
//     128 × {тип части, предмет}; сколько и каких частей — говорит СТРОКА
//     АНАТОМИИ (человек/дракон/спрут); маска предмета — по ТИПАМ частей, а не
//     по индексам».
//
// That last clause is the whole design. If a mask named INDICES, an octopus
// would need eight enum values for eight tentacles and every ring in the game
// would have to list all of them; because it names TYPES, an octopus is one
// anatomy row with eight cells of one type, and a ring says "Finger" once.
// Growing a limb, losing one, or bolting on a prosthetic is then a change to
// the cell array and nothing else — no code, no new id, no mask edited.
//
// WHO CARRIES ONE. Not the crowd. A creature's own defence is a number on its
// row (npc.h `NpcTypeDef::armor`, the owner's «броня массовки = число из
// строки») because a troll's hide is what a troll IS. `Equipment` is opt-in,
// for bodies that actually own things — the player, named lords — so sixteen
// thousand macro squads pay nothing for a system they do not use.
#pragma once

#include "core/table_guard.h"
#include "macro/items.h"

#include <array>
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

// ── The shapes a body can be ─────────────────────────────────────────────

// 128 cells (po2) is the envelope, and it is generous on purpose: an octopus
// with eight tentacles, a hydra with nine heads and a centipede all fit
// without a second container, and a body that grows or loses a limb changes
// this array and nothing else.
inline constexpr int kMaxBodyParts = 128;

enum class AnatomyId : std::uint8_t {
    Humanoid, Quadruped, Serpent, Avian,
    Count
};

// One line of a shape: "this many of this part". The cells are the EXPANSION
// of these lines, which is why eight tentacles cost one line and not eight
// names in an enum.
struct AnatomyPart {
    BodyPartId   part;
    std::uint8_t count;
};

inline constexpr int kMaxAnatomyLines = 24;

struct AnatomyDef {
    // MUST equal the row's index in kAnatomyDefs (guard below the table).
    AnatomyId    id;
    const char*  key;
    const char*  label;
    AnatomyPart  parts[kMaxAnatomyLines];   // trailing {} lines are absent
};

inline constexpr AnatomyDef kAnatomyDefs[] = {
    {AnatomyId::Humanoid, "humanoid", "Humanoid",
     {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Neck, 1},
      {BodyPartId::Torso, 1}, {BodyPartId::Back, 1}, {BodyPartId::Shoulder, 2},
      {BodyPartId::Arm, 2}, {BodyPartId::Hand, 2}, {BodyPartId::Waist, 1},
      {BodyPartId::Leg, 2}, {BodyPartId::Foot, 2},
      {BodyPartId::Grip, 1}, {BodyPartId::OffGrip, 1}, {BodyPartId::Mouth, 1},
      {BodyPartId::Finger, 10}, {BodyPartId::Ear, 2}, {BodyPartId::Eye, 2},
      {BodyPartId::Brow, 1}, {BodyPartId::Pendant, 1}, {BodyPartId::Charm, 2},
      {BodyPartId::Pack, 1}, {BodyPartId::Belt, 1}, {BodyPartId::Quiver, 1},
      {BodyPartId::Pocket, 2}}},

    {AnatomyId::Quadruped, "quadruped", "Quadruped",
     {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Neck, 1},
      {BodyPartId::Torso, 1}, {BodyPartId::Back, 1}, {BodyPartId::Leg, 4},
      {BodyPartId::Foot, 4}, {BodyPartId::Tail, 1}, {BodyPartId::Mouth, 1},
      {BodyPartId::Horn, 2}, {BodyPartId::Ear, 2}, {BodyPartId::Eye, 2},
      {BodyPartId::Pack, 1}}},

    {AnatomyId::Serpent, "serpent", "Serpent",
     {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Torso, 1},
      {BodyPartId::Tail, 1}, {BodyPartId::Mouth, 1}, {BodyPartId::Eye, 2},
      {BodyPartId::Stinger, 1}}},

    {AnatomyId::Avian, "avian", "Avian",
     {{BodyPartId::Head, 1}, {BodyPartId::Face, 1}, {BodyPartId::Neck, 1},
      {BodyPartId::Torso, 1}, {BodyPartId::Back, 1}, {BodyPartId::Wing, 2},
      {BodyPartId::Leg, 2}, {BodyPartId::Foot, 2}, {BodyPartId::Tail, 1},
      {BodyPartId::Mouth, 1}, {BodyPartId::Eye, 2}, {BodyPartId::Charm, 1}}},
};
static_assert(sizeof(kAnatomyDefs) / sizeof(kAnatomyDefs[0])
                  == std::size_t(AnatomyId::Count),
              "kAnatomyDefs must carry one row per AnatomyId");
static_assert(rows_in_enum_order(kAnatomyDefs, &AnatomyDef::id),
              "kAnatomyDefs rows must stand in AnatomyId order");

inline constexpr const AnatomyDef& anatomy_def(AnatomyId id) {
    return kAnatomyDefs[std::size_t(id)];
}

// How many cells this shape expands to. A row that asks for more than the
// envelope holds is TRUNCATED here and the excess is invisible — which is why
// the contract test counts every row against kMaxBodyParts.
inline constexpr int anatomy_cell_count(const AnatomyDef& d) {
    int n = 0;
    for (const AnatomyPart& p : d.parts) {
        if (p.count == 0) continue;
        n += int(p.count);
    }
    return n < kMaxBodyParts ? n : kMaxBodyParts;
}

// THE expansion: which part type cell `i` is. This is the only place a shape
// row becomes an array of slots, so "eight tentacles" and "two hands" are the
// same sentence to everything downstream.
inline constexpr BodyPartId anatomy_part_at(const AnatomyDef& d, int cell) {
    int seen = 0;
    for (const AnatomyPart& p : d.parts) {
        if (p.count == 0) continue;
        if (cell < seen + int(p.count)) return p.part;
        seen += int(p.count);
    }
    return BodyPartId::Count;   // past the body: no such part
}

static_assert(anatomy_cell_count(kAnatomyDefs[0]) <= kMaxBodyParts);
static_assert(anatomy_cell_count(kAnatomyDefs[1]) <= kMaxBodyParts);
static_assert(anatomy_cell_count(kAnatomyDefs[2]) <= kMaxBodyParts);
static_assert(anatomy_cell_count(kAnatomyDefs[3]) <= kMaxBodyParts);

// ── What a body is wearing ───────────────────────────────────────────────
//
// A flat array indexed by CELL, exactly the owner's «DOD-массив, где ячейка =
// тип слота». No graph, no per-slot struct, no map: the cell's type is a pure
// function of the anatomy row, so the container stores only what is IN each
// cell and nothing about what the cell IS.
struct Equipment {
    std::uint8_t anatomy = std::uint8_t(AnatomyId::Humanoid);
    std::array<ItemRef, kMaxBodyParts> worn{};

    const AnatomyDef& shape() const {
        const std::uint8_t a = anatomy < std::uint8_t(AnatomyId::Count)
                                   ? anatomy : 0;
        return kAnatomyDefs[a];
    }
    int cells() const { return anatomy_cell_count(shape()); }
    BodyPartId part_at(int cell) const {
        return anatomy_part_at(shape(), cell);
    }
};

// Can this item go on that cell? Two questions, both answered by masks the
// item row carries: does the cell's TYPE appear in `slotMask`, and is the cell
// within the body at all.
bool item_fits_cell(const Equipment& eq, int cell, const ItemDef& def);

// Put it on. Returns the cell it landed in, or -1 if nothing on this body can
// take it — REFUSAL, never a silent drop, because an item that vanishes on
// equip is an item the conservation law lost.
//
// A two-hander is the reason `blocksMask` exists: wearing it also occupies
// every cell whose type it blocks, and those cells must be EMPTY first. What
// blocking looks like in the array is a reserved marker, not a second
// container — see `kBlockedByDef`.
int equip(Equipment& eq, const ItemRef& item);

// Take it off the given cell, returning what was there ({} if nothing). Any
// cells that item was blocking are released with it.
ItemRef unequip(Equipment& eq, int cell);

// Everything worn, summed: the standing bonuses of every affix on every worn
// item, in the one currency the sheet already reads.
BonusTotals worn_bonuses(const Equipment& eq);

// Re-derive the cells a worn item BLOCKS from the rows it is wearing. The
// blocks are not stored — a second copy of what the catalog already says
// could disagree with it after a retune — so a load re-marks them.
void remark_equipment_blocks(Equipment& eq);

// How many cells actually hold something — for a panel that wants to say
// "3 of 24 filled" without walking the array itself.
int worn_cells(const Equipment& eq);

// Armour worn, summed per DamageType column (saturating at the column's 255
// ceiling) — the addend `sub/damage.cpp defense_of` was waiting for. A
// creature's own row profile and what it wears meet column by column.
ArmorProfile worn_armor(const Equipment& eq);

} // namespace sm

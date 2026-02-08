#pragma once

#include <cstdint>

namespace systems {

enum class PerkID : std::uint8_t {
    Immortal,        // never die from old age, 100% more exp needed to lvl up.
    ShortLived,      // die of old age at 33, but gain 100% more exp.
    Mechanical,      // no level-up or EXP; start with +100 attribute points & choose 10 skills (level 1)
    Talented,        // instantly gain 1 level; uses perk point
    Gifted,          // choose two attributes; one is multiplied by 2, another is divided by 2
    GodsMark,        // after 1 year you die; all attributes doubled
    Saint,           // live according to Light; sin = -1 random attribute, obey = +1 random attribute
    Possess,         // no own body; can possess living beings; adopts their level/skills/attributes; cannot grow
    DeathWord,       // you can kill anyone with one word; you have only 1 HP forever
    Antimagus,       // cannot cast magic; invincible to magic
    MagicBody,       // Mana counts as HP (HP is not used anymore)
    BloodMagic,      // can spend HP to cast spells (MP is not used anymore)
    Autist,          // choose a mirror attribute; all checks swaps that attribute with the mirror (e.g. STR <-> INT, WIS <-> CHA, etc.)
    Leader,          // start with own faction, ready-to-play, your age is increased by 10 years.
    Specialization,  // all attribute & skill points go to chosen stat/skill, + 1 more point per level.
    Generalist,      // all attribute & skill points go to lowest stats/skills, + 1 more point per level.
    Educated,         // gain +1 skill point per level, but no attribute points
    Natural,         // gain +1 attribute point per level, but no skill points
    Apostle,         // you are champion of dead god, everyone is your enemy except cultists, + 1 random attribute per level and random black artifact
    Demiurg,         // you cannot interact with mortals, but can shape the world - editor/spectator mode.
    Revenant,        // death is not the end; after death you resurrect with 1 HP, but lose all items and gold; each resurrection reduces base HP by 10 and -1 random attribute; if base HP reaches 0, you die permanently.
    Stonks,          // duplicate all your gold immediately; but bubble can burst... with 10% you loose all your gold instead.
    Sacrilegist,     // your spellpower is increased by 100%, but you are hated by Empire and Cultists.
    KingPesant,      // you posess black spear artifact and 50% magic resistant, but you are hated by Magi.

    Count
};

} // namespace systems

// THE sprite table — one row per visible kind, assets and procedural bodies in
// the SAME list (owner, 2026-08-20). A goblin sits next to a peasant: one names
// a body plan the shader draws, the other names a PNG the artist drew, and a
// reader of this file never has to know there were ever two mechanisms.
//
// The law (sprites.md):
//   * drawn art wins — a row with an `asset` is that picture;
//   * the procedural archetype is the FLOOR, not a fallback of shame — a row
//     without art is drawn by shaders/creature_sprite.glsl from its body plan,
//     which is also what casts its shadow (one coverage, one shadow);
//   * every visible kind resolves to SOMETHING. Nobody is left without a
//     sprite, which is what "the game takes from here contextually" means.
//
// Context is expressed as SEPARATE ROWS, not as a variant hidden in one
// (SpireActive / SpireSpent). Flat rows are readable at a glance and the
// enum-order guard covers them; the one time a variant was picked by arithmetic
// instead of by a row, a spire chose its picture by the PARITY OF ITS ID and no
// smoke could see it (problems.md §23).
//
// Kind tables point HERE by ordinal (`FaunaEntry::sprite`, `NpcTypeDef::sprite`,
// `kLandmarkDraw[].sprite`). Several kinds may share one row on purpose — every
// unremarkable townsman is `Peasant` — so this table is the vocabulary of
// PICTURES, not a mirror of the kind lists.
//
// Lives in macro/ because that is where this project keeps its universal data
// registries (faction.h, fauna.h, commodity.h, spells.h): every layer above can
// read it, and it depends on nothing but core/.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/table_guard.h"

namespace sm {

// Procedural body plan for the subworld billboard. Values MUST match
// shaders/creature_sprite.glsl, which draws the silhouette AND is called by the
// depth-only caster (shadow_creature.frag) so no body casts a shadow it lacks.
enum class CreatureArchetype : std::uint8_t {
    Quadruped = 0, // horizontal 4-legged beast (rabbit / wolf / bear / croc …)
    Avian     = 1, // winged (hawk / eagle)
    Serpent   = 2, // legless sinuous (snake)
    Biped     = 3, // upright humanoid monster (goblin / troll / swamp thing)
    Undead    = 4, // thin bony / ghostly upright (skeleton / ice wraith)
    Hulk      = 5, // massive blocky (stone golem)
    Critter   = 6, // tiny squat blob (frog)
};

// A row that is not a body at all — a town, a spire, a coin icon. It has no
// procedural silhouette to fall back to; if its art is missing the consumer
// draws its own mark (kLandmarkDraw's glyph circle, an ImGui blank).
inline constexpr std::uint8_t kNoBody = 0xFF;

// (width, height) billboard multipliers per body plan — how much quad one unit
// of creature `size` buys. This lived as a GLSL function copied into
// creature.vert AND shadow_creature.vert under a "MUST match" comment; since
// the unified billboard idiom (gpu/bb_instance.h) the aspect is applied ONCE on
// the CPU and both silhouettes read the same instance extents — the data
// belongs beside the enum it describes, not in two shaders.
struct CreatureAspect {
    float w, h;
};
inline constexpr CreatureAspect kCreatureAspects[7] = {
    {1.70f, 1.15f}, // Quadruped (wide, low)
    {1.50f, 1.05f}, // Avian
    {1.15f, 1.50f}, // Serpent (tall)
    {1.25f, 1.80f}, // Biped
    {1.10f, 1.80f}, // Undead
    {1.80f, 2.05f}, // Hulk
    {0.95f, 0.80f}, // Critter
};
inline CreatureAspect creature_arch_aspect(std::uint8_t archetype) {
    return kCreatureAspects[archetype < 7 ? archetype : 6];
}

// ── The vocabulary of pictures ──────────────────────────────────────
// Order is free to grow at the tail; `None` stays first so a zeroed row draws
// nothing rather than a city.
enum class SpriteId : std::uint8_t {
    None = 0,
    // Places — drawn art; no body plan, no tint (the ONE colour authority for a
    // landmark is kLandmarks[].color, owner ruling 2026-08-14).
    City,
    Village,
    SpireActive,
    SpireSpent,
    // People — drawn art today, a biped silhouette the day an artist has not
    // caught up. Several NPC kinds share each of these on purpose.
    Peasant,
    Caravan,
    Witch,
    Sorceress,
    Bandit,
    // Wildlife — procedural, tinted per creature.
    Rabbit, Deer, Fox, Wolf, Bear, Boar, Snake, Hawk, Frog, Goat, Eagle,
    Crocodile,
    // Monsters — procedural.
    Goblin, Skeleton, Troll, SwampThing, IceWraith, SandScorpion, StoneGolem,
    // Interface marks that are not world bodies.
    Coins,
    Count_,
};

struct SpriteDef {
    SpriteId      id;         // MUST equal the row's index — asserted below
    const char*   name;       // stable machine id, for tools and tests
    const char*   asset;      // PNG under assets/sprites/, nullptr = none drawn
    std::uint8_t  archetype;  // CreatureArchetype value, or kNoBody
    std::uint32_t tint;       // 0xRRGGBB for a procedural body; 0 = art speaks
};

inline constexpr SpriteDef kSpriteRows[std::size_t(SpriteId::Count_)] = {
    {SpriteId::None,         "none",          nullptr,              kNoBody, 0u},

    {SpriteId::City,         "city",          "city_256.png",       kNoBody, 0u},
    {SpriteId::Village,      "village",       "village_256.png",    kNoBody, 0u},
    {SpriteId::SpireActive,  "spire_active",  "spireA_256.png",     kNoBody, 0u},
    {SpriteId::SpireSpent,   "spire_spent",   "spireD_256.png",     kNoBody, 0u},

    // The player borrows `peasant` until his own figure is drawn — one row to
    // change, not a branch anywhere (sprites.md, stage 1).
    {SpriteId::Peasant,      "peasant",       "peasant_256.png",
                                              std::uint8_t(CreatureArchetype::Biped), 0xB0A090u},
    {SpriteId::Caravan,      "caravan",       "corovan_256.png",
                                              std::uint8_t(CreatureArchetype::Biped), 0xA08050u},
    {SpriteId::Witch,        "witch",         "witch_256.png",
                                              std::uint8_t(CreatureArchetype::Biped), 0xB464C8u},
    {SpriteId::Sorceress,    "sorceress",     "cultistka_256.png",
                                              std::uint8_t(CreatureArchetype::Biped), 0x78C8E6u},
    {SpriteId::Bandit,       "bandit",        "imp_golem_256.png",
                                              std::uint8_t(CreatureArchetype::Biped), 0x8A3A3Au},

    {SpriteId::Rabbit,       "rabbit",        nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0xB8A080u},
    {SpriteId::Deer,         "deer",          nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0xA08060u},
    {SpriteId::Fox,          "fox",           nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0xCC6633u},
    {SpriteId::Wolf,         "wolf",          nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0x666666u},
    {SpriteId::Bear,         "bear",          nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0x5A3A1Au},
    {SpriteId::Boar,         "boar",          nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0x6B4E37u},
    {SpriteId::Snake,        "snake",         nullptr,
                                              std::uint8_t(CreatureArchetype::Serpent),   0x3A5A2Au},
    {SpriteId::Hawk,         "hawk",          nullptr,
                                              std::uint8_t(CreatureArchetype::Avian),     0x8B6B4Bu},
    {SpriteId::Frog,         "frog",          nullptr,
                                              std::uint8_t(CreatureArchetype::Critter),   0x2A8A2Au},
    {SpriteId::Goat,         "goat",          nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0xB0A090u},
    {SpriteId::Eagle,        "eagle",         nullptr,
                                              std::uint8_t(CreatureArchetype::Avian),     0x5A4030u},
    {SpriteId::Crocodile,    "crocodile",     nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0x4A6A3Au},

    {SpriteId::Goblin,       "goblin",        nullptr,
                                              std::uint8_t(CreatureArchetype::Biped),     0x4A8A2Au},
    {SpriteId::Skeleton,     "skeleton",      nullptr,
                                              std::uint8_t(CreatureArchetype::Undead),    0xD0C8B0u},
    {SpriteId::Troll,        "troll",         nullptr,
                                              std::uint8_t(CreatureArchetype::Biped),     0x3A6A3Au},
    {SpriteId::SwampThing,   "swamp_thing",   nullptr,
                                              std::uint8_t(CreatureArchetype::Biped),     0x2A4A1Au},
    {SpriteId::IceWraith,    "ice_wraith",    nullptr,
                                              std::uint8_t(CreatureArchetype::Undead),    0xA0D0E0u},
    {SpriteId::SandScorpion, "sand_scorpion", nullptr,
                                              std::uint8_t(CreatureArchetype::Quadruped), 0xC0A050u},
    {SpriteId::StoneGolem,   "stone_golem",   nullptr,
                                              std::uint8_t(CreatureArchetype::Hulk),      0x7A7A7Au},

    {SpriteId::Coins,        "coins",         "coins.png",          kNoBody, 0u},
};

static_assert(rows_in_enum_order(kSpriteRows, &SpriteDef::id),
              "kSpriteRows must be in SpriteId order");

inline constexpr const SpriteDef& sprite_row(SpriteId id) {
    return kSpriteRows[std::size_t(id) < std::size_t(SpriteId::Count_)
                           ? std::size_t(id)
                           : std::size_t(SpriteId::None)];
}

} // namespace sm

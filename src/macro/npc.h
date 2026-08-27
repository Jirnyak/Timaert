// NPC type registry — faithful port of `src/game/npc.ts`.
//
// Pure data tables. AI lives in macro/npc_ai.{h,cpp}.
// To add a new NPC type:
//   1. Add an enum value to `NPCType`.
//   2. Add an entry to `kNpcTypeDefs[]` with names, talk lines, combat.
//   3. (optional) Add an AI function in npc_ai.cpp and reference it via
//      the `AIBehaviour` enum below.
// No other file needs to change.
#pragma once
#include <array>
#include "core/table_guard.h"
#include "macro/behaviour.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "macro/army.h"
#include "macro/sprite_rows.h"

namespace sm {

enum class NPCType : std::uint8_t {
    Peasant = 0, Woodcutter, Merchant, Caravan, Bandit, Guard, Witch, Sorceress,
    // The gatherer professions of the deposit rows (resources.md): a
    // profession per resource, appended so saved kinds stay stable.
    Miner, Quarryman, ClayDigger,
    // ── and every creature, in the SAME space ─────────────────────────────
    // The monster catalog used to be a second table addressed by `0x100 | row`
    // — a second vocabulary written as a number. It is gone: a wolf is a row
    // here like a guard is, and "is this a monster" is not a question the
    // engine asks any more (owner, 2026-08-20). Appended, so saved ordinals
    // stay stable.
    Rabbit, Deer, Fox, Wolf, Bear, Boar, Snake, Hawk, Frog, Goat, Eagle, Croc,
    Goblin, Skeleton, Troll, SwampThing, IceWraith, SandScorpion, StoneGolem,
    Count,
};

enum class NPCState : std::uint8_t {
    Idle = 0, Wandering, Traveling, Returning, Working, Chasing, Patrolling, Resting,
    // Running from a stronger hostile squad (Session 15): set by the universal
    // threat step in npc_ai.cpp, cleared by it when the threat is gone.
    // Runtime-only like every state here — the ECS is never serialized.
    Fleeing,
};

enum class NPCTrait : std::uint8_t {
    Greedy = 0, Honorable, Cowardly, Brave, Aggressive, Generous, Suspicious, Curious,
    Count,
};


// Fixed-arity name / dialogue pools — POD-friendly.
constexpr std::size_t kMaxNpcNames     = 16;
constexpr std::size_t kMaxNpcTalkLines = 6;
constexpr int kNpcUpkeepNone = -1;

struct NpcTypeDef {
    // MUST equal the row's index in kNpcTypeDefs (guard below the table).
    NPCType         type;
    // Stable machine id — what the console, the spawn tables and any future
    // content file NAME this row by ("peasant", "wolf"). The creature rows
    // brought it with them; the humanoid rows had only a display label, and a
    // display string is not an id.
    const char*     id;
    const char*     label;
    // This kind's picture — a row of THE sprite table (macro/sprite_rows.h),
    // which decides drawn art vs procedural body. Kinds share rows on purpose:
    // every unremarkable townsman is a peasant to the eye. It replaced a dead
    // `portrait` path string that no code ever read — a fourth asset vocabulary
    // nobody was speaking.
    SpriteId        sprite;
    int             baseHp;
    int             baseLevel;
    AIBehaviour     ai;
    CombatTemplate  combat;
    int             upkeepGoldPerDay;
    bool            hireable;
    int             xpReward;

    // ── The wild half of the row ──────────────────────────────────────────
    // These four columns arrived with the creature catalog when the two body
    // tables merged (owner, 2026-08-20: one system, "лорд может быть не только
    // человеком но и драконом"). They are not "monster fields": they are the
    // questions the world asks about ANY row, and a townsman simply answers
    // them with the default.
    //
    // `weight` — how commonly the world rolls this row when it is asked for
    // something by weight rather than by name. It stays on the CREATURE, not
    // on the spawn table (owner: otherwise every dungeon would have to name
    // its bestiary by hand, and "give me a plausible enemy" would be
    // unsayable). 0 = never rolled blind; a place must name this row to get it.
    std::uint16_t   weight     = 0;
    // NO faction column — deliberately (owner ruling 2026-08-27: «в записи
    // существа вообще не должно быть фракции»). Faction is an INSTANCE
    // property (ecs::NPCKind.factionIdx), assigned at birth by the SPAWNER's
    // context: a town dresses its crowd in its kingdom's colours, a landmark
    // in its spawnFaction, a squad in its leader's, the open land in the
    // spawn law's own wildFaction column (macro/fauna.cpp). The same wolf can
    // be wildlife in a meadow, a demon in a ruin, or the player's own.
    // Loot profile override; nullptr = the faction default of the one loot
    // registry (macro/items.h).
    const char*     lootId     = nullptr;
    // Body radius in metres. 0 = derive it from the sheet like a humanoid
    // does (sub/body.h) — the creature rows author it because a rabbit is not
    // a man-sized thing.
    float           radius     = 0.0f;

    // Pools — first `nameCount` / `talkCount` entries are valid.
    std::array<const char*, kMaxNpcNames>     names;
    std::uint8_t                               nameCount;
    std::array<const char*, kMaxNpcTalkLines> talkLines;
    std::uint8_t                               talkCount;

    // Optional carried point light (a torch, lantern or arcane glow). Pure DATA
    // — the subworld renderer already lights any entity that carries an
    // ecs::LightEmitter through one universal gather, so a lit NPC needs no
    // engine change, only a spawn-time attach keyed off these fields (see
    // maybe_emplace_carried_light in sub/spawn.cpp). Kept as plain floats here
    // (not an ecs::LightEmitter) so the macro data layer has no dependency on
    // the ECS component headers. `lightRadius <= 0` ⇒ this type carries no light
    // (the default for every row that omits the field), so lighting a type is
    // strictly opt-in and costs nothing for the rest. Colour is linear RGB, the
    // offset seats the light on the body (metres), intensity is the scalar gain
    // — the same knobs the player lantern uses.
    // Default-initialised to "no light" so every row that omits them (all but
    // the ones that opt in) is dark and warning-free — a class with default
    // member initialisers is still an aggregate (C++14+), so the constexpr
    // brace-init of kNpcTypeDefs below is unaffected.
    float lightRadius    = 0.0f;   // attenuation reach (m); 0 = no carried light
    float lightIntensity = 0.0f;   // scalar gain
    float lightR = 0.0f, lightG = 0.0f, lightB = 0.0f;   // linear RGB radiance
    float lightHeight    = 0.0f;   // metres up from the feet the light is seated
};

inline constexpr CombatTemplate kPeasantCombat   {25, 3,  20, 2.0f, 1.5f, "Psr", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kWoodcutterCombat{30, 8,  20, 2.0f, 1.2f, "Wdc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kMerchantCombat  {30, 5,  25, 2.0f, 1.5f, "Mrc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kCaravanCombat   {25, 4,  30, 2.0f, 1.5f, "Cvn", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kBanditCombat    {50, 12, 45, 3.0f, 1.0f, "Bnd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kGuardCombat     {55, 14, 35, 3.0f, 1.0f, "Grd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kWitchCombat     {60, 18, 30, 20.0f,2.0f, "Wtc", CombatTemplate::Missile, 180, 0, 0xFFA070D0u};
inline constexpr CombatTemplate kSorceressCombat {70, 22, 25, 25.0f,1.8f, "Src", CombatTemplate::Missile, 200, 6, 0xFF70C0E0u};

inline constexpr NpcTypeDef kNpcTypeDefs[std::size_t(NPCType::Count)] = {
    // Peasant
    {
        NPCType::Peasant, "peasant", "Peasant", SpriteId::Peasant, 25, 1,
        AIBehaviour::Gatherer, kPeasantCombat, 1, true, 10,
        /*weight*/55, /*loot*/nullptr, /*radius*/0.0f,
        {{"Ivan","Pyotr","Sergey","Dmitry","Alexei","Nikolai","Vasily","Grigory",
          "Fedor","Andrei","Olga","Natalya","Katya","Masha","Dasha"}}, 15,
        {{"The harvest has been poor this year...",
          "Have you heard? Bandits roam the roads at night.",
          "Blessings upon you, traveler.",
          "I sell nothing of interest, but the merchant might.",
          "Stay safe out there. The wilderness is harsh."}}, 5,
    },
    // Woodcutter
    {
        NPCType::Woodcutter, "woodcutter", "Woodcutter", SpriteId::Peasant, 30, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Borislav","Timofey","Yegor","Luka","Matvey"}}, 5,
        {{"These woods hold many secrets.",
          "Good timber is hard to find lately.",
          "Watch for wolves near the tree line.",
          "I chop from dawn to dusk. Honest work."}}, 4,
    },
    // Merchant
    {
        NPCType::Merchant, "merchant", "Merchant", SpriteId::Caravan, 30, 3,
        AIBehaviour::Trader, kMerchantCombat, kNpcUpkeepNone, false, 30,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Kartash","Bazukin","Torgin","Menkov","Skaldin"}}, 5,
        {{"Looking to trade? I have fine wares!",
          "Gold makes the world go round, friend.",
          "I travel between settlements. The roads are dangerous.",
          "Business has been slow. Perhaps you need something?"}}, 4,
    },
    // Caravan
    {
        NPCType::Caravan, "caravan", "Caravan", SpriteId::Caravan, 25, 2,
        AIBehaviour::CaravanTrade, kCaravanCombat, kNpcUpkeepNone, false, 20,
        /*weight*/0, /*loot*/nullptr, /*radius*/0.0f,
        {{"Putnik","Dorozhkin","Obozov","Strannik","Koleso"}}, 5,
        {{"Long road ahead. Care to trade before I move on?",
          "I have seen many lands. Each stranger than the last.",
          "The roads between settlements grow more perilous.",
          "My oxen grow weary. We rest here briefly."}}, 4,
    },
    // Bandit
    {
        NPCType::Bandit, "bandit", "Bandit", SpriteId::Bandit, 50, 2,
        AIBehaviour::Aggressive, kBanditCombat, kNpcUpkeepNone, false, 20,
        /*weight*/0, /*loot*/nullptr, /*radius*/0.0f,
        {{"Razboy","Diki","Grozny","Slyak","Khvat"}}, 5,
        {{"Your gold or your life!",
          "Heh, another fool wandering the wilds.",
          "I take what I want. Got a problem with that?",
          "The strong survive. The weak feed us."}}, 4,
    },
    // Guard
    {
        NPCType::Guard, "guard", "Guard", SpriteId::Peasant, 55, 3,
        AIBehaviour::Patrol, kGuardCombat, 3, true, 30,
        /*weight*/0, /*loot*/nullptr, /*radius*/0.0f,
        {{"Strazhnik","Boyar","Vityaz","Desyatnik","Druzhina"}}, 5,
        {{"Move along, citizen. Nothing to see here.",
          "The settlement is safe under our watch.",
          "Report any bandit sightings to the elder.",
          "Stay on the roads if you value your life."}}, 4,
        // Night-watch torch: a warm carried light, a touch smaller and dimmer
        // than the player's lantern (radius 16 / intensity 1.35) so the player's
        // own pool still reads as primary and a patrolled street gains pools of
        // firelight that move with the guards. Additive over the directional
        // term ⇒ a warm pool at night, washed out by day, exactly like the
        // lantern — no day/night special-casing. Seated 1.1 m up (chest/held).
        /*lightRadius=*/11.0f, /*lightIntensity=*/1.15f,
        /*lightR=*/1.00f, /*lightG=*/0.66f, /*lightB=*/0.34f,
        /*lightHeight=*/1.1f,
    },
    // Witch
    {
        NPCType::Witch, "witch", "Witch", SpriteId::Witch, 60, 5,
        AIBehaviour::Teleporter, kWitchCombat, kNpcUpkeepNone, false, 50,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.0f,
        {{"Yaga","Vedma","Znakharka","Koldunia","Volshebnitsa"}}, 5,
        {{"The spirits whisper of your coming...",
          "I see great trials ahead for you.",
          "Herbs and potions are my trade. Interested?",
          "The forest knows all. Listen carefully."}}, 4,
    },
    // Sorceress
    {
        NPCType::Sorceress, "sorceress", "Sorceress", SpriteId::Sorceress, 70, 6,
        AIBehaviour::Wanderer, kSorceressCombat, kNpcUpkeepNone, false, 60,
        /*weight*/0, /*loot*/nullptr, /*radius*/0.0f,
        {{"Charodejka","Zaklinatelnitsa","Mistika","Runara","Svetozara"}}, 5,
        {{"The arcane currents shift around you...",
          "Few mortals seek me out willingly.",
          "I deal in mysteries beyond your understanding.",
          "Power has a price. Are you willing to pay?"}}, 4,
    },
    // Miner — the iron villages' man (spawned where a vein anchors the home)
    {
        NPCType::Miner, "miner", "Miner", SpriteId::Peasant, 30, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Prokhor","Savva","Demyan","Zakhar","Foma"}}, 5,
        {{"The vein runs deep, but so do we.",
          "Iron feeds this village better than grain ever did.",
          "Mind the shafts after rain.",
          "Every ingot you buy began as my day's sweat."}}, 4,
    },
    // Quarryman — stone out of the mountain, the same law of labour
    {
        NPCType::Quarryman, "quarryman", "Quarryman", SpriteId::Peasant, 30, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Gavril","Osip","Trofim","Nazar","Kondrat"}}, 5,
        {{"Stone does not grow back. Good thing there is a mountain of it.",
          "Every wall you have ever leaned on came through hands like mine.",
          "The quarry sings if you strike it right."}}, 3,
    },
    // Clay-digger — the riverbank's man
    {
        NPCType::ClayDigger, "clay_digger", "Clay-digger", SpriteId::Peasant, 30, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Yermolai","Panteley","Averyan","Selivan","Mitrofan"}}, 5,
        {{"Good clay wants a river and patience.",
          "Bricks, pots, ovens - it all starts in my pit.",
          "Cold work, wet work, honest work."}}, 3,
    },
    // Rabbit
    {
        NPCType::Rabbit, "rabbit", "Rabbit", SpriteId::Rabbit, 5, 1,
        AIBehaviour::Flee, {5, 0, 55, 0, 9.0f, "Rbt"}, kNpcUpkeepNone, false, 0,
        /*weight*/15, /*loot*/nullptr, /*radius*/0.4f,
        {{}}, 0, {{}}, 0,
    },
    // Deer
    {
        NPCType::Deer, "deer", "Deer", SpriteId::Deer, 15, 1,
        AIBehaviour::Flee, {15, 2, 50, 2, 2.0f, "Der"}, kNpcUpkeepNone, false, 0,
        /*weight*/12, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Fox
    {
        NPCType::Fox, "fox", "Fox", SpriteId::Fox, 12, 1,
        AIBehaviour::Wanderer, {12, 4, 45, 2, 1.2f, "Fox"}, kNpcUpkeepNone, false, 0,
        /*weight*/8, /*loot*/nullptr, /*radius*/0.5f,
        {{}}, 0, {{}}, 0,
    },
    // Wolf
    {
        NPCType::Wolf, "wolf", "Wolf", SpriteId::Wolf, 30, 2,
        AIBehaviour::Aggressive, {30, 10, 50, 3, 1.0f, "Wlf"}, kNpcUpkeepNone, false, 0,
        /*weight*/6, /*loot*/nullptr, /*radius*/0.7f,
        {{}}, 0, {{}}, 0,
    },
    // Bear
    {
        NPCType::Bear, "bear", "Bear", SpriteId::Bear, 80, 3,
        AIBehaviour::Aggressive, {80, 18, 35, 3, 1.5f, "Ber"}, kNpcUpkeepNone, false, 0,
        /*weight*/3, /*loot*/nullptr, /*radius*/1.0f,
        {{}}, 0, {{}}, 0,
    },
    // Boar
    {
        NPCType::Boar, "boar", "Boar", SpriteId::Boar, 40, 2,
        AIBehaviour::Aggressive, {40, 12, 40, 3, 1.2f, "Bor"}, kNpcUpkeepNone, false, 0,
        /*weight*/5, /*loot*/nullptr, /*radius*/0.7f,
        {{}}, 0, {{}}, 0,
    },
    // Snake
    {
        NPCType::Snake, "snake", "Snake", SpriteId::Snake, 10, 1,
        AIBehaviour::Aggressive, {10, 8, 30, 2, 0.8f, "Snk"}, kNpcUpkeepNone, false, 0,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.3f,
        {{}}, 0, {{}}, 0,
    },
    // Hawk
    {
        NPCType::Hawk, "hawk", "Hawk", SpriteId::Hawk, 8, 1,
        AIBehaviour::Wanderer, {8, 5, 60, 3, 1.0f, "Hwk"}, kNpcUpkeepNone, false, 0,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.4f,
        {{}}, 0, {{}}, 0,
    },
    // Frog
    {
        NPCType::Frog, "frog", "Frog", SpriteId::Frog, 3, 1,
        AIBehaviour::Flee, {3, 0, 30, 0, 9.0f, "Frg"}, kNpcUpkeepNone, false, 0,
        /*weight*/10, /*loot*/nullptr, /*radius*/0.3f,
        {{}}, 0, {{}}, 0,
    },
    // Mountain Goat
    {
        NPCType::Goat, "goat", "Mountain Goat", SpriteId::Goat, 20, 1,
        AIBehaviour::Flee, {20, 5, 40, 2, 1.5f, "Mgt"}, kNpcUpkeepNone, false, 0,
        /*weight*/8, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Eagle
    {
        NPCType::Eagle, "eagle", "Eagle", SpriteId::Eagle, 12, 2,
        AIBehaviour::Wanderer, {12, 7, 65, 3, 1.0f, "Egl"}, kNpcUpkeepNone, false, 0,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.5f,
        {{}}, 0, {{}}, 0,
    },
    // Crocodile
    {
        NPCType::Croc, "crocodile", "Crocodile", SpriteId::Crocodile, 50, 3,
        AIBehaviour::Aggressive, {50, 15, 25, 3, 1.5f, "Crc"}, kNpcUpkeepNone, false, 0,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.8f,
        {{}}, 0, {{}}, 0,
    },
    // Goblin
    {
        NPCType::Goblin, "goblin", "Goblin", SpriteId::Goblin, 25, 2,
        AIBehaviour::Aggressive, {25, 8, 40, 3, 1.0f, "Gbl"}, kNpcUpkeepNone, false, 0,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Skeleton
    {
        NPCType::Skeleton, "skeleton", "Skeleton", SpriteId::Skeleton, 35, 3,
        AIBehaviour::Aggressive, {35, 10, 30, 3, 1.2f, "Skl"}, kNpcUpkeepNone, false, 0,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Troll
    {
        NPCType::Troll, "troll", "Troll", SpriteId::Troll, 120, 5,
        AIBehaviour::Aggressive, {120, 25, 25, 4, 2.0f, "Trl"}, kNpcUpkeepNone, false, 0,
        /*weight*/1, /*loot*/nullptr, /*radius*/1.2f,
        {{}}, 0, {{}}, 0,
    },
    // Swamp Thing
    {
        NPCType::SwampThing, "swamp_thing", "Swamp Thing", SpriteId::SwampThing, 60, 3,
        AIBehaviour::Aggressive, {60, 14, 20, 4, 1.5f, "Swt"}, kNpcUpkeepNone, false, 0,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.9f,
        {{}}, 0, {{}}, 0,
    },
    // Ice Wraith
    {
        NPCType::IceWraith, "ice_wraith", "Ice Wraith", SpriteId::IceWraith, 45, 4,
        AIBehaviour::Aggressive, {45, 16, 35, 5, 1.3f, "Iwr"}, kNpcUpkeepNone, false, 0,
        /*weight*/2, /*loot*/nullptr, /*radius*/0.7f,
        {{}}, 0, {{}}, 0,
    },
    // Sand Scorpion
    {
        NPCType::SandScorpion, "sand_scorpion", "Sand Scorpion", SpriteId::SandScorpion, 35, 2,
        AIBehaviour::Aggressive, {35, 12, 35, 3, 1.0f, "Ssc"}, kNpcUpkeepNone, false, 0,
        /*weight*/5, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Stone Golem
    {
        NPCType::StoneGolem, "stone_golem", "Stone Golem", SpriteId::StoneGolem, 150, 5,
        AIBehaviour::Aggressive, {150, 20, 15, 4, 2.5f, "Glm"}, kNpcUpkeepNone, false, 0,
        /*weight*/1, /*loot*/nullptr, /*radius*/1.3f,
        {{}}, 0, {{}}, 0,
    },
};
static_assert(rows_in_enum_order(kNpcTypeDefs, &NpcTypeDef::type),
              "kNpcTypeDefs row order must mirror NPCType");

inline constexpr const NpcTypeDef& npc_def(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)];
}

// THE id space, and it has one half now. Any "kind" that travels — a roster
// record, an ECS NPCKind, a save — is an ordinal of the one table above, and a
// wolf is as legal as a spearman (CANON.md S16). The `0x100 | catalog row`
// encoding that used to mark "monster" is gone with the second table.
inline bool valid_npc_kind(std::uint16_t raw) {
    return raw < static_cast<std::uint16_t>(NPCType::Count);
}

// The last remnant of the old split, and it is TEMPORARY: two births still
// exist below (sub/spawn.cpp), one that projects a sheet and one that reads a
// row's raw combat line. They merge in the next step and this predicate dies
// with them. It is an ordinal boundary and it is deliberately ugly, so that
// nobody mistakes it for a fact about the world.
inline constexpr bool is_creature_row(NPCType t) {
    return t >= NPCType::Rabbit;
}
inline bool is_monster_kind(std::uint16_t raw) {
    return valid_npc_kind(raw) && is_creature_row(NPCType(raw));
}

// The row behind a record, or Peasant for a number that names none.
inline NPCType soldier_npc_type(const SoldierRecord& s) {
    return s.kind < std::uint16_t(NPCType::Count) ? NPCType(s.kind)
                                                  : NPCType::Peasant;
}

inline bool npc_hireable(NPCType t) {
    const auto& def = npc_def(t);
    return def.hireable && def.upkeepGoldPerDay >= 0;
}

inline int npc_upkeep_base(NPCType t) {
    const int upkeep = npc_def(t).upkeepGoldPerDay;
    return upkeep < 0 ? 0 : upkeep;
}

// A beast draws no pay — and the ROW already says so: every creature line
// authors `kNpcUpkeepNone`, which npc_upkeep_base reads as zero. The special
// case that used to stand here asked whether the record was a monster, which
// is a question about a class of thing rather than about this thing's column.
inline int soldier_upkeep(const SoldierRecord& s) {
    return npc_upkeep_base(soldier_npc_type(s)) * soldier_level_factor(s.level);
}

inline int calculate_squad_upkeep(const SoldierSquad& squad, int charisma = 0) {
    int base = 0;
    for (const auto& s : squad.members) base += soldier_upkeep(s);
    const float discount = std::clamp(float(charisma) * 0.01f, 0.0f, 0.90f);
    return int(float(base) * (1.0f - discount));
}

inline int hire_price_for(const SoldierRecord& s) {
    const int upkeep = soldier_upkeep(s);
    return upkeep > 0 ? upkeep * 30 : 0;
}

inline int npc_hire_price_base(NPCType t) {
    const SoldierRecord preview = make_soldier(
        static_cast<std::uint8_t>(t), npc_def(t).baseLevel, 0u);
    return hire_price_for(preview);
}

inline int npc_xp_reward(NPCType t, int level) {
    const int base = npc_def(t).xpReward;
    const int safeLevel = normalize_soldier_level(level);
    return base + (safeLevel - 1) * 5;
}

inline std::uint32_t garrison_soldier_id_base(int settlementId, int day) {
    const std::uint32_t sid = std::uint32_t(std::max(0, settlementId)) & 0x7FFu;
    const std::uint32_t d = std::uint32_t(std::max(0, day)) & 0x7FFFu;
    return 0x80000000u | (sid << 20) | (d << 5);
}

struct GarrisonResult { SoldierSquad garrison; int popCost = 0; };

template <class Rng01>
inline GarrisonResult generate_garrison(int population, Rng01&& rng,
                                        std::uint32_t idBase = 1u) {
    GarrisonResult r{};
    if (population < 20) return r;
    int budget = int(std::sqrt(float(population)) * 0.3f);
    if (budget > 10) budget = 10;
    if (budget <= 0) return r;
    r.garrison.members.reserve(std::size_t(budget));

    for (int i = 0; i < budget; ++i) {
        const float roll = rng();
        NPCType kind = NPCType::Peasant;
        if      (roll < 0.55f) kind = NPCType::Peasant;
        else if (roll < 0.85f) kind = NPCType::Woodcutter;
        else                   kind = NPCType::Guard;
        const int level = npc_def(kind).baseLevel;
        r.garrison.members.push_back(make_soldier(
            static_cast<std::uint8_t>(kind), level, idBase + std::uint32_t(i)));
        r.popCost += 1;
    }
    return r;
}

inline int hire_npc(SoldierSquad& playerSquad, SoldierSquad& garrison,
                    NPCType kind, int& playerGold) {
    if (!npc_hireable(kind)) return 0;
    for (auto it = garrison.members.begin(); it != garrison.members.end(); ++it) {
        if (it->kind != static_cast<std::uint8_t>(kind)) continue;
        const int cost = hire_price_for(*it);
        if (playerGold < cost) return 0;
        playerGold -= cost;
        reserve_soldiers_for_append(playerSquad, 1u);
        playerSquad.members.push_back(*it);
        garrison.members.erase(it);
        return cost;
    }
    return 0;
}

// Case-insensitive token → registry row ("bandit" → NPCType::Bandit). Purely
// data-driven off kNpcTypeDefs labels: a new type is matchable the moment its
// row exists, no per-type branch. Returns false on no match — the CALLER
// decides its own fallback (the subworld console spawner keeps its historical
// silent-Bandit default; the SpawnEntity consumer refuses to spawn).
inline bool npc_type_from_label(const char* token, NPCType& out) {
    if (!token || token[0] == '\0') return false;
    for (int i = 0; i < int(NPCType::Count); ++i) {
        const char* label = npc_def(NPCType(i)).label;
        std::size_t k = 0;
        while (token[k] != '\0' && label[k] != '\0') {
            const int a = std::tolower(static_cast<unsigned char>(token[k]));
            const int b = std::tolower(static_cast<unsigned char>(label[k]));
            if (a != b) break;
            ++k;
        }
        if (token[k] == '\0' && label[k] == '\0') {
            out = NPCType(i);
            return true;
        }
    }
    return false;
}

} // namespace sm

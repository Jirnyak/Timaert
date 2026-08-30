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
    // The PLAYER's own row (owner's ruling 2026-08-27: «игрок = обычный сквад,
    // просто с флажком»). His macro squad is an ordinary squad entity, and an
    // ordinary squad entity names a row of THIS table — so the player needed
    // one. Appended, so every saved ordinal stays where it was.
    Adventurer,
    // The village vendor (owner 2026-08-30): the crew that walks the home
    // surplus to the nearest city market. Appended, so saved ordinals stay.
    Vendor,
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

// The armour at which a blow is HALVED — and therefore the whole scale on
// which every armour number in the game reads. It is not picked, it is the
// game's own plain blow: `kPlayerBaseMeleeDamage` (sub/engine.h), what an
// untrained man does with his bare hands. So "armour 10" is not a number
// needing a table to interpret — it says «this body halves a plain blow», and
// twice that quarters it. Armour and damage share units because they meet in
// one formula, and the halving point is where they meet.
//
// ONE home, next to the `armor` column below, because BOTH laws of battle
// read it: the damage door mitigates every blow by
// kArmorHalving / (kArmorHalving + armor) (sub/damage.cpp), and the
// auto-resolve credits the identical protection as EFFECTIVE HP — a body of
// hp H dies to exactly H * (kArmorHalving + armor) / kArmorHalving worth of
// raw blows, which is the same law read from the other side.
inline constexpr float kArmorHalving = 10.0f;

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
    // NO baseHp column — the ONE hp floor a body fights with is its
    // CombatTemplate's `hp` (the sheet projection's own floor); a second
    // number here was never read.
    int             baseLevel;
    AIBehaviour     ai;
    CombatTemplate  combat;
    int             upkeepGoldPerDay;
    bool            hireable;
    // ONE XP law (owner, 2026-08-29): every kill pays npc_xp_reward =
    // xpReward + (level−1)·5, in the subworld and the auto-resolve alike —
    // the old exp_from_fight(lvl) = 10·lvl fallback for rows that named 0 is
    // dead. Creature rows author xpReward = 5·(baseLevel+1), DERIVED to keep
    // the old subworld feel at the row's own level:
    //     5·(L₀+1) + (L₀−1)·5 = 10·L₀  — exactly what 10·lvl used to pay.
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
    // Body radius in metres — THE one width column of the one body table
    // (damage-door Inc 4: CombatTemplate's shadow copy is gone). 0 = the
    // man-shaped default (npc_body_radius below); the creature rows author it
    // because a rabbit is not a man-sized thing. The same number scales the
    // creature's sprite, so visual size and hit size cannot drift.
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

    // How many BACKS this row hauls with. The overload law is UNIVERSAL now
    // (owner ruling, 2026-08-27: «перегруз универсальный всем»), and it was
    // written for a man and his pack — capacity is STR and athletics. A
    // caravan is not a man with a big rucksack, it is wagons and mules, and
    // charging its cargo against one merchant's shoulders priced commerce out
    // of the world the hour the law went universal: the first honest run of it
    // stranded every caravan on the map, and the trade test said so.
    //
    // So the answer is a COLUMN, not an exemption — the same law, applied to
    // the back the row actually has. 1 = a person, and every row that omits
    // this is a person. The number is DATA and the owner's to retune; nothing
    // reads it but the one capacity call (squad.h refresh_leader_travel_stats).
    // Last field on purpose, beside the light block: an opt-in column at the
    // end costs no other row a comma.
    float haulMult = 1.0f;

    // ARMOUR THE ROW IS WEARING — the crowd's defence, as a NUMBER rather than
    // as instances (owner ruling, 2026-08-27: «броня массовки = ЧИСЛО ИЗ
    // СТРОКИ»). A troll's hide and a guard's plate are what those rows ARE;
    // giving sixteen thousand bodies an equipment container each to say so
    // would be one fact stored ten thousand times. A body that also WEARS
    // things adds them on top of this — the same shape the authored body
    // radius has, where a creature's own number and the default meet at one
    // reader. Zero (every row that omits it) is a body in its own skin.
    //
    // Units are the damage's own, because the two meet in one formula
    // (sub/damage.cpp).
    int armor = 0;

    // WHAT A BODY OF THIS ROW COSTS to take into a roster, in gold at its
    // level-1 worth (CANON S25: a creature's price is a column of its row,
    // exactly like a sword's — not a formula living beside the item prices).
    // DERIVATION: the row's own upkeepGoldPerDay × 30 days — a recruit is
    // bought for a month of his pay, which is byte-for-byte the price
    // `hire_price_for` computed inline until 2026-08-29 (Peasant 1×30 = 30,
    // Guard 3×30 = 90). Level scales it through THE one level law
    // (army.h soldier_level_factor), applied by the reader; 0 — every row
    // that omits it, i.e. everything with no upkeep — is not for sale.
    int hireGold = 0;
};

inline constexpr CombatTemplate kPeasantCombat   {25,3, 1.0f, 2.0f, 1.5f, "Psr", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kWoodcutterCombat{30,8, 1.0f, 2.0f, 1.2f, "Wdc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kMerchantCombat  {30,5, 1.25f, 2.0f, 1.5f, "Mrc", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kCaravanCombat   {25,4, 1.5f, 2.0f, 1.5f, "Cvn", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kBanditCombat    {50,12, 2.25f, 3.0f, 1.0f, "Bnd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kGuardCombat     {55,14, 1.75f, 3.0f, 1.0f, "Grd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu};
inline constexpr CombatTemplate kWitchCombat     {60,18, 1.5f, 20.0f,2.0f, "Wtc", CombatTemplate::Missile, 180, 0, 0xFFA070D0u};
inline constexpr CombatTemplate kSorceressCombat {70,22, 1.25f, 25.0f,1.8f, "Src", CombatTemplate::Missile, 200, 6, 0xFF70C0E0u};

inline constexpr NpcTypeDef kNpcTypeDefs[std::size_t(NPCType::Count)] = {
    // Peasant
    {
        NPCType::Peasant, "peasant", "Peasant", SpriteId::Peasant, 1,
        AIBehaviour::Gatherer, kPeasantCombat, 1, true, 10,
        /*weight*/55, /*loot*/nullptr, /*radius*/0.0f,
        {{"Ivan","Pyotr","Sergey","Dmitry","Alexei","Nikolai","Vasily","Grigory",
          "Fedor","Andrei","Olga","Natalya","Katya","Masha","Dasha"}}, 15,
        {{"The harvest has been poor this year...",
          "Have you heard? Bandits roam the roads at night.",
          "Blessings upon you, traveler.",
          "I sell nothing of interest, but the merchant might.",
          "Stay safe out there. The wilderness is harsh."}}, 5,
        // Dark, one back, own skin — the defaults, spelled out only to reach
        // the price column at the row's end: upkeep 1 × 30 days.
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f, /*haulMult=*/1.0f, /*armor=*/0,
        /*hireGold=*/30,
    },
    // Woodcutter
    {
        NPCType::Woodcutter, "woodcutter", "Woodcutter", SpriteId::Peasant, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Borislav","Timofey","Yegor","Luka","Matvey"}}, 5,
        {{"These woods hold many secrets.",
          "Good timber is hard to find lately.",
          "Watch for wolves near the tree line.",
          "I chop from dawn to dusk. Honest work."}}, 4,
        // Defaults to reach the price column: upkeep 1 × 30 days.
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f, /*haulMult=*/1.0f, /*armor=*/0,
        /*hireGold=*/30,
    },
    // Merchant
    {
        NPCType::Merchant, "merchant", "Merchant", SpriteId::Caravan, 3,
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
        NPCType::Caravan, "caravan", "Caravan", SpriteId::Caravan, 2,
        AIBehaviour::CaravanTrade, kCaravanCombat, kNpcUpkeepNone, false, 20,
        /*weight*/0, /*loot*/nullptr, /*radius*/0.0f,
        {{"Putnik","Dorozhkin","Obozov","Strannik","Koleso"}}, 5,
        {{"Long road ahead. Care to trade before I move on?",
          "I have seen many lands. Each stranger than the last.",
          "The roads between settlements grow more perilous.",
          "My oxen grow weary. We rest here briefly."}}, 4,
        // No carried light — the four zeroes are the dark default, spelled
        // out here only because the wagons that follow them are not.
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f,
        // "My oxen grow weary" — this row says it in its own talk line. Wagons
        // and a team, not a rucksack.
        /*haulMult=*/32.0f,
    },
    // Bandit
    {
        NPCType::Bandit, "bandit", "Bandit", SpriteId::Bandit, 2,
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
        NPCType::Guard, "guard", "Guard", SpriteId::Peasant, 3,
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
        // The disciplined tank of the table wears what his row already
        // describes. `haulMult` is spelled out only because the plate after it
        // is not the default; kArmorHalving is what makes 10 legible — it
        // HALVES a plain blow.
        /*haulMult=*/1.0f,
        /*armor=*/10,
        // The price column: upkeep 3 × 30 days.
        /*hireGold=*/90,
    },
    // Witch
    {
        NPCType::Witch, "witch", "Witch", SpriteId::Witch, 5,
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
        NPCType::Sorceress, "sorceress", "Sorceress", SpriteId::Sorceress, 6,
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
        NPCType::Miner, "miner", "Miner", SpriteId::Peasant, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Prokhor","Savva","Demyan","Zakhar","Foma"}}, 5,
        {{"The vein runs deep, but so do we.",
          "Iron feeds this village better than grain ever did.",
          "Mind the shafts after rain.",
          "Every ingot you buy began as my day's sweat."}}, 4,
        // Defaults to reach the price column: upkeep 1 × 30 days.
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f, /*haulMult=*/1.0f, /*armor=*/0,
        /*hireGold=*/30,
    },
    // Quarryman — stone out of the mountain, the same law of labour
    {
        NPCType::Quarryman, "quarryman", "Quarryman", SpriteId::Peasant, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Gavril","Osip","Trofim","Nazar","Kondrat"}}, 5,
        {{"Stone does not grow back. Good thing there is a mountain of it.",
          "Every wall you have ever leaned on came through hands like mine.",
          "The quarry sings if you strike it right."}}, 3,
        // Defaults to reach the price column: upkeep 1 × 30 days.
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f, /*haulMult=*/1.0f, /*armor=*/0,
        /*hireGold=*/30,
    },
    // Clay-digger — the riverbank's man
    {
        NPCType::ClayDigger, "clay_digger", "Clay-digger", SpriteId::Peasant, 1,
        AIBehaviour::Gatherer, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Yermolai","Panteley","Averyan","Selivan","Mitrofan"}}, 5,
        {{"Good clay wants a river and patience.",
          "Bricks, pots, ovens - it all starts in my pit.",
          "Cold work, wet work, honest work."}}, 3,
        // Defaults to reach the price column: upkeep 1 × 30 days.
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f, /*haulMult=*/1.0f, /*armor=*/0,
        /*hireGold=*/30,
    },
    // Rabbit
    {
        NPCType::Rabbit, "rabbit", "Rabbit", SpriteId::Rabbit, 1,
        AIBehaviour::Flee, {5, 0, 2.75f, 0, 9.0f, "Rbt"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/15, /*loot*/nullptr, /*radius*/0.4f,
        {{}}, 0, {{}}, 0,
    },
    // Deer
    {
        NPCType::Deer, "deer", "Deer", SpriteId::Deer, 1,
        AIBehaviour::Flee, {15, 2, 2.5f, 2, 2.0f, "Der"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/12, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Fox
    {
        NPCType::Fox, "fox", "Fox", SpriteId::Fox, 1,
        AIBehaviour::Wanderer, {12, 4, 2.25f, 2, 1.2f, "Fox"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/8, /*loot*/nullptr, /*radius*/0.5f,
        {{}}, 0, {{}}, 0,
    },
    // Wolf
    {
        NPCType::Wolf, "wolf", "Wolf", SpriteId::Wolf, 2,
        AIBehaviour::Aggressive, {30, 10, 2.5f, 3, 1.0f, "Wlf"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/15,
        /*weight*/6, /*loot*/nullptr, /*radius*/0.7f,
        {{}}, 0, {{}}, 0,
    },
    // Bear
    {
        NPCType::Bear, "bear", "Bear", SpriteId::Bear, 3,
        AIBehaviour::Aggressive, {80, 18, 1.75f, 3, 1.5f, "Ber"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/20,
        /*weight*/3, /*loot*/nullptr, /*radius*/1.0f,
        {{}}, 0, {{}}, 0,
    },
    // Boar
    {
        NPCType::Boar, "boar", "Boar", SpriteId::Boar, 2,
        AIBehaviour::Aggressive, {40, 12, 2.0f, 3, 1.2f, "Bor"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/15,
        /*weight*/5, /*loot*/nullptr, /*radius*/0.7f,
        {{}}, 0, {{}}, 0,
    },
    // Snake
    {
        NPCType::Snake, "snake", "Snake", SpriteId::Snake, 1,
        AIBehaviour::Aggressive, {10, 8, 1.5f, 2, 0.8f, "Snk"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.3f,
        {{}}, 0, {{}}, 0,
    },
    // Hawk
    {
        NPCType::Hawk, "hawk", "Hawk", SpriteId::Hawk, 1,
        AIBehaviour::Wanderer, {8, 5, 3.0f, 3, 1.0f, "Hwk"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.4f,
        {{}}, 0, {{}}, 0,
    },
    // Frog
    {
        NPCType::Frog, "frog", "Frog", SpriteId::Frog, 1,
        AIBehaviour::Flee, {3, 0, 1.5f, 0, 9.0f, "Frg"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/10, /*loot*/nullptr, /*radius*/0.3f,
        {{}}, 0, {{}}, 0,
    },
    // Mountain Goat
    {
        NPCType::Goat, "goat", "Mountain Goat", SpriteId::Goat, 1,
        AIBehaviour::Flee, {20, 5, 2.0f, 2, 1.5f, "Mgt"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/10,
        /*weight*/8, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Eagle
    {
        NPCType::Eagle, "eagle", "Eagle", SpriteId::Eagle, 2,
        AIBehaviour::Wanderer, {12, 7, 3.25f, 3, 1.0f, "Egl"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/15,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.5f,
        {{}}, 0, {{}}, 0,
    },
    // Crocodile
    {
        NPCType::Croc, "crocodile", "Crocodile", SpriteId::Crocodile, 3,
        AIBehaviour::Aggressive, {50, 15, 1.25f, 3, 1.5f, "Crc"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/20,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.8f,
        {{}}, 0, {{}}, 0,
    },
    // Goblin
    {
        NPCType::Goblin, "goblin", "Goblin", SpriteId::Goblin, 2,
        AIBehaviour::Aggressive, {25, 8, 2.0f, 3, 1.0f, "Gbl"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/15,
        /*weight*/4, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Skeleton
    {
        NPCType::Skeleton, "skeleton", "Skeleton", SpriteId::Skeleton, 3,
        AIBehaviour::Aggressive, {35, 10, 1.5f, 3, 1.2f, "Skl"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/20,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Troll
    {
        NPCType::Troll, "troll", "Troll", SpriteId::Troll, 5,
        AIBehaviour::Aggressive, {120, 25, 1.25f, 4, 2.0f, "Trl"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/30,
        /*weight*/1, /*loot*/nullptr, /*radius*/1.2f,
        {{}}, 0, {{}}, 0,
    },
    // Swamp Thing
    {
        NPCType::SwampThing, "swamp_thing", "Swamp Thing", SpriteId::SwampThing, 3,
        AIBehaviour::Aggressive, {60, 14, 1.0f, 4, 1.5f, "Swt"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/20,
        /*weight*/3, /*loot*/nullptr, /*radius*/0.9f,
        {{}}, 0, {{}}, 0,
    },
    // Ice Wraith
    {
        NPCType::IceWraith, "ice_wraith", "Ice Wraith", SpriteId::IceWraith, 4,
        AIBehaviour::Aggressive, {45, 16, 1.75f, 5, 1.3f, "Iwr"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/25,
        /*weight*/2, /*loot*/nullptr, /*radius*/0.7f,
        {{}}, 0, {{}}, 0,
    },
    // Sand Scorpion
    {
        NPCType::SandScorpion, "sand_scorpion", "Sand Scorpion", SpriteId::SandScorpion, 2,
        AIBehaviour::Aggressive, {35, 12, 1.75f, 3, 1.0f, "Ssc"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/15,
        /*weight*/5, /*loot*/nullptr, /*radius*/0.6f,
        {{}}, 0, {{}}, 0,
    },
    // Stone Golem
    {
        NPCType::StoneGolem, "stone_golem", "Stone Golem", SpriteId::StoneGolem, 5,
        AIBehaviour::Aggressive, {150, 20, 0.75f, 4, 2.5f, "Glm"}, kNpcUpkeepNone, false, /*xp = 5*(baseLevel+1)*/30,
        /*weight*/1, /*loot*/nullptr, /*radius*/1.3f,
        {{}}, 0, {{}}, 0,
    },
    // The player. An ordinary row of the ordinary table (owner, 2026-08-27),
    // because his macro squad is an ordinary squad and a squad names a row
    // here. `weight` 0: the world never rolls an adventurer out of thin air —
    // this row is reached BY NAME, by the one entity that wears the flag. Not
    // hireable, worth no XP (his death is a game-over, not a kill), and his
    // loot is the bag he actually carries rather than a rolled profile.
    {
        NPCType::Adventurer, "adventurer", "Adventurer", SpriteId::Peasant, 1,
        AIBehaviour::Wanderer, kGuardCombat, kNpcUpkeepNone, false, 0,
        /*weight*/0, /*loot*/nullptr, /*radius*/0.0f,
        {{}}, 0, {{}}, 0,
    },
    // Vendor — the village surplus on the road to town (owner 2026-08-30).
    // haulMult 1: no wagon train — the crew's carry is the sum of its backs
    // (spawn_squad), and the rotation sizes the crew to the village.
    {
        NPCType::Vendor, "vendor", "Vendor", SpriteId::Peasant, 1,
        AIBehaviour::VendorTrade, kWoodcutterCombat, 1, true, 12,
        /*weight*/21, /*loot*/nullptr, /*radius*/0.0f,
        {{"Matvey","Luka","Yefim","Silanty","Avdey"}}, 5,
        {{"Fresh from the village, best prices before noon.",
          "The road eats a share of every sack, but town coin is real.",
          "Buy now - by evening the good grain is gone."}}, 3,
        /*lightRadius=*/0.0f, /*lightIntensity=*/0.0f,
        /*lightR=*/0.0f, /*lightG=*/0.0f, /*lightB=*/0.0f,
        /*lightHeight=*/0.0f, /*haulMult=*/1.0f, /*armor=*/0,
        /*hireGold=*/30,
    },
};
static_assert(rows_in_enum_order(kNpcTypeDefs, &NpcTypeDef::type),
              "kNpcTypeDefs row order must mirror NPCType");

inline constexpr const NpcTypeDef& npc_def(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)];
}

// THE man-shaped half-width (world units ≈ metres): what a row that authors
// no radius IS — a person. This default lived twice (here as the humanoid
// rows' silence, and as CombatTemplate::bodyRadius's 0.55 that no row ever
// authored); the template copy is dead, the number lives beside the column
// it defaults.
inline constexpr float kNpcBodyRadiusDefault = 0.55f;

// A row's body radius, default resolved — the ONE answer every consumer
// derives from (sub/body.h body_radius for live entities, the spawners for
// the footprint they stamp, the auto-battle fixture for its bodies).
inline constexpr float npc_body_radius(const NpcTypeDef& def) {
    return def.radius > 0.0f ? def.radius : kNpcBodyRadiusDefault;
}

// ── THE purse: how much coin a body of this row carries ───────────────────
// One table, enum-ordered beside the row it describes (the kSpawnHabitats /
// kGathererDefs idiom). It lived inside macro/npc_spawn.cpp's make_npc, so
// only PERSISTENT macro bodies had a purse; the derived bodies of the
// subworld paid out through a second, faction-keyed multiplier in
// items.cpp (wildlife 0.1× / demons 0.6× / bandits 0.8×) — a second wealth
// vocabulary that the faction ruling of 2026-08-27 made outright wrong: with
// faction an INSTANCE property, the very same wolf carried six times more
// coin under a ruin's banner than in a meadow.
//
// The row answers now, for both worlds: a beast has no pockets whatever
// banner it fights under, a merchant is rich because he is a merchant. What
// the world modulates on top is the WEALTH OF THE PLACE (landmark_registry
// wealthMul, through the one context door), and what the banner still decides
// is which realm's COIN it is — S10's «база из таблицы, мир — модуляция».
//
// (Scar: this table silently zero-filled when NPCType grew the three gatherer
// professions — a miner spawned with an empty purse. The row-order guard
// makes a short table refuse to compile instead.)
struct NpcPurseRow { NPCType type; int min, max; };
inline constexpr NpcPurseRow kNpcPurse[std::size_t(NPCType::Count)] = {
    {NPCType::Peasant,    1, 10},
    {NPCType::Woodcutter, 1, 10},
    {NPCType::Merchant,   50, 200},
    {NPCType::Caravan,    50, 200},
    {NPCType::Bandit,     5, 30},
    {NPCType::Guard,      5, 20},
    {NPCType::Witch,      10, 40},
    {NPCType::Sorceress,  10, 40},
    // The gatherer professions carry a labourer's pocket, like the
    // peasant/woodcutter class they share their build with.
    {NPCType::Miner,      1, 10},
    {NPCType::Quarryman,  1, 10},
    {NPCType::ClayDigger, 1, 10},
    // A beast carries no purse — it has no pockets and no use for coin.
    // The one exception is the goblin, who robs what he kills.
    {NPCType::Rabbit,       0, 0},
    {NPCType::Deer,         0, 0},
    {NPCType::Fox,          0, 0},
    {NPCType::Wolf,         0, 0},
    {NPCType::Bear,         0, 0},
    {NPCType::Boar,         0, 0},
    {NPCType::Snake,        0, 0},
    {NPCType::Hawk,         0, 0},
    {NPCType::Frog,         0, 0},
    {NPCType::Goat,         0, 0},
    {NPCType::Eagle,        0, 0},
    {NPCType::Croc,         0, 0},
    {NPCType::Goblin,       1, 12},
    {NPCType::Skeleton,     0, 0},
    {NPCType::Troll,        0, 0},
    {NPCType::SwampThing,   0, 0},
    {NPCType::IceWraith,    0, 0},
    {NPCType::SandScorpion, 0, 0},
    {NPCType::StoneGolem,   0, 0},
    // The player's purse is his INVENTORY — what he actually carries — never a
    // rolled amount, so his row asks for nothing.
    {NPCType::Adventurer,   0, 0},
    {NPCType::Vendor,       1, 10},
};
static_assert(rows_in_enum_order(kNpcPurse, &NpcPurseRow::type),
              "kNpcPurse row order must mirror NPCType");

inline constexpr const NpcPurseRow& npc_purse(NPCType t) {
    return kNpcPurse[std::size_t(t)];
}

// ── The map dot's colour: one row per kind ────────────────────────────────
// What the MACRO map paints a walker of this row (ui/macro_overlay.cpp),
// enum-ordered beside the row like kNpcPurse. These are the overlay's own
// historical colours, deliberately NOT the sprite tints: kSpriteRows[].tint
// colours the procedural BODY, and three town kinds share one sprite row
// while wearing three different dots. 0xRRGGBB.
struct NpcMapColorRow { NPCType type; std::uint32_t rgb; };
inline constexpr NpcMapColorRow kNpcMapColor[std::size_t(NPCType::Count)] = {
    {NPCType::Peasant,      0xDCC8A0u},
    {NPCType::Woodcutter,   0x5A9646u},
    {NPCType::Merchant,     0xF0C850u},
    {NPCType::Caravan,      0xB48C50u},
    {NPCType::Bandit,       0xDC3C3Cu},
    {NPCType::Guard,        0x508CDCu},
    {NPCType::Witch,        0xB464C8u},
    {NPCType::Sorceress,    0x78C8E6u},
    // Every other row wears the neutral crowd grey the old default painted.
    {NPCType::Miner,        0xC8C8C8u},
    {NPCType::Quarryman,    0xC8C8C8u},
    {NPCType::ClayDigger,   0xC8C8C8u},
    {NPCType::Rabbit,       0xC8C8C8u},
    {NPCType::Deer,         0xC8C8C8u},
    {NPCType::Fox,          0xC8C8C8u},
    {NPCType::Wolf,         0xC8C8C8u},
    {NPCType::Bear,         0xC8C8C8u},
    {NPCType::Boar,         0xC8C8C8u},
    {NPCType::Snake,        0xC8C8C8u},
    {NPCType::Hawk,         0xC8C8C8u},
    {NPCType::Frog,         0xC8C8C8u},
    {NPCType::Goat,         0xC8C8C8u},
    {NPCType::Eagle,        0xC8C8C8u},
    {NPCType::Croc,         0xC8C8C8u},
    {NPCType::Goblin,       0xC8C8C8u},
    {NPCType::Skeleton,     0xC8C8C8u},
    {NPCType::Troll,        0xC8C8C8u},
    {NPCType::SwampThing,   0xC8C8C8u},
    {NPCType::IceWraith,    0xC8C8C8u},
    {NPCType::SandScorpion, 0xC8C8C8u},
    {NPCType::StoneGolem,   0xC8C8C8u},
    {NPCType::Adventurer,   0xC8C8C8u},
    {NPCType::Vendor,       0xC8C8C8u},
};
static_assert(rows_in_enum_order(kNpcMapColor, &NpcMapColorRow::type),
              "kNpcMapColor row order must mirror NPCType");

inline constexpr std::uint32_t npc_map_color(NPCType t) {
    return kNpcMapColor[std::size_t(t)].rgb;
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

// `tradeDiscount` is the ONE derived sheet's column (attributes.h
// calculate_derived — cha × 1 %), not a private copy of that formula; the
// derived column is uncapped, so the 90 % ceiling stays HERE, at the site
// that applies it to a payroll.
inline int calculate_squad_upkeep(const SoldierSquad& squad,
                                  float tradeDiscount = 0.0f) {
    int base = 0;
    for (const auto& s : squad) base += soldier_upkeep(s);
    const float discount = std::clamp(tradeDiscount, 0.0f, 0.90f);
    return int(float(base) * (1.0f - discount));
}

// The row's price column × THE one level law (soldier_level_factor) — the
// same product the old inline `upkeep × 30` computed, read from data
// (CANON S25). An unpriced row (hireGold 0) costs nothing and npc_hireable
// already refuses it.
inline int hire_price_for(const SoldierRecord& s) {
    return npc_def(soldier_npc_type(s)).hireGold
           * soldier_level_factor(s.level);
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
    for (int i = 0; i < budget; ++i) {
        const float roll = rng();
        NPCType kind = NPCType::Peasant;
        if      (roll < 0.55f) kind = NPCType::Peasant;
        else if (roll < 0.85f) kind = NPCType::Woodcutter;
        else                   kind = NPCType::Guard;
        const int level = npc_def(kind).baseLevel;
        if (!r.garrison.push(make_soldier(
                static_cast<std::uint8_t>(kind), level,
                idBase + std::uint32_t(i)))) {
            break;   // a full roster refuses out loud; the town keeps the head
        }
        r.popCost += 1;
    }
    return r;
}

inline int hire_npc(SoldierSquad& playerSquad, SoldierSquad& garrison,
                    NPCType kind, int& playerGold) {
    if (!npc_hireable(kind)) return 0;
    // A recruit MOVES between two rosters — and the move can be refused at
    // either end: an empty garrison has nobody, a full squad has no room. The
    // ceiling is the same one every squad has (kMaxSquadMembers): the player's
    // army used to have none at all, which walked straight into the save's
    // 8192-record wall and made the whole file refuse to write.
    for (int i = 0; i < garrison.size(); ++i) {
        if (garrison[i].kind != static_cast<std::uint8_t>(kind)) continue;
        const int cost = hire_price_for(garrison[i]);
        if (playerGold < cost) return 0;
        if (playerSquad.full()) return 0;
        playerGold -= cost;
        playerSquad.push(garrison[i]);
        garrison.remove_at(i);
        return cost;
    }
    return 0;
}

// Case-insensitive token → registry row, matched against the row's stable
// machine `id` FIRST ("clay_digger" → NPCType::ClayDigger) — that column
// exists precisely to be what content names a row by — with the display
// `label` kept as a convenience fallback ("Clay-digger" still works at the
// console). Purely data-driven off kNpcTypeDefs: a new type is matchable the
// moment its row exists, no per-type branch. Returns false on no match — the
// CALLER decides its own fallback (the subworld console spawner keeps its
// historical silent-Bandit default; the SpawnEntity consumer refuses to
// spawn).
inline bool npc_type_from_label(const char* token, NPCType& out) {
    if (!token || token[0] == '\0') return false;
    const auto matches = [](const char* t, const char* name) {
        std::size_t k = 0;
        while (t[k] != '\0' && name[k] != '\0') {
            const int a = std::tolower(static_cast<unsigned char>(t[k]));
            const int b = std::tolower(static_cast<unsigned char>(name[k]));
            if (a != b) return false;
            ++k;
        }
        return t[k] == '\0' && name[k] == '\0';
    };
    for (int i = 0; i < int(NPCType::Count); ++i) {
        if (matches(token, npc_def(NPCType(i)).id)) {
            out = NPCType(i);
            return true;
        }
    }
    for (int i = 0; i < int(NPCType::Count); ++i) {
        if (matches(token, npc_def(NPCType(i)).label)) {
            out = NPCType(i);
            return true;
        }
    }
    return false;
}

} // namespace sm

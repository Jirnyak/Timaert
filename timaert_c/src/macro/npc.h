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
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "macro/army.h"

namespace sm {

enum class NPCType : std::uint8_t {
    Peasant = 0, Woodcutter, Merchant, Caravan, Bandit, Guard, Witch, Sorceress,
    Count,
};

enum class NPCState : std::uint8_t {
    Idle = 0, Wandering, Traveling, Returning, Working, Chasing, Patrolling, Resting,
};

enum class NPCTrait : std::uint8_t {
    Greedy = 0, Honorable, Cowardly, Brave, Aggressive, Generous, Suspicious, Curious,
    Count,
};

// AI behaviour selector — each value maps to one function in npc_ai.cpp.
// Keeping the indirection as an enum (instead of a raw function pointer)
// keeps the registry POD + constexpr-friendly and lets the AI layer be
// swapped without touching this header.
enum class AIBehaviour : std::uint8_t {
    HomeWanderer = 0, Woodcutter, Trader, Nomad,
    Aggressive, Patrol, Teleporter, Wanderer,
    Count,
};

// Fixed-arity name / dialogue pools — POD-friendly.
constexpr std::size_t kMaxNpcNames     = 16;
constexpr std::size_t kMaxNpcTalkLines = 6;
constexpr int kNpcUpkeepNone = -1;

struct NpcTypeDef {
    const char*     label;
    const char*     portrait;
    int             baseHp;
    int             baseLevel;
    AIBehaviour     ai;
    CombatTemplate  combat;
    int             upkeepGoldPerDay;
    bool            hireable;
    int             xpReward;

    // Pools — first `nameCount` / `talkCount` entries are valid.
    std::array<const char*, kMaxNpcNames>     names;
    std::uint8_t                               nameCount;
    std::array<const char*, kMaxNpcTalkLines> talkLines;
    std::uint8_t                               talkCount;
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
        "Peasant", "/assets/sprites/peasant_256.png", 25, 1,
        AIBehaviour::HomeWanderer, kPeasantCombat, 1, true, 10,
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
        "Woodcutter", "/assets/sprites/peasant_256.png", 30, 1,
        AIBehaviour::Woodcutter, kWoodcutterCombat, 1, true, 12,
        {{"Borislav","Timofey","Yegor","Luka","Matvey"}}, 5,
        {{"These woods hold many secrets.",
          "Good timber is hard to find lately.",
          "Watch for wolves near the tree line.",
          "I chop from dawn to dusk. Honest work."}}, 4,
    },
    // Merchant
    {
        "Merchant", "/assets/sprites/corovan_256.png", 30, 3,
        AIBehaviour::Trader, kMerchantCombat, kNpcUpkeepNone, false, 30,
        {{"Kartash","Bazukin","Torgin","Menkov","Skaldin"}}, 5,
        {{"Looking to trade? I have fine wares!",
          "Gold makes the world go round, friend.",
          "I travel between settlements. The roads are dangerous.",
          "Business has been slow. Perhaps you need something?"}}, 4,
    },
    // Caravan
    {
        "Caravan", "/assets/sprites/corovan_256.png", 25, 2,
        AIBehaviour::Nomad, kCaravanCombat, kNpcUpkeepNone, false, 20,
        {{"Putnik","Dorozhkin","Obozov","Strannik","Koleso"}}, 5,
        {{"Long road ahead. Care to trade before I move on?",
          "I have seen many lands. Each stranger than the last.",
          "The roads between settlements grow more perilous.",
          "My oxen grow weary. We rest here briefly."}}, 4,
    },
    // Bandit
    {
        "Bandit", "/assets/sprites/imp_golem_256.png", 50, 2,
        AIBehaviour::Aggressive, kBanditCombat, kNpcUpkeepNone, false, 20,
        {{"Razboy","Diki","Grozny","Slyak","Khvat"}}, 5,
        {{"Your gold or your life!",
          "Heh, another fool wandering the wilds.",
          "I take what I want. Got a problem with that?",
          "The strong survive. The weak feed us."}}, 4,
    },
    // Guard
    {
        "Guard", "/assets/sprites/peasant_256.png", 55, 3,
        AIBehaviour::Patrol, kGuardCombat, 3, true, 30,
        {{"Strazhnik","Boyar","Vityaz","Desyatnik","Druzhina"}}, 5,
        {{"Move along, citizen. Nothing to see here.",
          "The settlement is safe under our watch.",
          "Report any bandit sightings to the elder.",
          "Stay on the roads if you value your life."}}, 4,
    },
    // Witch
    {
        "Witch", "/assets/sprites/witch_256.png", 60, 5,
        AIBehaviour::Teleporter, kWitchCombat, kNpcUpkeepNone, false, 50,
        {{"Yaga","Vedma","Znakharka","Koldunia","Volshebnitsa"}}, 5,
        {{"The spirits whisper of your coming...",
          "I see great trials ahead for you.",
          "Herbs and potions are my trade. Interested?",
          "The forest knows all. Listen carefully."}}, 4,
    },
    // Sorceress
    {
        "Sorceress", "/assets/sprites/witch_256.png", 70, 6,
        AIBehaviour::Wanderer, kSorceressCombat, kNpcUpkeepNone, false, 60,
        {{"Charodejka","Zaklinatelnitsa","Mistika","Runara","Svetozara"}}, 5,
        {{"The arcane currents shift around you...",
          "Few mortals seek me out willingly.",
          "I deal in mysteries beyond your understanding.",
          "Power has a price. Are you willing to pay?"}}, 4,
    },
};

inline constexpr const NpcTypeDef& npc_def(NPCType t) {
    return kNpcTypeDefs[std::size_t(t)];
}

inline bool valid_npc_kind(std::uint8_t raw) {
    return raw < static_cast<std::uint8_t>(NPCType::Count);
}

inline NPCType soldier_npc_type(const SoldierRecord& s) {
    return valid_npc_kind(s.kind) ? NPCType(s.kind) : NPCType::Peasant;
}

inline bool npc_hireable(NPCType t) {
    const auto& def = npc_def(t);
    return def.hireable && def.upkeepGoldPerDay >= 0;
}

inline int npc_upkeep_base(NPCType t) {
    const int upkeep = npc_def(t).upkeepGoldPerDay;
    return upkeep < 0 ? 0 : upkeep;
}

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

// ── Faction helpers (matches `settlementFaction` in npc.ts) ─────
// Pure derivation from normalised position. Latitude-banded.
inline const char* settlement_faction(int sx, int sy, int mapW, int mapH) {
    const float nx = float(sx) / float(mapW);
    const float ny = float(sy) / float(mapH);
    if (ny < 0.3f) return nx < 0.5f ? "magika" : "barbarians";
    if (ny > 0.7f) return "timaert";
    return "empire";
}

} // namespace sm

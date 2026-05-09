// Macro-world game state. Mirrors state.ts (compact form).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "macro/attributes.h"
#include "macro/items.h"
#include "macro/army.h"
#include "macro/economy.h"
#include "macro/politik.h"
#include "macro/markers.h"

namespace sm {

constexpr int kSaveVersion = 3;

enum class SettlementMood : std::uint8_t { Prosperous, Stable, Tense, Unrest, Revolt };

struct SettlementHistory { std::vector<int> days, population; };

struct Settlement {
    int id;
    std::string name;
    int x, y;
    int population;
    SettlementMood mood;
    Inventory inventory;
    SettlementHistory history;
    ArmyComposition garrison;
    EconomyState eco;     // Local market (resources, goods, prices, wealth, happiness)
    int kingdomIdx;
    std::string economy;  // 'farming' | 'mining' | 'trade' | 'fishing' | 'crafting'
};

struct Village {
    int id;
    std::string name;
    int x, y;
    int population;
    SettlementMood mood;
    Inventory inventory;
    EconomyState eco;     // Includes localResources for gathering
    int nearestCityId;
    int lastTradeDay;
    int kingdomIdx;
    SettlementHistory history;
};

struct Spire {
    int id;
    int x, y;
    std::uint32_t spellId;
    bool depleted;
};

struct WorldTime { int day; int hour; int minute; };

enum class GameSubStateKind : std::uint8_t {
    Exploring, Paused, Trading, ViewingMap, Event, Battle,
};
struct GameSubState {
    GameSubStateKind kind = GameSubStateKind::Exploring;
    int settlementId = -1;
    std::string eventId;
    std::string enemyId;
    int pendingEncounterIdx = -1; // index into kEncounters; -1 = none
};

struct Faction {
    std::string id, name, description;
    std::uint32_t color;
    std::unordered_map<std::string, int> relations; // -100..100
};

enum class LogType : std::uint8_t { Combat, Economy, Politics, World };
struct LogEntry { LogType type; std::string message; int day; };

struct PlayerState {
    std::string name;
    int ageDays = 1000;
    float x = 0, y = 0;
    int gold = 100;
    Attributes attributes;
    CombatStats combatStats;
    LevelData   levelData;
    Skills      skills;
    Perks       perks;
    Inventory   inventory;
    std::unordered_map<std::string, int> reputation;
    ArmyComposition army;
    std::vector<std::string> codexUnlocked;
    std::vector<LogEntry>    eventLog;
    std::vector<std::string> spellBookSpellIds;
    std::unordered_map<std::string, int> factionPeaceUntilDay;
    std::vector<int> completedQuestIds;
};

struct GameState {
    int version = kSaveVersion;
    std::string saveName;
    std::uint32_t worldSeed = 0;
    int mapW = 1024, mapH = 1024;

    std::vector<Settlement> settlements;
    std::vector<Village>    villages;
    std::vector<Spire>      spires;
    std::vector<Marker>     markers;
    std::unordered_map<std::string, Faction> factions;

    Politik politik;
    PlayerState player;
    WorldTime   worldTime{0, 6, 0};
    GameSubState subState;
    ArmyComposition deserterPool;          // Global pool of fired/deserted soldiers.

    // Active trade caravans in flight; settled when arrivalDay reached.
    // Mirrors `activeTradeRoutes` in TS GameScreen.
    std::vector<TradeRoute> activeTradeRoutes;
    // Per-city last trade dispatch day (cityId → day). TS keeps this in
    // a module-static Map; storing it on GameState makes it deterministic
    // across save/load cycles.
    std::unordered_map<int, int> cityLastTradeDay;
};

// ── Factories ────────────────────────────────────────────────
// Mirror `defaultPlayer` / `createGameState` / `createRandomGameState`
// from state.ts. Faction relations are sampled deterministically from
// `seed` using the band system in state.ts.
PlayerState default_player();
void       create_factions(GameState& gs, std::uint32_t seed);
GameState  default_game_state(std::uint32_t seed, int mapW, int mapH);

// Bridge politik → landmark lists. After `generate_politik` (and the
// `snap_cities_to_land` post-pass) the `gs.politik.cities` array holds
// the world's capitals and major cities. This populates the gameplay-
// facing `gs.settlements` (one per politik city) and scatters 1–3
// villages around each city on land cells. Idempotent — clears prior
// landmarks before populating.
struct TerrainData;  // fwd
void populate_landmarks_from_politik(GameState& gs,
                                     const TerrainData& terrain,
                                     std::uint8_t seaLevel8);

} // namespace sm

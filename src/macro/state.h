// Macro-world game state. Mirrors state.ts (compact form).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include "core/time.h"
#include "macro/attributes.h"
#include "macro/character_sheet.h"
#include "macro/items.h"
#include "macro/army.h"
#include "macro/npc.h"
#include "macro/economy.h"
#include "macro/politik.h"
#include "macro/markers.h"
#include "macro/spell_book_state.h"
#include "macro/map_generator.h"

namespace sm {

// v12: the faction registry unification (macro/faction.h) — one row per
// faction incl. the previously unregistered "magika" and the relation matrix
// re-sampled from the temperament bands in registry order, so a v11 faction
// set / reputation map no longer matches the world the code would regenerate.
// v13: sparse per-cell tree-count overrides (`treeOverrides`) — the persisted
// mutations of the derived TreeLayer (felled trees / future woodcutters).
// v14: forests are no longer a feature — FT_Tree removed (FT_DirtRoad byte
// 3 → 2) and the tree layer derives from the spawn_trees massif mask with
// small biome ambience, so a v13 world's derived layers (and hence its
// override baselines) no longer match what this code regenerates.
// v15: the player's entry-side context (PlayerState entryDir/entryTicks,
// macro/entry_context.h) — which side the player walked into the current macro
// cell from, persisted so a save made at a river bank re-enters the subworld
// with the same army-facing placement.
// v16: the player became an ordinary faction row ("player") and his reputation
// map left PlayerState — his standing is now his row in the ONE relation matrix
// (gs.factions), so the player block no longer carries a string→int map and the
// faction matrix carries one more row.
// v17: the `athletics` skill — training that multiplies the speed `spd` grants
// (attributes add, skills multiply). Skills are a POD block in the save, so a
// new field shifts it.
// v18: the world clock is ONE integer tick (core/time.h WorldTime), not a
// {day, hour, minute} triple with a float minute accumulator riding alongside
// it. The save now states the instant exactly, to 1/64 of a real second, and
// the block shrank from three ints to one u64.
constexpr int kSaveVersion = 18;

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
    SoldierSquad garrison;
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

enum class GameSubStateKind : std::uint8_t {
    Exploring, Paused, Trading, ViewingMap, Event,
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
    // Shared character sheet — the SAME sm::CharacterSheet type an NPC carries
    // (attributes + skills + perks + levelData). Serialized field-by-field in
    // save.cpp with the on-disk order UNCHANGED (no kSaveVersion bump). The
    // player and every humanoid NPC now describe their RPG state through one
    // type; see macro/character_sheet.h.
    CharacterSheet sheet;
    // Derived runtime combat block (HP/MP/SP + regen). Stays a top-level field,
    // NOT inside the sheet — it is projected FROM the sheet, not persisted as
    // part of it (mirrors an NPC's ECS Health/Combat living outside its sheet).
    CombatStats combatStats;
    Inventory   inventory;
    // NOTE. There is no `reputation` map here any more. The player's standing
    // with every faction IS his row in the one relation matrix
    // (gs.factions["player"].relations) — see player_reputation /
    // add_player_reputation below. Two stores for one number meant the battle
    // pass and the macro matrix could disagree about the same pair.
    SoldierSquad army;
    std::vector<std::string> codexUnlocked;
    std::vector<LogEntry>    eventLog;
    SpellBook spellBook;
    std::unordered_map<std::string, int> factionPeaceUntilDay;
    std::vector<std::string> completedQuestIds;
    std::vector<std::string> failedQuestIds;
    // Possession persistence (Inc 5e-2, kSaveVersion 10). If the player left a
    // subworld while possessing a projected macro NPC, this holds that NPC's
    // deterministic spawn ordinal (ecs::MacroSpawnId) so the PlayerTag flag can
    // be re-attached to the SAME regenerated NPC after load. -1 = not possessing
    // anyone (the flag rides the ordinary hero husk). The ordinal — not an
    // entt::entity — is the one identity that survives the ECS never being
    // serialized (see components.h MacroSpawnId, boot_world_from_save).
    int possessedMacroSpawnId = -1;
    // Entry-side context (macro/entry_context.h, kSaveVersion 15): the packed
    // signed step of the last macro cell change (0xFF = unknown) and the
    // saturating count of AI ticks spent in the cell since. Same two bytes a
    // macro NPC carries in MacroNpcRuntime; consumed by SubworldEngine::enter
    // to place the player near the edge it actually walked in from.
    std::uint8_t entryDir = 0xFF;
    std::uint8_t entryTicks = 0;
    // Transient accumulator toward the next entryTicks increment; NOT
    // serialized (worst case a load loses < kAiTicks of band depth).
    std::uint32_t entryTickAccum = 0;   // world ticks toward the next entry tick
};

struct GameState {
    int version = kSaveVersion;
    std::string saveName;
    std::string savedAt;
    std::uint32_t worldSeed = 0;
    int mapW = 1024, mapH = 1024;
    LayerParameters mapParams{};
    int cityCountTarget = 0;

    std::vector<Settlement> settlements;
    std::vector<Village>    villages;
    std::vector<Spire>      spires;
    std::vector<Marker>     markers;
    std::unordered_map<std::string, Faction> factions;

    Politik politik;
    PlayerState player;
    WorldTime   worldTime = world_time_at(0, 6, 0);
    GameSubState subState;
    SoldierSquad deserterPool;             // Fired/deserted NPC soldiers.

    // Active trade caravans in flight; settled when arrivalDay reached.
    // Mirrors `activeTradeRoutes` in TS GameScreen.
    std::vector<TradeRoute> activeTradeRoutes;
    // Per-city last trade dispatch day (cityId → day). TS keeps this in
    // a module-static Map; storing it on GameState makes it deterministic
    // across save/load cycles.
    std::unordered_map<int, int> cityLastTradeDay;

    // Sparse tree-count mutations: cell index (y*mapW+x) → current count.
    // The full TreeLayer is derived from the seed each boot (macro/tree_layer.h);
    // only cells changed by play persist here (v13). Same shape as
    // sm::TreeOverrides — kept as a plain map to avoid an include cycle.
    std::unordered_map<std::uint32_t, std::uint16_t> treeOverrides;
};

// ── Relations, including the player's ────────────────────────
//
// ONE storage for "how does A regard B": gs.factions[A].relations[B]. The player
// is a row in it like anyone else (macro/faction.h "player"), so his standing —
// what the game calls reputation — is not a second map living on PlayerState.
// It used to be, and that meant two sources of truth for the same number: the
// battle pass asked reputation while the macro matrix held its own stale answer
// for the very same pair.
//
// Writes are SYMMETRIC, mirroring create_factions: a change to how the player
// regards a faction is the same change to how it regards him. That is what lets
// every consumer — combat masks, dialogue, quests, the diplomacy panel — ask one
// function about any pair without caring whether the player is on either side.

// Relation of `a` toward `b`, degrading SAFELY to 0 (neutral) for null/empty ids,
// unknown ids, or an absent matrix entry. Same faction → 100.
inline int faction_relation(const GameState* gs, const char* a, const char* b) {
    if (!gs || !a || !b || a[0] == '\0' || b[0] == '\0') return 0;
    if (std::strcmp(a, b) == 0) return 100;
    const auto itA = gs->factions.find(a);
    if (itA == gs->factions.end()) return 0;
    const auto itR = itA->second.relations.find(b);
    return itR == itA->second.relations.end() ? 0 : itR->second;
}

// The player's standing with `factionId` — a plain relation lookup on his row.
inline int player_reputation(const GameState* gs, const char* factionId) {
    return faction_relation(gs, kPlayerFactionId, factionId);
}

// Fetch a faction's row, creating it WITH ITS IDENTITY if this is the first
// mention of it in this world. Never insert a bare row: save.cpp re-keys the
// whole map by Faction::id on load, so a row written with an empty id comes back
// under the empty key — and takes every other bare row down with it.
inline Faction& ensure_faction_row(GameState& gs, const char* id) {
    Faction& f = gs.factions[id];
    if (!f.id.empty()) return f;
    f.id = id;
    const int fi = faction_index(id);
    if (fi >= 0) {
        f.name        = kFactionDefs[fi].name;
        f.description = kFactionDefs[fi].description;
        f.color       = kFactionDefs[fi].color;
    } else {
        f.name = id;   // an id from data/script with no registry row
    }
    return f;
}

// Move that standing by `delta`, writing both directions of the pair.
inline void add_player_reputation(GameState& gs, const char* factionId,
                                  int delta) {
    if (!factionId || factionId[0] == '\0' || delta == 0) return;
    if (std::strcmp(factionId, kPlayerFactionId) == 0) return;  // no self-standing
    const int next = player_reputation(&gs, factionId) + delta;
    ensure_faction_row(gs, kPlayerFactionId).relations[factionId] = next;
    ensure_faction_row(gs, factionId).relations[kPlayerFactionId] = next;
}

// ── Factories ────────────────────────────────────────────────
// Mirror `defaultPlayer` / `createGameState` / `createRandomGameState`
// from state.ts. Faction relations are sampled deterministically from
// `seed` using the band system in state.ts.
PlayerState default_player();
void       create_factions(GameState& gs, std::uint32_t seed);
GameState  default_game_state(std::uint32_t seed, int mapW, int mapH,
                              const LayerParameters& mapParams = LayerParameters{},
                              int cityCountTarget = 0);

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

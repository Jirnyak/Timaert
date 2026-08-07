// Macro-world game state. Mirrors state.ts (compact form).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include "core/rng.h"
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
// v19: rng.h next_f01() honest [0,1) (top-24-bit grid, the old /2^32 rounded
// the top codes to exactly 1.0f). Same seed now regenerates a different world,
// so every save that stores a worldSeed is invalidated.
// v20: EventTag renumbered densely after the 16 never-referenced tags were
// deleted — quest onAccept events persist tag VALUES, so the numbering is
// part of the save format.
// v21: TradeRoute carries origin/dest KIND (village vs settlement) — the two
// id spaces both start at zero and the old settlements-first arrival lookup
// credited village revenue to whatever city shared the number.
// v22: lastWorldRebakeDay joins GameState (Session 17) — the monthly re-bake/
// autosave phase used to live on App and reset on every load, so a load
// always pushed the next autosave a full season away.
// v23: the macro-ECS snapshot (Session 17, macro/macro_snapshot.h) — every
// persistent macro NPC rides the save as a record, load restores instead of
// re-spawning from the seed, and MacroSpawnId ordinals come from ONE
// persistent monotonic counter (nextMacroSpawnOrdinal) instead of a
// max-over-living scan that reissued dead men's identities (19.24).
// v24: the world's runtime rhythms (Session 17) — the daily-tick queue with
// its remainder and jitter RNG (WorldTickRuntime, now a GameState member)
// and the macro-AI sweep rhythm (MacroAiRhythm). Without them every load
// re-rolled the SAME jitter sequence, dropped queued days and reset the
// sweep phase.
// v25: story progress (Session 17) — which logic nodes still EXIST and which
// are ACTIVE. Loads used to skip register_intro_story_nodes entirely
// (3 nodes -> 1), so a loaded game lost the intro AND chapter 1.
// v26: sparse deposit overrides (W2a, macro/deposit_layer.h) — the drained
// remains of the world's clay/iron/stone cells, tree-override pattern.
constexpr int kSaveVersion = 26;

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
    // Forced pre-battle encounter (Session 15): a hostile squad on the map
    // stopped the player — the M&B screen is up and the world is paused.
    // Appended LAST: Event == 4 is pinned by save_roundtrip_test. The target
    // squad is runtime App state (an entt handle is not save material); a
    // loaded save that says PreBattle with no live target resets to
    // Exploring on the first frame — fail closed, no version bump.
    PreBattle,
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
// The player's log is a RING capped at this many entries (po2). save.cpp
// counts every vector against a hard cap and refuses to write past it, so an
// unbounded log would not crash — it would make EVERY future save fail while
// the UI keeps showing the stale file on disk as Ready (the garrison day-820
// lesson, world_tick.h). The cap is enforced at push_event_log below, the one
// door every gameplay writer goes through.
inline constexpr std::size_t kMaxEventLogEntries = 8192;

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

// The ONE door into the player's event log: past kMaxEventLogEntries the
// OLDEST entry is dropped, so the log can grow for a ten-year campaign and a
// save can always count it. Direct eventLog.push_back is reserved for the
// save loader restoring an already-counted payload.
inline void push_event_log(PlayerState& p, LogEntry e) {
    if (p.eventLog.size() >= kMaxEventLogEntries)
        p.eventLog.erase(p.eventLog.begin());
    p.eventLog.push_back(std::move(e));
}

// World-tick runtime (moved here from world_tick.h in v24, because it is
// STATE): the budgeted daily-simulation queue, the subworld step remainder
// and the jitter stream the daily economy rolls. All integers on purpose —
// the old float scale could not survive a save or a pause without losing a
// sliver of a day.
struct WorldTickRuntime {
    int pendingDailyTicks = 0;
    int nextDailyTickDay = 0;
    // Subworld only: the clock there advances one tick per
    // kSubworldTickDivisor simulation steps; this counts the steps not yet
    // spent.
    std::uint64_t subworldStepRemainder = 0;
    Rng jitter{0xC0FFEEu};
};

// The persistent half of the macro-AI runtime (npc_ai.h MacroNpcAiRuntime):
// the jitter stream and the sweep rhythm. The transient half (the squad
// index) is rebuilt every drive and stays out of the save. Synced with the
// live runtime at exactly two doors — staging before a save, applying after
// a load (src/app/main.cpp) — fixed-width fields because this is a save
// block.
struct MacroAiRhythm {
    Rng           jitter{0xA1F0u};
    std::uint32_t sweepAccum = 0;
    std::int32_t  pendingSweeps = 0;
    std::uint64_t sweepCursor = 0;
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
    // The day the slow world last re-baked (path-cost grid) and autosaved —
    // once a season, together (Session 21). Lives HERE, not on App, so a load
    // keeps the phase instead of pushing the next autosave a season away (v22).
    int lastWorldRebakeDay = 0;
    // The ONE issuer of MacroSpawnId ordinals (v23): monotonic, never reused,
    // survives the save. Every creation path (boot spawn, quest spawn, console
    // squads) draws from here — the old max-over-living scan reissued a dead
    // NPC's ordinal, and a load could wake the player in a stranger's body
    // (problems.md 19.24).
    std::uint32_t nextMacroSpawnOrdinal = 0;
    // The world's runtime rhythms (v24). worldTickRt is the LIVE runtime —
    // world_tick.cpp mutates it in place; macroAiRhythm is the staged image
    // of App::npcAi's persistent half (see the two sync doors in main.cpp).
    WorldTickRuntime worldTickRt;
    MacroAiRhythm    macroAiRhythm;
    // Story progress (v25): the ids of logic nodes that still EXIST (a
    // consumed one-shot stays consumed) and of those ACTIVE. Definitions are
    // code, re-registered on every boot; these two lists replay the
    // progress. Staged/applied at the same two doors as macroAiRhythm.
    std::vector<std::string> logicNodesRegistered;
    std::vector<std::string> logicNodesActive;
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

    // Sparse deposit mutations (v26): cell index → remaining units. The
    // deposit layer itself derives from the seed each boot
    // (macro/deposit_layer.h); only veins play has drained persist here.
    // Same raw-map shape as treeOverrides, for the same include-cycle reason.
    std::unordered_map<std::uint32_t, std::int32_t> depositOverrides;
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

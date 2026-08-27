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
#include "macro/agent_memory.h"
#include "macro/army.h"
#include "macro/resource_field.h"
#include "macro/npc.h"
#include "macro/economy.h"
#include "macro/politik.h"
#include "macro/relations.h"
#include "macro/knowledge.h"
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
// v27: MacroNpcRuntime grows homeIsVillage (W2b) — an agent's home names its
// id SPACE, so a village woodcutter is finally the village's man; the
// runtime rides the macro snapshot as a POD block, so the layout is format.
// v28: AgentMemory joins the macro record (W2b, macro/agent_memory.h) —
// what a leader remembers (a caravan's market snapshot, a raided village)
// survives the save, 136 padding-free bytes per agent.
// v29: the OLD economy is gone (W2b-4): EconomyState's float arrays, the
// abstract TradeRoute system and cityLastTradeDay leave the save; landmarks
// carry the honest day's readouts instead (starved/unmet/famine + the
// logistic population carry).
// v30: deposit overrides carry the KIND (packed u64, W2c) — a discovered
// iron vein (stone quarry struck iron) must survive a load.
// v31: faction CURRENCIES (owner, W2d): money is a commodity — four coin
// rows replace the "gold" item; treasuries hold the kingdom's coin, purses
// the agent's faction's (an extra make_npc RNG draw re-rolls worlds).
// v32: PlayerState::gold is GONE — the player's money is coin in his
// inventory like every other squad's; PlayerState gains AgentMemory (debt
// facts live there, summed by the fact arithmetic).
// v33: sparse fauna-count overrides (`faunaOverrides`, Session 16) — the
// wild headcount is an honest macro stock: cells the hunt has scarred
// persist, so a cleared pack stays cleared across a load.
// v34: sparse crop-harvest scars (`cropOverrides`, Field Inc F3) — the
// standing wheat is an honest macro stock: what the sickle took stays
// taken across a load and regrows on the world clock.
// v35: ONE resource-field container (Field Inc F7 / R1): fauna and wheat
// scars live in `resourceScars[ResourceFieldId]` — one dialect (the SCAR),
// one generic save block per field. The old fauna remaining-count override
// died with its dialect.
// v36: the forest is the Trees CARRIER row of the resource-field registry
// and the save carries the tree grid WHOLE (a living, growing field is not
// derivable from seed + sparse scars) — `treeOverrides` died with the
// derive-plus-overlay model.
// v37: deposits are three carrier rows (Clay/Iron/Stone) with one sparse map
// PER KIND — a cell may hold several kinds (iron found IN a stone mountain;
// nothing vanishes), so the single-kind packed override died and the save
// carries the deposit cells whole, like the tree grid.
// v38: Spire carries its spell's tier — the spire's whole context (zone gate
// at placement, tower storey count, guard site) derives from it, and the
// subworld may not reach up into the spell registry to recompute it.
// v39: the tier cache dies — the spell registry moved into the world layers
// (macro/spells.h, ARCHITECTURE.md Rule 13), so every consumer derives tier
// from spellId at the moment of reading. The save stops carrying a registry
// number as cargo.
// v40: the player's knowledge of the map (macro/knowledge.h) — the explored
// grid rides the save whole, one byte per cell. Visible (2) is a session
// projection of where the player stands and decays to Explored (1) on write;
// a load recomputes sight from the restored position. Terra incognita became
// a fact of the world, so a v39 save — a world the player "knew" entirely —
// no longer describes one.
// v41: spell cooldowns are STEPS, not float seconds (core/time.h). The field
// changed type as well as meaning, so a v40 slot's floats would be read as
// enormous step counts — a saved book would come back locked for hours.
// v42: a squad member's `kind` is 16 bits — the ONE id space bodies already
// share (humanoid ordinal below 0x100, monster catalog row at or above it).
// A beast could not stand in a roster while the field was a byte, so a wolf
// pack was not expressible as a squad; the macro snapshot refused monster
// entities for the same reason. Both are now open (CANON.md S4/S16).
// v44: the player is an ordinary squad, so his goods and his head stopped
// being fields of PlayerState. His bag is the NpcInventory and his memory the
// AgentMemory on his squad entity, both written by the macro-snapshot record
// that already carries every other leader's — two blocks left the player
// section of the file and no block replaced them.
// v45: a macro leader's runtime carries his back — `carryCap` (the sheet's
// carry capacity, cached beside maxSp/travelRank/marathonRank/moveMult) and
// `overloadCost`, the SP surcharge his current load is costing. The overload
// law was the player's alone, so a caravan hauling a ton marched like an
// empty scout; it is universal now (owner ruling), and MacroNpcRuntime rides
// the macro snapshot as a POD block, so its layout IS the format.
// v46: skill RANKS are a flat envelope of bytes and the meanings are rows
// (macro/attributes.h kSkillDefs). The block is the same 32 bytes it was as
// eight ints, which is exactly why the version had to move: a v45 slot would
// load with the same LENGTH and none of the same meaning — four ranks read out
// of one, and no reader the wiser.
constexpr int kSaveVersion = 46;

enum class SettlementMood : std::uint8_t { Prosperous, Stable, Tense, Unrest, Revolt };

struct SettlementHistory { std::vector<int> days, population; };

struct Settlement {
    int id;
    std::string name;
    int x, y;
    int population;
    SettlementMood mood;
    // THE store (owner's ruling, W2): the landmark's universal Inventory is
    // its market, its granary and its warehouse in one — agents deliver into
    // it, the day-loop eats from it, the trade panel sells out of it.
    Inventory inventory;
    SettlementHistory history;
    SoldierSquad garrison;
    int kingdomIdx;
    // The honest economy's daily readouts (v29): yesterday's hunger and
    // comfort shortfall (for the eye and the mood), the famine edge flag,
    // and the fractional carry of the LOGISTIC population law.
    std::uint16_t starvedYesterday = 0;
    std::uint16_t unmetYesterday = 0;
    std::uint8_t  famineActive = 0;
    float         popGrowthCarry = 0.0f;
};

struct Village {
    int id;
    std::string name;
    int x, y;
    int population;
    SettlementMood mood;
    Inventory inventory;
    int nearestCityId;
    int kingdomIdx;
    SettlementHistory history;
    std::uint16_t starvedYesterday = 0;
    std::uint16_t unmetYesterday = 0;
    std::uint8_t  famineActive = 0;
    float         popGrowthCarry = 0.0f;
};

struct Spire {
    int id;
    int x, y;
    // kSpellDefs row ordinal (macro/spells.h, append-only). The spire's whole
    // difficulty context — placement gate, tower storeys, guard site — is the
    // spell's tier, derived from this ordinal at the moment of reading.
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

// (struct Faction is gone. Its four identity columns — id, name, description,
// colour — were verbatim copies of the registry row that already declares them
// (macro/faction.h kFactionDefs), duplicated into every save; its `relations`
// map became the flat matrix in macro/relations.h. What a faction IS lives in
// the registry; how factions REGARD each other lives in the matrix; there is
// nothing a third structure could hold.)

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
    // (No gold FIELD: money is faction coin in `inventory` — macro/currency.h
    // wallet math. The player is a squad like any other, v32.)
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
    // (No `inventory` field. The player's bag is the ordinary
    // ecs::NpcInventory on his squad entity — macro/player_entity.h
    // player_inventory(). It was the last large field that made him a
    // different kind of thing from the squads around him.)
    // NOTE. There is no `reputation` map here any more. The player's standing
    // with every faction IS his row in the one relation matrix
    // (gs.factions["player"].relations) — see player_reputation /
    // add_player_reputation below. Two stores for one number meant the battle
    // pass and the macro matrix could disagree about the same pair.
    // (No `army` field. The player's squad is an ORDINARY squad — the roster
    // is ecs::SquadRoster on his macro entity, reached through
    // macro/player_entity.h player_roster(). It sat here as a SoldierSquad of
    // its own until 2026-08-27, and every consumer of it was a
    // player-specific path CANON S4 forbids by name.)
    std::vector<std::string> codexUnlocked;
    std::vector<LogEntry>    eventLog;
    SpellBook spellBook;
    // Truce clocks, one per faction SLOT (macro/relations.h): the day a
    // cease-fire with that faction runs out. It was the last string-keyed
    // faction map in the game — and it has no gameplay reader yet, so the
    // concept is kept (S24 politics will want truces) in the shape everything
    // else about factions now has: a flat array indexed by ordinal.
    std::array<std::int32_t, kMaxWorldFactions> factionPeaceUntilDay{};
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
    // (No `memory` field. The player's head is the ordinary AgentMemory on
    // his squad entity — the same component every squad leader carries, saved
    // by the same macro record. It sat here as a second store with ZERO
    // readers in src/: everything that remembers anything about the player
    // was already going through the entity.)
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
    // The player's map knowledge (v40): Unknown / Explored / Visible per cell.
    // Explored persists; Visible is re-derived from the player's position
    // (update_player_sight) — save.cpp clamps it away on write.
    KnowledgeLayer knowledge;
    // THE relation matrix — flat, by ordinal (macro/relations.h). The
    // string-keyed map of string-keyed maps it replaced cost two temporaries,
    // two hashes and two strcmps per question, and the battle asks K² of them
    // per tick.
    RelationMatrix relations{};

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
    // (The abstract TradeRoute system is GONE (v29): trade is caravan
    // AGENTS carrying real cargo between real inventories — macro/npc_ai.cpp
    // ai_caravan.)

    // (treeOverrides and depositOverrides are GONE (v36/v37): forests and
    // deposits are carrier rows of the resource-field registry, and the save
    // carries their live state whole — save.cpp takes the carriers alongside
    // the state.)

    // The resource fields' scars (v35, macro/resource_field.h): one map per
    // ResourceFieldId, cell index → units play has taken and regrowth has
    // not yet returned. Sparse-dialect rows only (wheat, fauna) — the
    // baseline is derived (pure terrain/climate), the scar is the only
    // storage, a healed cell erases itself. Carrier rows (trees) keep this
    // slot EMPTY: their live state is their carrier, saved whole.
    std::unordered_map<std::uint32_t, std::uint16_t>
        resourceScars[std::size_t(ResourceFieldId::Count)];
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
    return relation_of(gs->relations, faction_slot(gs->relations, a),
                       faction_slot(gs->relations, b));
}

// The player's standing with `factionId` — a plain relation lookup on his row.
inline int player_reputation(const GameState* gs, const char* factionId) {
    return faction_relation(gs, kPlayerFactionId, factionId);
}

// ── THE binary hostility rule (damage-door track Inc 3) ────────────────────
// ONE threshold over the ONE matrix, spelled ONCE. Everything else is a form
// of this answer, never a second formula:
//   • the battle masks are its BAKED form — build_faction_masks applies the
//     same threshold over the same matrix once per tick and hostility becomes
//     a shift-and-AND (sub/battle.h);
//   • the per-entity subworld door (hostile_to_player_entity) adds only
//     SESSION state on top: the player's own side is never hostile, and a
//     TempHostileToPlayer grudge overrides the matrix until the body dies or
//     the scene ends;
//   • the stance colours (player_stance) are its continuous projection for
//     the eye — they may soften the answer, never contradict it.
// Six hand-spelled `relation < threshold` comparisons converged here; the
// macro side (squad threat, aggressive pursuit, the forced encounter) asks
// these functions, so the map and the ground read one law.
inline bool factions_hostile(const GameState* gs, const char* a,
                             const char* b) {
    return faction_relation(gs, a, b) < kHostileThreshold;
}

// Is this faction the PLAYER's enemy — the pair every player-facing consumer
// asks about (melee oracle, flee brain, dev cheat, forced encounters).
inline bool player_hostile_to(const GameState* gs, const char* factionId) {
    return factions_hostile(gs, kPlayerFactionId, factionId);
}

// Fetch a faction's row, creating it WITH ITS IDENTITY if this is the first
// mention of it in this world. Never insert a bare row: save.cpp re-keys the
// whole map by Faction::id on load, so a row written with an empty id comes back
// under the empty key — and takes every other bare row down with it.
// A faction's SLOT, claimed if this is the first the world hears of it. The
// map form created a phantom row keyed by a bare id here, and save.cpp re-keyed
// the whole map by Faction::id on load — so a row written with an empty id came
// back under the empty key and took every other bare row with it. A slot cannot
// be bare: it is a number, and an unknown id claims a reserved one.
inline FactionSlot ensure_faction_slot(GameState& gs, const char* id) {
    return claim_faction_slot(gs.relations, id);
}

// Move that standing by `delta`, writing both directions of the pair.
inline void add_player_reputation(GameState& gs, const char* factionId,
                                  int delta) {
    if (!factionId || factionId[0] == '\0' || delta == 0) return;
    if (std::strcmp(factionId, kPlayerFactionId) == 0) return;  // no self-standing
    const FactionSlot me = ensure_faction_slot(gs, kPlayerFactionId);
    const FactionSlot them = ensure_faction_slot(gs, factionId);
    if (me == kNoFactionSlot || them == kNoFactionSlot) return;
    set_relation(gs.relations, me, them,
                 relation_of(gs.relations, me, them) + delta);
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
// facing `gs.settlements` (one per politik city) and settles villages
// on the best-scoring cells of each city's hinterland (R2: resources
// are primary, settlement is derived — macro/settlement_score.h), so
// the tree and deposit layers must exist BEFORE this runs. Idempotent —
// clears prior landmarks before populating.
struct TerrainData;  // fwd
struct TreeLayer;
struct DepositLayer;
void populate_landmarks_from_politik(GameState& gs,
                                     const TerrainData& terrain,
                                     std::uint8_t seaLevel8,
                                     TreeLayer& trees,
                                     DepositLayer& deposits);

} // namespace sm

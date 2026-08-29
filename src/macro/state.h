// Macro-world game state. Mirrors state.ts (compact form).
#pragma once
#include <cstdint>
#include <cstdio>
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
#include "macro/landmark_registry.h"
#include "macro/resource_field.h"
#include "macro/npc.h"
#include "macro/economy.h"
#include "macro/politik.h"
#include "macro/relations.h"
#include "macro/knowledge.h"
#include "macro/chronicle.h"
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
// v47: attribute SCORES are a flat envelope of bytes too (macro/attributes.h
// kAttributeDefs), 16 slots for the 9 the game names — so naming the tenth is
// a row, not a format. The block shrank from nine ints to sixteen bytes, and a
// v46 slot would be read at the wrong length entirely.
// v48: an item's effect is rows of the ONE bonus registry (macro/bonus.h).
// `ItemEffect`'s six named ints are gone — three of them were fiction nothing
// read, and one named an attribute the sheet does not have — and `ItemAffix`
// is now literally the registry's `Bonus`. That last one is byte-identical, so
// no saved item moved; the version follows the catalog's meaning changing
// under the same bytes.
// v49: a body's WORN gear rides the macro record (macro/anatomy.h Equipment).
// The cells are a flat array with holes, so the file carries cell INDICES and
// the crowd — which wears nothing — costs a zero count. A two-hander's blocked
// cells are DERIVED on load rather than stored: a second copy of what the
// catalog already says could disagree with it after a retune.
// v50: a settlement's history is a fixed RING of a season (32 days), written
// oldest-first so the file carries a past rather than a ring's seam. It was
// two heap vectors per settlement capped by `erase(begin())` — an O(n) shift
// per town per game day — and the window was 30, a month from another
// calendar; it is kDaysPerSeason now, the span the whole world already grows
// by. The block's LENGTH changed, so the version had to move.
// v51: the world's own memory rides the save (macro/chronicle.h, CANON S20.1).
// Both tiers whole — the ring's LIVE facts (a fresh world costs four bytes,
// not two megabytes of zeroes) and the annals entire, because the annals are
// not a cache but part of the world and a legends mode will read exactly them
// (owner's ruling: «сейв обязательно нёс историю»). The per-cell chains are
// NOT written: a link is derived from the facts, and a stored derivative is a
// second truth waiting to disagree.
// v52: a squad carries RENOWN (ecs::MacroNpcRuntime). A band starts nameless —
// its deeds are weather the ring forgets in a season — and BECOMES a figure by
// doing enough, after which its deeds go into the annals for good (owner,
// 2026-08-27). One number, not a counter plus a flag: "is it named" is derived
// from it, so the two can never disagree about the same band. The runtime is a
// POD block of the macro record, so its layout is the format.
// v53: RENOWN belongs to every MACRO entity with an identity, not to squads
// alone (owner, 2026-08-27) — a band, a city, a people. Settlements and
// villages carry theirs, so the landmark blocks grew a field. And what a deed
// is worth became CONTEXTUAL: the base its row gives plus a share of what the
// victim was worth, which is a number the world already kept about them.
// v54: ONE landmark identity (owner, 2026-08-28). Cities, villages and spires
// used to be numbered from zero in three independent registers, so an id
// alone never named a place — the chronicle paid a village's renown to the
// city wearing the same number, and two crutch bits (MacroNpcRuntime
// homeIsVillage, MacroStockKey subjectIsVillage) existed only to disambiguate.
// Now every landmark draws its id from GameState::nextLandmarkOrdinal — the
// same monotonic-ordinal law MacroSpawnId already lives by — and both
// crutches are dead, which changes the NPC runtime POD's layout.
// v55: ANNIHILATION of worked-out veins (owner, 2026-08-28: «истощённая жила
// — это не существующая жила»). A deposit cell leaves the map when it runs
// dry.
// v56: the scarcity baseline is DERIVED, not stored (owner: world level vs
// «суммарно железа в мире»). DepositLayer::virginUnits is recomputed from
// terrain + seed every boot — the v55 drainedCells counter left the format
// the day it arrived.
// v57: the player's JOURNAL — his knowledge of the world's facts (owner,
// 2026-08-28: the player knows only what he took part in, what happened on
// his cell while he stood there, and — later — rumours; and his journal is
// the log of his WHOLE game, it never forgets). Copies of chronicle records,
// append-only, loud cap; rides in write_player.
// v58: the event log is GONE (owner, 2026-08-28: «чисти ивент лог»). Session
// messages are a fading HUD feed that dies with the moment (SessionFeed,
// never serialized); the player's past is his JOURNAL of chronicle records;
// two of its lines became honest facts instead (a struck vein = Discovered,
// a player's deal = Traded). 8192 saved std::strings leave the format.
// v59: the spellbook is FLAT (macro/spell_book_state.h) — ordinal-indexed
// rows over the append-only spell registry; the string ids and the three
// heap containers left the format.
// v60: dead-code sweep. LayerParameters (a save-prefix POD) dropped its
// never-read tempMin/tempMax columns.
// v61: the world seed is an INTEGER (S26 «всё дискретно»). LayerParameters
// (a save-prefix POD) carries `seed` as uint32_t where a float sat — same
// four bytes, different meaning, so the version moves. Every seed the game
// ever wrote was < 100000 (the UI decimation), exactly representable in
// both types: the worlds themselves are bit-identical.
// v62: ONE landmark roster (CANON S9, owner 2026-08-29). The three vectors
// (settlements / villages / spires) and their three serializers became one
// `std::vector<Landmark>` with a kind column and one serializer; every kind
// writes every column, unused ones ride at zero defaults. A S9 transition
// (village→city, spire→ruin) is now a column flip, not a record move.
// v63: quest identity is an ORDINAL (CANON S20.1, owner 2026-08-29). Quests
// carry nextQuestOrdinal numbers + a POD offer-provenance triple; the id
// string and its FNV event key are dead. The eternal completedQuestIds /
// failedQuestIds string vectors became settledQuestOffers (same-day dedup —
// their only living semantic) + two lifetime counters. The codex unlock
// state is a bit per article ordinal (macro/codex.h registry, was UI-owned
// string tables + a string vector).
constexpr int kSaveVersion = 63;

enum class SettlementMood : std::uint8_t {
    Prosperous, Stable, Tense, Unrest, Revolt, Count
};

// ── THE mood registry (CANON S16) ────────────────────────────────────────
// Everything the game says ABOUT a temper band is a column of ONE row: the
// label and colour the UI prints, the market multipliers the economy prices
// with (economy.cpp mood_price_mult — a prosperous town sells cheap and pays
// well; one in revolt charges a risk premium and haggles the traveller down),
// and the inn bed's cost. These lived as four switch-shaped dictionaries
// across ui/overlays.cpp and macro/economy.cpp until 2026-08-29.
struct MoodRow {
    SettlementMood mood;      // MUST equal the row's index (guard below)
    const char*    label;
    std::uint32_t  color;     // 0xRRGGBB — the UI's tint for this band
    float          buyMul;    // what the town charges the traveller
    float          sellMul;   // what it pays him
    int            restCost;  // inn bed, gold
};
inline constexpr MoodRow kMoodRows[std::size_t(SettlementMood::Count)] = {
    {SettlementMood::Prosperous, "Prosperous", 0x5ADC78u, 0.9f, 1.1f,   5},
    {SettlementMood::Stable,     "Stable",     0xDCDCDCu, 1.0f, 1.0f,  10},
    {SettlementMood::Tense,      "Tense",      0xF0C850u, 1.0f, 1.0f,  15},
    {SettlementMood::Unrest,     "Unrest",     0xDC8250u, 1.2f, 0.85f, 20},
    {SettlementMood::Revolt,     "Revolt",     0xE64646u, 1.4f, 0.7f,  30},
};
static_assert(rows_in_enum_order(kMoodRows, &MoodRow::mood),
              "kMoodRows row order must mirror SettlementMood");

inline constexpr const MoodRow& mood_row(SettlementMood m) {
    // A byte from outside the band table answers as Stable — the neutral row
    // the old switch defaults painted (colour, multiplier and cost match).
    return std::size_t(m) < std::size_t(SettlementMood::Count)
               ? kMoodRows[std::size_t(m)]
               : kMoodRows[std::size_t(SettlementMood::Stable)];
}

// A settlement's recent past, as a fixed RING of a season (owner's decision,
// 2026-08-27). It was two heap vectors per settlement with a cap enforced by
// `erase(begin())` — an O(n) shift, per settlement, every single game day, on
// a container that had a heap header for every town on the map.
//
// 32 is not a chosen number: it is `kDaysPerSeason`, the epoch the forest
// grows by and the population's own carry accrues over, so "the recent past"
// means the same span here as everywhere else. The old 30 was a month from
// another calendar.
inline constexpr int kSettlementHistoryDays = 32;

struct SettlementHistory {
    std::array<std::int32_t, kSettlementHistoryDays> day{};
    std::array<std::int32_t, kSettlementHistoryDays> population{};
    std::uint8_t count = 0;   // entries that are real; < kSettlementHistoryDays
    std::uint8_t head  = 0;   // where the NEXT day is written

    int size() const { return int(count); }
    bool empty() const { return count == 0; }
    // Oldest first, so a caller reads the past in the order it happened
    // without knowing where the ring's seam is.
    int day_at(int i) const {
        const int first = int(count) < kSettlementHistoryDays
                              ? 0 : int(head);
        return day[std::size_t((first + i) % kSettlementHistoryDays)];
    }
    int population_at(int i) const {
        const int first = int(count) < kSettlementHistoryDays
                              ? 0 : int(head);
        return population[std::size_t((first + i) % kSettlementHistoryDays)];
    }
    void push(int d, int pop) {
        day[std::size_t(head)] = d;
        population[std::size_t(head)] = pop;
        head = std::uint8_t((int(head) + 1) % kSettlementHistoryDays);
        if (int(count) < kSettlementHistoryDays) ++count;
    }
};

// ── THE landmark record (CANON S9, owner verdict 2026-08-29) ─────────────
// One struct, one vector, a KIND COLUMN. City, village and spire used to be
// three structs in three GameState vectors — which made "what stands on a
// cell" a switch, made a S9 transition (village→city, spire→ruin) a record
// MOVE between types, and made every new landmark kind a new vector plus
// save code. Now the kind is data: a transition flips `type` (plus a grid
// rebake), and a new kind is its registry row plus the columns it reads.
// Fields a kind does not use sit at their zero defaults — the zero
// contribution, CANON S6 — and cost nothing but bytes (S26: size is not an
// argument).
struct Landmark {
    int id = -1;             // world-unique ordinal (nextLandmarkOrdinal, v54)
    LandmarkType type = LandmarkType::None;  // THE kind column (registry row)
    std::string name;        // "" where the kind carries none (spires derive)
    int x = 0, y = 0;
    int population = 0;
    SettlementMood mood = SettlementMood::Stable;
    // THE store (owner's ruling, W2): the landmark's universal Inventory is
    // its market, its granary and its warehouse in one — agents deliver into
    // it, the day-loop eats from it, the trade panel sells out of it.
    Inventory inventory;
    SettlementHistory history;
    SoldierSquad garrison;       // empty unless the kind keeps one (cities)
    int kingdomIdx = -1;
    int nearestCityId = -1;      // a village's market city; -1 elsewhere
    // The honest economy's daily readouts (v29): yesterday's hunger and
    // comfort shortfall (for the eye and the mood), the famine edge flag,
    // and the fractional carry of the LOGISTIC population law.
    std::uint16_t starvedYesterday = 0;
    std::uint16_t unmetYesterday = 0;
    std::uint8_t  famineActive = 0;
    float         popGrowthCarry = 0.0f;
    // WHAT THE WORLD THINKS OF THIS PLACE (macro/chronicle.h). Renown is not
    // a squad's private counter — it belongs to every MACRO entity that has an
    // identity (owner, 2026-08-27): a band, a city, a people. A famous city is
    // a harder prize and a louder loss, and beating it is worth more precisely
    // because it was famous. The microworld has none of this.
    std::uint32_t renown = 0;
    // Spire columns: kSpellDefs row ordinal (macro/spells.h, append-only) —
    // the spire's whole difficulty context (placement gate, tower storeys,
    // guard site) is the spell's tier, derived at the moment of reading —
    // and whether its orb has been drained.
    std::uint32_t spellId = 0;
    bool depleted = false;
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

// ── THE SESSION FEED: words that die with the moment ─────────────────────
// (owner, 2026-08-28: «это вообще не нужно хранить даже в сессии — пишется
// в UI как в Might & Magic и сразу забывается»). The old EventLogRing kept
// 8192 std::strings in the SAVE; but a session message ("Game saved.",
// "You have learned Fireball!") is not a fact of the world and not the
// player's journal — it is presentation. So the channel is a tiny POD ring
// the HUD fades out, NEVER serialized. What the world remembers is the
// chronicle; what the player learned is his journal; this is neither.
struct SessionFeed {
    static constexpr int kLines = 8;     // more than fits on screen anyway
    static constexpr int kTextCap = 112; // one HUD line, NUL included
    struct Line {
        char  text[kTextCap];
        float ttl = 0.0f;                // seconds of screen life left
    };
    Line lines[kLines]{};
    std::uint8_t head = 0;               // where the NEXT line goes
};

// A settled quest OFFER's provenance — the POD triple that names an offer
// uniquely (events/quests/quest_types.h Quest: each generator fires at most
// once per settlement per day). Defined here, not there, because the layer
// order runs macro → events: player state stores it, the quest engine reads
// it through this door.
struct SettledQuestOffer {
    std::int32_t giverSettlementId = -1;
    std::int32_t bornDay = -1;
    std::uint8_t offerSlot = 0;
};

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
    // Codex unlock state: one bit per article ordinal (macro/codex.h
    // CodexArticleId; the static_assert there is the loud cap). Replaced a
    // vector of id STRINGS (v63) — a string was doing an ordinal's job.
    std::uint64_t codexUnlockedBits = 0;
    // The player's log — a RING, not a vector that shifts. It was capped by
    // `erase(begin())`, which memmoves up to eight thousand std::strings on
    // every entry past the cap: the same defect the settlement history and the
    // event bus both had, and the same fix. The cap lives in the container, so
    // no caller can forget it and nothing shifts to enforce it.
    //
    // (No event log. Session messages die with the moment — GameState's
    // sessionFeed below; what the player LEARNED is the journal right here;
    // what the world remembers is the chronicle. Three questions, three
    // answers, no fourth store.)
    // Loud cap of the journal below — kChronicleAnnals' own size (owner,
    // 2026-08-28: «на века, не жалко»): the player's whole-game log gets no
    // less room than the world's eternal memory. 2^20 copies × 32 B = 32 MB
    // at the END of a long life (the vector grows as it fills — size is no
    // argument either way, CANON S26); at even 100 learned facts a day that
    // is 80+ game years. Hitting it flips journalFull, never a silent drop.
    static constexpr std::uint32_t kJournalFactsCap = 1u << 20;
    // The player's JOURNAL: his KNOWLEDGE of the world's facts (?27 half-
    // ruling, owner 2026-08-28). The chronicle is the world's one memory; the
    // journal is what of it the player LEARNED — by taking part, by standing
    // on the cell where it happened, and (later, through the same door) by
    // buying rumours. Two owner laws shape the container:
    //   · the journal NEVER forgets — it is the log of his whole game — so it
    //     is append-only with a LOUD cap, not a ring;
    //   · the world's ring DOES forget, so the journal holds COPIES of the
    //     32-byte records, not seq references that would dangle. A fact is
    //     immutable from the moment it is filed, so a copy of it is not a
    //     second truth — there is nothing for the two to disagree about.
    // Words are still derived at display time (fact_sentence): the journal
    // stays a VIEW on the chronicle; what it stores is WHOSE the knowledge is.
    std::vector<WorldFact>   journal;
    std::uint32_t            journalSeenSeq = 0;  // last chronicle seq scanned
    std::uint8_t             journalFull = 0;     // the loud cap flag
    SpellBook spellBook;
    // Truce clocks, one per faction SLOT (macro/relations.h): the day a
    // cease-fire with that faction runs out. It was the last string-keyed
    // faction map in the game — and it has no gameplay reader yet, so the
    // concept is kept (S24 politics will want truces) in the shape everything
    // else about factions now has: a flat array indexed by ordinal.
    std::array<std::int32_t, kMaxWorldFactions> factionPeaceUntilDay{};
    // Quest OFFERS the player has settled (completed or failed) — the POD
    // provenance triples the quest engine's is_known compares against, so a
    // settlement does not re-offer what was already done TODAY. An offer's
    // identity includes its bornDay (quest_types.h), so an entry whose day
    // has passed can never be generated again — the engine prunes stale
    // entries each tick, and the list stays a handful of records.
    //
    // v63: this replaced completedQuestIds/failedQuestIds — two ETERNAL
    // string vectors whose only living semantic was exactly this same-day
    // dedup (the day was baked into the id string, so an old entry never
    // matched anything again — dead weight growing in the save forever).
    std::vector<SettledQuestOffer> settledQuestOffers;
    // Lifetime tallies (the honest split: the old string lists filed a
    // failure into BOTH, a TS relic). Display/stats only — dedup is the
    // provenance list above; history will be the chronicle's job.
    std::uint32_t completedQuestCount = 0;
    std::uint32_t failedQuestCount = 0;
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

// The ONE door into the session feed (drawn and faded by the HUD, never
// saved). Overlong lines are cut at the HUD's own width — a feed line is a
// glance, not a document.
inline void session_feed_push(SessionFeed& f, const char* text) {
    if (!text || text[0] == '\0') return;
    SessionFeed::Line& l = f.lines[f.head % SessionFeed::kLines];
    std::snprintf(l.text, sizeof l.text, "%s", text);
    l.ttl = 6.0f;   // seconds on screen — M&M's own unhurried fade
    f.head = std::uint8_t((f.head + 1) % SessionFeed::kLines);
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

    // THE landmark roster (CANON S9, 2026-08-29): every placed landmark of
    // every kind, one vector, kind = the record's `type` column. Ownership
    // priority for a contested cell is for_each_landmark's yield order
    // (landmark_iter.h), not storage order.
    std::vector<Landmark>   landmarks;
    std::vector<Marker>     markers;
    // The player's map knowledge (v40): Unknown / Explored / Visible per cell.
    // Explored persists; Visible is re-derived from the player's position
    // (update_player_sight) — save.cpp clamps it away on write.
    KnowledgeLayer knowledge;
    // WHAT HAPPENED (macro/chronicle.h, CANON S20.1). The world's own memory,
    // in two tiers: a ring the world is ASKED (indexed by cell — this is what
    // lets a witcher find a monster by the traces it left) and annals the
    // world REMEMBERS. Both ride the save whole: the annals are not a cache,
    // they are part of the world, and a legends mode will read exactly them
    // (owner, 2026-08-27).
    Chronicle chronicle;
    // The session feed (see SessionFeed above): presentation, NEVER saved.
    SessionFeed sessionFeed;
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
    // The ONE issuer of LANDMARK ids (v54): cities, villages, spires — every
    // named place draws from this counter at generation, so an id names ONE
    // place across all landmark kinds. 0 is reserved for "no landmark" (the
    // chronicle already files unknown subjects as 0), so issuance starts at 1.
    // Same monotonic-ordinal law as nextMacroSpawnOrdinal above: a hash or a
    // per-kind register is not an identity (CANON S20.1).
    std::uint32_t nextLandmarkOrdinal = 1;
    // The ONE issuer of QUEST ordinals (v63), same law again. Issued at
    // ACCEPT (QuestEngine::accept) — the moment an offer stops being a
    // seed-regenerated projection and becomes an object the world stores;
    // 0 is reserved for "an offer not yet accepted". The FNV hash of the
    // quest id string that used to ride events (quest_id_key) is dead.
    std::uint32_t nextQuestOrdinal = 1;
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

// ── The landmark-fact door: file the deed AND pay the fame ───────────────
//
// Where a place's standing lives, by the ONE landmark id space (v54). A
// spire has no standing (yet) and answers nullptr — its deeds are recorded,
// nothing is paid.
inline std::uint32_t* landmark_renown_slot(GameState& gs, int id) {
    if (id <= 0) return nullptr;
    for (auto& lm : gs.landmarks) if (lm.id == id) return &lm.renown;
    return nullptr;
}

// THE by-id find over the one landmark roster. Ids are world-unique (v54's
// single ordinal issuer), so no kind is needed to resolve one.
inline Landmark* landmark_by_id(GameState& gs, int id) {
    if (id < 0) return nullptr;
    for (auto& lm : gs.landmarks) if (lm.id == id) return &lm;
    return nullptr;
}
inline const Landmark* landmark_by_id(const GameState& gs, int id) {
    if (id < 0) return nullptr;
    for (const auto& lm : gs.landmarks) if (lm.id == id) return &lm;
    return nullptr;
}

// ONE action (S20.1: a writer that filed without paying would give a world
// where no place ever becomes somewhere; paying without filing, a legend
// nobody can read). The landmark twin of the app-side `record_deed`, and the
// same order: figure-ness is marked from the PRE-deed renown — «с этого дня
// её дела идут в анналы» — then the fact is filed, then the deed is paid
// (base + a tenth of what the OBJECT was worth: fame is made of fame for
// places exactly as for bands). Figure-ness itself is DERIVED, never stored
// (owner, 2026-08-28): a name is a word; historical weight is renown.
inline std::uint32_t record_landmark_fact(GameState& gs, FactKind kind,
                                          int landmarkId, int x, int y,
                                          int amount,
                                          int objectLandmarkId = 0) {
    std::uint32_t* subjSlot = landmark_renown_slot(gs, landmarkId);
    const std::uint32_t* objSlot =
        landmark_renown_slot(gs, objectLandmarkId);
    const std::uint32_t bar = std::uint32_t(renown_to_be_named());
    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(kind);
    f.subjectKind = fact_subject(FactSubject::Landmark,
                                 subjSlot && *subjSlot >= bar);
    f.subject = std::uint32_t(landmarkId < 0 ? 0 : landmarkId);
    if (objectLandmarkId > 0) {
        f.objectKind = fact_subject(FactSubject::Landmark,
                                    objSlot && *objSlot >= bar);
        f.object = std::uint32_t(objectLandmarkId);
    }
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = amount;
    const std::uint32_t seq = chronicle_record(gs.chronicle, f);
    if (seq != 0u && subjSlot) {
        *subjSlot += renown_for_deed(kind, objSlot ? *objSlot : 0u);
    }
    return seq;
}

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

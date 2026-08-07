// Macroworld NPC AI: behaviour dispatch for persistent macro NPCs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "ecs/world.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "macro/spawners.h"
#include "macro/tree_layer.h"

namespace sm {

// How often a macro NPC thinks, in WORLD TICKS (core/time.h) — not in wall
// seconds. On the map that is 32 ticks = half a real second, exactly what it
// always was. Underground it is half a second of WORLD time, which is eight
// real seconds, because the AI now rides the same clock as the economy, the
// calendar and the sun instead of keeping a timer of its own. A lord no longer
// crosses the continent while you clear one room, and the macro world costs
// kSubworldTickDivisor times less to simulate while you are down there.
inline constexpr std::uint32_t kAiTicks = 32;
static_assert(kTicksPerDay % kAiTicks == 0,
              "the AI cadence must divide the day");

// Real seconds one AI period lasts ON THE MACRO MAP — the only place these
// interpolated positions are ever seen. Presentation only; the AI itself never
// reads it.
inline constexpr float kAiPeriodSeconds =
    float(kAiTicks) / float(kTicksPerRealSecond);

// GAME HOURS one AI think covers — the exchange rate that lets a squad pay
// and recover through the same per-game-hour laws the player uses
// (kSpRegenPctPerHour, kMacroWalkCellsPerHour). 24 × 32 / 8192 = 0.09375 h:
// 256 thinks make the day, exactly.
inline constexpr float kAiTickGameHours =
    24.0f * float(kAiTicks) / float(kTicksPerDay);

struct TreeGrid {
    int cellSize = 32;
    int cols = 0;
    int rows = 0;
    std::vector<std::vector<std::uint32_t>> buckets;
    const std::vector<TreePoint>* trees = nullptr;
};

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize = 32);

// Transient spatial index of macro SQUADS (Session 15) — the sibling of
// TreeGrid above, rebuilt from the live registry at the top of every AI
// drive, NEVER stored per map cell and never serialized (owner constraint:
// no per-cell NPC arrays). It is what lets a squad SEE another squad: the
// threat step scans the neighbouring buckets instead of every entity.
struct SquadIndex {
    int cellSize = 8;
    int cols = 0;
    int rows = 0;
    std::vector<std::vector<entt::entity>> buckets;
};

void build_squad_index(SquadIndex& g, ecs::World& w, int mapW, int mapH,
                       int cellSize = 8);

struct MacroNpcAiRuntime {
    Rng           jitter{0xA1F0u};
    std::uint32_t sweepAccum = 0;   // world ticks toward the next sweep
    int         pendingSweeps = 0;
    std::size_t sweepCursor = 0;
    SquadIndex  squadIndex;         // rebuilt per drive; buckets reused
};

struct MacroNpcAiSliceResult {
    int  npcsProcessed = 0;
    int  sweepsCompleted = 0;
    bool backlog = false;
};

void reset_macro_npc_ai_runtime(MacroNpcAiRuntime& runtime, std::uint32_t seed);

struct TickContext {
    int             mapW;
    int             mapH;
    GameState*      gs;
    const TreeGrid* treeGrid;
    Rng*            rng;
    float           playerX;
    float           playerY;
    // Squad↔squad perception (Session 15). `world` + `squads` let a behaviour
    // see the OTHER squads (the one thing no behaviour could do before);
    // `allowAutoBattle` gates the meeting's resolution — the map path fights,
    // the underground drive only perceives, because squads standing in the
    // player's 3×3 window may have LIVE projected bodies whose fight belongs
    // to the ground, not to the resolver (default arguments below encode
    // exactly that split, so no call site changed).
    ecs::World*       world = nullptr;
    const SquadIndex* squads = nullptr;
    bool              allowAutoBattle = true;
    // The baked SP-weight grid (Session 21) — the SAME one the player's A*
    // walks (app.pathCost). try_move prices and paces every squad step from
    // it and steers around expensive ground; water refuses rest through its
    // flag. nullptr (tests, headless drivers without a world) reads as a
    // featureless free-road world: weight 1.0 everywhere, no water — the
    // greedy step then walks exactly the straight line it always walked.
    const PathCostData* pathCost = nullptr;
    // The LIVE tree layer (W2b): a woodcutter's chop goes through the one
    // set_tree_count door (overrides via gs->treeOverrides), so the wood he
    // hauls home really left the world. nullptr = agents work no honest
    // gathering (tests without a world stay as they were).
    TreeLayer* trees = nullptr;
    // The feature layer (W2b): a farmer works the nearest FT_Field of his
    // home. nullptr = peasants fall back to the home wander.
    const FeatureLayer* features = nullptr;
};

// Macro-view path: scans all macro NPCs each step and dispatches those whose
// per-NPC tick accumulator matured. `ticks` is world ticks elapsed.
void tick_macro_npc_ai(GameState& gs, ecs::World& w,
                       const TreeGrid* treeGrid,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks,
                       bool allowAutoBattle = true,
                       const PathCostData* pathCost = nullptr,
                       TreeLayer* trees = nullptr,
                       const FeatureLayer* features = nullptr);

// Smooth macro NPC render positions toward their logical cell positions.
// Mirrors TS `visualX/Y` interpolation and snaps long seam/teleport jumps.
void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt);

// Subworld path: queues one AI sweep per kAiTicks of WORLD time, then
// dispatches at most `max_npc_ticks` entities this step. Because the clock
// crawls underground, so do the sweeps — the macro world keeps thinking, at the
// pace of the day it is actually living through.
MacroNpcAiSliceResult tick_macro_npc_ai_budgeted(
    GameState& gs, ecs::World& w, const TreeGrid* treeGrid,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks,
    bool allowAutoBattle = false,
    const PathCostData* pathCost = nullptr,
    TreeLayer* trees = nullptr,
    const FeatureLayer* features = nullptr);

} // namespace sm

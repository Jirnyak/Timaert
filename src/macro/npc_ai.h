// Macroworld NPC AI: behaviour dispatch for persistent macro NPCs.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/rng.h"
#include "ecs/world.h"
#include "macro/state.h"
#include "macro/spawners.h"

namespace sm {

inline constexpr float kAiTickSec = 0.5f;

struct TreeGrid {
    int cellSize = 32;
    int cols = 0;
    int rows = 0;
    std::vector<std::vector<std::uint32_t>> buckets;
    const std::vector<TreePoint>* trees = nullptr;
};

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize = 32);

struct MacroNpcAiRuntime {
    Rng         jitter{0xA1F0u};
    float       sweepAccum = 0.0f;
    int         pendingSweeps = 0;
    std::size_t sweepCursor = 0;
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
};

// Macro-view path: scans all macro NPCs each frame and dispatches those whose
// per-NPC 0.5s accumulator matured.
void tick_macro_npc_ai(GameState& gs, ecs::World& w,
                       const TreeGrid* treeGrid,
                       MacroNpcAiRuntime& runtime, float dt);

// Smooth macro NPC render positions toward their logical cell positions.
// Mirrors TS `visualX/Y` interpolation and snaps long seam/teleport jumps.
void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt);

// Subworld path: queues 0.5s AI sweeps, then dispatches at most
// `max_npc_ticks` entities this frame.
MacroNpcAiSliceResult tick_macro_npc_ai_budgeted(
    GameState& gs, ecs::World& w, const TreeGrid* treeGrid,
    MacroNpcAiRuntime& runtime, float dt, int max_npc_ticks);

} // namespace sm

// Macroworld NPC AI — faithful port of `src/game/npc-ai.ts`.
//
// One free function per AI behaviour (selected via `AIBehaviour` on the
// NPC type registry). `tick_macro_npc_ai` advances every NPC carrying
// `Position + NPCKind + MacroNpcRuntime` once per `kTickSec` (0.5 s, same
// as the TS GameScreen tick).
//
// Tree lookups go through a simple hashed spatial grid so the woodcutter
// behaviour scales linearly with NPC count instead of #trees per tick.
#pragma once
#include <cstdint>
#include <vector>
#include "ecs/world.h"
#include "macro/state.h"
#include "macro/spawners.h"   // TreePoint

namespace sm {

inline constexpr float kAiTickSec = 0.5f;

// Hashed spatial grid over tree positions for O(1) nearest-tree query.
struct TreeGrid {
    int                     cellSize = 32;
    int                     cols = 0, rows = 0;
    // bucket[gy*cols + gx] = list of tree indices into `trees`.
    std::vector<std::vector<std::uint32_t>> buckets;
    const std::vector<TreePoint>*           trees = nullptr;
};

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize = 32);

// Per-tick context handed to every behaviour. Cheap POD pointers — the
// grid + tree list are owned by the app, the settlements vector by gs.
struct TickContext {
    int             mapW;
    int             mapH;
    GameState*      gs;
    const TreeGrid* treeGrid;       // optional; falls back to linear scan
    float           playerX;
    float           playerY;
};

// Public entry point — called once per frame from the main loop.
// Internally accumulates `dt` into a 0.5 s tick and runs all behaviours.
void tick_macro_npc_ai(GameState& gs, ecs::World& w,
                       const TreeGrid* treeGrid, float dt);

} // namespace sm
// Macroworld NPC AI tick — universal day-step movement for travellers
// (peasant wandering, merchant routes, caravan visiting cities, bandit
// roaming). Mirrors npc-ai.ts. Operates over Position + NPCKind, biased
// by NPCType.
#pragma once
#include <cstdint>
#include "ecs/world.h"
#include "macro/state.h"

namespace sm {

void tick_macro_npc_ai(GameState& gs, ecs::World& w, float dt);

} // namespace sm

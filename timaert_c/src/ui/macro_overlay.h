// Macro-map overlay — draws settlements, villages, NPCs and player on top
// of the macro shader using ImGui's background draw list. Lightweight
// debug/visual layer; not the final art pass but lets us SEE the world
// state we are simulating.
#pragma once

#include "macro/pathfinding.h"
#include <vector>

namespace sm {
struct GameState;
struct TerrainData;
struct FeatureLayer;
namespace ecs { struct World; }
}

namespace sm::ui {

// Persistent mouse-driven cursor state for the macro view.
//   * `hoverValid` / `hoverX` / `hoverY`  — current cell under the mouse.
//   * `requestPath`                       — set when the user left-clicks
//                                           a cell (consumed by main loop).
//   * `path` / `pathIdx`                  — auto-walk path the player is
//                                           currently following; advanced
//                                           in `step_macro_walk()`.
struct MacroCursor {
    bool hoverValid = false;
    int  hoverX = 0, hoverY = 0;
    bool requestPath = false;
    int  requestX = 0, requestY = 0;

    std::vector<PathPoint> path;
    std::size_t pathIdx = 0;
};

// Draw overlay markers + hover tooltip + click-to-travel polyline. Reads
// `terrain` / `features` so it can show biome / feature / landmark in the
// tooltip. Mutates `cursor` (hover, click request).
void draw_macro_overlay(GameState& gs, ecs::World& w,
                        const TerrainData& terrain,
                        const FeatureLayer& features,
                        MacroCursor& cursor,
                        float camX, float camY, float zoom,
                        int viewW, int viewH, int mapW, int mapH);

// Advance auto-walk: if `cursor.path` is non-empty, move `gs.player` toward
// the next cell at `cellsPerSec`. Pops the cell when reached.
void step_macro_walk(GameState& gs, MacroCursor& cursor, float dt,
                     float cellsPerSec);

// Right-edge stack of clickable badges for every NPC on the player's
// cell or any of the 8 adjacent cells (Chebyshev distance ≤ 1, with
// torus wrap). Mirrors `NpcProximityPanel.svelte`. No interaction
// hookup yet — clicks log a talk line via the EventBus.
class EventBus;
void draw_npc_proximity_panel(GameState& gs, ecs::World& w,
                              int viewW, int viewH);

} // namespace sm::ui

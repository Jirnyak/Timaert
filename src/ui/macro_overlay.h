// Macro-map overlay — draws settlements, villages, NPCs and player on top
// of the macro shader using ImGui's background draw list. Lightweight
// debug/visual layer; not the final art pass but lets us SEE the world
// state we are simulating.
#pragma once

#include "macro/pathfinding.h"
#include <entt/entity/entity.hpp>
#include <cstddef>
#include <vector>

namespace sm {
struct GameState;
struct TerrainData;
struct TreeLayer;
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
    int  hoverSettlementId = -1;
    int  clickedSettlementId = -1;

    std::vector<PathPoint> path;
    std::size_t pathIdx = 0;
};

struct NpcProximityResult {
    entt::entity attackNpc = entt::null;
};

using MacroWalkReachedFn = void (*)(void* user, int x, int y);

// Draw overlay markers + hover tooltip + click-to-travel polyline. Reads
// `terrain` / `features` so it can show biome / feature / landmark in the
// tooltip. Mutates `cursor` (hover, click request).
//
// `showMarkers` gates only the navigation CHROME — the hover-cell highlight,
// its tooltip, and the click-to-move path line — so the universal UI settings
// can hide them. Map content (settlements, NPCs, the player) and all cursor
// input (hover pick, click-to-move) are unaffected: hiding the chrome never
// disables map interaction.
//
// `showQuestMarkers` / `questMarkerScale` gate + size the universal world-marker
// pass — the floating quest "!" pins and any other entry in `gs.markers`; wired
// to the QuestMarkers UI-settings element. The pass is data-driven: it renders
// whatever producers have pushed into `gs.markers`, with no per-style code here.
void draw_macro_overlay(GameState& gs, ecs::World& w,
                        const TerrainData& terrain,
                        const FeatureLayer& features,
                        MacroCursor& cursor,
                        float camX, float camY, float zoom,
                        int viewW, int viewH, int mapW, int mapH,
                        bool showMarkers = true,
                        bool showQuestMarkers = true,
                        float questMarkerScale = 1.0f,
                        const TreeLayer* treeLayer = nullptr);

// Advance auto-walk: if `cursor.path` is non-empty, move `gs.player` toward
// the next cell at `cellsPerSec`. Calls `onReached` once per entered path cell.
std::size_t step_macro_walk(GameState& gs, MacroCursor& cursor, float dt,
                            float cellsPerSec,
                            MacroWalkReachedFn onReached = nullptr,
                            void* onReachedUser = nullptr);

// Right-edge stack of clickable badges for every NPC on the player's
// cell or any of the 8 adjacent cells (Chebyshev distance <= 1, with
// torus wrap). Mirrors `NpcProximityPanel.svelte`, with native Talk /
// Trade affordances kept in-panel until the full interaction overlay exists.
NpcProximityResult draw_npc_proximity_panel(GameState& gs, ecs::World& w,
                                            int viewW, int viewH,
                                            bool showRows = true,
                                            float scale = 1.0f);

// Test/runtime hook used by smoke scripts and non-mouse callers. It opens the
// same NPC trade popup as the proximity panel's Trade button.
void open_npc_trade_panel(entt::entity npc);
bool npc_proximity_popup_open();

} // namespace sm::ui

// ImGui overlays — Diplomacy, Settlement, Quest, Codex, Map. Each panel is
// a free function that draws into the current ImGui frame and is toggled by
// a boolean owned by the application. Mirrors svelte overlays in TS.
#pragma once
#include "macro/state.h"
#include "events/quests/quest_engine.h"

namespace sm {
struct TerrainData;
struct ZoneLayer;
class  EventBus;
namespace sub { class SeamlessSubworldManager; }
}

namespace sm::ui {

struct Toggles {
    bool diplomacy   = false;
    bool settlement  = false;
    bool quest       = false;
    bool codex       = false;
    bool map         = false;
    int  settlementId = -1;
};

void draw_diplomacy(GameState& gs, bool* open);
void draw_settlement(GameState& gs, int settlementId, bool* open);
void draw_quest_log(GameState& gs, const std::vector<Quest>& quests, bool* open);
void draw_codex(GameState& gs, bool* open);
void draw_map_overlay(GameState& gs, const TerrainData& terrain, bool* open);
void draw_encounter_modal(GameState& gs, EventBus& bus);

// Subworld minimap — circular HUD always-on (top-right) showing the local
// 3×3 cell tile composite around the player. Cheap: rebuilds a small RGBA
// texture only when the seamless centre changes or every ~2 s.
void draw_subworld_minimap_hud(const sub::SeamlessSubworldManager& mgr,
                               float playerX, float playerY,
                               float cameraYaw,
                               int viewportW, int viewportH);

// Subworld fullscreen map page (toggled by the macro M-key when the
// subworld is active). Shows the entire 3×3 composite + player marker.
void draw_subworld_map_overlay(const sub::SeamlessSubworldManager& mgr,
                               float playerX, float playerY,
                               float cameraYaw,
                               bool* open);

} // namespace sm::ui

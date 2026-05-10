// ImGui overlays — Diplomacy, Settlement, Quest, Codex, Map. Each panel is
// a free function that draws into the current ImGui frame and is toggled by
// a boolean owned by the application. Mirrors svelte overlays in TS.
#pragma once
#include "macro/state.h"
#include "events/quests/quest_engine.h"
#include <cstdint>

namespace sm {
struct TerrainData;
struct ZoneLayer;
class  EventBus;
namespace sub { class SeamlessSubworldManager; }
}

namespace sm::ui {

enum class CharacterPanelTab : std::uint8_t {
    Stats,
    Inventory,
    Army,
    Equipment,
    Spells,
};

enum class SettlementPanelTab : std::uint8_t {
    Trade,
    Garrison,
    Recruit,
    Inventory,
    History,
    Rest,
    Quests,
    Build,
};

struct Toggles {
    bool diplomacy   = false;
    bool settlement  = false;
    bool quest       = false;
    bool codex       = false;
    bool map         = false;
    bool character   = false;
    int  settlementId = -1;
    CharacterPanelTab characterTab = CharacterPanelTab::Stats;
    SettlementPanelTab settlementTab = SettlementPanelTab::Trade;
};

void draw_diplomacy(GameState& gs, bool* open);
void draw_character_panel(GameState& gs, bool* open, CharacterPanelTab* tab);
void draw_settlement(GameState& gs,
                     int settlementId,
                     const std::vector<Quest>& availableQuests,
                     std::vector<Quest>& activeQuests,
                     QuestEngine& questEngine,
                     EventBus& bus,
                     SettlementPanelTab* tab,
                     bool* open);
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

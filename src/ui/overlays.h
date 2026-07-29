// ImGui overlays — Diplomacy, Settlement, Quest, Codex, Map. Each panel is
// a free function that draws into the current ImGui frame and is toggled by
// a boolean owned by the application. Mirrors svelte overlays in TS.
#pragma once
#include "macro/state.h"
#include "events/quests/quest_engine.h"
#include <array>
#include <cstdint>
#include <cstddef>

namespace sm {
struct TerrainData;
struct ZoneLayer;
class  EventBus;
namespace sub { class SeamlessSubworldManager; struct MinimapBlip; }
namespace content { struct StoryDef; }
}

namespace sm::ui {

inline constexpr std::size_t kStoryOverlayMaxPhases = 8;
inline constexpr std::size_t kStoryOverlayMaxText = 64;

enum class CharacterPanelTab : std::uint8_t {
    Stats,
    Inventory,
    Army,
    Equipment,
    Spells,
};

enum class SettlementPanelTab : std::uint8_t {
    Info,
    Build,
    Trade,
    Garrison,
    Recruit,
    Map,
    Inventory,
    History,
    Rest,
    Quests,
};

struct Toggles {
    bool diplomacy   = false;
    bool settlement  = false;
    bool quest       = false;
    bool codex       = false;
    bool map         = false;
    bool character   = false;
    bool settings    = false;   // universal Interface (UI show/hide + size) panel
    int  settlementId = -1;
    int  questSelection = 0;
    CharacterPanelTab characterTab = CharacterPanelTab::Stats;
    SettlementPanelTab settlementTab = SettlementPanelTab::Info;
};

struct StoryOverlayState {
    bool open = false;
    const content::StoryDef* story = nullptr;
    std::size_t phaseIndex = 0;
    std::size_t slideIndex = 0;
    bool inputPrepared = false;
    std::array<int, kStoryOverlayMaxPhases> selectedChoice{};
    std::array<std::array<char, kStoryOverlayMaxText>, kStoryOverlayMaxPhases> values{};
    std::array<bool, kStoryOverlayMaxPhases> hasValue{};
};

struct DialogOverlayState {
    bool hasResult = false;
    bool hasNodeActivation = false;
    std::array<char, 96> resultMessage{};
    std::array<char, 64> nodeId{};
};

void draw_diplomacy(GameState& gs, bool* open, float scale = 1.0f);
void draw_character_panel(GameState& gs, bool* open, CharacterPanelTab* tab, float scale = 1.0f);
void draw_settlement(GameState& gs,
                     int settlementId,
                     const std::vector<Quest>& availableQuests,
                     std::vector<Quest>& activeQuests,
                     QuestEngine& questEngine,
                     EventBus& bus,
                     SettlementPanelTab* tab,
                     bool* open,
                     float scale = 1.0f);
void draw_quest_log(GameState& gs,
                    std::vector<Quest>& quests,
                    QuestEngine& questEngine,
                    EventBus& bus,
                    int* selectedQuestIndex,
                    bool* open,
                    float scale = 1.0f);
void draw_codex(GameState& gs, bool* open, float scale = 1.0f);
void draw_show_dialog(GameState& gs,
                      const GameEvent& dialog,
                      EventBus& bus,
                      DialogOverlayState& state,
                      bool* open);
void open_story_overlay(StoryOverlayState& state, const content::StoryDef& story);
bool story_overlay_active(const StoryOverlayState& state);
bool set_story_overlay_value(StoryOverlayState& state,
                             const char* phaseId,
                             const char* value);
bool complete_story_overlay(StoryOverlayState& state, EventBus& bus);
void draw_story_overlay(StoryOverlayState& state, EventBus& bus);
void draw_map_overlay(GameState& gs, const TerrainData& terrain, bool* open, float scale = 1.0f);
void draw_encounter_modal(GameState& gs, EventBus& bus);

// Subworld minimap — circular HUD always-on (top-right) showing the local
// 3×3 cell tile composite around the player. Cheap: rebuilds a small RGBA
// texture only when the seamless centre changes or every ~2 s. `blips` are the
// live NPC/monster markers (see SubworldEngine::collect_minimap_blips); each is
// drawn as a dot coloured by its PlayerRelation. Pass a null/empty span to draw
// terrain + player only.
void draw_subworld_minimap_hud(const sub::SeamlessSubworldManager& mgr,
                               float playerX, float playerY,
                               float cameraYaw,
                               int viewportW, int viewportH,
                               const sub::MinimapBlip* blips,
                               std::size_t blipCount,
                               float scale, float topInset);

// Subworld fullscreen map page (toggled by the macro M-key when the
// subworld is active). Shows the entire 3×3 composite + player marker.
void draw_subworld_map_overlay(const sub::SeamlessSubworldManager& mgr,
                               float playerX, float playerY,
                               float cameraYaw,
                               bool* open,
                               float scale = 1.0f);

} // namespace sm::ui

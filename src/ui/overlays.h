// ImGui overlays — Settlement, Quest, Codex, Map. Each panel is
// a free function that draws into the current ImGui frame and is toggled by
// a boolean owned by the application. Mirrors svelte overlays in TS.
#pragma once
#include "macro/state.h"
#include "macro/store.h"   // MacroHandle — субъект-сквад после шага 1г
#include "events/quests/quest_engine.h"
#include <array>
#include <cstdint>
#include <cstddef>

namespace sm {
struct TerrainData;
struct ZoneLayer;
struct MacroWorld;
class  EventBus;
namespace sub { class SeamlessSubworldManager; struct MinimapBlip; struct SubObjects; }
namespace content { struct StoryDef; }
}

namespace sm::ui {

enum class CharacterPanelTab : std::uint8_t {
    Stats,
    Inventory,
    Army,
    Equipment,
    Spells,
    // The bench of the reversible reaction (CANON «Крафт/Скрап»): craft is a
    // manual act over the player's own bag, so it lives IN the character
    // panel — a tab, never a parallel window (the menu-session lesson).
    Craft,
};

// (Рода `Build` и `Map` УНИЧТОЖЕНЫ 2026-10-19, M-290, вердиктами владельца
//  «Build снести» и «карта города это фигня». Первый не строил ничего и сам
//  это признавал надписью; второй рисовал превью ПЛАНИРОВКИ города —
//  локальную карту, не карту мира, — и тащил на кадр панели прогон
//  генератора субмира. Карта мира осталась своим экраном, `map_screen.cpp`:
//  это ОТДЕЛЬНЫЙ модуль, с миникартой субмира не связанный (владелец
//  2026-10-19: «в макромире карта другой совсем модуль они не связаны»).)
enum class SettlementPanelTab : std::uint8_t {
    Info,
    Trade,
    Garrison,
    Recruit,
    Inventory,
    Quests,
};

struct Toggles {
    bool settlement  = false;
    bool quest       = false;
    bool codex       = false;
    bool map         = false;
    bool character   = false;
    bool settings    = false;   // universal Interface (UI show/hide + size) panel
    bool controls    = false;   // universal Controls (key rebinding) panel
    // (ПОЛЕ `settlementId` УНИЧТОЖЕНО 2026-10-19, M-290. Оно было ВТОРЫМ
    //  носителем субъекта панели — ординалом места РЯДОМ с хэндлом
    //  `App::subjectSquad`, — и держала их пара ветвей внутри самой панели:
    //  клик по ряду обязан был выбрать ветвь. Ветвей нет, носитель один,
    //  хэндл. «В каком поселении игрок СТОИТ» — другой вопрос и другое поле,
    //  `GameState::subState.settlementId`: это состояние мира, а не выбор
    //  окна, и оно живо.)
    int  questSelection = 0;
    CharacterPanelTab characterTab = CharacterPanelTab::Stats;
    SettlementPanelTab settlementTab = SettlementPanelTab::Info;
};

// A story on screen is a run of slides and a finger in it — nothing else.
// The choice/input machinery that used to live here died with the story
// phases: the creation screen owns the asking (content/plot/intro.h).
struct StoryOverlayState {
    bool open = false;
    const content::StoryDef* story = nullptr;
    std::size_t slideIndex = 0;
};

struct DialogOverlayState {
    bool hasResult = false;
    bool hasNodeActivation = false;
    std::array<char, 96> resultMessage{};
    std::array<char, 64> nodeId{};
};

// `st` is here because the player's СУЩЕСТВ КОНТЕЙНЕРА is an ordinary squad
// slot of the MacroStore now (macro/player_entity.h), not a field of
// PlayerState; `sceneObjs` (nullable — no scene on the macro map) carries the
// avatar's Combat gate for the Use-potion ability charge. The UI sits above
// every layer and may read from them; it still owns no game logic.
void draw_character_panel(GameState& gs, MacroStore& st,
                          sub::SubObjects* sceneObjs, bool* open,
                          CharacterPanelTab* tab, float scale = 1.0f);
// THE subject panel (меню-сессия, фидбек владельца 2026-09-11: «сделай
// единую систему... меню города хороший пример, его обобщить»). ONE window,
// one pause law, one tab bar — И ОДИН ПРОХОД: субъект назван ОДНИМ хэндлом,
// ветвей по его роду внутри нет ни одной (M-290). Чем город отличается от
// ходока — это КОЛОНКИ: цифры баннера берёт `figuresFromPools`
// (`ui/squad_draw.h`), глаголы и вкладки — маска `actions_of`
// (`macro/squad.h`), имя и подпись — цепочка «своё имя → ярлык строки рода →
// ярлык строки существа». Attack сообщается через `attackRequest`;
// приложение ведёт нападающего на клетку защитника (закон передачи) и
// открывает встречу.
//
// Конверта мира в сигнатуре НЕТ: его просила одна вкладка (`Map` с превью
// планировки, снесена 2026-10-19), а панель читает колонки своего слота.
void draw_settlement(GameState& gs,
                     MacroStore& st,
                     MacroHandle subject,
                     const std::vector<Quest>& availableQuests,
                     std::vector<Quest>& activeQuests,
                     QuestEngine& questEngine,
                     EventBus& bus,
                     SettlementPanelTab* tab,
                     bool* open,
                     MacroHandle* attackRequest,
                     float scale = 1.0f);
// `closeKeyName` is the LIVE name of the key that toggles this panel
// (SDL_GetScancodeName of the Quests binding) — the Close button quotes it,
// never a literal, so a rebind can't strand a stale hint (CANON S22).
// nullptr/empty = the action is unbound, the button says just "Close".
void draw_quest_log(GameState& gs,
                    std::vector<Quest>& quests,
                    QuestEngine& questEngine,
                    EventBus& bus,
                    int* selectedQuestIndex,
                    bool* open,
                    float scale = 1.0f,
                    const char* closeKeyName = nullptr);
void draw_codex(GameState& gs, bool* open, float scale = 1.0f);
void draw_show_dialog(GameState& gs,
                      const Inventory* bag,
                      const GameEvent& dialog,
                      EventBus& bus,
                      DialogOverlayState& state,
                      bool* open);
void open_story_overlay(StoryOverlayState& state, const content::StoryDef& story);
bool story_overlay_active(const StoryOverlayState& state);
void draw_story_overlay(StoryOverlayState& state, EventBus& bus);
// (The macro "World Map" minimap window is GONE — the M toggle now opens the
// full-screen map page, ui/map_screen.h: the shipping macro shader under a
// second camera. Subworld map overlays below are untouched.)

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

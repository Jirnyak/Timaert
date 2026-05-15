// Application screen state machine (title / playing / paused / dead).
// Mirrors the Svelte top-level routing: TitleScreen → GameScreen with
// PauseOverlay / DeathOverlay. Drawn via ImGui as full-window centred
// panels — no separate render target, no extra GL state.
#pragma once

#include "macro/map_generator.h"   // LayerParameters
#include "macro/save.h"            // SaveSummary

namespace sm {
struct GameState;
}

namespace sm::ui {

enum class AppState : int {
    Title       = 0,
    Playing     = 1,
    Paused      = 2,
    Dead        = 3,
    CustomNewGame = 4,
    Load        = 5,
};

// World-generation parameters editable from the Custom New Game screen.
// Pure POD: extend by adding a field here and one row in `kCustomParamSpec`
// in screens.cpp — the slider appears automatically. Map size is a power
// of 2 in [128, 4096]; everything else is a forwarded `LayerParameters`
// field. `seed` of 0 means "fresh from clock".
struct CustomGameParams {
    int             mapSizeLog2     = 10;     // 2^10 = 1024
    std::uint32_t   seed            = 0;
    int             cityCountTarget = 100;    // 0 = use registry defaults
    LayerParameters layer{};
};

struct ShellResult {
    bool startNewGame       = false;   // title → playing (default world)
    bool openCustomNewGame  = false;   // title → CustomNewGame
    bool startCustomNewGame = false;   // CustomNewGame → playing
    bool cancelCustomNewGame= false;   // CustomNewGame → title
    bool regenerateCustom   = false;   // CustomNewGame: rebuild preview
    bool loadGame           = false;   // title/pause -> Load, Load -> playing
    bool cancelLoad         = false;   // Load -> previous shell state
    bool openCodex          = false;   // pause -> playing with Codex overlay
    bool saveGame           = false;   // pause → save & stay paused
    bool resume             = false;   // pause → playing
    bool returnToTitle      = false;   // pause → title (drops world)
    bool quit               = false;   // any → exit app
};

// Title menu: New Game / Custom New Game / Load Game / Quit.
ShellResult draw_title_menu(int viewportW, int viewportH);

// Custom new game screen: a wide two-column window. Left column is a
// scrollable parameter editor (map size, seed, city target, all
// LayerParameters). Right column is a live preview of the generated
// world (caller renders the world into `previewTex` then passes its GL
// id here). `worldReady` gates the Start button so the user can't enter
// a half-built world. Adding a new param = one row in `kCustomParamSpec`
// in screens.cpp — the slider appears automatically.
ShellResult draw_custom_new_game(CustomGameParams& params,
                                 unsigned previewTex,
                                 int previewW, int previewH,
                                 bool worldReady,
                                 int viewportW, int viewportH);

// Single-slot load screen. Shows save header/status and exposes Load / Back.
ShellResult draw_load_screen(const SaveSummary& save,
                             int viewportW, int viewportH);

// Pause menu: Resume / Save / Load / Title / Quit.
ShellResult draw_pause_menu(int viewportW, int viewportH);

// Death overlay: brief epitaph + Return to Title / Quit.
ShellResult draw_death_screen(const GameState& gs, int viewportW, int viewportH);

// Top-left player HUD: HP / MP / SP bars, gold, day/time, level, position.
void draw_player_hud(const GameState& gs);

// Proto_c-style bottom command toolbar — visual buttons that emit
// semantic intents the app loop translates into actions.
struct ToolbarResult {
    bool pause = false, speed1 = false, speed4 = false, rest = false;
    bool inventory = false, map = false, build = false, quests = false;
    bool party = false, equipment = false, codex = false, diplomacy = false;
    bool toggleSubworld = false;
    bool zoomIn = false, zoomOut = false;
};
ToolbarResult draw_bottom_toolbar(const GameState& gs, bool subworldActive);

// Bottom-centre hint bar: shows the current key bindings for context.
void draw_hint_bar(AppState state, bool subworldActive, int viewportW, int viewportH);

} // namespace sm::ui

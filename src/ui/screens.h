// Application screen state machine (title / playing / menu / dead).
// Mirrors the Svelte top-level routing: TitleScreen → GameScreen with
// MenuOverlay / DeathOverlay. Drawn via ImGui as full-window centred
// panels — no separate render target, no extra GL state.
//
// NOTE the split, because it used to be one word for two things: this state
// machine owns SCREENS, and `Menu` is the Esc screen. Pausing the world is not
// a screen — it is one flag on the app (Space, or the toolbar's II) that you
// keep playing in front of.
#pragma once

#include "imgui.h"
#include <cstdint>

#include "macro/character_sheet.h" // CharacterSheet (the creation screen authors one)
#include "macro/map_generator.h"   // LayerParameters
#include "macro/save.h"            // SaveSummary

namespace sm {
struct GameState;
}

namespace sm::ui {

class Keymap;

// Height (logical px) of the persistent top status strip drawn by
// draw_player_hud(). The single source of truth for that bar's extent, so
// other HUD elements (e.g. the subworld minimap) can sit clear of it without
// re-guessing the number. Change it here and every consumer follows.
inline constexpr float kTopStatusBarHeight = 36.0f;

enum class AppState : int {
    Title       = 0,
    Playing     = 1,
    Menu        = 2,   // the Esc screen (Resume / Save / Load / Title / Quit)
    Dead        = 3,
    CustomNewGame = 4,
    Load        = 5,
    // The pre-world character creation screen (owner verdict, 2026-09-03):
    // New Game asks WHO before it generates WHERE. Name, sex, homeland,
    // attribute points and learn picks are authored here through the sheet's
    // own doors; the world boots only on Start.
    CharacterCreation = 6,
    // The pre-world intro slideshow (owner verdict, 2026-09-04): the nine
    // authored slides play BETWEEN the menu and character creation, on no
    // engine at all — fullscreen frame, typewriter caption, Esc to skip.
    // The world's own opening is the single arrival slide, in-world.
    IntroSlides = 7,
    // The studio splash (owner verdict, 2026-09-04): the very first screen.
    // A swarm of blood-dark pixels assembles into the pixel sword and the
    // TENEVIK GAMES letters — procedural assembly IS the studio's face; the
    // cursor can shove the particles (the Lionhead nod), the spring wins.
    Splash = 8,
};

// (The two cinematic pre-world screens — the studio splash and the intro
// slideshow — and their state live in ui/intro_screens.h. They answer with
// the same ShellResult, and depend on nothing in the game.)

// Everything the creation screen authors, gathered so the app can apply it in
// one place after the world boots. `sheet` is edited through the REAL doors
// (spend_attribute_point / spend_learn_pick), so the screen can never grant
// what the game would refuse; the two refunds (a creation-only right) are the
// screen's own.
struct CreationState {
    char name[25]   = "Traveller";   // the intro input's own cap (24 + NUL)
    int  sexIdx     = -1;            // index into creation_sex_choices; -1 = unpicked
    int  realmIdx   = -1;            // index into creation_realm_choices; -1 = unpicked
    CharacterSheet sheet{};          // default pools: 5 attr points, 5 learn picks
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
    bool splashDone         = false;   // Splash → Title (input or timeout)
    bool startNewGame       = false;   // title → IntroSlides (default world)
    bool introFinished      = false;   // IntroSlides → CharacterCreation (last slide or Esc)
    bool creationDefault    = false;   // CharacterCreation: fill the default preset
    bool startCreatedGame   = false;   // CharacterCreation → playing (boot + apply)
    bool cancelCreation     = false;   // CharacterCreation → back where it came from
    bool openCustomNewGame  = false;   // title → CustomNewGame
    bool startCustomNewGame = false;   // CustomNewGame → playing
    bool cancelCustomNewGame= false;   // CustomNewGame → title
    bool regenerateCustom   = false;   // CustomNewGame: rebuild preview
    bool loadGame           = false;   // title/menu -> Load, Load -> playing
    bool loadAutosave       = false;   // Load -> playing, from the autosave slot
    bool cancelLoad         = false;   // Load -> previous shell state
    bool openCodex          = false;   // menu -> playing with Codex overlay
    bool openInterface      = false;   // menu -> playing with Interface panel
    bool openControls       = false;   // menu -> playing with Controls (keymap) panel
    bool saveGame           = false;   // menu → save & stay in the menu
    bool resume             = false;   // menu → playing
    bool returnToTitle      = false;   // menu → title (drops world)
    bool quit               = false;   // any → exit app
};

// Every screen below reads its geometry from ImGui::GetIO().DisplaySize
// itself (logical points — the only honest unit under HiDPI), so none of
// them takes a viewport size: the parameters everyone passed were drawable
// pixels and every body ignored them.

// Title menu: New Game / Custom New Game / Load Game / Quit.
ShellResult draw_title_menu();

// The character creation screen (pre-world). Renders the authored choice
// tables (content/plot/intro.h creation_*_choices) plus the sheet's own
// registries; Start stays disabled until name, sex and homeland are given.
ShellResult draw_character_creation(CreationState& cs);

// THE default hero — one definition behind the creation screen's Default
// button AND the new_game smoke, so the sheet a test boots with is exactly
// the sheet a player gets from one click. Deterministic, spent through the
// same doors the screen uses; Start still belongs to the caller.
void creation_apply_default(CreationState& cs);

// Custom new game screen: a wide two-column window. Left column is a
// scrollable parameter editor (map size, seed, city target, all
// LayerParameters). Right column is a live preview of the generated
// world (caller renders the world into `previewTex` then passes its GL
// id here). `worldReady` gates the Start button so the user can't enter
// a half-built world. Adding a new param = one row in `kCustomParamSpec`
// in screens.cpp — the slider appears automatically.
ShellResult draw_custom_new_game(CustomGameParams& params,
                                 ImTextureID previewTex,
                                 int previewW, int previewH,
                                 bool worldReady);

// Two-slot load screen: the manual save and the monthly autosave, each with
// its own header/status and Load button, plus Back.
ShellResult draw_load_screen(const SaveSummary& save,
                             const SaveSummary& autosave);

// Game menu [Esc]: Resume / Save / Load / Title / Quit. This is the MENU, not
// the pause — pausing the world is one flag toggled by Space and the toolbar's
// II, and it is a state you keep playing in. Opening this screen stops the
// world too, but only because nothing ticks outside AppState::Playing.
ShellResult draw_game_menu();

// Death overlay: brief epitaph + Return to Title / Quit.
ShellResult draw_death_screen(const GameState& gs);

// Top-left player HUD: HP / MP / SP bars, gold, day/time, level, position.
void draw_player_hud(const GameState& gs, float scale = 1.0f);

// Proto_c-style bottom command toolbar — visual buttons that emit
// semantic intents the app loop translates into actions.
struct ToolbarResult {
    // `pause` is THE pause — the same world-stop the Space key toggles, never a
    // second one. `menu` opens the Esc screen.
    bool pause = false, resume = false, menu = false, speed4 = false, rest = false;
    bool stats = false, inventory = false, map = false, build = false, quests = false;
    bool party = false, equipment = false, codex = false, diplomacy = false;
    bool toggleSubworld = false;
    bool zoomIn = false, zoomOut = false;
};
// `km` feeds the tooltips: a button quotes the key that CURRENTLY triggers it,
// straight from the live keymap, so a rebind updates every tooltip the same
// frame and no hint can go stale.
ToolbarResult draw_bottom_toolbar(const GameState& gs, bool subworldActive,
                                  const Keymap& km, float scale = 1.0f);

} // namespace sm::ui

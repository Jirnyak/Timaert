// Universal rebindable keymap — ONE registry governing every game key, across
// BOTH the macroworld and the microworld. The control-scheme twin of
// ui_settings.h: a single table (kActionSpec) drives one Controls panel, one
// persisted prefs file, and the per-action binding everything else reads.
// There is deliberately no per-world input code to keep in lockstep — a
// binding IS a row here, and the worlds differ only by each row's scope.
//
// Bindings are SCANCODES, not keysyms: positional, so a layout change (or a
// non-QWERTY keyboard) never scatters the controls. Esc is the one fixed key —
// it opens the menu and cancels a rebind, so the player can never lock
// themselves out of the way back.
//
// To add an action: add an ActionId enumerator here and one row in kActionSpec
// (keymap.cpp), in the SAME order. It then appears in the Controls panel,
// persists, and can be read at its call-site — with no other code.
#pragma once

#include "ui/ui_settings.h"   // UiScope — one scope vocabulary for panels AND keys

#include <SDL_scancode.h>

#include <cstddef>
#include <string>

namespace sm::ui {

// One enumerator per bindable action. Order == display order within a scope.
// `Count` must stay last; a static_assert on the row count guards drift.
enum class ActionId : std::uint8_t {
    // Both worlds
    Character,
    ArmyTab,
    SpellsTab,
    Codex,
    Map,
    Quests,
    Diplomacy,
    Settlement,
    EnterLeave,
    Save,
    Load,
    DebugOverlay,
    // Macroworld
    Pause,
    Rest,
    EquipmentTab,
    PanUp,
    PanDown,
    PanLeft,
    PanRight,
    // Microworld
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Attack,
    CastSpell,
    Jump,
    Interact,
    Possess,
    Count
};

inline constexpr std::size_t kActionCount =
    static_cast<std::size_t>(ActionId::Count);

// Static descriptor for one action. `key` is the STABLE identifier written to
// the prefs file (order-independent, so reordering the enum never corrupts an
// existing file); `label` is user-facing. `scope` says which world listens —
// the same key may serve a Macro action and a Sub action at once (E is the
// equipment sheet above ground and the interact hand below), because the two
// worlds are never active together.
struct ActionSpec {
    ActionId     id;
    const char*  key;
    const char*  label;
    UiScope      scope;
    SDL_Scancode def;
};

// THE registry (defined in keymap.cpp) — one row per action.
extern const ActionSpec kActionSpec[kActionCount];

// Spec row for an id (index into kActionSpec == enumerator value).
const ActionSpec& action_spec(ActionId id);

// true when `scope` listens in the current world.
bool scope_active(UiScope scope, bool subworldActive);

// The single source of truth for key bindings. The constructor seeds every
// entry from kActionSpec, so a fresh install (no prefs file yet) is already
// correct without any extra logic.
class Keymap {
public:
    Keymap();

    SDL_Scancode get(ActionId id) const { return binds_[idx(id)]; }

    // Bind `sc` to `id`, and UNBIND it from any other action whose scope
    // shares a world with `id` — one key, one meaning per world. The robbed
    // action is left unbound (shown as "—" in the panel), never silently
    // duplicated.
    void set(ActionId id, SDL_Scancode sc);

    void reset_defaults();

    // The Controls panel's "press a key now" latch: the ActionId being
    // rebound, or -1. Armed by the panel, consumed by the app's event loop
    // (the SDL event is the only honest source of a scancode).
    int pendingRebind = -1;

private:
    static std::size_t idx(ActionId id) { return static_cast<std::size_t>(id); }
    SDL_Scancode binds_[kActionCount];
};

// Persistence — the same tiny, forgiving text KV file idiom as ui_settings
// ("<key> <scancode int>" per line). A missing file or unknown/absent keys are
// non-fatal: defaults are kept. Adding or removing an action never invalidates
// an existing file.
bool load_keymap(Keymap& m, const std::string& path);
bool save_keymap(const Keymap& m, const std::string& path);

// The one Controls panel (menu → Controls), shared by both worlds. Returns
// true if the user changed anything this frame (so the caller can flush the
// prefs file).
bool draw_keymap_panel(Keymap& m, bool* open);

} // namespace sm::ui

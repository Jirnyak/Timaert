// Universal in-game UI settings — ONE registry governing every toggleable and
// resizable UI element, across BOTH the macroworld and the microworld. There is
// deliberately no per-world or per-widget settings code: a single table
// (kUiElementSpec) drives one settings panel, one persisted prefs file, and the
// per-element visibility/scale everything else reads.
//
// Mirrors the "spec table -> auto UI" idiom of kCustomParamSpec (screens.cpp):
// to add a new element, add one UiElementId enumerator here and one row in
// kUiElementSpec (ui_settings.cpp). It then appears in the settings panel,
// persists, and can be gated/scaled at its draw call-site — with no other code.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sm::ui {

// Which world an element belongs to — used only to GROUP rows in the settings
// panel. The visibility flag itself is honoured at every call-site the element
// has, regardless of scope.
enum class UiScope : std::uint8_t { Both, Macro, Sub };

// One enumerator per registered UI element. Order == display order within a
// scope. `Count` must stay last. Keep in lockstep with kUiElementSpec — a
// static_assert on the row count guards against drift.
enum class UiElementId : std::uint8_t {
    PlayerHud,
    BottomToolbar,
    PanelCharacter,
    PanelQuestLog,
    PanelCodex,
    PanelMap,
    MacroOverlay,
    QuestMarkers,
    NpcProximity,
    PanelDiplomacy,
    PanelSettlement,
    SubMinimap,
    SubCombatLog,
    SubDangerGem,
    SubCrosshair,
    Count
};

inline constexpr std::size_t kUiElementCount =
    static_cast<std::size_t>(UiElementId::Count);

// Static descriptor for one element. `key` is the STABLE identifier written to
// the prefs file (order-independent, so reordering the enum never corrupts an
// existing file); `label`/`tip` are user-facing.
struct UiElementSpec {
    UiElementId id;
    const char* key;
    const char* label;
    UiScope     scope;
    bool        defaultVisible;
    bool        scalable;       // false -> no size slider (e.g. a world-space overlay)
    float       scaleMin;
    float       scaleMax;
    float       scaleDefault;
    const char* tip;
};

// THE registry (defined in ui_settings.cpp) — one row per element.
extern const UiElementSpec kUiElementSpec[kUiElementCount];

// Spec row for an id (index into kUiElementSpec == enumerator value).
const UiElementSpec& ui_spec(UiElementId id);

// Per-element mutable user state.
struct UiElementState {
    bool  visible;
    float scale;
};

// The single source of truth for UI preferences. The constructor seeds every
// entry from kUiElementSpec, so a fresh install (no prefs file yet) is already
// correct without any extra logic.
class UiSettings {
public:
    UiSettings();

    bool  visible(UiElementId id) const { return state_[idx(id)].visible; }
    float scale(UiElementId id)   const { return state_[idx(id)].scale; }

    UiElementState&       mut(UiElementId id)       { return state_[idx(id)]; }
    const UiElementState& get(UiElementId id) const { return state_[idx(id)]; }

    void reset_defaults();

private:
    static std::size_t idx(UiElementId id) { return static_cast<std::size_t>(id); }
    std::array<UiElementState, kUiElementCount> state_{};
};

// Persistence — a tiny, forgiving text KV file (one line per element:
// "<key> <visible 0|1> <scale>"). A missing file or unknown/absent keys are
// non-fatal: defaults are kept. This tolerance is deliberate — adding or
// removing a UI element never invalidates an existing prefs file. load returns
// true if a file was found and read; save returns false only on write failure.
bool load_ui_settings(UiSettings& s, const std::string& path);
bool save_ui_settings(const UiSettings& s, const std::string& path);

// The one settings panel, shared by both worlds. Returns true if the user
// changed anything this frame (so the caller can mark the prefs dirty and flush
// on close).
bool draw_ui_settings_panel(UiSettings& s, bool* open);

} // namespace sm::ui

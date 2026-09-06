#include "ui/keymap.h"

#include "imgui.h"

#include <SDL_keyboard.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace sm::ui {

// ── The registry ─────────────────────────────────────────────────────────────
// Add an action: add an ActionId enumerator (keymap.h) and one row here, in the
// SAME order. It then appears in the Controls panel, persists, and can be read
// at its call-site — with no other code.
const ActionSpec kActionSpec[kActionCount] = {
    // id                       key              label                  scope          default
    {ActionId::Character,    "act.character",  "Character / inventory", UiScope::Both, SDL_SCANCODE_I},
    {ActionId::ArmyTab,      "act.army",       "Party / army",          UiScope::Both, SDL_SCANCODE_P},
    {ActionId::SpellsTab,    "act.spells",     "Spellbook",             UiScope::Both, SDL_SCANCODE_B},
    {ActionId::Codex,        "act.codex",      "Codex",                 UiScope::Both, SDL_SCANCODE_C},
    {ActionId::Map,          "act.map",        "Map",                   UiScope::Both, SDL_SCANCODE_M},
    {ActionId::Quests,       "act.quests",     "Quest log",             UiScope::Both, SDL_SCANCODE_Q},
    {ActionId::Diplomacy,    "act.diplomacy",  "Diplomacy",             UiScope::Both, SDL_SCANCODE_K},
    {ActionId::Settlement,   "act.settlement", "Settlement",            UiScope::Both, SDL_SCANCODE_T},
    {ActionId::EnterLeave,   "act.enterleave", "Enter / leave cell",    UiScope::Both, SDL_SCANCODE_RETURN},
    {ActionId::Save,         "act.save",       "Quick save",            UiScope::Both, SDL_SCANCODE_F5},
    {ActionId::Load,         "act.load",       "Load screen",           UiScope::Both, SDL_SCANCODE_F9},
    {ActionId::DebugOverlay, "act.debug",      "Debug overlay",         UiScope::Both, SDL_SCANCODE_F3},
    {ActionId::Pause,        "act.pause",      "Pause",                 UiScope::Macro, SDL_SCANCODE_SPACE},
    {ActionId::Rest,         "act.rest",       "Rest",                  UiScope::Macro, SDL_SCANCODE_Z},
    {ActionId::EquipmentTab, "act.equipment",  "Equipment",             UiScope::Macro, SDL_SCANCODE_E},
    {ActionId::PanUp,        "act.panup",      "Pan camera up",         UiScope::Macro, SDL_SCANCODE_W},
    {ActionId::PanDown,      "act.pandown",    "Pan camera down",       UiScope::Macro, SDL_SCANCODE_S},
    {ActionId::PanLeft,      "act.panleft",    "Pan camera left",       UiScope::Macro, SDL_SCANCODE_A},
    {ActionId::PanRight,     "act.panright",   "Pan camera right",      UiScope::Macro, SDL_SCANCODE_D},
    {ActionId::MoveForward,  "act.forward",    "Move forward",          UiScope::Sub,  SDL_SCANCODE_UP},
    {ActionId::MoveBack,     "act.back",       "Move back",             UiScope::Sub,  SDL_SCANCODE_DOWN},
    {ActionId::MoveLeft,     "act.left",       "Move left",             UiScope::Sub,  SDL_SCANCODE_LEFT},
    {ActionId::MoveRight,    "act.right",      "Move right",            UiScope::Sub,  SDL_SCANCODE_RIGHT},
    {ActionId::Attack,       "act.attack",     "Attack (also LMB)",     UiScope::Sub,  SDL_SCANCODE_A},
    {ActionId::CastSpell,    "act.cast",       "Cast active spell",     UiScope::Sub,  SDL_SCANCODE_S},
    {ActionId::Jump,         "act.jump",       "Jump",                  UiScope::Sub,  SDL_SCANCODE_SPACE},
    {ActionId::Interact,     "act.interact",   "Interact",              UiScope::Sub,  SDL_SCANCODE_E},
};

static_assert(sizeof(kActionSpec) / sizeof(kActionSpec[0]) == kActionCount,
              "kActionSpec must have exactly one row per ActionId");

const ActionSpec& action_spec(ActionId id) {
    const std::size_t i = static_cast<std::size_t>(id);
    assert(i < kActionCount && kActionSpec[i].id == id &&
           "kActionSpec rows must be in ActionId order");
    return kActionSpec[i];
}

bool scope_active(UiScope scope, bool subworldActive) {
    return scope == UiScope::Both ||
           scope == (subworldActive ? UiScope::Sub : UiScope::Macro);
}

// Bitmask of the worlds a scope listens in — the overlap test behind the
// one-key-one-meaning rule in Keymap::set.
static unsigned scope_worlds(UiScope scope) {
    switch (scope) {
        case UiScope::Macro: return 1u;
        case UiScope::Sub:   return 2u;
        default:             return 3u;
    }
}

Keymap::Keymap() { reset_defaults(); }

void Keymap::reset_defaults() {
    // Index by the row's own id so this stays correct even if rows reorder.
    for (const auto& spec : kActionSpec)
        binds_[idx(spec.id)] = spec.def;
}

void Keymap::set(ActionId id, SDL_Scancode sc) {
    const unsigned worlds = scope_worlds(action_spec(id).scope);
    if (sc != SDL_SCANCODE_UNKNOWN) {
        for (const auto& spec : kActionSpec) {
            if (spec.id == id) continue;
            if ((scope_worlds(spec.scope) & worlds) == 0) continue;  // never coexist
            if (binds_[idx(spec.id)] == sc)
                binds_[idx(spec.id)] = SDL_SCANCODE_UNKNOWN;
        }
    }
    binds_[idx(id)] = sc;
}

// ── Persistence: the same tiny, forgiving text KV file as ui_settings ────────
bool load_keymap(Keymap& m, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;               // no prefs yet -> constructor defaults stand

    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;          // skip leading whitespace
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        char key[64] = {0};
        int  sc      = SDL_SCANCODE_UNKNOWN;
        if (std::sscanf(p, "%63s %d", key, &sc) < 2) continue;
        if (sc < 0 || sc >= SDL_NUM_SCANCODES) continue;

        for (const auto& spec : kActionSpec) {            // resolve key -> action
            if (std::strcmp(spec.key, key) != 0) continue; // unknown keys ignored
            // Through Keymap::set, so the steal rule holds even for a
            // hand-edited file: a duplicate within one world resolves to the
            // LAST line, the earlier action left visibly unbound.
            m.set(spec.id, static_cast<SDL_Scancode>(sc));
            break;
        }
    }
    std::fclose(f);
    return true;
}

bool save_keymap(const Keymap& m, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# timaert keymap v1\n");
    std::fprintf(f, "# <action> <SDL scancode>   (0 = unbound; Esc is fixed)\n");
    for (const auto& spec : kActionSpec)
        std::fprintf(f, "%s %d\n", spec.key, int(m.get(spec.id)));
    std::fclose(f);
    return true;
}

// ── The one Controls panel (shared by both worlds) ───────────────────────────
bool draw_keymap_panel(Keymap& m, bool* open) {
    if (!open || !*open) return false;
    bool changed = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 560.0f), ImGuiCond_Appearing);

    if (ImGui::Begin("Controls", open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextDisabled(
            "Click a binding, then press the new key. Saved automatically.\n"
            "A key serves one action per world. Esc is fixed: menu / cancel.");
        ImGui::Spacing();

        // Render every row whose scope matches, drawing the section header only
        // once a matching row exists. One loop body serves all groups and every
        // action — the universal, no-special-cases core (same as ui_settings).
        auto section = [&](const char* title, UiScope scope) {
            bool headerDrawn = false;
            for (const auto& spec : kActionSpec) {
                if (spec.scope != scope) continue;
                if (!headerDrawn) { ImGui::SeparatorText(title); headerDrawn = true; }

                ImGui::PushID(spec.key);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(spec.label);
                ImGui::SameLine(240.0f);
                const bool pending = m.pendingRebind == int(spec.id);
                const SDL_Scancode sc = m.get(spec.id);
                const char* name = pending ? "press a key..."
                                 : sc == SDL_SCANCODE_UNKNOWN ? "—"
                                 : SDL_GetScancodeName(sc);
                if (name == nullptr || name[0] == '\0') name = "?";
                if (pending)
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          ImVec4(0.55f, 0.42f, 0.12f, 1.0f));
                if (ImGui::Button(name, ImVec2(180.0f, 0.0f)))
                    m.pendingRebind = pending ? -1 : int(spec.id);
                if (pending) ImGui::PopStyleColor();
                ImGui::PopID();
            }
        };

        section("Global", UiScope::Both);
        section("Macroworld", UiScope::Macro);
        section("Microworld", UiScope::Sub);

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset to defaults")) {
            m.reset_defaults();
            m.pendingRebind = -1;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) *open = false;
    }
    ImGui::End();
    if (!*open) m.pendingRebind = -1;   // closing the panel disarms the latch
    return changed;
}

} // namespace sm::ui

#include "ui/ui_settings.h"

#include "imgui.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace sm::ui {

// ── The registry ─────────────────────────────────────────────────────────────
// Add an element: add a UiElementId enumerator (ui_settings.h) and one row here,
// in the SAME order. It then appears in the settings panel, persists, and can be
// gated/scaled at its draw site — with no other code.
const UiElementSpec kUiElementSpec[kUiElementCount] = {
    // id                         key                 label                 scope          vis   scale  min    max    def   tip
    {UiElementId::PlayerHud,      "hud.player",       "Player status bar",  UiScope::Both,  true,  true, 0.60f, 1.80f, 1.0f, "Top strip: HP/MP/SP, day/time, position, level."},
    {UiElementId::BottomToolbar,  "hud.toolbar",      "Command toolbar",    UiScope::Both,  true,  true, 0.60f, 1.80f, 1.0f, "Bottom row of command buttons."},
    {UiElementId::PanelCharacter, "panel.character",  "Character panel",    UiScope::Both,  true,  true, 0.60f, 2.00f, 1.0f, "Stats / inventory / army / equipment / spells."},
    {UiElementId::PanelQuestLog,  "panel.quest",      "Quest log",          UiScope::Both,  true,  true, 0.60f, 2.00f, 1.0f, "Active and available quests."},
    {UiElementId::PanelCodex,     "panel.codex",      "Codex",              UiScope::Both,  true,  true, 0.60f, 2.00f, 1.0f, "Lore and bestiary reference."},
    {UiElementId::PanelMap,       "panel.map",        "Map",                UiScope::Both,  true,  true, 0.60f, 2.00f, 1.0f, "Full-screen world / subworld map page."},
    {UiElementId::MacroOverlay,   "macro.overlay",    "Map hover & path",   UiScope::Macro, true,  false,1.0f,  1.0f,  1.0f, "World-map hover highlight, tooltip and the click-to-move path line."},
    {UiElementId::QuestMarkers,   "macro.questmarks", "Quest markers",      UiScope::Macro, true,  true, 0.60f, 1.80f, 1.0f, "Yellow \"!\" pins floating over active quest targets on the world map."},
    {UiElementId::NpcProximity,   "macro.npcnear",    "Nearby NPCs",        UiScope::Macro, true,  true, 0.60f, 1.80f, 1.0f, "Right-edge stack of nearby characters."},
    {UiElementId::PanelDiplomacy, "panel.diplomacy",  "Diplomacy panel",    UiScope::Macro, true,  true, 0.60f, 2.00f, 1.0f, "Faction relations."},
    {UiElementId::PanelSettlement,"panel.settlement", "Settlement panel",   UiScope::Macro, true,  true, 0.60f, 2.00f, 1.0f, "Town: build / trade / garrison / recruit."},
    {UiElementId::SubMinimap,     "sub.minimap",      "Minimap",            UiScope::Sub,   true,  true, 0.60f, 2.00f, 1.0f, "Compass minimap disc (top-right)."},
    {UiElementId::SubCombatLog,   "sub.combatlog",    "Combat log",         UiScope::Sub,   true,  true, 0.60f, 1.80f, 1.0f, "Recent hit / damage messages."},
    {UiElementId::SubDangerGem,   "sub.dangergem",    "Danger indicator",   UiScope::Sub,   true,  true, 0.60f, 1.80f, 1.0f, "Top-left threat pip."},
    {UiElementId::SubCrosshair,   "sub.crosshair",    "Crosshair",          UiScope::Sub,   true,  true, 0.40f, 2.50f, 1.0f, "Centre-screen aiming reticle."},
};

static_assert(sizeof(kUiElementSpec) / sizeof(kUiElementSpec[0]) == kUiElementCount,
              "kUiElementSpec must have exactly one row per UiElementId");

const UiElementSpec& ui_spec(UiElementId id) {
    const std::size_t i = static_cast<std::size_t>(id);
    assert(i < kUiElementCount && kUiElementSpec[i].id == id &&
           "kUiElementSpec rows must be in UiElementId order");
    return kUiElementSpec[i];
}

UiSettings::UiSettings() { reset_defaults(); }

void UiSettings::reset_defaults() {
    // Index by the row's own id so this stays correct even if rows are reordered.
    for (const auto& spec : kUiElementSpec)
        state_[idx(spec.id)] = UiElementState{spec.defaultVisible, spec.scaleDefault};
}

// ── Persistence: a tiny, forgiving text KV file ──────────────────────────────
bool load_ui_settings(UiSettings& s, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;               // no prefs yet -> constructor defaults stand

    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;          // skip leading whitespace
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;

        char  key[64] = {0};
        int   vis     = 1;
        float scale   = 1.0f;
        // A line may carry key only, key+vis, or key+vis+scale; sscanf leaves the
        // rest at the defaults above.
        if (std::sscanf(p, "%63s %d %f", key, &vis, &scale) < 1) continue;

        for (const auto& spec : kUiElementSpec) {           // resolve key -> element
            if (std::strcmp(spec.key, key) != 0) continue;  // unknown keys ignored
            UiElementState& st = s.mut(spec.id);
            st.visible = (vis != 0);
            if (spec.scalable) {                            // clamp into allowed range
                if (scale < spec.scaleMin) scale = spec.scaleMin;
                if (scale > spec.scaleMax) scale = spec.scaleMax;
                st.scale = scale;
            } else {
                st.scale = spec.scaleDefault;
            }
            break;
        }
    }
    std::fclose(f);
    return true;
}

bool save_ui_settings(const UiSettings& s, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# timaert ui prefs v1\n");
    std::fprintf(f, "# <element> <visible 0|1> <size multiplier>\n");
    for (const auto& spec : kUiElementSpec) {
        const UiElementState& st = s.get(spec.id);
        std::fprintf(f, "%s %d %.2f\n", spec.key, st.visible ? 1 : 0, st.scale);
    }
    std::fclose(f);
    return true;
}

// ── The one settings panel (shared by both worlds) ───────────────────────────
bool draw_ui_settings_panel(UiSettings& s, bool* open) {
    if (!open || !*open) return false;
    bool changed = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);

    if (ImGui::Begin("Interface", open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextDisabled("Show/hide and resize any UI element. Saved automatically.");
        ImGui::Spacing();

        // Render every row whose scope matches, drawing the section header only
        // once a matching row exists. One loop body serves all groups and every
        // element — the universal, no-special-cases core.
        auto section = [&](const char* title, UiScope scope) {
            bool headerDrawn = false;
            for (const auto& spec : kUiElementSpec) {
                if (spec.scope != scope) continue;
                if (!headerDrawn) { ImGui::SeparatorText(title); headerDrawn = true; }

                UiElementState& st = s.mut(spec.id);
                ImGui::PushID(spec.key);
                if (ImGui::Checkbox(spec.label, &st.visible)) changed = true;
                if (spec.tip && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", spec.tip);
                if (spec.scalable) {
                    ImGui::SameLine(240.0f);
                    ImGui::SetNextItemWidth(180.0f);
                    if (ImGui::SliderFloat("##size", &st.scale,
                                           spec.scaleMin, spec.scaleMax, "%.2fx"))
                        changed = true;
                }
                ImGui::PopID();
            }
        };

        section("Global", UiScope::Both);
        section("Macroworld", UiScope::Macro);
        section("Microworld", UiScope::Sub);

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset to defaults")) { s.reset_defaults(); changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Close")) *open = false;
    }
    ImGui::End();
    return changed;
}

} // namespace sm::ui

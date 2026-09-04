#include "ui/screens.h"
#include "ui/keymap.h"
#include "ui/ui_theme.h"   // palette + viewport_size + draw_title_backdrop
#include "git_hash.h"      // TIMAERT_GIT_HASH, generated per build (CMakeLists)
#include "macro/state.h"
#include "content/plot/intro.h"   // creation_*_choices: the authored tables
#include "imgui.h"
#include <SDL_keyboard.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace sm::ui {
namespace {

void centred_window(ImVec2 size) {
    const ImVec2 vp = viewport_size();
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
}

void big_button(bool* slot, const char* label, ImVec2 size) {
    if (ImGui::Button(label, size)) *slot = true;
}

void draw_dim_background(float alpha) {
    const ImVec2 vp = viewport_size();
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), vp,
                      IM_COL32(0, 0, 0, int(alpha * 255.0f)));
}

} // namespace

ShellResult draw_title_menu() {
    ShellResult r{};
    draw_title_backdrop();
    centred_window(ImVec2(440, 420));
    ImGui::Begin("##title", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(wp, ImVec2(wp.x + 440, wp.y + 56), kPalNight);
    dl->AddLine(ImVec2(wp.x, wp.y + 56), ImVec2(wp.x + 440, wp.y + 56),
                kPalBronze, 1.0f);
    ImGui::SetCursorPosY(14);
    ImGui::PushFont(nullptr);
    ImGui::SetWindowFontScale(2.4f);
    ImGui::SetCursorPosX((440 - ImGui::CalcTextSize("Legacy of Sacrilege").x) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, kPalParchment);
    ImGui::Text("Legacy of Sacrilege");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    ImGui::SetCursorPosY(72);
    ImGui::SetCursorPosX((440 - ImGui::CalcTextSize("The Timaert Chronicles").x) * 0.5f);
    ImGui::TextDisabled("The Timaert Chronicles");
    ImGui::Dummy(ImVec2(0, 30));
    const ImVec2 sz(360, 44);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(&r.startNewGame, "New Game",  sz);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(&r.openCustomNewGame, "Custom New Game", sz);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(&r.loadGame,     "Load Game", sz);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(&r.quit,         "Quit",      sz);
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::SetCursorPosX(16);
    // The built commit, for tracking a build back to its source. A "+"
    // suffix marks uncommitted changes in the tree it was built from.
    ImGui::TextDisabled("build " TIMAERT_GIT_HASH);
    ImGui::End();
    return r;
}

// ── Character creation (pre-world) ──────────────────────────────────────
//
// The screen authors a CreationState through the sheet's OWN doors —
// spend_attribute_point and spend_learn_pick — so it cannot grant what the
// game would refuse. The two "-" refunds are the screen's own right: creation
// is authoring, nothing in the world has read the sheet yet, and taking a
// point back before the world exists is not a respec. The choice tables
// (sex, homeland) come from content/plot/intro.h verbatim — the same authored
// rows the intro story used to ask through.
ShellResult draw_character_creation(CreationState& cs) {
    ShellResult r{};
    draw_title_backdrop();
    centred_window(ImVec2(780, 660));
    ImGui::Begin("##creation", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::SetCursorPosX((780 - ImGui::CalcTextSize("Create Your Character").x * 1.6f) * 0.5f);
    ImGui::Text("Create Your Character");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    // ── identity ──
    ImGui::Text("Name");
    ImGui::SameLine(120);
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("##name", cs.name, sizeof(cs.name));

    std::size_t sexCount = 0;
    const content::StoryChoice* sexes = content::creation_sex_choices(sexCount);
    ImGui::Text("Nature");
    for (std::size_t i = 0; i < sexCount; ++i) {
        ImGui::SameLine(i == 0 ? 120.0f : 0.0f);
        if (ImGui::RadioButton(sexes[i].label, cs.sexIdx == int(i)))
            cs.sexIdx = int(i);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", sexes[i].description);
    }

    std::size_t realmCount = 0;
    const content::StoryChoice* realms =
        content::creation_realm_choices(realmCount);
    ImGui::Text("Homeland");
    for (std::size_t i = 0; i < realmCount; ++i) {
        ImGui::SameLine(i == 0 ? 120.0f : 0.0f);
        if (ImGui::RadioButton(realms[i].label, cs.realmIdx == int(i)))
            cs.realmIdx = int(i);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", realms[i].description);
    }
    ImGui::Separator();

    LevelData& ld = cs.sheet.levelData;
    if (ImGui::BeginTable("creation_cols", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("attrs", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        ImGui::TableSetupColumn("skills");
        ImGui::TableNextRow();

        // ── attributes: 5 points, refundable while authoring ──
        ImGui::TableNextColumn();
        ImGui::Text("Attributes");
        ImGui::SameLine();
        ImGui::TextDisabled("(%d points left)", ld.attributePoints);
        for (const AttributeDef& row : kAttributeDefs) {
            ImGui::PushID(row.label);
            ImGui::Text("%s %d", row.label, cs.sheet.attributes.of(row.id));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.effect);
            ImGui::SameLine(120);
            ImGui::BeginDisabled(cs.sheet.attributes.of(row.id) <= 1);
            if (ImGui::SmallButton("-")) {
                --cs.sheet.attributes[row.id];
                ++ld.attributePoints;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(ld.attributePoints <= 0);
            if (ImGui::SmallButton("+"))
                spend_attribute_point(ld, cs.sheet.attributes, row.id);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Text("Perks");
        // Honest, not coy: the system is designed separately (CANON S14) and
        // creation owes one starter-pool pick when it lands.
        ImGui::TextDisabled("One starter perk arrives with\nthe perk system.");

        // ── skills: 5 learn picks; rank 1 = known ──
        ImGui::TableNextColumn();
        ImGui::Text("Skills");
        ImGui::SameLine();
        ImGui::TextDisabled("(%d picks left — a pick learns a skill)",
                            ld.learnPicks);
        if (ImGui::BeginChild("##skill_list", ImVec2(0, 330), true)) {
            for (const SkillDef& row : kSkillDefs) {
                ImGui::PushID(row.label);
                const bool known = cs.sheet.skills.of(row.id) > 0;
                if (known) {
                    ImGui::Text("%s", row.label);
                    ImGui::SameLine(200);
                    if (ImGui::SmallButton("x")) {
                        cs.sheet.skills[row.id] = 0;   // creation-only refund
                        ++ld.learnPicks;
                    }
                } else {
                    ImGui::TextDisabled("%s", row.label);
                    ImGui::SameLine(200);
                    ImGui::BeginDisabled(ld.learnPicks <= 0);
                    if (ImGui::SmallButton("Learn"))
                        spend_learn_pick(ld, cs.sheet.skills, row.id);
                    ImGui::EndDisabled();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%c%d%% %s",
                                      row.buysCostDown ? '-' : '+',
                                      int(row.pctPerRank), row.effect);
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    ImGui::Separator();
    const bool ready = cs.name[0] != '\0' && cs.sexIdx >= 0 && cs.realmIdx >= 0;
    if (!ready)
        ImGui::TextDisabled("Name, nature and homeland make a person.");
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Start", ImVec2(220, 44))) r.startCreatedGame = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Default", ImVec2(140, 44))) r.creationDefault = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fill everything with the standard wanderer preset.");
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(140, 44))) r.cancelCreation = true;
    ImGui::End();
    return r;
}

// The standard wanderer: an Empire swordhand of the road. Spent through the
// REAL doors, so if a budget or a cap ever shrinks this preset fails loudly
// in the smoke instead of silently granting the impossible.
void creation_apply_default(CreationState& cs) {
    cs = CreationState{};                    // name stays "Traveller"
    cs.sexIdx = 0;                           // Male: +1 skill point
    cs.realmIdx = 1;                         // Empire of Light (keeps the
                                             // imperial starting coin)
    LevelData& ld = cs.sheet.levelData;
    spend_attribute_point(ld, cs.sheet.attributes, AttributeId::Str);
    spend_attribute_point(ld, cs.sheet.attributes, AttributeId::Str);
    spend_attribute_point(ld, cs.sheet.attributes, AttributeId::End);
    spend_attribute_point(ld, cs.sheet.attributes, AttributeId::End);
    spend_attribute_point(ld, cs.sheet.attributes, AttributeId::Spd);
    spend_learn_pick(ld, cs.sheet.skills, SkillId::Sword);
    spend_learn_pick(ld, cs.sheet.skills, SkillId::LightArmor);
    spend_learn_pick(ld, cs.sheet.skills, SkillId::Shield);
    spend_learn_pick(ld, cs.sheet.skills, SkillId::Athletics);
    spend_learn_pick(ld, cs.sheet.skills, SkillId::Travel);
}

namespace {

const char* save_status_label(SaveInspectStatus s) {
    switch (s) {
        case SaveInspectStatus::Missing:         return "Missing";
        case SaveInspectStatus::Unreadable:      return "Unreadable";
        case SaveInspectStatus::VersionMismatch: return "Version mismatch";
        case SaveInspectStatus::Ready:           return "Ready";
    }
    return "Unknown";
}

} // namespace

// One slot's header block + Load button; returns true when Load was clicked.
// `label` names the slot, `fallbackFile` shows when the path is empty.
static bool draw_save_slot(const char* label, const char* fallbackFile,
                           const SaveSummary& save) {
    ImGui::Text("%s: %s", label,
                save.path.empty() ? fallbackFile : save.path.c_str());
    ImGui::Text("Status: %s", save_status_label(save.status));
    if (save.status == SaveInspectStatus::Ready) {
        ImGui::Text("Name: %s", save.saveName.empty() ? "(unnamed)" : save.saveName.c_str());
        ImGui::Text("Saved: %s", save.savedAt.empty() ? "(unknown)" : save.savedAt.c_str());
        ImGui::Text("Version: %d", save.version);
        ImGui::Text("Seed: 0x%08X", save.worldSeed);
        ImGui::Text("Time: Day %d  %02d:%02d", save.day, save.hour, save.minute);
    } else if (save.status == SaveInspectStatus::VersionMismatch) {
        ImGui::Text("Save version: %d  Required: %d", save.version, kSaveVersion);
    } else if (save.status == SaveInspectStatus::Missing) {
        ImGui::TextDisabled("No save file is present.");
    } else {
        ImGui::TextDisabled("Save header could not be read.");
    }
    const bool canLoad = save.status == SaveInspectStatus::Ready;
    bool clicked = false;
    if (!canLoad) ImGui::BeginDisabled();
    ImGui::PushID(label);
    if (ImGui::Button("Load", ImVec2(180, 36))) clicked = true;
    ImGui::PopID();
    if (!canLoad) ImGui::EndDisabled();
    return clicked;
}

ShellResult draw_load_screen(const SaveSummary& save,
                             const SaveSummary& autosave) {
    ShellResult r{};
    draw_dim_background(0.85f);
    centred_window(ImVec2(500, 520));
    ImGui::Begin("##load", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);

    ImGui::SetWindowFontScale(1.6f);
    ImGui::SetCursorPosX((500 - ImGui::CalcTextSize("LOAD GAME").x) * 0.5f);
    ImGui::Text("LOAD GAME");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    if (draw_save_slot("Slot", "save.bin", save)) r.loadGame = true;
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::Separator();
    if (draw_save_slot("Autosave", "autosave.bin", autosave))
        r.loadAutosave = true;

    ImGui::Dummy(ImVec2(0, 20));
    if (ImGui::Button("Back", ImVec2(180, 36))) r.cancelLoad = true;

    ImGui::End();
    return r;
}

// ── Custom new game ─────────────────────────────────────────────
//
// One row per parameter. Add a new entry here and the slider appears
// automatically — no other code changes required (this is the
// "expandable menu" the design calls for). `field` is a pointer to the
// LayerParameters member; `min`/`max` are the slider bounds; `format`
// is the printf-style label format. Logarithmic flag uses an ImGui
// power slider for nicer feel on wide ranges.
struct ParamSpec {
    const char* label;
    float       LayerParameters::* field;
    float       minV;
    float       maxV;
    const char* format;
    const char* tooltip;
};

static constexpr ParamSpec kCustomParamSpec[] = {
    {"Sea level",            &LayerParameters::seaLevel,             0.10f, 0.80f, "%.2f",
     "Fraction of map below water (0.40 = balanced)"},
    {"Continent scale",      &LayerParameters::continentScale,       0.10f, 1.50f, "%.2f",
     "Larger = fewer, bigger landmasses"},
    {"Continent intensity",  &LayerParameters::continentIntensity,   0.00f, 1.00f, "%.2f",
     "How strongly the continent bias dominates noise"},
    {"Ridge intensity",      &LayerParameters::ridgeIntensity,       0.00f, 0.60f, "%.2f",
     "Mountain-ridge sharpness"},
    {"Domain warp",          &LayerParameters::domainWarp,           0.00f, 1.00f, "%.2f",
     "Coastline irregularity / non-circular continents"},
    {"Height octaves",       &LayerParameters::heightOctaves,        2.0f,  8.0f,  "%.0f",
     "More = finer detail (slower)"},
    {"Moisture octaves",     &LayerParameters::moistureOctaves,      2.0f,  8.0f,  "%.0f",
     "Moisture-field detail"},
    {"Temperature variance", &LayerParameters::temperatureVariation, 0.00f, 1.00f, "%.2f",
     "Noise added to latitude-driven temperature"},
    {"Height scale",         &LayerParameters::heightScale,          0.30f, 2.00f, "%.2f",
     "Vertical exaggeration"},
    {"Moisture scale",       &LayerParameters::moistureScale,        0.30f, 2.00f, "%.2f",
     "Moisture noise wavelength"},
};

ShellResult draw_custom_new_game(CustomGameParams& p,
                                 ImTextureID previewTex,
                                 int previewW, int previewH,
                                 bool worldReady) {
    ShellResult r{};
    draw_dim_background(0.85f);

    // Wide two-column layout: ~360 px parameter column on the left,
    // square preview on the right. Total fits 1280×800 default window.
    const ImVec2 vp = viewport_size();
    const float winW = std::min(vp.x - 40.0f, 980.0f);
    const float winH = std::min(vp.y - 40.0f, 660.0f);
    centred_window(ImVec2(winW, winH));
    ImGui::Begin("##custom", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);

    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("CUSTOM NEW GAME");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    const float footerH = 48.0f;
    const float bodyH   = ImGui::GetContentRegionAvail().y - footerH;
    const float leftW   = 380.0f;

    // ── Left column: parameters ─────────────────────────────────
    ImGui::BeginChild("##params", ImVec2(leftW, bodyH), true);

    // Map size: power-of-2 slider 128…4096.
    if (p.mapSizeLog2 < 7)  p.mapSizeLog2 = 7;
    if (p.mapSizeLog2 > 12) p.mapSizeLog2 = 12;
    int sizeVal = 1 << p.mapSizeLog2;
    char sizeLbl[32];
    std::snprintf(sizeLbl, sizeof(sizeLbl), "%d × %d", sizeVal, sizeVal);
    ImGui::SliderInt("World size", &p.mapSizeLog2, 7, 12, sizeLbl);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Power of 2: 128, 256, 512, 1024, 2048, 4096");

    // Seed: 0 means "random from clock at start time".
    int seedI = int(p.seed);
    if (ImGui::InputInt("Seed (0 = random)", &seedI)) {
        if (seedI < 0) seedI = 0;
        p.seed = std::uint32_t(seedI);
    }

    // City count: scales every kingdom's min/max proportionally.
    if (p.cityCountTarget < 10)  p.cityCountTarget = 10;
    if (p.cityCountTarget > 400) p.cityCountTarget = 400;
    ImGui::SliderInt("Total cities", &p.cityCountTarget, 10, 400, "%d");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Approximate total city count across all kingdoms.\n"
                          "Each kingdom is scaled proportionally; capitals always present.");

    ImGui::Separator();
    ImGui::TextDisabled("Terrain generation");
    ImGui::Spacing();

    for (const auto& spec : kCustomParamSpec) {
        ImGui::SliderFloat(spec.label, &(p.layer.*spec.field),
                          spec.minV, spec.maxV, spec.format);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", spec.tooltip);
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset terrain to defaults",
                      ImVec2(ImGui::GetContentRegionAvail().x, 28))) {
        p.layer = LayerParameters{};
    }

    ImGui::EndChild();

    // ── Right column: preview ───────────────────────────────────
    ImGui::SameLine();
    ImGui::BeginChild("##preview", ImVec2(0, bodyH), true);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float side = std::min(avail.x, avail.y - 24.0f);
    if (previewTex && previewW > 0 && previewH > 0) {
        const float pad = (avail.x - side) * 0.5f;
        if (pad > 0) ImGui::Dummy(ImVec2(pad, 0));
        if (pad > 0) ImGui::SameLine();
        ImGui::Image(previewTex, ImVec2(side, side));
        ImGui::Spacing();
        ImGui::TextDisabled("Preview %d × %d", previewW, previewH);
    } else {
        const float ty = (avail.y - ImGui::GetTextLineHeight()) * 0.5f;
        if (ty > 0) ImGui::Dummy(ImVec2(0, ty));
        const char* msg = "Press Regenerate to build a preview";
        const float tx = (avail.x - ImGui::CalcTextSize(msg).x) * 0.5f;
        if (tx > 0) ImGui::Dummy(ImVec2(tx, 0));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", msg);
    }
    ImGui::EndChild();

    // ── Footer buttons ─────────────────────────────────────────
    ImGui::Separator();
    if (ImGui::Button("Regenerate", ImVec2(160, 32))) r.regenerateCustom = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 32)))     r.cancelCustomNewGame = true;
    ImGui::SameLine();
    {
        ImGui::BeginDisabled(!worldReady);
        if (ImGui::Button("Start", ImVec2(160, 32)))  r.startCustomNewGame = true;
        ImGui::EndDisabled();
    }
    if (!worldReady) {
        ImGui::SameLine();
        ImGui::TextDisabled("(regenerate first)");
    }

    ImGui::End();
    return r;
}

ShellResult draw_game_menu() {
    ShellResult r{};
    draw_dim_background(0.55f);
    centred_window(ImVec2(360, 444));
    ImGui::Begin("##menu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::SetCursorPosX((360 - ImGui::CalcTextSize("MENU").x) * 0.5f);
    ImGui::Text("MENU");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 16));
    const ImVec2 sz(300, 36);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.resume,        "Resume",    sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.saveGame,      "Save",      sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.loadGame,      "Load",      sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.openCodex,     "Codex",     sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.openInterface, "Interface", sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.openControls,  "Controls",  sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.returnToTitle, "Title",     sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(&r.quit,          "Quit",      sz);
    ImGui::End();
    return r;
}

ShellResult draw_death_screen(const GameState& gs) {
    ShellResult r{};
    draw_dim_background(0.85f);
    centred_window(ImVec2(420, 240));
    ImGui::Begin("##dead", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::SetCursorPosX((420 - ImGui::CalcTextSize("YOU DIED").x) * 0.5f);
    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "YOU DIED");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::TextWrapped("%s fell on day %d at age %d.",
        gs.player.name.empty() ? "The wanderer" : gs.player.name.c_str(),
        gs.worldTime.day(), gs.player.ageDays);
    ImGui::Dummy(ImVec2(0, 12));
    const ImVec2 sz(360, 36);
    ImGui::SetCursorPosX((420 - sz.x) * 0.5f); big_button(&r.returnToTitle, "Return to Title", sz);
    ImGui::SetCursorPosX((420 - sz.x) * 0.5f); big_button(&r.quit,          "Quit",            sz);
    ImGui::End();
    return r;
}

void draw_player_hud(const GameState& gs, float scale) {
    // Proto_c-style top status bar — single horizontal strip across the
    // full width of the window: Time / HP / MP / SP / Coords / name+level.
    const auto& cs = gs.player.combatStats;
    const ImVec2 vp = viewport_size();
    const float barH = kTopStatusBarHeight * scale;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp.x, barH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(10, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(14, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##timaert_topbar", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(scale);

    // ── Time ───────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.78f, 0.82f, 1.0f, 1.0f),
        "Day %d  %02d:%02d", gs.worldTime.day(), gs.worldTime.hour(), gs.worldTime.minute());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ── Compact bar ┃HP/MP/SP┃ — coloured fill + numeric overlay ──
    auto compact_bar = [](const char* label, int cur, int max, ImU32 col, float w) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%s %d/%d", label, cur, max);
        const ImVec2 p   = ImGui::GetCursorScreenPos();
        const float  h   = ImGui::GetTextLineHeight() + 4.0f;
        auto*        dl  = ImGui::GetWindowDrawList();
        const float  frac = max > 0 ? float(cur) / float(max) : 0.0f;
        const float  cl   = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(20, 20, 20, 200), 3.0f);
        dl->AddRectFilled(p, ImVec2(p.x + w * cl, p.y + h), col, 3.0f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 50), 3.0f);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + 2.0f),
                    IM_COL32(255, 255, 255, 235), buf);
        ImGui::Dummy(ImVec2(w, h));
    };
    compact_bar("HP", cs.currentHp, cs.maxHp, IM_COL32(200,  60,  60, 220), 130.0f * scale);
    ImGui::SameLine();
    compact_bar("MP", cs.currentMp, cs.maxMp, IM_COL32( 80, 120, 220, 220), 130.0f * scale);
    ImGui::SameLine();
    compact_bar("SP", cs.currentSp, cs.maxSp, IM_COL32( 80, 200,  90, 220), 130.0f * scale);

    // No Coin / Items numbers here: coin is a per-faction COMMODITY since the
    // barter rework, so a single summed "wallet" (and a raw item count) is a
    // number with no meaning. The inventory panel shows the real holdings.
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Pos %4.0f, %4.0f", gs.player.x, gs.player.y);

    // Right-aligned: name + level
    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s  Lv %d   %d / %d EXP",
        gs.player.name.empty() ? "Wanderer" : gs.player.name.c_str(),
        gs.player.sheet.levelData.level,
        gs.player.sheet.levelData.exp,
        gs.player.sheet.levelData.expToNext);
    const float nw = ImGui::CalcTextSize(nameBuf).x;
    ImGui::SameLine(vp.x - nw - 14.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.85f, 1.0f), "%s", nameBuf);

    ImGui::End();
    ImGui::PopStyleVar(4);
}

ToolbarResult draw_bottom_toolbar(const GameState& /*gs*/, bool subworldActive,
                                  const Keymap& km, float scale) {
    // Proto_c-style bottom command toolbar — full-width strip.
    // Buttons are visual placeholders that emit semantic intents the
    // app loop translates into actions (open inventory, toggle pause,
    // change speed, etc.). Keeps the shell decoupled from gameplay.
    ToolbarResult r;
    const ImVec2 vp = viewport_size();
    const float barH = 44.0f * scale;
    ImGui::SetNextWindowPos(ImVec2(0, vp.y - barH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp.x, barH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(8, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##timaert_toolbar", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);
    ImGui::SetWindowFontScale(scale);

    auto tbtn = [scale](const char* glyph, const char* tooltip) {
        const bool clicked = ImGui::Button(glyph, ImVec2(34 * scale, 32 * scale));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        return clicked;
    };
    // Tooltip text with the key that CURRENTLY triggers the action appended,
    // read from the live keymap — never a literal, so a rebind can't strand a
    // stale hint. An unbound action gets no suffix at all.
    char tipBuf[96];
    auto tip = [&km, &tipBuf](const char* text, ActionId a) -> const char* {
        const SDL_Scancode sc = km.get(a);
        if (sc == SDL_SCANCODE_UNKNOWN) return text;
        std::snprintf(tipBuf, sizeof(tipBuf), "%s [%s]",
                      text, SDL_GetScancodeName(sc));
        return tipBuf;
    };

    // II and > are the two faces of ONE pause — the same flag the Space key
    // toggles on the map, never a second mechanism. The menu lives on its own
    // button, because Esc is a menu and not a speed control.
    //
    // Both are dead underground, and LOOK dead: the subworld runs in real time
    // and only a window or the menu stops it, so a live-looking button that
    // did nothing would be the lie.
    if (subworldActive) ImGui::BeginDisabled();
    if (tbtn("II",  subworldActive ? "Pause — world map only"
                                   : tip("Pause", ActionId::Pause)))
        r.pause  = true; ImGui::SameLine();
    if (tbtn(">",   subworldActive ? "Resume — world map only"
                                   : tip("Resume", ActionId::Pause)))
        r.resume = true; ImGui::SameLine();
    // Speed and rest are map-only for the same honesty reason as the pause:
    // the subworld runs in real time.
    if (tbtn(">>",  subworldActive ? "Fast — world map only"
                                   : "Fast (4x)"))      r.speed4 = true; ImGui::SameLine();
    // The Rest glyph IS the key that performs it — quoted from the live
    // keymap like every tooltip here, never a literal (CANON S22). An unbound
    // action shows a sleep mark that names no key.
    const SDL_Scancode restSc = km.get(ActionId::Rest);
    const char* restName = restSc != SDL_SCANCODE_UNKNOWN
                             ? SDL_GetScancodeName(restSc) : "";
    if (tbtn(restName[0] ? restName : "Zz",
             subworldActive ? "Rest — world map only"
                            : tip("Rest until stamina is full", ActionId::Rest)))
        r.rest = true; ImGui::SameLine();
    if (subworldActive) ImGui::EndDisabled();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    if (tbtn("Stat", "Stats / progression"))        r.stats      = true; ImGui::SameLine();
    if (tbtn("Inv", tip("Inventory", ActionId::Character)))    r.inventory = true; ImGui::SameLine();
    if (tbtn("Map", tip("World Map", ActionId::Map)))          r.map       = true; ImGui::SameLine();
    if (tbtn("Stl", tip("Settlement", ActionId::Settlement)))  r.build     = true; ImGui::SameLine();
    if (tbtn("Qst", tip("Quests", ActionId::Quests)))          r.quests    = true; ImGui::SameLine();
    if (tbtn("Par", tip("Party / Army", ActionId::ArmyTab)))   r.party     = true; ImGui::SameLine();
    if (tbtn("Eq",  subworldActive ? "Equipment"   // E interacts down here
                                   : tip("Equipment", ActionId::EquipmentTab)))
        r.equipment  = true; ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    if (tbtn("Esc", "Menu [Esc]"))                  r.menu       = true; ImGui::SameLine();
    if (tbtn("Cdx", tip("Codex", ActionId::Codex)))            r.codex     = true; ImGui::SameLine();
    if (tbtn("Dip", tip("Diplomacy", ActionId::Diplomacy)))    r.diplomacy = true; ImGui::SameLine();
    if (tbtn(subworldActive ? "Out" : "In",
             tip(subworldActive ? "Leave subworld" : "Enter cell",
                 ActionId::EnterLeave)))
        r.toggleSubworld = true; ImGui::SameLine();

    // Right-edge: zoom controls
    const float right = vp.x - 16.0f - (34 * scale + 6) * 2;
    ImGui::SameLine(right);
    if (tbtn("-",   "Zoom out [Wheel]"))            r.zoomOut    = true; ImGui::SameLine();
    if (tbtn("+",   "Zoom in [Wheel]"))             r.zoomIn     = true;

    ImGui::End();
    ImGui::PopStyleVar(4);
    return r;
}

} // namespace sm::ui

#include "ui/screens.h"
#include "macro/state.h"
#include "imgui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace sm::ui {
namespace {

// All ImGui geometry is in *logical points* (DisplaySize), never in
// drawable pixels. On a Retina display the SDL drawable size is 2x the
// window size, so passing drawable pixels here would push the menu off
// screen. Always read size from ImGui itself.
ImVec2 viewport_size() {
    return ImGui::GetIO().DisplaySize;
}

void centred_window(const char* /*id*/, ImVec2 size) {
    const ImVec2 vp = viewport_size();
    ImGui::SetNextWindowPos(ImVec2(vp.x * 0.5f, vp.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
}

void big_button(ShellResult& /*out*/, bool* slot, const char* label, ImVec2 size) {
    if (ImGui::Button(label, size)) *slot = true;
}

void draw_dim_background(float alpha) {
    const ImVec2 vp = viewport_size();
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), vp,
                      IM_COL32(0, 0, 0, int(alpha * 255.0f)));
}

void bar(const char* label, int cur, int max, ImU32 col, float width) {
    ImGui::Text("%-3s %4d/%4d", label, cur, max);
    ImGui::SameLine();
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), IM_COL32(20, 20, 20, 200), 2.0f);
    float frac = max > 0 ? float(cur) / float(max) : 0.0f;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    dl->AddRectFilled(p, ImVec2(p.x + width * frac, p.y + h), col, 2.0f);
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), IM_COL32(255, 255, 255, 60), 2.0f);
    ImGui::Dummy(ImVec2(width, h));
}

} // namespace

ShellResult draw_title_menu(int /*vw*/, int /*vh*/) {
    ShellResult r{};
    draw_dim_background(0.85f);
    centred_window("##title", ImVec2(440, 420));
    ImGui::Begin("##title", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    dl->AddRectFilled(wp, ImVec2(wp.x + 440, wp.y + 56), IM_COL32(30, 18, 8, 255));
    ImGui::SetCursorPosY(14);
    ImGui::PushFont(nullptr);
    ImGui::SetWindowFontScale(2.4f);
    ImGui::SetCursorPosX((440 - ImGui::CalcTextSize("SAMOSBOR").x) * 0.5f);
    ImGui::Text("SAMOSBOR");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    ImGui::SetCursorPosY(72);
    ImGui::SetCursorPosX((440 - ImGui::CalcTextSize("Timaert chronicles").x) * 0.5f);
    ImGui::TextDisabled("Timaert chronicles");
    ImGui::Dummy(ImVec2(0, 30));
    const ImVec2 sz(360, 44);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(r, &r.startNewGame, "New Game",  sz);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(r, &r.openCustomNewGame, "Custom New Game", sz);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(r, &r.loadGame,     "Load Game", sz);
    ImGui::SetCursorPosX((440 - sz.x) * 0.5f); big_button(r, &r.quit,         "Quit",      sz);
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::SetCursorPosX(16);
    ImGui::TextDisabled("v0.1 — C++ / OpenGL / EnTT port");
    ImGui::End();
    return r;
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

ShellResult draw_load_screen(const SaveSummary& save, int /*vw*/, int /*vh*/) {
    ShellResult r{};
    draw_dim_background(0.85f);
    centred_window("##load", ImVec2(500, 300));
    ImGui::Begin("##load", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);

    ImGui::SetWindowFontScale(1.6f);
    ImGui::SetCursorPosX((500 - ImGui::CalcTextSize("LOAD GAME").x) * 0.5f);
    ImGui::Text("LOAD GAME");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    ImGui::Text("Slot: %s", save.path.empty() ? "save.bin" : save.path.c_str());
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

    ImGui::Dummy(ImVec2(0, 20));
    const ImVec2 sz(180, 36);
    const bool canLoad = save.status == SaveInspectStatus::Ready;
    if (!canLoad) ImGui::BeginDisabled();
    if (ImGui::Button("Load", sz)) r.loadGame = true;
    if (!canLoad) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Back", sz)) r.cancelLoad = true;

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
                                 bool worldReady,
                                 int /*vw*/, int /*vh*/) {
    ShellResult r{};
    draw_dim_background(0.85f);

    // Wide two-column layout: ~360 px parameter column on the left,
    // square preview on the right. Total fits 1280×800 default window.
    const ImVec2 vp = viewport_size();
    const float winW = std::min(vp.x - 40.0f, 980.0f);
    const float winH = std::min(vp.y - 40.0f, 660.0f);
    centred_window("##custom", ImVec2(winW, winH));
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

ShellResult draw_pause_menu(int /*vw*/, int /*vh*/) {
    ShellResult r{};
    draw_dim_background(0.55f);
    centred_window("##pause", ImVec2(360, 360));
    ImGui::Begin("##pause", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::SetCursorPosX((360 - ImGui::CalcTextSize("PAUSED").x) * 0.5f);
    ImGui::Text("PAUSED");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Dummy(ImVec2(0, 16));
    const ImVec2 sz(300, 36);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(r, &r.resume,        "Resume",    sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(r, &r.saveGame,      "Save",      sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(r, &r.loadGame,      "Load",      sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(r, &r.openCodex,     "Codex",     sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(r, &r.returnToTitle, "Title",     sz);
    ImGui::SetCursorPosX((360 - sz.x) * 0.5f); big_button(r, &r.quit,          "Quit",      sz);
    ImGui::End();
    return r;
}

ShellResult draw_death_screen(const GameState& gs, int /*vw*/, int /*vh*/) {
    ShellResult r{};
    draw_dim_background(0.85f);
    centred_window("##dead", ImVec2(420, 240));
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
        gs.worldTime.day, gs.player.ageDays);
    ImGui::Dummy(ImVec2(0, 12));
    const ImVec2 sz(360, 36);
    ImGui::SetCursorPosX((420 - sz.x) * 0.5f); big_button(r, &r.returnToTitle, "Return to Title", sz);
    ImGui::SetCursorPosX((420 - sz.x) * 0.5f); big_button(r, &r.quit,          "Quit",            sz);
    ImGui::End();
    return r;
}

void draw_player_hud(const GameState& gs) {
    // Proto_c-style top status bar — single horizontal strip across the
    // full width of the window. Reads `aim.png` and proto_c HudState
    // (Time / Gold / HP / MP / SP / Items / At-settlement / Coords).
    const auto& cs = gs.player.combatStats;
    const ImVec2 vp = viewport_size();
    const float barH = 36.0f;

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

    // ── Time ───────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.78f, 0.82f, 1.0f, 1.0f),
        "Day %d  %02d:%02d", gs.worldTime.day, gs.worldTime.hour, gs.worldTime.minute);
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
    compact_bar("HP", cs.currentHp, cs.maxHp, IM_COL32(200,  60,  60, 220), 130.0f);
    ImGui::SameLine();
    compact_bar("MP", cs.currentMp, cs.maxMp, IM_COL32( 80, 120, 220, 220), 130.0f);
    ImGui::SameLine();
    compact_bar("SP", cs.currentSp, cs.maxSp, IM_COL32( 80, 200,  90, 220), 130.0f);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "Gold %d", gs.player.gold);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.78f, 0.78f, 0.78f, 1.0f),
                       "Items %d", gs.player.inventory.total());
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

ToolbarResult draw_bottom_toolbar(const GameState& /*gs*/, bool subworldActive) {
    // Proto_c-style bottom command toolbar — full-width strip.
    // Buttons are visual placeholders that emit semantic intents the
    // app loop translates into actions (open inventory, toggle pause,
    // change speed, etc.). Keeps the shell decoupled from gameplay.
    ToolbarResult r;
    const ImVec2 vp = viewport_size();
    const float barH = 44.0f;
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

    auto tbtn = [](const char* glyph, const char* tooltip) {
        const bool clicked = ImGui::Button(glyph, ImVec2(34, 32));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        return clicked;
    };

    if (tbtn("II",  "Pause [Esc]"))                 r.pause      = true; ImGui::SameLine();
    if (tbtn(">",   "Play (1x)"))                   r.speed1     = true; ImGui::SameLine();
    if (tbtn(">>",  "Fast (4x)"))                   r.speed4     = true; ImGui::SameLine();
    if (tbtn("Z",   "Rest until morning"))          r.rest       = true; ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    if (tbtn("Stat", "Stats / progression"))        r.stats      = true; ImGui::SameLine();
    if (tbtn("Inv", "Inventory [I]"))               r.inventory  = true; ImGui::SameLine();
    if (tbtn("Map", "World Map [M]"))               r.map        = true; ImGui::SameLine();
    if (tbtn("Stl", "Settlement [T]"))              r.build      = true; ImGui::SameLine();
    if (tbtn("Qst", "Quests [Q]"))                  r.quests     = true; ImGui::SameLine();
    if (tbtn("Par", "Party / Army"))                r.party      = true; ImGui::SameLine();
    if (tbtn("Eq",  "Equipment"))                   r.equipment  = true; ImGui::SameLine();
    ImGui::TextDisabled("|"); ImGui::SameLine();
    if (tbtn("Cdx", "Codex [C]"))                   r.codex      = true; ImGui::SameLine();
    if (tbtn("Dip", "Diplomacy [K]"))               r.diplomacy  = true; ImGui::SameLine();
    if (tbtn(subworldActive ? "Out" : "In",
             subworldActive ? "Leave subworld [Enter]" : "Enter cell [Enter]"))
        r.toggleSubworld = true; ImGui::SameLine();

    // Right-edge: zoom controls
    const float right = vp.x - 16.0f - (34 + 6) * 2;
    ImGui::SameLine(right);
    if (tbtn("-",   "Zoom out [Wheel]"))            r.zoomOut    = true; ImGui::SameLine();
    if (tbtn("+",   "Zoom in [Wheel]"))             r.zoomIn     = true;

    ImGui::End();
    ImGui::PopStyleVar(4);
    return r;
}

void draw_hint_bar(AppState state, bool subworldActive, int /*vw*/, int /*vh*/) {
    if (state != AppState::Playing) return;
    const char* text = subworldActive
        ? "[Esc] pause   [Arrows] move   [A/LMB] attack   [Space] spell   [Enter] leave   [F] 2D/3D"
        : "[Esc] pause   [Enter] enter cell   [WASD] pan   [I] character   [T] settlement   [Q] quests   [F5] save   [F9] load";
    const ImVec2 vp = ImGui::GetIO().DisplaySize;
    auto* dl = ImGui::GetForegroundDrawList();
    ImVec2 sz = ImGui::CalcTextSize(text);
    float pad = 10.0f;
    float x = (vp.x - sz.x) * 0.5f;
    float y = vp.y - sz.y - 18.0f;
    dl->AddRectFilled(ImVec2(x - pad, y - 4),
                      ImVec2(x + sz.x + pad, y + sz.y + 4),
                      IM_COL32(0, 0, 0, 160), 4.0f);
    dl->AddText(ImVec2(x, y), IM_COL32(220, 220, 220, 230), text);
}

} // namespace sm::ui

#include "ui/map_screen.h"

#include "macro/landmark_iter.h"
#include "macro/landmark_registry.h"
#include "macro/markers.h"
#include "macro/state.h"
#include "ui/landmark_draw.h"
#include "ui/screens.h"

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace sm::ui {

namespace {

ImU32 argb_im(std::uint32_t argb, int alpha) {
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
                    alpha);
}

// The legend swatch speaks the SAME visual language as the map pins: shape
// from the presentation row (ui/landmark_draw.h MiniShape), colour from the
// ONE registry authority — the legend can never drift from the map.
void legend_shape(ImDrawList* dl, ImVec2 c, MiniShape shape, float r,
                  ImU32 col) {
    switch (shape) {
        case MiniShape::Ring:
            dl->AddCircleFilled(c, r, col, 10);
            dl->AddCircle(c, r + 1.5f, col, 10, 1.0f);
            break;
        case MiniShape::Dot:
            dl->AddCircleFilled(c, r * 0.8f, col, 8);
            break;
        case MiniShape::Diamond: {
            const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y),
                                   ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
            dl->AddConvexPolyFilled(pts, 4, col);
            break;
        }
    }
}

// Presentation names for the four marker styles (markers.h owns glyph +
// colour; the label is legend-only chrome).
constexpr const char* kMarkerLegendLabel[4] = {"Quest", "Point of interest",
                                               "Danger", "Waypoint"};

} // namespace

float map_fit_zoom(int viewHPx, int mapH) {
    if (viewHPx <= 0 || mapH <= 0) return 1.0f;
    return float(viewHPx) / float(mapH);
}

void draw_map_screen(MapScreenState& st, GameState& gs, bool* open,
                     int /*viewW*/, int viewH, float scale) {
    if (!open || !*open) return;

    // Explored census — recounted only when knowledge actually moved (the
    // page pauses the world, so that is the opening sweep or a quest reveal).
    if (st.censusRev != gs.knowledge.revision
        && gs.knowledge.has_complete_storage()) {
        unsigned long long n = 0;
        for (const std::uint8_t v : gs.knowledge.data)
            n += (v >= kKnowledgeExplored) ? 1u : 0u;
        st.exploredCells = n;
        st.censusRev = gs.knowledge.revision;
    }
    const double cells = double(gs.knowledge.cell_count());
    const double exploredPct =
        cells > 0.0 ? 100.0 * double(st.exploredCells) / cells : 0.0;

    // Discovered landmarks, by type — the legend lists only what the player's
    // map KNOWS: an uncharted world shows an honest empty legend, and totals
    // never leak how much world is left.
    int discovered[std::size_t(LandmarkType::Count)] = {};
    for_each_landmark(gs, [&](const LandmarkView& lm) {
        if (gs.knowledge.at(lm.x, lm.y) != kKnowledgeUnknown)
            ++discovered[std::size_t(lm.type)];
    });
    bool styleSeen[4] = {};
    for (const Marker& m : gs.markers) {
        if (gs.knowledge.at(int(std::floor(m.x)), int(std::floor(m.y)))
            != kKnowledgeUnknown)
            styleSeen[std::size_t(m.style) & 3u] = true;
    }

    ImGui::SetNextWindowPos(ImVec2(8.0f, kTopStatusBarHeight + 8.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.82f);
    if (ImGui::Begin("World Map", open,
                     ImGuiWindowFlags_AlwaysAutoResize
                         | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_NoCollapse)) {
        ImGui::SetWindowFontScale(scale);
        ImGui::Text("%d x %d   Seed 0x%X", gs.mapW, gs.mapH, gs.worldSeed);
        ImGui::Text("Position %.0f, %.0f", gs.player.x, gs.player.y);
        ImGui::Text("Charted %.2f%%", exploredPct);
        ImGui::Separator();

        // Legend — one loop over the landmark registry, one over the marker
        // styles; swatches reuse the map's own shapes and colours.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rowH = ImGui::GetTextLineHeightWithSpacing();
        const float sw = rowH * 0.45f;
        for (std::size_t t = 0; t < std::size_t(LandmarkType::Count); ++t) {
            if (discovered[t] == 0) continue;
            const LandmarkDef& def = kLandmarks[t];
            const LandmarkDrawRow& row = kLandmarkDraw[t];
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            legend_shape(dl, ImVec2(cur.x + sw, cur.y + rowH * 0.42f),
                         row.mini, sw, argb_im(def.color, 230));
            ImGui::Dummy(ImVec2(sw * 2.4f, rowH * 0.8f));
            ImGui::SameLine();
            ImGui::Text("%.*s", int(def.label.size()), def.label.data());
            ImGui::SameLine();
            ImGui::TextDisabled("%d", discovered[t]);
        }
        for (std::size_t s = 0; s < 4; ++s) {
            if (!styleSeen[s]) continue;
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            dl->AddText(ImVec2(cur.x + sw * 0.4f, cur.y),
                        argb_im(kMarkerColor[s], 255), kMarkerGlyph[s]);
            ImGui::Dummy(ImVec2(sw * 2.4f, rowH * 0.8f));
            ImGui::SameLine();
            ImGui::Text("%s", kMarkerLegendLabel[s]);
        }

        // The player's own pins — the "user_" waypoints the page's
        // double-click toggles (main.cpp). Rename in place, jump the camera
        // to one, or lift it; everything else about a pin (draw style,
        // colour, persistence, the knowledge gate) is the ordinary
        // markers.h layer.
        bool anyPin = false;
        int pinRow = 0;
        for (auto it = gs.markers.begin(); it != gs.markers.end();) {
            Marker& m = *it;
            if (m.style != MarkerStyle::Waypoint
                || m.id.rfind("user_", 0) != 0) {
                ++it;
                continue;
            }
            if (!anyPin) {
                ImGui::Separator();
                anyPin = true;
            }
            ImGui::PushID(++pinRow);
            if (ImGui::SmallButton("\xE2\x97\x8E")) {  // ◎ centre the camera
                st.camX = m.x + 0.5f;
                st.camY = m.y + 0.5f;
            }
            ImGui::SameLine();
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s", m.label.c_str());
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
            char hint[32];
            std::snprintf(hint, sizeof hint, "%d, %d", int(m.x), int(m.y));
            if (ImGui::InputTextWithHint("##pin", hint, buf, sizeof buf))
                m.label = buf;
            ImGui::SameLine();
            const bool lift = ImGui::SmallButton("x");
            ImGui::PopID();
            it = lift ? gs.markers.erase(it) : it + 1;
        }

        ImGui::Separator();
        ImGui::TextDisabled("wheel zoom · drag pan · click travel · 2x-click pin");
        (void)viewH;
    }
    ImGui::End();
}

} // namespace sm::ui

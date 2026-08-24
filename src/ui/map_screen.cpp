#include "ui/map_screen.h"

#include "macro/biomes.h"
#include "macro/landmark_iter.h"
#include "macro/landmark_registry.h"
#include "macro/map_generator.h"
#include "macro/markers.h"
#include "macro/state.h"
#include "ui/landmark_draw.h"
#include "ui/screens.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace sm::ui {

namespace {

// The canonical camera transform (shader: worldPx = (uv-0.5)·view/zoom + cam;
// world +Y = screen UP), torus-wrapped — the page's own copy, because the map
// deliberately shares NO drawing with the world overlay.
inline float wrap_delta(float d, float period) {
    return torus_delta(d, period);   // core/torus.h owns the fold
}
inline ImVec2 to_screen(float wx, float wy, const MapScreenState& st,
                        float zoomL, int viewW, int viewH, int mapW, int mapH) {
    const float dx = wrap_delta(wx - st.camX, float(mapW));
    const float dy = wrap_delta(wy - st.camY, float(mapH));
    return ImVec2(viewW * 0.5f + dx * zoomL, viewH * 0.5f - dy * zoomL);
}

ImU32 argb_im(std::uint32_t argb, int alpha) {
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF,
                    alpha);
}

// The chart's ONE shape vocabulary — primitives only (owner ruling: no
// sprites, no paperdolls on a document). Landmarks use their presentation
// row's MiniShape; the same routine draws the legend swatches, so map and
// legend can never drift.
void chart_shape(ImDrawList* dl, ImVec2 c, MiniShape shape, float r,
                 ImU32 fill, ImU32 rim) {
    switch (shape) {
        case MiniShape::Ring:
            dl->AddCircleFilled(c, r, fill, 16);
            dl->AddCircle(c, r + 1.0f, rim, 16, 1.5f);
            break;
        case MiniShape::Dot:
            dl->AddCircleFilled(c, r * 0.8f, fill, 12);
            break;
        case MiniShape::Diamond: {
            const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y),
                                   ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
            dl->AddConvexPolyFilled(pts, 4, fill);
            dl->AddPolyline(pts, 4, rim, ImDrawFlags_Closed, 1.0f);
            break;
        }
    }
}

// Marker ink on the chart: the waypoint diamond and the POI cross are
// geometry (★◆ are not in the font); quest/danger keep their "!" glyph.
void marker_ink(ImDrawList* dl, ImVec2 c, MarkerStyle style, float r,
                ImU32 col) {
    switch (style) {
        case MarkerStyle::Waypoint: {
            const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y),
                                   ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
            dl->AddConvexPolyFilled(pts, 4, col);
            dl->AddPolyline(pts, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed,
                            1.0f);
            break;
        }
        case MarkerStyle::POI:
            dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 2.0f);
            dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 2.0f);
            break;
        default: {  // quest / danger — a typographic "!" is chart-native
            ImFont* font = ImGui::GetFont();
            const float px = r * 2.2f;
            const ImVec2 ts = font->CalcTextSizeA(px, 3.4e38f, 0.0f, "!");
            const ImVec2 tp(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
            dl->AddText(font, px, ImVec2(tp.x + 1, tp.y + 1),
                        IM_COL32(0, 0, 0, 200), "!");
            dl->AddText(font, px, tp, col, "!");
            break;
        }
    }
}

// Presentation names for the four marker styles (markers.h owns glyph +
// colour; the label is legend-only chrome).
constexpr const char* kMarkerLegendLabel[4] = {"Quest", "Point of interest",
                                               "Danger", "Waypoint"};

// (chart_biome lived here until 2026-08-24 — the twin of macro_overlay's
// sampler, and the two disagreed at the coast, canon-audit H10. The one
// cascade is map_generator.h biome_at_cell.)

} // namespace

float map_fit_zoom(int viewHPx, int mapH) {
    if (viewHPx <= 0 || mapH <= 0) return 1.0f;
    return float(viewHPx) / float(mapH);
}

void draw_map_screen(MapScreenState& st, GameState& gs,
                     const TerrainData& terrain, bool* open,
                     int viewW, int viewH, float zoomLogical, float scale) {
    if (!open || !*open) return;
    const int mapW = gs.mapW, mapH = gs.mapH;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

    // ── The page's own cursor: hover cell, tooltip, click = pin toggle.
    // The map is a DOCUMENT — a click annotates it and commands nothing in
    // the world (no marches, no settlement panels; the world overlay is not
    // even drawn while the page is open).
    if (!io.WantCaptureMouse && zoomLogical > 0.0f) {
        const ImVec2 mp = io.MousePos;
        if (mp.x >= 0 && mp.y >= 0 && mp.x < float(viewW)
            && mp.y < float(viewH)) {
            const float wx = st.camX + (mp.x - viewW * 0.5f) / zoomLogical;
            const float wy = st.camY - (mp.y - viewH * 0.5f) / zoomLogical;
            const int cx = ((int(std::floor(wx)) % mapW) + mapW) % mapW;
            const int cy = ((int(std::floor(wy)) % mapH) + mapH) % mapH;
            const std::uint8_t know = gs.knowledge.at(cx, cy);

            // Hover rect (cell top = wy+1 after the Y flip).
            const ImVec2 tl = to_screen(float(cx), float(cy) + 1.0f, st,
                                        zoomLogical, viewW, viewH, mapW, mapH);
            const ImVec2 br = to_screen(float(cx) + 1.0f, float(cy), st,
                                        zoomLogical, viewW, viewH, mapW, mapH);
            dl->AddRect(tl, br, IM_COL32(255, 255, 255, 160), 0.0f, 0, 1.5f);

            ImGui::BeginTooltip();
            ImGui::Text("(%d, %d)", cx, cy);
            if (know == kKnowledgeUnknown) {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1),
                                   "Uncharted");
            } else {
                const Biome b = biome_at_cell(terrain, cx, cy);
                ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, 1), "%s",
                                   kBiomes[std::size_t(b)].name);
                for_each_landmark(gs, [&](const LandmarkView& lm) {
                    if (lm.x == cx && lm.y == cy && lm.name[0])
                        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1), "%s",
                                           lm.name);
                });
            }
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && know != kKnowledgeUnknown) {
                char pinId[32];
                std::snprintf(pinId, sizeof pinId, "user_%d_%d", cx, cy);
                if (!remove_marker(gs.markers, pinId)) {
                    add_marker(gs.markers, pinId, MarkerStyle::Waypoint,
                               float(cx), float(cy));
                }
            }
        }
    }

    // ── Landmarks — primitives only: the presentation row's MiniShape at
    // the registry's ONE colour; ashen when depleted, faded when merely
    // remembered. The mark grows to half its cell when zoomed in, floored
    // at the row's legend radius so it never dissolves at world-fit.
    for_each_landmark(gs, [&](const LandmarkView& lm) {
        const std::uint8_t know = gs.knowledge.at(lm.x, lm.y);
        if (know == kKnowledgeUnknown) return;
        const bool faded = know < kKnowledgeVisible;
        const LandmarkDrawRow& row = landmark_draw(lm.type);
        const ImVec2 p = to_screen(float(lm.x) + 0.5f, float(lm.y) + 0.5f, st,
                                   zoomLogical, viewW, viewH, mapW, mapH);
        if (p.x < -64 || p.x > viewW + 64 || p.y < -64 || p.y > viewH + 64)
            return;
        const float r = std::max(row.miniR * 2.0f, zoomLogical * 0.5f);
        const std::uint32_t argb = landmark_def(lm.type).color;
        // Depleted = half strength, remembered = reduced alpha — the same
        // derivations the old panel used; ONE colour authority throughout.
        const std::uint32_t base = lm.depleted
            ? ((argb >> 1) & 0x7F7F7Fu) : argb;
        const ImU32 fill = argb_im(base, faded ? 140 : 230);
        const ImU32 rim = argb_im(base >> 2 & 0x3F3F3Fu, 255);
        chart_shape(dl, p, row.mini, r, fill, rim);
        if (row.labelZoom > 0.0f && zoomLogical >= row.labelZoom
            && lm.name[0]) {
            const ImVec2 ts = ImGui::CalcTextSize(lm.name);
            const ImVec2 tp(p.x - ts.x * 0.5f, p.y - r - ts.y - 3.0f);
            dl->AddText(tp, IM_COL32(20, 22, 26, 220), lm.name);
            dl->AddText(ImVec2(tp.x - 1, tp.y - 1),
                        IM_COL32(235, 232, 220, faded ? 150 : 255), lm.name);
        }
    });

    // ── Marker ink (kMarkerSurface & Map): waypoints and any world signal
    // that also prints on the chart. Drawn at the cell, primitive shapes.
    for (const Marker& m : gs.markers) {
        const std::size_t si = std::size_t(m.style) & 3u;
        if (!(kMarkerSurface[si] & kMarkerOnMap)) continue;
        if (gs.knowledge.at(int(std::floor(m.x)), int(std::floor(m.y)))
            == kKnowledgeUnknown)
            continue;
        const ImVec2 p = to_screen(m.x + 0.5f, m.y + 0.5f, st, zoomLogical,
                                   viewW, viewH, mapW, mapH);
        if (p.x < -32 || p.x > viewW + 32 || p.y < -32 || p.y > viewH + 32)
            continue;
        const float r = std::max(4.0f, zoomLogical * 0.4f);
        marker_ink(dl, p, m.style, r, argb_im(kMarkerColor[si], 255));
    }

    // ── The player — a position mark, not a figure: cyan ring + crosshair
    // (the old panel's mark, at page scale).
    {
        const ImVec2 p = to_screen(gs.player.x + 0.5f, gs.player.y + 0.5f, st,
                                   zoomLogical, viewW, viewH, mapW, mapH);
        const ImU32 cy = IM_COL32(80, 240, 240, 255);
        const float r = std::max(4.0f, zoomLogical * 0.35f);
        dl->AddCircle(p, r, cy, 16, 2.0f);
        dl->AddLine(ImVec2(p.x - r * 1.8f, p.y), ImVec2(p.x + r * 1.8f, p.y),
                    cy, 1.2f);
        dl->AddLine(ImVec2(p.x, p.y - r * 1.8f), ImVec2(p.x, p.y + r * 1.8f),
                    cy, 1.2f);
    }

    // ── Chrome: header readout + legend + the pin list.
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

    int discovered[std::size_t(LandmarkType::Count)] = {};
    for_each_landmark(gs, [&](const LandmarkView& lm) {
        if (gs.knowledge.at(lm.x, lm.y) != kKnowledgeUnknown)
            ++discovered[std::size_t(lm.type)];
    });
    bool styleSeen[4] = {};
    for (const Marker& m : gs.markers) {
        if ((kMarkerSurface[std::size_t(m.style) & 3u] & kMarkerOnMap)
            && gs.knowledge.at(int(std::floor(m.x)), int(std::floor(m.y)))
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

        ImDrawList* wdl = ImGui::GetWindowDrawList();
        const float rowH = ImGui::GetTextLineHeightWithSpacing();
        const float sw = rowH * 0.45f;
        for (std::size_t t = 0; t < std::size_t(LandmarkType::Count); ++t) {
            if (discovered[t] == 0) continue;
            const LandmarkDef& def = kLandmarks[t];
            const LandmarkDrawRow& row = kLandmarkDraw[t];
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            chart_shape(wdl, ImVec2(cur.x + sw, cur.y + rowH * 0.42f),
                        row.mini, sw, argb_im(def.color, 230),
                        argb_im((def.color >> 2) & 0x3F3F3Fu, 255));
            ImGui::Dummy(ImVec2(sw * 2.4f, rowH * 0.8f));
            ImGui::SameLine();
            ImGui::Text("%.*s", int(def.label.size()), def.label.data());
            ImGui::SameLine();
            ImGui::TextDisabled("%d", discovered[t]);
        }
        for (std::size_t s = 0; s < 4; ++s) {
            if (!styleSeen[s]) continue;
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            marker_ink(wdl, ImVec2(cur.x + sw, cur.y + rowH * 0.42f),
                       MarkerStyle(s), sw, argb_im(kMarkerColor[s], 255));
            ImGui::Dummy(ImVec2(sw * 2.4f, rowH * 0.8f));
            ImGui::SameLine();
            ImGui::Text("%s", kMarkerLegendLabel[s]);
        }

        // The player's own pins: rename in place, jump the camera, or lift.
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
            if (ImGui::SmallButton("go")) {
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
        ImGui::TextDisabled("wheel zoom · drag pan · click pin");
    }
    ImGui::End();
}

} // namespace sm::ui

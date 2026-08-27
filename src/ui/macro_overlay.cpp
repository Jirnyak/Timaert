// Macro-map overlay — debug-style markers for settlements, villages,
// macro NPCs and the player. Uses ImGui background draw list (drawn
// after the map shader, before UI panels).
//
// Coordinate system: world units = macro cell coords. Shader transform:
//   worldPx = (uv - 0.5) * viewSize / zoom + cam
// Inverse:
//   screenPx = viewSize/2 + (worldPx - cam) * zoom
// Torus wrap: pick the wrapped copy of `worldPx` whose distance to `cam`
// is minimal so off-screen markers don't smear across the seam.

#include "ui/macro_overlay.h"
#include "ui/trade_widgets.h"
#include "ecs/world.h"
#include "ecs/components.h"
#include "macro/state.h"
#include "macro/landmark_iter.h"
#include "ui/landmark_draw.h"
#include "macro/markers.h"
#include "macro/tree_layer.h"
#include "macro/npc.h"
#include "macro/faction.h"
#include "macro/npc_spawn.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"
#include "assets/sprite_atlas.h"

#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace sm::ui {

namespace {

// (was a private one-period fold — core/torus.h owns the real one)
inline float wrap_delta(float d, float period) {
    return torus_delta(d, period);
}

inline ImVec2 world_to_screen(float wx, float wy, float camX, float camY,
                              float zoom, int viewW, int viewH,
                              int mapW, int mapH) {
    float dx = wrap_delta(wx - camX, float(mapW));
    float dy = wrap_delta(wy - camY, float(mapH));
    // Shader convention: world +Y = screen +Y (i.e. UP). ImGui: +Y = DOWN.
    return ImVec2(viewW * 0.5f + dx * zoom, viewH * 0.5f - dy * zoom);
}

inline bool on_screen(const ImVec2& p, int viewW, int viewH, float pad) {
    return p.x > -pad && p.x < viewW + pad && p.y > -pad && p.y < viewH + pad;
}



// (A private biome sampler lived here until 2026-08-24 — no bounds check, no
// mask, and a hard-coded 0.40 sea level at its call site. The one cascade is
// map_generator.h biome_at_cell.)

const char* feature_name(FeatureType f) {
    switch (f) {
        case FT_Road:     return "Road";
        case FT_DirtRoad: return "Dirt Road";
        default:          return "";
    }
}

ImU32 npc_color(NPCType t) {
    switch (t) {
        case NPCType::Peasant:    return IM_COL32(220, 200, 160, 255);
        case NPCType::Woodcutter: return IM_COL32( 90, 150,  70, 255);
        case NPCType::Merchant:   return IM_COL32(240, 200,  80, 255);
        case NPCType::Caravan:    return IM_COL32(180, 140,  80, 255);
        case NPCType::Bandit:     return IM_COL32(220,  60,  60, 255);
        case NPCType::Guard:      return IM_COL32( 80, 140, 220, 255);
        case NPCType::Witch:      return IM_COL32(180, 100, 200, 255);
        case NPCType::Sorceress:  return IM_COL32(120, 200, 230, 255);
        default:                  return IM_COL32(200, 200, 200, 255);
    }
}

// NPC type → its picture. The kind's own row says which; this used to be a
// switch here, which is exactly the if-chain a data-driven registry forbids —
// a new NPC kind had to remember to come and edit the map's drawing code.
SpriteId npc_sprite(NPCType t) {
    return npc_def(t).sprite;
}

// Draw a centered sprite at screen pos `c`, sized `pixSize` (square).
// Falls back to a coloured filled circle if the asset is missing.
//
// 256-px source sprites are authored with a 128-px CENTER (the cell)
// and 64-px borders that overlap into the four neighbouring cells:
//
//          64
//      +--------+
//   64 |  cell  | 64
//      +--------+
//          64
//
// So `pixSize == zoom * 2` makes the inner half land exactly on the
// owning cell at any zoom level. The whole sprite is centered on `c`
// (not bottom-anchored — that would push every figure half a sprite
// off the cell it belongs to and produce the diagonal offset shift
// we were seeing).
void draw_sprite(ImDrawList* dl, ImVec2 c, SpriteId id, float pixSize,
                 ImU32 fallbackCol, ImU32 tint = IM_COL32_WHITE) {
    const Sprite* s = sprite_get(id);
    const float r = pixSize * 0.5f;
    if (!s) {
        dl->AddCircleFilled(c, std::max(2.0f, r * 0.4f), fallbackCol, 12);
        return;
    }
    ImVec2 tl(c.x - r, c.y - r);
    ImVec2 br(c.x + r, c.y + r);
    dl->AddImage(s->tex, tl, br, ImVec2(0, 0), ImVec2(1, 1), tint);
}

// How dark a FIGURE on the map stands at this hour. The ground has its own
// celestial light in macro.frag; a figure is drawn over it by ImGui and would
// otherwise keep full daylight colour at midnight — one law, two renderers.
float figure_night_darken(const WorldTime& time) {
    int minutes = time.hour() * 60 + time.minute();
    minutes %= 24 * 60;
    if (minutes < 0) minutes += 24 * 60;

    const float progress = float(minutes) / float(24 * 60);
    if (progress < 0.2f || progress > 0.9f) return 1.0f;
    if (progress < 0.35f) return 1.0f - (progress - 0.2f) / 0.15f;
    if (progress < 0.75f) return 0.0f;
    return (progress - 0.75f) / 0.15f;
}

ImU32 figure_tint_for_time(const WorldTime& time) {
    const float mix = figure_night_darken(time) * 0.82f;
    if (mix <= 0.0f) return IM_COL32(255, 255, 255, 255);
    auto channel = [mix](float tint) {
        const float v = std::clamp(1.0f + (tint - 1.0f) * mix, 0.0f, 1.0f);
        return int(v * 255.0f + 0.5f);
    };
    return IM_COL32(channel(0.05f), channel(0.05f), channel(0.15f), 255);
}

// Universal landmark scale. `zoom` is pixels-per-cell (range [4, 96]); a
// landmark sprite is world-anchored at kLandmarkCellSpan cells wide, so it
// shrinks with the map on zoom-out rather than growing relative to the terrain.
//
// The readable-size floor (minPx) keeps icons legible in the mid-zoom range,
// but that floor is itself capped at kLandmarkMaxCells cells: without the cap,
// at deep zoom-out the fixed 28-px floor spanned ~7 cells (4-px cells) and read
// as a giant blob — the reported "cities grow on zoom-out" bug. Capping the
// floor to a few cells lets the icon keep shrinking with the map instead of
// plateauing into a blob, while staying readable wherever the map is dense
// enough to warrant it. maxPx bounds the zoom-in end so a city never devours
// the viewport.
inline constexpr float kLandmarkCellSpan = 2.0f;  // sprite covers this many cells
inline constexpr float kLandmarkMaxCells = 3.0f;  // readable floor may not exceed this
inline float landmark_size(float zoom, float minPx, float maxPx) {
    const float world   = zoom * kLandmarkCellSpan;
    const float floorPx  = std::min(minPx, zoom * kLandmarkMaxCells);
    return std::clamp(world, floorPx, maxPx);
}

// markers.h stores colours as 0xAARRGGBB (CSS/TS order); ImGui's ImU32 is
// packed through IM_COL32. Repack via the macro so a gold quest pin stays
// gold regardless of ImGui's channel-shift configuration.
inline ImU32 marker_imcol(std::uint32_t argb) {
    return IM_COL32((argb >> 16) & 0xFFu, (argb >> 8) & 0xFFu,
                    argb & 0xFFu, (argb >> 24) & 0xFFu);
}

} // namespace

void draw_macro_overlay(GameState& gs, ecs::World& w,
                        const TerrainData& terrain,
                        const FeatureLayer& features,
                        MacroCursor& cursor,
                        float camX, float camY, float zoom,
                        int viewW, int viewH, int mapW, int mapH,
                        bool showMarkers,
                        bool showQuestMarkers, float questMarkerScale,
                        const TreeLayer* treeLayer) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImU32 figureTint = figure_tint_for_time(gs.worldTime);

    // ── Mouse → cell. `viewW`/`viewH` are logical points (matching
    // ImGui's coordinate system); `zoom` is logical-points-per-cell.
    // The shader uses drawable pixels and drawable-px-per-cell, but
    // both unprojections collapse to the same camera math when fed
    // their respective space.
    cursor.hoverValid = false;
    cursor.hoverSettlementId = -1;
    cursor.clickedSettlementId = -1;
    if (!io.WantCaptureMouse) {
        ImVec2 m = io.MousePos;
        if (m.x >= 0 && m.y >= 0 && m.x < float(viewW) && m.y < float(viewH)
            && zoom > 0.0f) {
            float wx = camX + (m.x - viewW * 0.5f) / zoom;
            // Y flip: ImGui +Y is down, world +Y is up.
            float wy = camY - (m.y - viewH * 0.5f) / zoom;
            int cx = int(std::floor(wx));
            int cy = int(std::floor(wy));
            cx = ((cx % mapW) + mapW) % mapW;
            cy = ((cy % mapH) + mapH) % mapH;
            cursor.hoverValid = true;
            cursor.hoverX = cx;
            cursor.hoverY = cy;

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                cursor.requestPath = true;
                cursor.requestX = cx;
                cursor.requestY = cy;
            }
        }
    }

    // ── Auto-travel polyline (drawn underneath everything else). Chrome:
    // suppressed when the overlay's hover/path markers are toggled off.
    if (showMarkers && cursor.path.size() > cursor.pathIdx + 1) {
        const ImU32 pathCol = IM_COL32(120, 220, 255, 200);
        ImVec2 prev = world_to_screen(float(cursor.path[cursor.pathIdx].x) + 0.5f,
                                      float(cursor.path[cursor.pathIdx].y) + 0.5f,
                                      camX, camY, zoom, viewW, viewH, mapW, mapH);
        for (std::size_t i = cursor.pathIdx + 1; i < cursor.path.size(); ++i) {
            ImVec2 p = world_to_screen(float(cursor.path[i].x) + 0.5f,
                                       float(cursor.path[i].y) + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            // Only draw segments that don't cross the torus seam.
            if (std::fabs(p.x - prev.x) < float(viewW) * 0.5f
                && std::fabs(p.y - prev.y) < float(viewH) * 0.5f) {
                dl->AddLine(prev, p, pathCol, 2.0f);
            }
            prev = p;
        }
        // Endpoint flag.
        const auto& endP = cursor.path.back();
        ImVec2 ep = world_to_screen(float(endP.x) + 0.5f, float(endP.y) + 0.5f,
                                    camX, camY, zoom, viewW, viewH, mapW, mapH);
        dl->AddCircle(ep, 6.0f, IM_COL32(120, 220, 255, 255), 16, 2.0f);
    }

    // ── Hover-cell highlight + tooltip (chrome) and settlement pick (input).
    if (cursor.hoverValid) {
        // Landmark under the cursor is resolved ALWAYS — click-to-select
        // must keep working even when the overlay chrome is hidden. One
        // visitor pass names EVERY kind (first hit wins, in the visitor's
        // resolve_context priority order) and picks the settlement id for
        // the click when the hit is a city.
        const char* landmark = "";
        int hoverSettlementId = -1;
        for_each_landmark(gs, [&](const LandmarkView& lm) {
            if (lm.x != cursor.hoverX || lm.y != cursor.hoverY) return;
            if (!landmark[0]) landmark = lm.name;
            if (lm.type == LandmarkType::City && hoverSettlementId < 0)
                hoverSettlementId = lm.id;
        });

        if (showMarkers) {
            // After the Y flip the cell's screen rect has top = wy+1, bottom = wy.
            ImVec2 tl = world_to_screen(float(cursor.hoverX),         float(cursor.hoverY) + 1.0f,
                                        camX, camY, zoom, viewW, viewH, mapW, mapH);
            ImVec2 br = world_to_screen(float(cursor.hoverX) + 1.0f,  float(cursor.hoverY),
                                        camX, camY, zoom, viewW, viewH, mapW, mapH);
            dl->AddRect(tl, br, IM_COL32(255, 255, 255, 180), 0.0f, 0, 1.5f);

            // Tooltip: biome / feature / landmark / coords — but only what
            // the player KNOWS (macro/knowledge.h). Terra incognita answers
            // its coordinates and nothing else.
            const std::uint8_t hoverKnow =
                gs.knowledge.at(cursor.hoverX, cursor.hoverY);
            ImGui::BeginTooltip();
            ImGui::Text("(%d, %d)", cursor.hoverX, cursor.hoverY);
            if (hoverKnow == kKnowledgeUnknown) {
                ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1),
                                   "Uncharted");
            } else {
                Biome b = biome_at_cell(terrain, cursor.hoverX,
                                        cursor.hoverY);
                FeatureType f = features.at(cursor.hoverX, cursor.hoverY);
                ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, 1), "%s",
                                   kBiomes[b].name);
                const char* fn = feature_name(f);
                if (fn[0])
                    ImGui::TextColored(ImVec4(1.00f, 0.75f, 0.40f, 1), "%s", fn);
                // Forest is a count class, not a feature: label it from the
                // tree-count layer, with the live number (рубка visibly ticks
                // it) — a LIVE reading, so it speaks only inside current
                // sight; memory keeps the map, not this season's tree count.
                if (hoverKnow == kKnowledgeVisible && treeLayer
                    && treeLayer->has_complete_storage()) {
                    const int tc =
                        int(treeLayer->at(cursor.hoverX, cursor.hoverY));
                    if (is_forest_cell(tc)) {
                        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1),
                                           "Forest (%d trees)", tc);
                    } else if (tc > 0) {
                        ImGui::TextColored(ImVec4(0.45f, 0.75f, 0.45f, 1),
                                           "Trees: %d", tc);
                    }
                }
                if (landmark[0])
                    ImGui::TextColored(ImVec4(1.00f, 0.90f, 0.40f, 1), "%s",
                                       landmark);
            }
            ImGui::EndTooltip();
        }

        cursor.hoverSettlementId = hoverSettlementId;
        if (hoverSettlementId >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            cursor.clickedSettlementId = hoverSettlementId;
        }
    }


    // Landmarks — ONE data-driven loop: the visitor enumerates, the
    // presentation row (ui/landmark_draw.h) sizes and gates, the sprite
    // variant IS the state (a consumed spire draws its dark tower — same
    // rule as the night glow, macro_lighting.cpp), and the glyph-circle
    // fallback takes its colour from the ONE authority, the registry row.
    for_each_landmark(gs, [&](const LandmarkView& lm) {
        // The knowledge law (macro/knowledge.h): terra incognita hides even
        // the glyph; an Explored landmark is MEMORY and draws faded — same
        // sprite, same ONE registry colour, alpha alone says "remembered".
        const std::uint8_t know = gs.knowledge.at(lm.x, lm.y);
        if (know == kKnowledgeUnknown) return;
        const bool faded = know < kKnowledgeVisible;
        const LandmarkDrawRow& row = landmark_draw(lm.type);
        if (row.minZoom > 0.0f && zoom < row.minZoom) return;
        ImVec2 p = world_to_screen(float(lm.x) + 0.5f, float(lm.y) + 0.5f,
                                   camX, camY, zoom, viewW, viewH, mapW, mapH);
        if (!on_screen(p, viewW, viewH, 128.0f)) return;
        const float size = landmark_size(zoom, row.basePx, row.maxPx);
        const std::uint32_t argb = landmark_def(lm.type).color;
        const ImU32 fallback = IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF,
                                        argb & 0xFF, faded ? 120 : 230);
        draw_sprite(dl, p, lm.depleted ? row.spriteDepleted : row.sprite,
                    size, fallback,
                    faded ? IM_COL32(255, 255, 255, 120) : IM_COL32_WHITE);
        if (row.labelZoom > 0.0f && zoom >= row.labelZoom && lm.name[0]) {
            ImVec2 ts = ImGui::CalcTextSize(lm.name);
            ImVec2 tp(p.x - ts.x * 0.5f, p.y - size * 0.5f - ts.y - 2.0f);
            dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 1),
                              ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 1),
                              IM_COL32(0, 0, 0, faded ? 90 : 140), 2.0f);
            dl->AddText(tp, IM_COL32(255, 245, 200, faded ? 140 : 255),
                        lm.name);
        }
    });

    // NPCs — real character PNGs, sized to fit ONE cell (NPCs are mobile
    // entities, not landmarks; the 256-px sprite is rendered to occupy
    // exactly the cell footprint = `zoom` pixels). Squad NPCs draw a
    // 3-figure cluster around the cell centre. Hidden at low zoom so
    // the procedural feature shaders (trees / mountains / roads) read
    // cleanly — at zoom < 10 px/cell a 256-px sprite shrinks to a
    // monochromatic blob that visually competes with the GLSL features.
    if (zoom >= 10.0f) {
        auto view = w.reg.view<ecs::Position, ecs::NPCKind, ecs::Health>(
            entt::exclude<ecs::Dead, ecs::PlayerTag>);  // possessed macro NPC = player, not a figure (Inc 5e-2)
        for (auto e : view) {
            const auto& pos  = view.get<ecs::Position>(e);
            const auto& kind = view.get<ecs::NPCKind>(e);
            const auto& hp   = view.get<ecs::Health>(e);
            if (hp.hp <= 0.0f) continue;
            const ecs::VisualPos* visual = w.reg.try_get<ecs::VisualPos>(e);
            const float drawX = visual ? visual->vx : pos.x;
            const float drawY = visual ? visual->vy : pos.y;
            // A living walker is the WORLD, not the map: memory keeps no
            // people. Only cells in the player's current sight draw theirs.
            if (gs.knowledge.at(int(std::floor(drawX)), int(std::floor(drawY)))
                != kKnowledgeVisible)
                continue;
            // +0.5 so the sprite sits at the CELL CENTRE (the GLSL grid
            // bounds cells at integer worldPx, so cell (X,Y) spans
            // [X..X+1, Y..Y+1] and its centre is at X+0.5, Y+0.5).
            ImVec2 p = world_to_screen(drawX + 0.5f, drawY + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            if (!on_screen(p, viewW, viewH, 64.0f)) continue;
            const NPCType t = NPCType(kind.type);
            const SpriteId sid = npc_sprite(t);
            const ImU32 col = npc_color(t);
            // One cell wide; clamp so it stays readable on the world map
            // and never devours surrounding cells when zoomed in.
            const float size = std::clamp(zoom, 12.0f, 56.0f);
            // ONE walker, ONE sprite — a squad is not drawn as a crowd of
            // figures. Its kind IS its picture; how many souls march under it
            // is the roster's business, not the map's (owner, 2026-08-20).
            draw_sprite(dl, p, sid, size, col, figureTint);
        }
    }

    // ── Universal world markers (quest "!", POI ★, danger, waypoint ◆). A
    // style-indexed glyph pin floats above the target cell, ALWAYS visible
    // (no zoom gate — a quest pin must read from across the map) and torus-
    // culled. Purely data-driven: producers push into gs.markers, this pass
    // renders whatever is there with no per-style branching. Gated + sized by
    // the universal UI settings (QuestMarkers element).
    if (showQuestMarkers && !gs.markers.empty()) {
        ImFont* font = ImGui::GetFont();
        const float glyphPx = std::clamp(zoom * 1.8f, 18.0f, 46.0f)
                            * std::clamp(questMarkerScale, 0.4f, 3.0f);
        const float r = glyphPx * 0.72f;                       // pin disc radius
        for (const auto& m : gs.markers) {
            // Surface law (markers.h kMarkerSurface): the map is an IMAGE of
            // the world — a waypoint is ink on the chart and never floats in
            // the world; quest/POI/danger signal on both.
            const std::size_t si = std::size_t(m.style) & 3u;
            if (!(kMarkerSurface[si] & kMarkerOnWorld))
                continue;
            // Pins live on the map the player HAS: an uncharted cell shows
            // nothing — a quest that wants to point into the dark reveals its
            // target area first (reveal_area).
            if (gs.knowledge.at(int(std::floor(m.x)), int(std::floor(m.y)))
                == kKnowledgeUnknown)
                continue;
            ImVec2 p = world_to_screen(m.x + 0.5f, m.y + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            if (!on_screen(p, viewW, viewH, 96.0f)) continue;
            const ImU32 col   = marker_imcol(kMarkerColor[si]);
            // Float clear of anything on the cell (a city sprite's half-height).
            const float above = landmark_size(zoom, 28.0f, 192.0f) * 0.5f + r + 4.0f;
            const ImVec2 c(p.x, p.y - above);                  // pin centre
            // Dark disc + coloured ring so the pin reads over any biome.
            dl->AddCircleFilled(c, r, IM_COL32(0, 0, 0, 150), 20);
            dl->AddCircle(c, r, col, 20, 2.0f);
            // The "!" styles are text; ★ and ◆ are NOT in ImGui's default
            // font (they fell back to "?") — those two draw their shape as
            // GEOMETRY, which also matches the map page legend's swatches.
            if (m.style == MarkerStyle::Waypoint) {
                const float dr = r * 0.55f;
                const ImVec2 pts[4] = {ImVec2(c.x, c.y - dr),
                                       ImVec2(c.x + dr, c.y),
                                       ImVec2(c.x, c.y + dr),
                                       ImVec2(c.x - dr, c.y)};
                dl->AddConvexPolyFilled(pts, 4, col);
            } else if (m.style == MarkerStyle::POI) {
                const float lr = r * 0.62f, sr = r * 0.26f;
                dl->AddLine(ImVec2(c.x - lr, c.y), ImVec2(c.x + lr, c.y), col, 2.0f);
                dl->AddLine(ImVec2(c.x, c.y - lr), ImVec2(c.x, c.y + lr), col, 2.0f);
                dl->AddLine(ImVec2(c.x - sr, c.y - sr), ImVec2(c.x + sr, c.y + sr), col, 1.5f);
                dl->AddLine(ImVec2(c.x - sr, c.y + sr), ImVec2(c.x + sr, c.y - sr), col, 1.5f);
            } else {
                const char* glyph = kMarkerGlyph[si];
                const ImVec2 ts = font->CalcTextSizeA(glyphPx, FLT_MAX, 0.0f, glyph);
                const ImVec2 tp(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
                dl->AddText(font, glyphPx, ImVec2(tp.x + 1.0f, tp.y + 1.0f),
                            IM_COL32(0, 0, 0, 190), glyph);    // shadow
                dl->AddText(font, glyphPx, tp, col, glyph);
            }
            // Label beneath the pin once zoomed in enough to read it.
            if (zoom >= 6.0f && !m.label.empty()) {
                const ImVec2 ls = ImGui::CalcTextSize(m.label.c_str());
                const ImVec2 lp(c.x - ls.x * 0.5f, c.y + r + 2.0f);
                dl->AddRectFilled(ImVec2(lp.x - 3.0f, lp.y - 1.0f),
                                  ImVec2(lp.x + ls.x + 3.0f, lp.y + ls.y + 1.0f),
                                  IM_COL32(0, 0, 0, 140), 2.0f);
                dl->AddText(lp, col, m.label.c_str());
            }
        }
    }

    // Player sprite — sized to fit one cell like other mobile entities.
    // Half-cell offset matches the landmark / NPC convention
    // (cell centre = X+0.5).
    {
        ImVec2 p = world_to_screen(gs.player.x + 0.5f, gs.player.y + 0.5f,
                                   camX, camY, zoom, viewW, viewH, mapW, mapH);
        const float size = std::clamp(zoom * 1.1f, 14.0f, 64.0f);
        draw_sprite(dl, p, SpriteId::Peasant, size,
                    IM_COL32(255, 255, 255, 255), figureTint);
    }
}

std::size_t step_macro_walk(GameState& gs, MacroCursor& cursor, float dt,
                            float cellsPerSec,
                            MacroWalkReachedFn onReached,
                            void* onReachedUser) {
    if (cursor.path.empty() || cursor.pathIdx >= cursor.path.size()) return 0u;

    const int W = gs.mapW;
    const int H = gs.mapH;
    // wrap_delta: the file-scope helper at the top of this TU.

    float remaining = cellsPerSec * dt;
    std::size_t reached = 0u;
    while (remaining > 0.0f && cursor.pathIdx < cursor.path.size()) {
        const auto& nxt = cursor.path[cursor.pathIdx];
        // Player position is stored in the SAME integer-cell convention
        // as every other entity (NPCs, landmarks). The +0.5 cell-centre
        // offset is applied ONLY at render time. Do not pre-bake it into
        // the stored coord — that would put the player at X+1.0 once the
        // renderer adds its own +0.5.
        float tx = float(nxt.x);
        float ty = float(nxt.y);
        float dx = wrap_delta(tx - gs.player.x, float(W));
        float dy = wrap_delta(ty - gs.player.y, float(H));
        float d  = std::sqrt(dx * dx + dy * dy);
        if (d <= remaining) {
            gs.player.x = tx;
            gs.player.y = ty;
            remaining -= d;
            ++cursor.pathIdx;
            ++reached;
            if (onReached) {
                onReached(onReachedUser, nxt.x, nxt.y);
            }
        } else {
            float k = remaining / std::max(d, 1e-6f);
            gs.player.x += dx * k;
            gs.player.y += dy * k;
            remaining = 0.0f;
        }
        if (gs.player.x < 0)             gs.player.x += float(W);
        if (gs.player.x >= float(W))     gs.player.x -= float(W);
        if (gs.player.y < 0)             gs.player.y += float(H);
        if (gs.player.y >= float(H))     gs.player.y -= float(H);
    }

    if (cursor.pathIdx >= cursor.path.size()) {
        cursor.path.clear();
        cursor.pathIdx = 0;
    }
    return reached;
}

// ── Nearby-NPC proximity panel ─────────────────────────────────────────
//
// Mirrors `src/screens/NpcProximityPanel.svelte`: a vertical stack of
// clickable badges anchored to the right edge, one per alive NPC on
// the player's cell or any of the 8 adjacent cells (Chebyshev ≤ 1,
// torus-wrapped). Each badge shows the NPC sprite, name, type+level,
// faction tag (coloured by faction), HP, and a direction chip.
//
// Native actions live in-panel until the full InteractionOverlay shell exists.
// Talk uses the NPC line pool. Trade uses real inventory stacks. Attack returns
// the selected macro NPC so the app can route it into normal subworld combat.

namespace {

// Eight-way compass label, identical to TS `directionLabel(dx, dy)`
// in GameScreen.svelte. Writes into row-local storage so multiple NPC
// rows cannot alias the same static buffer.
void direction_label(int dx, int dy, char (&buf)[5]) {
    if (dx == 0 && dy == 0) {
        buf[0] = 'H';
        buf[1] = 'e';
        buf[2] = 'r';
        buf[3] = 'e';
        buf[4] = '\0';
        return;
    }
    const char* ns = (dy > 0) ? "N" : (dy < 0 ? "S" : "");
    const char* ew = (dx > 0) ? "E" : (dx < 0 ? "W" : "");
    int i = 0;
    while (*ns) buf[i++] = *ns++;
    while (*ew) buf[i++] = *ew++;
    buf[i] = '\0';
    if (!buf[0]) {
        buf[0] = 'H';
        buf[1] = 'e';
        buf[2] = 'r';
        buf[3] = 'e';
        buf[4] = '\0';
    }
}

int proximity_row_rank(int dx, int dy) {
    return std::abs(dx) + std::abs(dy);
}

NPCType npc_type_or_default(std::uint16_t raw) {
    if (raw < static_cast<std::uint16_t>(NPCType::Count)) {
        return static_cast<NPCType>(std::uint8_t(raw));
    }
    return NPCType::Peasant;
}

inline int wrap_chebyshev(int d, int period) {
    if (d >  period / 2) d -= period;
    if (d < -period / 2) d += period;
    return d;
}

// Random talk-line popup state — module-static so the panel function
// stays stateless. `entity` doubles as a dirty bit (entt::null = no
// popup).
entt::entity g_talk_npc = entt::null;
const char*  g_talk_line = nullptr;
entt::entity g_trade_npc = entt::null;
entt::entity g_trade_message_npc = entt::null;
char         g_trade_message[160] = "";
int          g_trade_amount = 1;   // shared staging step (Amount)
BarterState  g_npc_barter;         // the staged package deal

void clear_talk_popup() {
    g_talk_npc = entt::null;
    g_talk_line = nullptr;
}

void clear_trade_popup() {
    g_trade_npc = entt::null;
    g_trade_message_npc = entt::null;
    g_trade_message[0] = '\0';
    g_npc_barter.clear();
}

const char* npc_display_name(const NpcTypeDef& def, const ecs::NpcCharacter& ch) {
    if (def.nameCount > 0) return def.names[ch.nameIdx % def.nameCount];
    return def.label;
}

bool live_npc_entity(const ecs::World& w, entt::entity e) {
    if (e == entt::null || !w.reg.valid(e)) return false;
    const auto* hp = w.reg.try_get<ecs::Health>(e);
    return hp && hp->hp > 0.0f;
}

bool valid_trade_npc_entity(const ecs::World& w, entt::entity e) {
    return live_npc_entity(w, e) &&
           w.reg.all_of<ecs::NPCKind, ecs::NpcInventory, ecs::NpcCharacter>(e);
}

void sanitize_popup_state(const ecs::World& w) {
    if (g_talk_npc != entt::null && !live_npc_entity(w, g_talk_npc)) {
        clear_talk_popup();
    }
    if (g_trade_npc != entt::null && !valid_trade_npc_entity(w, g_trade_npc)) {
        clear_trade_popup();
    }
}

// Another trader = another deal: drop the message AND the staged package.
void sync_trade_message_for(entt::entity e) {
    if (g_trade_message_npc == e) return;
    g_trade_message_npc = e;
    g_trade_message[0] = '\0';
    g_npc_barter.clear();
}

void reset_trade_message_for(entt::entity e) {
    g_trade_message_npc = e;
    g_trade_message[0] = '\0';
    g_npc_barter.clear();
}

void set_deal_message(int gave, int took) {
    std::snprintf(g_trade_message, sizeof(g_trade_message),
                  "Deal: gave %d g, received %d g.", gave, took);
}

void push_deal_log(GameState& gs, const char* traderName,
                   int gave, int took) {
    char message[192];
    std::snprintf(message, sizeof(message),
                  "Deal with %s: gave %d g, received %d g",
                  traderName ? traderName : "trader", gave, took);
    push_event_log(gs.player, {LogType::Economy, message, gs.worldTime.day()});
}

const char* npc_trait_label(std::uint8_t raw) {
    switch (static_cast<NPCTrait>(raw)) {
        case NPCTrait::Greedy:     return "Greedy";
        case NPCTrait::Honorable:  return "Honorable";
        case NPCTrait::Cowardly:   return "Cowardly";
        case NPCTrait::Brave:      return "Brave";
        case NPCTrait::Aggressive: return "Aggressive";
        case NPCTrait::Generous:   return "Generous";
        case NPCTrait::Suspicious: return "Suspicious";
        case NPCTrait::Curious:    return "Curious";
        default:                   return "?";
    }
}

// (npc_has_trait moved with the temperament column into
// macro/economy.cpp — the law's home owns its own predicate.)

// (The temperament column moved to the law's own home — macro/economy.h
// trait_price_mult.)

int trade_overlay_buy_price(int baseValue,
                            int charisma,
                            const ecs::NpcTraits* traits) {
    return sm::player_trade_price(baseValue, charisma, /*bargaining*/ 0,
                                  sm::trait_price_mult(traits, true),
                                  /*buying*/ true);
}

int trade_overlay_sell_price(int baseValue, int charisma,
                             const ecs::NpcTraits* traits) {
    return sm::player_trade_price(baseValue, charisma, /*bargaining*/ 0,
                                  sm::trait_price_mult(traits, false),
                                  /*buying*/ false);
}

} // namespace

void open_npc_trade_panel(entt::entity npc) {
    clear_talk_popup();
    reset_trade_message_for(npc);
    g_trade_npc = npc;
}

bool npc_proximity_popup_open() {
    return g_talk_npc != entt::null || g_trade_npc != entt::null;
}

NpcProximityResult draw_npc_proximity_panel(GameState& gs, ecs::World& w,
                                            int viewW, int viewH,
                                            bool showRows, float scale) {
    NpcProximityResult result{};
    if (gs.mapW <= 0 || gs.mapH <= 0) return result;
    sanitize_popup_state(w);

    // Right-edge anchor; width matches Svelte (`w-52` ~= 208px).
    constexpr float kPanelW = 220.0f;
    constexpr float kPanelTop = 80.0f;
    constexpr float kBottomReserve = 54.0f;
    constexpr float kRowStep = 92.0f;
    constexpr int kMaxProximityRows = 12;
    const float availableH = std::max(88.0f, float(viewH) - kPanelTop - kBottomReserve);
    int rowBudget = std::max(1, int(availableH / kRowStep));
    if (rowBudget > kMaxProximityRows) rowBudget = kMaxProximityRows;

    const bool drawRowsEnabled = showRows && !npc_proximity_popup_open();
    if (drawRowsEnabled) {

        auto view = w.reg.view<ecs::Position, ecs::NPCKind, ecs::Health,
                               ecs::NpcLevel, ecs::NpcCharacter>(
            entt::exclude<ecs::PlayerTag>);  // don't list the player in its own proximity panel (Inc 5e-2)

        // Fixed row buffer: this render hot path must not grow heap storage
        // when multiple NPCs share adjacent cells.
        struct Row {
            entt::entity e;
            int dx, dy;
            int rank;
            char dir[5];
        };
        std::array<Row, std::size_t(kMaxProximityRows)> rows{};
        std::size_t rowCount = 0;
        std::size_t totalRows = 0;

        const int px = int(std::floor(gs.player.x));
        const int py = int(std::floor(gs.player.y));
        const int W  = gs.mapW;
        const int H  = gs.mapH;

        for (auto e : view) {
            const auto& pos = view.get<ecs::Position>(e);
            const auto& hp  = view.get<ecs::Health>(e);
            if (hp.hp <= 0) continue;

            int nx = int(std::floor(pos.x));
            int ny = int(std::floor(pos.y));
            int dx = wrap_chebyshev(nx - px, W);
            int dy = wrap_chebyshev(ny - py, H);
            if (std::abs(dx) > 1 || std::abs(dy) > 1) continue;

            ++totalRows;
            Row row{};
            row.e = e;
            row.dx = dx;
            row.dy = dy;
            row.rank = proximity_row_rank(dx, dy);
            direction_label(dx, dy, row.dir);
            if (rowCount < rows.size()) {
                rows[rowCount++] = row;
            } else {
                std::size_t worst = 0;
                for (std::size_t i = 1; i < rows.size(); ++i) {
                    if (rows[i].rank > rows[worst].rank) {
                        worst = i;
                    }
                }
                if (row.rank < rows[worst].rank) {
                    rows[worst] = row;
                }
            }
        }

        if (totalRows > 0) {

            for (std::size_t i = 1; i < rowCount; ++i) {
                Row key = rows[i];
                std::size_t j = i;
                while (j > 0 && key.rank < rows[j - 1].rank) {
                    rows[j] = rows[j - 1];
                    --j;
                }
                rows[j] = key;
            }

            // Right-edge anchor; width matches Svelte (`w-52` ≈ 208px).
            std::size_t drawRows = std::min(rowCount, std::size_t(rowBudget));
            const bool hiddenRows = totalRows > drawRows;
            if (hiddenRows && drawRows > 1) {
                --drawRows;
            }

            ImGui::SetNextWindowPos(ImVec2(float(viewW) - kPanelW * scale - 8.0f, kPanelTop),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(kPanelW * scale, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(4, 3));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4, 4));
            constexpr ImGuiWindowFlags kFlags =
                ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
                ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

            if (ImGui::Begin("##npc_proximity", nullptr, kFlags)) {
                ImGui::SetWindowFontScale(scale);
                for (std::size_t rowIdx = 0; rowIdx < drawRows; ++rowIdx) {
                    auto& r = rows[rowIdx];
                    const auto& kind = view.get<ecs::NPCKind>(r.e);
                    const auto& hp   = view.get<ecs::Health>(r.e);
                    const auto& lvl  = view.get<ecs::NpcLevel>(r.e);
                    const auto& ch   = view.get<ecs::NpcCharacter>(r.e);

                    const NPCType t  = npc_type_or_default(kind.type);
                    const auto&   def = npc_def(t);

                    // Resolve faction colour through the macro registry. Falls
                    // back to a neutral grey for unknown ids.
                    // Colour and name come from THE registry row — they were
                    // copied into the per-save faction map and read back from
                    // there, which is how a save could disagree with the game
                    // it was running in.
                    const char* fid = faction_id_for_index(kind.factionIdx);
                    ImU32 fcol = IM_COL32(160, 160, 160, 255);
                    const char* fname = fid;
                    if (const FactionDef* fd =
                            faction_def_by_index(kind.factionIdx)) {
                        fcol  = fd->color | 0xFF000000u; // ensure opaque
                        fname = fd->name;
                    }

                    // Per-NPC display name pulled from the type's name pool.
                    const char* npcName = npc_display_name(def, ch);

                    // Row group with explicit action buttons.
                    ImGui::PushID(int(entt::to_integral(r.e)));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 14, 8, 220));
                    ImGui::PushStyleColor(ImGuiCol_Border,
                                          (fcol & 0x00FFFFFFu) | 0x60000000u);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
                    // Both sizes are DERIVED from what the row actually holds,
                    // because both used to be pixels off the ceiling (a 40-px
                    // picture beside a 51-px block of text, in an 88-px row).
                    // The picture is as tall as the three lines it stands
                    // beside — name, role · level, faction — and the row is
                    // that block plus the button strip under it. Nothing here
                    // is a constant, so the whole card follows the font instead
                    // of shearing away from it the moment the text grows.
                    const ImGuiStyle& rowStyle = ImGui::GetStyle();
                    const float kindPx = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
                    const float rowPx = kindPx + ImGui::GetFrameHeight()
                                      + rowStyle.ItemSpacing.y
                                      + rowStyle.WindowPadding.y * 2.0f;
                    ImGui::BeginChild("##row", ImVec2(0.0f, rowPx), true,
                                      ImGuiWindowFlags_NoScrollbar);

                    // Sprite — the kind's drawn PNG, falls back to a blank.
                    {
                        const Sprite* sp = sprite_get(npc_sprite(t));
                        const ImVec2 side(kindPx, kindPx);
                        if (sp && sp->tex) ImGui::Image(sp->tex, side);
                        else               ImGui::Dummy(side);
                    }
                    ImGui::SameLine();

                    // The chip on the right is as wide as the widest thing it
                    // can ever hold, measured rather than guessed — 44 px was a
                    // number off the ceiling and the text beside it did not
                    // know the chip existed at all. The two OVERLAPPED: a long
                    // middle line ran straight under the HP figures and drew
                    // "Woodcutter · L73/473" out of two legible strings
                    // (audit 4.1's family, seen in a capture).
                    const float chipW = ImGui::CalcTextSize("9999/9999").x;
                    const float chipX =
                        ImGui::GetWindowWidth() - chipW - rowStyle.WindowPadding.x;

                    // So the text column is CLIPPED at the chip's edge. A name
                    // too long for the card is cut off, which reads as "there
                    // is more" — two strings printed through each other read as
                    // corruption.
                    const ImVec2 textMin = ImGui::GetCursorScreenPos();
                    ImGui::PushClipRect(
                        textMin,
                        ImVec2(ImGui::GetWindowPos().x + chipX
                                   - rowStyle.ItemSpacing.x,
                               textMin.y + rowPx),
                        true);
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Text, fcol);
                    ImGui::TextUnformatted(npcName);
                    ImGui::PopStyleColor();
                    // The role alone. The level moved to the numbers column on
                    // the right, where it belongs beside the HP: words on the
                    // left, figures on the right, and the longest line on each
                    // side got shorter for free — which is why the two columns
                    // stopped fighting over the same pixels.
                    ImGui::TextDisabled("%s", def.label);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          (fcol & 0x00FFFFFFu) | 0xC0000000u);
                    ImGui::TextUnformatted(fname);
                    ImGui::PopStyleColor();
                    ImGui::EndGroup();
                    ImGui::PopClipRect();

                    // Right-aligned direction + HP chip.
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(chipX);
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 120, 255));
                    ImGui::Text("%s", r.dir);
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("Lv.%d", int(lvl.value));
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
                    ImGui::Text("%d/%d", int(hp.hp), int(hp.maxHp));
                    ImGui::PopStyleColor();
                    ImGui::EndGroup();

                    // The button strip sits directly under the text block, at
                    // the height that block actually occupies. It used to be a
                    // literal 58, tuned against the row's old literal 88, so a
                    // change to either silently slid the buttons.
                    ImGui::SetCursorPosY(kindPx + rowStyle.WindowPadding.y);
                    if (ImGui::Button("Talk", ImVec2(54, 22))) {
                        clear_trade_popup();
                        g_talk_npc = r.e;
                        if (def.talkCount > 0) {
                            const std::uint32_t pick =
                                ch.visualSeed % std::uint32_t(def.talkCount);
                            g_talk_line = def.talkLines[pick];
                        } else {
                            g_talk_line = "...";
                        }
                    }
                    ImGui::SameLine();
                    const bool tradeOk = w.reg.all_of<ecs::NpcInventory>(r.e);
                    if (!tradeOk) ImGui::BeginDisabled();
                    if (ImGui::Button("Trade", ImVec2(58, 22))) {
                        open_npc_trade_panel(r.e);
                    }
                    if (!tradeOk) ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !tradeOk) {
                        ImGui::SetTooltip("Missing backend: ecs::NpcInventory component.");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Attack", ImVec2(62, 22))) {
                        result.attackNpc = r.e;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enter normal subworld combat with this NPC.");
                    }

                    ImGui::EndChild();
                    ImGui::PopStyleVar();   // ChildBorderSize
                    ImGui::PopStyleColor(2); // ChildBg + Border
                    ImGui::PopID();
                }
                if (hiddenRows) {
                    ImGui::TextDisabled("+%d more nearby", int(totalRows - drawRows));
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
        }
    }

    // Talk-line popup — shows the most recent line until the player
    // clicks Close or another NPC. Kept lightweight; will be replaced
    // by the real interaction overlay when it lands.
    if (g_talk_npc != entt::null && g_talk_line) {
        ImGui::SetNextWindowPos(ImVec2(float(viewW) * 0.5f, 120.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(360, 0));
        if (ImGui::Begin("Talk", nullptr,
                         ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("%s", g_talk_line);
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0))) {
                clear_talk_popup();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                clear_talk_popup();
            }
        }
        ImGui::End();
    }

    if (g_trade_npc != entt::null) {
        const bool validTrade = valid_trade_npc_entity(w, g_trade_npc);
        if (!validTrade) {
            clear_trade_popup();
        } else {
            const auto& kind = w.reg.get<ecs::NPCKind>(g_trade_npc);
            auto& bag = w.reg.get<ecs::NpcInventory>(g_trade_npc);
            const auto& ch = w.reg.get<ecs::NpcCharacter>(g_trade_npc);
            const ecs::NpcTraits* traits =
                w.reg.try_get<ecs::NpcTraits>(g_trade_npc);
            const NPCType t = npc_type_or_default(kind.type);
            const auto& def = npc_def(t);
            {
                const char* npcName = npc_display_name(def, ch);
                // (The player's derived bonuses were computed here and never
                // read: `tradeDiscount` — what CHA is FOR — reaches no price in
                // this panel or anywhere else. Not fixed here, because the
                // attribute law is its own job; recorded in problems.md so it
                // stops hiding behind a variable that looks used.)
                sync_trade_message_for(g_trade_npc);
                ImGui::SetNextWindowPos(ImVec2(float(viewW) * 0.5f, 190.0f),
                                        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("NPC Trade", nullptr,
                                 ImGuiWindowFlags_NoCollapse)) {
                    ImGui::Text("%s  Coin %d", npcName,
                                wallet_value(gs.player.inventory));
                    draw_trade_carry_line(gs);
                    draw_counterparty_gold(bag.inv);
                    draw_trade_amount_input(&g_trade_amount);
                    if (traits && traits->count > 0) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("Traits:");
                        for (std::uint8_t ti = 0; ti < traits->count && ti < 2; ++ti) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", npc_trait_label(traits->traits[ti]));
                        }
                    }
                    if (g_trade_message[0] != '\0') {
                        ImGui::Spacing();
                        ImGui::TextWrapped("%s", g_trade_message);
                    }
                    ImGui::Separator();

                    // Price FROM STOCK at POST-TRADE quantity — a lone
                    // trader has no town demand: his scarcity is his own
                    // shelf. Coin never reaches these lambdas (face value
                    // inside draw_barter_column).
                    const auto buyUnit = [&](const std::string& id,
                                             const ItemDef& def, int n) {
                        return trade_overlay_buy_price(
                            stock_price(def.value, bag.inv.count(id) - n, 0),
                            gs.player.sheet.attributes.cha, traits);
                    };
                    const auto sellUnit = [&](const std::string& id,
                                              const ItemDef& def, int n) {
                        return trade_overlay_sell_price(
                            stock_price(def.value, bag.inv.count(id) + n, 0),
                            gs.player.sheet.attributes.cha, traits);
                    };

                    ImGui::Columns(2, "npc_trade_cols", true);
                    ImGui::TextUnformatted("Trader stock");
                    const int takeValue = draw_barter_column(
                        "##npc_stock", bag.inv, g_npc_barter.take,
                        g_trade_amount, buyUnit);
                    ImGui::NextColumn();
                    ImGui::TextUnformatted("Your inventory");
                    const int giveValue = draw_barter_column(
                        "##npc_player_stock", gs.player.inventory,
                        g_npc_barter.give, g_trade_amount, sellUnit);
                    ImGui::Columns(1);

                    if (draw_barter_deal_button(g_npc_barter,
                                                gs.player.inventory, bag.inv,
                                                giveValue, takeValue)) {
                        set_deal_message(giveValue, takeValue);
                        push_deal_log(gs, npcName, giveValue, takeValue);
                    }

                    ImGui::TextDisabled("Stage lines with +/- on both sides; one Deal settles the package.");
                    if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0))) {
                        clear_trade_popup();
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        clear_trade_popup();
                    }
                }
                ImGui::End();
            }
        }
    }
    return result;
}

} // namespace sm::ui

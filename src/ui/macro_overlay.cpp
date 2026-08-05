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
#include "ecs/world.h"
#include "ecs/components.h"
#include "macro/state.h"
#include "macro/markers.h"
#include "macro/tree_layer.h"
#include "macro/npc.h"
#include "macro/faction.h"
#include "macro/npc_spawn.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"
#include "assets/sprite_atlas.h"
#include "assets/character_paperdoll.h"
#include "assets/character_paperdoll_gl.h"

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

inline float wrap_delta(float d, float period) {
    if (d >  period * 0.5f) d -= period;
    if (d < -period * 0.5f) d += period;
    return d;
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



// Sample biome at a macro cell from the master terrain bitmap. Mirrors
// the GLSL path: water below seaLevel, Mountain above kMountainBiomeLevel,
// else the 3x3 climate matrix.
Biome biome_at_cell(const TerrainData& td, int x, int y, float seaLevel) {
    int wx = ((x % td.width)  + td.width)  % td.width;
    int wy = ((y % td.height) + td.height) % td.height;
    std::size_t i = (std::size_t(wy) * td.width + wx) * 4u;
    float h = td.rgba[i + 0] / 255.0f;
    float m = td.rgba[i + 1] / 255.0f;
    float t = td.rgba[i + 2] / 255.0f;
    return biome_at(t, m, h, seaLevel, kMountainBiomeLevel);
}

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

// NPC type → real PNG sprite from public/assets/sprites/.
SpriteId npc_sprite(NPCType t) {
    switch (t) {
        case NPCType::Caravan:   return SpriteId::Caravan;
        case NPCType::Bandit:    return SpriteId::ImpGolem;
        case NPCType::Witch:     return SpriteId::Witch;
        case NPCType::Sorceress: return SpriteId::Cultistka;
        // Peasant / Woodcutter / Merchant / Guard share the peasant atlas.
        default:                 return SpriteId::Peasant;
    }
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
                 ImU32 fallbackCol) {
    const Sprite* s = sprite_get(id);
    const float r = pixSize * 0.5f;
    if (!s) {
        dl->AddCircleFilled(c, std::max(2.0f, r * 0.4f), fallbackCol, 12);
        return;
    }
    ImVec2 tl(c.x - r, c.y - r);
    ImVec2 br(c.x + r, c.y + r);
    dl->AddImage(s->tex, tl, br);
}

character::CharacterTextureCache& paperdoll_cache() {
    static character::CharacterTextureCache cache;
    return cache;
}

character::Direction direction_from_delta(float dx, float dy) {
    if (std::fabs(dx) > std::fabs(dy)) {
        return dx < 0.0f ? character::Direction::Left
                         : character::Direction::Right;
    }
    if (std::fabs(dy) > 0.001f) {
        return dy > 0.0f ? character::Direction::Back
                         : character::Direction::Front;
    }
    return character::Direction::Front;
}

character::Direction npc_direction(const ecs::Position& pos,
                                   const ecs::VisualPos* visual,
                                   const ecs::MacroNpcRuntime* rt,
                                   int mapW,
                                   int mapH) {
    float dx = visual ? wrap_delta(pos.x - visual->vx, float(mapW)) : 0.0f;
    float dy = visual ? wrap_delta(pos.y - visual->vy, float(mapH)) : 0.0f;
    if (dx * dx + dy * dy <= 0.0001f) {
        if (!rt || rt->visualSpeed <= 0.05f) {
            return character::Direction::Front;
        }
        dx = wrap_delta(rt->targetX - pos.x, float(mapW));
        dy = wrap_delta(rt->targetY - pos.y, float(mapH));
    }
    return direction_from_delta(dx, dy);
}

bool npc_visually_moving(const ecs::Position& pos,
                         const ecs::VisualPos* visual,
                         int mapW,
                         int mapH) {
    if (!visual) return false;
    const float dx = wrap_delta(pos.x - visual->vx, float(mapW));
    const float dy = wrap_delta(pos.y - visual->vy, float(mapH));
    return dx * dx + dy * dy > 0.0001f;
}

character::Direction player_direction(const GameState& gs,
                                      const MacroCursor& cursor) {
    if (cursor.path.empty() || cursor.pathIdx >= cursor.path.size()) {
        return character::Direction::Front;
    }
    const PathPoint& next = cursor.path[cursor.pathIdx];
    const float dx = wrap_delta(float(next.x) - gs.player.x, float(gs.mapW));
    const float dy = wrap_delta(float(next.y) - gs.player.y, float(gs.mapH));
    return direction_from_delta(dx, dy);
}

float paperdoll_night_darken(const WorldTime& time) {
    int minutes = time.hour() * 60 + time.minute();
    minutes %= 24 * 60;
    if (minutes < 0) minutes += 24 * 60;

    const float progress = float(minutes) / float(24 * 60);
    if (progress < 0.2f || progress > 0.9f) return 1.0f;
    if (progress < 0.35f) return 1.0f - (progress - 0.2f) / 0.15f;
    if (progress < 0.75f) return 0.0f;
    return (progress - 0.75f) / 0.15f;
}

ImU32 paperdoll_tint_for_time(const WorldTime& time) {
    const float mix = paperdoll_night_darken(time) * 0.82f;
    if (mix <= 0.0f) return IM_COL32(255, 255, 255, 255);
    auto channel = [mix](float tint) {
        const float v = std::clamp(1.0f + (tint - 1.0f) * mix, 0.0f, 1.0f);
        return int(v * 255.0f + 0.5f);
    };
    return IM_COL32(channel(0.05f), channel(0.05f), channel(0.15f), 255);
}

character::AppearancePreset appearance_preset_for_npc(NPCType type) {
    switch (type) {
        case NPCType::Merchant:
        case NPCType::Caravan:
            return character::AppearancePreset::Backpack;
        case NPCType::Guard:
            return character::AppearancePreset::ShoulderArmor;
        case NPCType::Witch:
        case NPCType::Sorceress:
            return character::AppearancePreset::Horns;
        default:
            return character::AppearancePreset::None;
    }
}

void draw_paperdoll(ImDrawList* dl,
                    ImVec2 c,
                    std::uint32_t seed,
                    character::AppearancePreset preset,
                    character::AnimationType anim,
                    character::Direction dir,
                    float pixSize,
                    SpriteId fallback,
                    ImU32 fallbackCol,
                    ImU32 tintCol) {
    const float tMs = float(ImGui::GetTime() * 1000.0);
    const character::AnimationState state = character::make_animation_state(anim, dir, tMs);
    character::CharacterTextureCache& cache = paperdoll_cache();
    const character::CharacterTexture* tex =
        cache.texture_for(cache.descriptor_for_seed(seed, preset), state);
    if (!tex || !tex->tex) {
        draw_sprite(dl, c, fallback, pixSize, fallbackCol);
        return;
    }
    const float r = pixSize * 0.5f;
    dl->AddImage(tex->tex,
                 ImVec2(c.x - r, c.y - r),
                 ImVec2(c.x + r, c.y + r),
                 ImVec2(0.0f, 0.0f),
                 ImVec2(1.0f, 1.0f),
                 tintCol);
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
    const ImU32 paperdollTint = paperdoll_tint_for_time(gs.worldTime);

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
        // Settlement under the cursor is resolved ALWAYS — click-to-select
        // must keep working even when the overlay chrome is hidden.
        const char* landmark = "";
        int hoverSettlementId = -1;
        for (const auto& s : gs.settlements) {
            if (s.x == cursor.hoverX && s.y == cursor.hoverY) {
                landmark = s.name.c_str();
                hoverSettlementId = s.id;
                break;
            }
        }

        if (showMarkers) {
            // After the Y flip the cell's screen rect has top = wy+1, bottom = wy.
            ImVec2 tl = world_to_screen(float(cursor.hoverX),         float(cursor.hoverY) + 1.0f,
                                        camX, camY, zoom, viewW, viewH, mapW, mapH);
            ImVec2 br = world_to_screen(float(cursor.hoverX) + 1.0f,  float(cursor.hoverY),
                                        camX, camY, zoom, viewW, viewH, mapW, mapH);
            dl->AddRect(tl, br, IM_COL32(255, 255, 255, 180), 0.0f, 0, 1.5f);

            // Tooltip: biome / feature / landmark / coords.
            Biome b = biome_at_cell(terrain, cursor.hoverX, cursor.hoverY, 0.40f);
            FeatureType f = features.at(cursor.hoverX, cursor.hoverY);
            if (landmark[0] == 0) {
                for (const auto& v : gs.villages) {
                    if (v.x == cursor.hoverX && v.y == cursor.hoverY) { landmark = v.name.c_str(); break; }
                }
            }
            ImGui::BeginTooltip();
            ImGui::Text("(%d, %d)", cursor.hoverX, cursor.hoverY);
            ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, 1), "%s", kBiomes[b].name);
            const char* fn = feature_name(f);
            if (fn[0]) ImGui::TextColored(ImVec4(1.00f, 0.75f, 0.40f, 1), "%s", fn);
            // Forest is a count class, not a feature: label it from the
            // tree-count layer, with the live number (рубка visibly ticks it).
            if (treeLayer && treeLayer->has_complete_storage()) {
                const int tc = int(treeLayer->at(cursor.hoverX, cursor.hoverY));
                if (is_forest_cell(tc)) {
                    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1),
                                       "Forest (%d trees)", tc);
                } else if (tc > 0) {
                    ImGui::TextColored(ImVec4(0.45f, 0.75f, 0.45f, 1),
                                       "Trees: %d", tc);
                }
            }
            if (landmark[0]) ImGui::TextColored(ImVec4(1.00f, 0.90f, 0.40f, 1), "%s", landmark);
            ImGui::EndTooltip();
        }

        cursor.hoverSettlementId = hoverSettlementId;
        if (hoverSettlementId >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            cursor.clickedSettlementId = hoverSettlementId;
        }
    }


    // Settlements — real city PNG sprite, anchored to cell centre, scaled
    // with zoom via the universal landmark rule (sprite covers 2 cells).
    for (const auto& s : gs.settlements) {
        ImVec2 p = world_to_screen(float(s.x) + 0.5f, float(s.y) + 0.5f,
                                   camX, camY, zoom, viewW, viewH, mapW, mapH);
        if (!on_screen(p, viewW, viewH, 128.0f)) continue;
        const float size = landmark_size(zoom, 28.0f, 192.0f);
        draw_sprite(dl, p, SpriteId::City, size, IM_COL32(255, 220, 90, 230));
        if (zoom >= 6.0f && !s.name.empty()) {
            ImVec2 ts = ImGui::CalcTextSize(s.name.c_str());
            ImVec2 tp(p.x - ts.x * 0.5f, p.y - size * 0.5f - ts.y - 2.0f);
            dl->AddRectFilled(ImVec2(tp.x - 3, tp.y - 1),
                              ImVec2(tp.x + ts.x + 3, tp.y + ts.y + 1),
                              IM_COL32(0, 0, 0, 140), 2.0f);
            dl->AddText(tp, IM_COL32(255, 245, 200, 255), s.name.c_str());
        }
    }

    // Villages — same universal landmark rule as cities (256-px PNG with
    // 128-px central cell area). Hidden when very far out so they don't
    // collapse into single-pixel noise.
    if (zoom >= 3.0f) {
        for (const auto& v : gs.villages) {
            ImVec2 p = world_to_screen(float(v.x) + 0.5f, float(v.y) + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            if (!on_screen(p, viewW, viewH, 96.0f)) continue;
            const float size = landmark_size(zoom, 22.0f, 144.0f);
            draw_sprite(dl, p, SpriteId::Village, size,
                        IM_COL32(180, 140, 90, 220));
        }
    }

    // Spires — magical towers; alternate light/dark variants by id parity.
    for (const auto& sp : gs.spires) {
        ImVec2 p = world_to_screen(float(sp.x) + 0.5f, float(sp.y) + 0.5f,
                                   camX, camY, zoom, viewW, viewH, mapW, mapH);
        if (!on_screen(p, viewW, viewH, 128.0f)) continue;
        const float size = landmark_size(zoom, 26.0f, 160.0f);
        const SpriteId variant = (sp.id & 1) ? SpriteId::SpireDark
                                             : SpriteId::Spire;
        const ImU32 tint = sp.depleted ? IM_COL32(120, 90, 130, 200)
                                       : IM_COL32(220, 180, 240, 230);
        draw_sprite(dl, p, variant, size, tint);
    }

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
            // +0.5 so the sprite sits at the CELL CENTRE (the GLSL grid
            // bounds cells at integer worldPx, so cell (X,Y) spans
            // [X..X+1, Y..Y+1] and its centre is at X+0.5, Y+0.5).
            ImVec2 p = world_to_screen(drawX + 0.5f, drawY + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            if (!on_screen(p, viewW, viewH, 64.0f)) continue;
            const NPCType t = NPCType(kind.type);
            const SpriteId sid = npc_sprite(t);
            const ImU32 col = npc_color(t);
            const character::AppearancePreset preset = appearance_preset_for_npc(t);
            // One cell wide; clamp so it stays readable on the world map
            // and never devours surrounding cells when zoomed in.
            const float size = std::clamp(zoom, 12.0f, 56.0f);
            const bool squad =
                t == NPCType::Caravan ||
                t == NPCType::Bandit  ||
                t == NPCType::Guard   ||
                t == NPCType::Sorceress;
            if (squad) {
                const float dx = size * 0.30f;
                const float dy = size * 0.18f;
                const ecs::NpcCharacter* ch = w.reg.try_get<ecs::NpcCharacter>(e);
                const ecs::MacroNpcRuntime* rt = w.reg.try_get<ecs::MacroNpcRuntime>(e);
                const bool moving =
                    npc_visually_moving(pos, visual, mapW, mapH);
                const character::AnimationType anim =
                    moving ? character::AnimationType::Walk
                           : character::AnimationType::Idle;
                const character::Direction dir =
                    npc_direction(pos, visual, rt, mapW, mapH);
                if (ch) {
                    draw_paperdoll(dl, ImVec2(p.x - dx, p.y - dy),
                                   ch->visualSeed ^ 0xA341u, preset, anim,
                                   dir,
                                   size * 0.75f, sid, col, paperdollTint);
                    draw_paperdoll(dl, ImVec2(p.x + dx, p.y - dy),
                                   ch->visualSeed ^ 0xB653u, preset, anim,
                                   dir,
                                   size * 0.75f, sid, col, paperdollTint);
                    draw_paperdoll(dl, ImVec2(p.x, p.y + dy),
                                   ch->visualSeed, preset, anim,
                                   dir,
                                   size, sid, col, paperdollTint);
                } else {
                    draw_sprite(dl, ImVec2(p.x - dx, p.y - dy), sid, size * 0.75f, col);
                    draw_sprite(dl, ImVec2(p.x + dx, p.y - dy), sid, size * 0.75f, col);
                    draw_sprite(dl, ImVec2(p.x,       p.y + dy), sid, size,         col);
                }
            } else {
                const ecs::NpcCharacter* ch = w.reg.try_get<ecs::NpcCharacter>(e);
                const ecs::MacroNpcRuntime* rt = w.reg.try_get<ecs::MacroNpcRuntime>(e);
                if (ch) {
                    const bool moving =
                        npc_visually_moving(pos, visual, mapW, mapH);
                    const character::AnimationType anim =
                        moving ? character::AnimationType::Walk
                               : character::AnimationType::Idle;
                    const character::Direction dir =
                        npc_direction(pos, visual, rt, mapW, mapH);
                    draw_paperdoll(dl, p, ch->visualSeed, preset, anim,
                                   dir, size, sid, col, paperdollTint);
                } else {
                    draw_sprite(dl, p, sid, size, col);
                }
            }
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
            ImVec2 p = world_to_screen(m.x + 0.5f, m.y + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            if (!on_screen(p, viewW, viewH, 96.0f)) continue;
            const std::size_t si = std::size_t(m.style) & 3u;
            const char* glyph = kMarkerGlyph[si];
            const ImU32 col   = marker_imcol(kMarkerColor[si]);
            // Float clear of anything on the cell (a city sprite's half-height).
            const float above = landmark_size(zoom, 28.0f, 192.0f) * 0.5f + r + 4.0f;
            const ImVec2 c(p.x, p.y - above);                  // pin centre
            // Dark disc + coloured ring so the glyph reads over any biome.
            dl->AddCircleFilled(c, r, IM_COL32(0, 0, 0, 150), 20);
            dl->AddCircle(c, r, col, 20, 2.0f);
            const ImVec2 ts = font->CalcTextSizeA(glyphPx, FLT_MAX, 0.0f, glyph);
            const ImVec2 tp(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
            dl->AddText(font, glyphPx, ImVec2(tp.x + 1.0f, tp.y + 1.0f),
                        IM_COL32(0, 0, 0, 190), glyph);        // shadow
            dl->AddText(font, glyphPx, tp, col, glyph);
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
        const bool walking = !cursor.path.empty() && cursor.pathIdx < cursor.path.size();
        draw_paperdoll(dl, p, gs.worldSeed ^ 0x7150A11u,
                       character::AppearancePreset::None,
                       walking ? character::AnimationType::Walk
                               : character::AnimationType::Idle,
                       player_direction(gs, cursor),
                       size, SpriteId::Player, IM_COL32(255, 255, 255, 255),
                       paperdollTint);
    }
}

std::size_t step_macro_walk(GameState& gs, MacroCursor& cursor, float dt,
                            float cellsPerSec,
                            MacroWalkReachedFn onReached,
                            void* onReachedUser) {
    if (cursor.path.empty() || cursor.pathIdx >= cursor.path.size()) return 0u;

    const int W = gs.mapW;
    const int H = gs.mapH;
    auto wrap_delta = [](float d, float period) {
        if (d >  period * 0.5f) d -= period;
        if (d < -period * 0.5f) d += period;
        return d;
    };

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

void clear_talk_popup() {
    g_talk_npc = entt::null;
    g_talk_line = nullptr;
}

void clear_trade_popup() {
    g_trade_npc = entt::null;
    g_trade_message_npc = entt::null;
    g_trade_message[0] = '\0';
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

void sync_trade_message_for(entt::entity e) {
    if (g_trade_message_npc == e) return;
    g_trade_message_npc = e;
    g_trade_message[0] = '\0';
}

void reset_trade_message_for(entt::entity e) {
    g_trade_message_npc = e;
    g_trade_message[0] = '\0';
}

void set_trade_message(const char* verb, const ItemDef& item, int price) {
    std::snprintf(g_trade_message, sizeof(g_trade_message),
                  "%s %s for %d g", verb, item.name, price);
}

void push_trade_log(GameState& gs,
                    const char* verb,
                    const ItemDef& item,
                    const char* traderName,
                    int price) {
    char message[192];
    std::snprintf(message, sizeof(message), "%s %s %s %s for %d g",
                  verb, item.name,
                  verb[0] == 'B' ? "from" : "to",
                  traderName ? traderName : "trader",
                  price);
    gs.player.eventLog.push_back({LogType::Economy, message, gs.worldTime.day()});
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

bool npc_has_trait(const ecs::NpcTraits* traits, NPCTrait trait) {
    if (!traits) return false;
    const auto raw = static_cast<std::uint8_t>(trait);
    for (std::uint8_t i = 0; i < traits->count && i < 2; ++i) {
        if (traits->traits[i] == raw) return true;
    }
    return false;
}

int trade_overlay_buy_price(int baseValue,
                            float tradeDiscount,
                            const ecs::NpcTraits* traits) {
    float mult = 1.0f;
    if (npc_has_trait(traits, NPCTrait::Greedy)) {
        mult += 0.2f;
    }
    if (npc_has_trait(traits, NPCTrait::Generous)) {
        mult -= 0.1f;
    }
    mult -= tradeDiscount;
    return std::max(1, int(std::floor(float(baseValue) * std::max(0.1f, mult))));
}

int trade_overlay_sell_price(int baseValue, const ecs::NpcTraits* traits) {
    float mult = 0.5f;
    if (npc_has_trait(traits, NPCTrait::Greedy)) {
        mult -= 0.1f;
    }
    if (npc_has_trait(traits, NPCTrait::Generous)) {
        mult += 0.1f;
    }
    return std::max(1, int(std::floor(float(baseValue) * std::max(0.1f, mult))));
}

void draw_trade_item_tooltip(const ItemDef* item) {
    if (!item || !ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(item->name);
    if (item->description && item->description[0] != '\0') {
        ImGui::TextWrapped("%s", item->description);
    }
    ImGui::Text("Weight: %.2f kg", double(item->weight));
    ImGui::EndTooltip();
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
                    const char* fid = faction_id_for_index(kind.factionIdx);
                    ImU32 fcol = IM_COL32(160, 160, 160, 255);
                    const char* fname = fid;
                    auto it = gs.factions.find(fid);
                    if (it != gs.factions.end()) {
                        fcol  = it->second.color | 0xFF000000u; // ensure opaque
                        fname = it->second.name.c_str();
                    }

                    // Per-NPC display name pulled from the type's name pool.
                    const char* npcName = npc_display_name(def, ch);

                    // Row group with explicit action buttons.
                    ImGui::PushID(int(entt::to_integral(r.e)));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 14, 8, 220));
                    ImGui::PushStyleColor(ImGuiCol_Border,
                                          (fcol & 0x00FFFFFFu) | 0x60000000u);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
                    ImGui::BeginChild("##row", ImVec2(0.0f, 88.0f), true,
                                      ImGuiWindowFlags_NoScrollbar);

                    // Sprite — tries the real PNG, falls back to a coloured dot.
                    const character::AnimationState portraitAnim =
                        character::make_animation_state(character::AnimationType::Idle,
                                                        character::Direction::Front,
                                                        0.0f);
                    const character::CharacterTexture* portrait =
                        paperdoll_cache().texture_for(
                            paperdoll_cache().descriptor_for_seed(
                                ch.visualSeed,
                                appearance_preset_for_npc(t)),
                            portraitAnim);
                    if (portrait && portrait->tex) {
                        ImGui::Image(portrait->tex,
                                     ImVec2(40, 40));
                    } else {
                        const Sprite* sp = sprite_get(npc_sprite(t));
                        if (sp && sp->tex) {
                            ImGui::Image(sp->tex,
                                         ImVec2(40, 40));
                        } else {
                            ImGui::Dummy(ImVec2(40, 40));
                        }
                    }
                    ImGui::SameLine();

                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Text, fcol);
                    ImGui::TextUnformatted(npcName);
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("%s · Lv.%d", def.label, int(lvl.value));
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          (fcol & 0x00FFFFFFu) | 0xC0000000u);
                    ImGui::TextUnformatted(fname);
                    ImGui::PopStyleColor();
                    ImGui::EndGroup();

                    // Right-aligned direction + HP chip.
                    const float chipW = 44.0f;
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - chipW - 8.0f);
                    ImGui::BeginGroup();
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 210, 120, 255));
                    ImGui::Text("%s", r.dir);
                    ImGui::PopStyleColor();
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
                    ImGui::Text("%d/%d", int(hp.hp), int(hp.maxHp));
                    ImGui::PopStyleColor();
                    ImGui::EndGroup();

                    ImGui::SetCursorPosY(58.0f);
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
                const DerivedBonuses derived =
                    calculate_derived(gs.player.sheet.attributes, gs.player.sheet.skills);
                sync_trade_message_for(g_trade_npc);
                ImGui::SetNextWindowPos(ImVec2(float(viewW) * 0.5f, 190.0f),
                                        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("NPC Trade", nullptr,
                                 ImGuiWindowFlags_NoCollapse)) {
                    ImGui::Text("%s  Gold %d", npcName, gs.player.gold);
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

                    ImGui::Columns(2, "npc_trade_cols", true);
                    ImGui::TextUnformatted("Trader stock");
                    ImGui::BeginChild("##npc_stock", ImVec2(0, 280), true);
                    if (bag.inv.stacks.empty()) {
                        ImGui::TextDisabled("(empty)");
                    } else {
                        bool changed = false;
                        for (std::size_t i = 0; i < bag.inv.stacks.size() && !changed; ++i) {
                            const std::string& id = bag.inv.stacks[i].id;
                            const int count = bag.inv.stacks[i].count;
                            const ItemDef* item = item_def(id);
                            const int price = item
                                ? trade_overlay_buy_price(item->value,
                                                          derived.tradeDiscount,
                                                          traits)
                                : 0;
                            const bool canBuy = item && count > 0 && gs.player.gold >= price;
                            ImGui::PushID(int(i));
                            if (!canBuy) ImGui::BeginDisabled();
                            if (ImGui::Button("Buy", ImVec2(52, 0))) {
                                if (bag.inv.remove(id, 1)) {
                                    gs.player.gold -= price;
                                    gs.player.inventory.add(id, 1);
                                    set_trade_message("Bought", *item, price);
                                    push_trade_log(gs, "Bought", *item, npcName, price);
                                } else {
                                    std::snprintf(g_trade_message,
                                                  sizeof(g_trade_message),
                                                  "Item unavailable.");
                                }
                                changed = true;
                            }
                            if (!canBuy) ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                if (!item) {
                                    ImGui::SetTooltip("Unknown item id");
                                } else if (count <= 0) {
                                    ImGui::SetTooltip("Out of stock.");
                                } else if (gs.player.gold < price) {
                                    ImGui::SetTooltip("Not enough gold.");
                                }
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s x%d  %d g",
                                        item ? item->name : id.c_str(), count, price);
                            draw_trade_item_tooltip(item);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();

                    ImGui::NextColumn();
                    ImGui::TextUnformatted("Your inventory");
                    ImGui::BeginChild("##npc_player_stock", ImVec2(0, 280), true);
                    if (gs.player.inventory.stacks.empty()) {
                        ImGui::TextDisabled("(empty)");
                    } else {
                        bool changed = false;
                        for (std::size_t i = 0; i < gs.player.inventory.stacks.size() && !changed; ++i) {
                            const std::string& id = gs.player.inventory.stacks[i].id;
                            const int count = gs.player.inventory.stacks[i].count;
                            const ItemDef* item = item_def(id);
                            const int price = item
                                ? trade_overlay_sell_price(item->value, traits)
                                : 0;
                            ImGui::PushID(int(i));
                            if (!item) ImGui::BeginDisabled();
                            if (ImGui::Button("Sell", ImVec2(52, 0))) {
                                if (gs.player.inventory.remove(id, 1)) {
                                    gs.player.gold += price;
                                    bag.inv.add(id, 1);
                                    set_trade_message("Sold", *item, price);
                                    push_trade_log(gs, "Sold", *item, npcName, price);
                                } else {
                                    std::snprintf(g_trade_message,
                                                  sizeof(g_trade_message),
                                                  "Cannot sell.");
                                }
                                changed = true;
                            }
                            if (!item) ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !item) {
                                ImGui::SetTooltip("Unknown item id");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s x%d  %d g",
                                        item ? item->name : id.c_str(), count, price);
                            draw_trade_item_tooltip(item);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::Columns(1);

                    ImGui::TextDisabled("Click trader items to buy. Click your items to sell.");
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

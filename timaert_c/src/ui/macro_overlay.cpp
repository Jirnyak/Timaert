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
#include "events/event_bus.h"
#include "macro/state.h"
#include "macro/npc.h"
#include "macro/npc_spawn.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"
#include "assets/sprite_atlas.h"

#include "imgui.h"

#include <cmath>
#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#include <vector>

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
// the GLSL path: water if height < seaLevel, else 3x3 climate matrix.
Biome biome_at_cell(const TerrainData& td, int x, int y, float seaLevel) {
    int wx = ((x % td.width)  + td.width)  % td.width;
    int wy = ((y % td.height) + td.height) % td.height;
    std::size_t i = (std::size_t(wy) * td.width + wx) * 4u;
    float h = td.rgba[i + 0] / 255.0f;
    if (h < seaLevel) return Water;
    float m = td.rgba[i + 1] / 255.0f;
    float t = td.rgba[i + 2] / 255.0f;
    return biome_from_climate(t, m);
}

const char* feature_name(FeatureType f) {
    switch (f) {
        case FT_Road:     return "Road";
        case FT_Tree:     return "Forest";
        case FT_Mountain: return "Mountain";
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
    dl->AddImage((ImTextureID)(intptr_t)s->tex, tl, br);
}

// Universal landmark scale: the 128-px central area equals one cell, so
// the whole 256-px sprite is 2 cells wide on screen. We clamp to a
// minimum readable size for the world map and a max so cities don't
// devour the whole viewport when extremely zoomed in.
inline float landmark_size(float zoom, float minPx, float maxPx) {
    return std::clamp(zoom * 2.0f, minPx, maxPx);
}

} // namespace

void draw_macro_overlay(GameState& gs, ecs::World& w,
                        const TerrainData& terrain,
                        const FeatureLayer& features,
                        MacroCursor& cursor,
                        float camX, float camY, float zoom,
                        int viewW, int viewH, int mapW, int mapH) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io = ImGui::GetIO();

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

    // ── Auto-travel polyline (drawn underneath everything else).
    if (cursor.path.size() > cursor.pathIdx + 1) {
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

    // ── Hover-cell highlight + tooltip.
    if (cursor.hoverValid) {
        // After the Y flip the cell's screen rect has top = wy+1, bottom = wy.
        ImVec2 tl = world_to_screen(float(cursor.hoverX),         float(cursor.hoverY) + 1.0f,
                                    camX, camY, zoom, viewW, viewH, mapW, mapH);
        ImVec2 br = world_to_screen(float(cursor.hoverX) + 1.0f,  float(cursor.hoverY),
                                    camX, camY, zoom, viewW, viewH, mapW, mapH);
        dl->AddRect(tl, br, IM_COL32(255, 255, 255, 180), 0.0f, 0, 1.5f);

        // Tooltip: biome / feature / landmark / coords.
        Biome b = biome_at_cell(terrain, cursor.hoverX, cursor.hoverY, 0.40f);
        FeatureType f = features.at(cursor.hoverX, cursor.hoverY);
        const char* landmark = "";
        int hoverSettlementId = -1;
        for (const auto& s : gs.settlements) {
            if (s.x == cursor.hoverX && s.y == cursor.hoverY) {
                landmark = s.name.c_str();
                hoverSettlementId = s.id;
                break;
            }
        }
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
        if (landmark[0]) ImGui::TextColored(ImVec4(1.00f, 0.90f, 0.40f, 1), "%s", landmark);
        ImGui::EndTooltip();

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
        auto view = w.reg.view<ecs::Position, ecs::NPCKind>();
        for (auto e : view) {
            const auto& pos  = view.get<ecs::Position>(e);
            const auto& kind = view.get<ecs::NPCKind>(e);
            // +0.5 so the sprite sits at the CELL CENTRE (the GLSL grid
            // bounds cells at integer worldPx, so cell (X,Y) spans
            // [X..X+1, Y..Y+1] and its centre is at X+0.5, Y+0.5).
            ImVec2 p = world_to_screen(pos.x + 0.5f, pos.y + 0.5f,
                                       camX, camY, zoom, viewW, viewH, mapW, mapH);
            if (!on_screen(p, viewW, viewH, 64.0f)) continue;
            const NPCType t = NPCType(kind.type);
            const SpriteId sid = npc_sprite(t);
            const ImU32 col = npc_color(t);
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
                draw_sprite(dl, ImVec2(p.x - dx, p.y - dy), sid, size * 0.75f, col);
                draw_sprite(dl, ImVec2(p.x + dx, p.y - dy), sid, size * 0.75f, col);
                draw_sprite(dl, ImVec2(p.x,       p.y + dy), sid, size,         col);
            } else {
                draw_sprite(dl, p, sid, size, col);
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
        draw_sprite(dl, p, SpriteId::Player, size,
                    IM_COL32(255, 255, 255, 255));
    }
}

void step_macro_walk(GameState& gs, MacroCursor& cursor, float dt,
                     float cellsPerSec) {
    if (cursor.path.empty() || cursor.pathIdx >= cursor.path.size()) return;

    const int W = gs.mapW;
    const int H = gs.mapH;
    auto wrap_delta = [](float d, float period) {
        if (d >  period * 0.5f) d -= period;
        if (d < -period * 0.5f) d += period;
        return d;
    };

    float remaining = cellsPerSec * dt;
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
}

// ── Nearby-NPC proximity panel ─────────────────────────────────────────
//
// Mirrors `src/screens/NpcProximityPanel.svelte`: a vertical stack of
// clickable badges anchored to the right edge, one per alive NPC on
// the player's cell or any of the 8 adjacent cells (Chebyshev ≤ 1,
// torus-wrapped). Each badge shows the NPC sprite, name, type+level,
// faction tag (coloured by faction), HP, and a direction chip.
//
// Click handler is a stub for now (real interaction overlay isn't
// ported yet) — we just open a small ImGui popup with a random talk
// line from the NPC type's pool. Keeps the panel useful as a zero-cost
// way to read the world while exploring on foot.

namespace {

// Eight-way compass label, identical to TS `directionLabel(dx, dy)`
// in GameScreen.svelte.
const char* direction_label(int dx, int dy) {
    if (dx == 0 && dy == 0) return "Here";
    const char* ns = (dy < 0) ? "N" : (dy > 0 ? "S" : "");
    const char* ew = (dx > 0) ? "E" : (dx < 0 ? "W" : "");
    static char buf[4];
    int i = 0;
    while (*ns) buf[i++] = *ns++;
    while (*ew) buf[i++] = *ew++;
    buf[i] = '\0';
    return buf[0] ? buf : "Here";
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

bool npc_trade_supported(NPCType t) {
    static constexpr bool kTradeSupported[std::size_t(NPCType::Count)] = {
        false, // Peasant
        false, // Woodcutter
        true,  // Merchant
        true,  // Caravan
        false, // Bandit
        false, // Guard
        true,  // Witch
        false, // Sorceress
    };
    const std::size_t idx = std::size_t(t);
    return idx < (sizeof(kTradeSupported) / sizeof(kTradeSupported[0]))
        && kTradeSupported[idx];
}

const char* npc_display_name(const NpcTypeDef& def, const ecs::NpcCharacter& ch) {
    if (def.nameCount > 0) return def.names[ch.nameIdx % def.nameCount];
    return def.label;
}

void draw_battle_pending_modal(GameState& gs, int viewW) {
    if (gs.subState.kind != GameSubStateKind::Battle) return;

    ImGui::SetNextWindowPos(ImVec2(float(viewW) * 0.5f, 150.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    if (ImGui::Begin("Combat Resolver Pending", nullptr,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Target: %s",
                    gs.subState.enemyId.empty()
                        ? "(unknown)" : gs.subState.enemyId.c_str());
        ImGui::Text("Type: %s",
                    gs.subState.eventId.empty()
                        ? "(unknown)" : gs.subState.eventId.c_str());
        ImGui::Separator();
        ImGui::TextWrapped(
            "BattleStart was emitted. Combat resolution is not ported in "
            "this native build, so no damage or loot is simulated here.");
        if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0))) {
            gs.subState.kind = GameSubStateKind::Exploring;
            gs.subState.enemyId.clear();
            gs.subState.eventId.clear();
        }
    }
    ImGui::End();
}

} // namespace

void draw_npc_proximity_panel(GameState& gs, ecs::World& w, sm::EventBus& bus,
                              int viewW, int /*viewH*/) {
    auto view = w.reg.view<ecs::Position, ecs::NPCKind, ecs::Health,
                           ecs::NpcLevel, ecs::NpcCharacter>();

    // Collect entries up-front so we can early-out if empty.
    struct Row {
        entt::entity e;
        int dx, dy;
        const char* dir;
    };
    static std::vector<Row> rows; // recycled across frames
    rows.clear();

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

        rows.push_back({e, dx, dy, direction_label(dx, dy)});
    }

    if (rows.empty()) {
        draw_battle_pending_modal(gs, viewW);
        return;
    }

    // Right-edge anchor; width matches Svelte (`w-52` ≈ 208px).
    constexpr float kPanelW = 220.0f;
    ImGui::SetNextWindowPos(ImVec2(float(viewW) - kPanelW - 8.0f, 80.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelW, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(4, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(4, 4));
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##npc_proximity", nullptr, kFlags)) {
        for (auto& r : rows) {
            const auto& kind = view.get<ecs::NPCKind>(r.e);
            const auto& hp   = view.get<ecs::Health>(r.e);
            const auto& lvl  = view.get<ecs::NpcLevel>(r.e);
            const auto& ch   = view.get<ecs::NpcCharacter>(r.e);

            const NPCType t  = NPCType(kind.type);
            const auto&   def = npc_def(t);

            // Resolve faction colour through the macro registry. Falls
            // back to a neutral grey for unknown ids.
            const char* fid = faction_id_for_idx(kind.factionIdx);
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
            const Sprite* sp = sprite_get(npc_sprite(t));
            if (sp && sp->tex) {
                ImGui::Image(static_cast<ImTextureID>(sp->tex),
                             ImVec2(40, 40));
            } else {
                ImGui::Dummy(ImVec2(40, 40));
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
            const bool tradeOk = npc_trade_supported(t)
                && w.reg.all_of<ecs::NpcInventory>(r.e);
            if (!tradeOk) ImGui::BeginDisabled();
            if (ImGui::Button("Trade", ImVec2(58, 22))) {
                g_trade_npc = r.e;
            }
            if (!tradeOk) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !tradeOk) {
                ImGui::SetTooltip("Trade not wired for this NPC.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Attack", ImVec2(62, 22))) {
                GameEvent ev{EventTag::BattleStart};
                ev.a = entt::to_integral(r.e);
                ev.s1 = npcName;
                ev.s2 = def.label;
                ev.ix = int(lvl.value);
                bus.emit(ev);
                gs.subState.kind = GameSubStateKind::Battle;
                gs.subState.enemyId = npcName;
                gs.subState.eventId = def.label;
                g_talk_npc = entt::null;
                g_talk_line = nullptr;
                g_trade_npc = entt::null;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Emit BattleStart. Combat resolver pending.");
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();   // ChildBorderSize
            ImGui::PopStyleColor(2); // ChildBg + Border
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

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
                g_talk_npc = entt::null;
                g_talk_line = nullptr;
            }
        }
        ImGui::End();
    }

    if (g_trade_npc != entt::null) {
        const bool validTrade =
            w.reg.valid(g_trade_npc) &&
            w.reg.all_of<ecs::NPCKind, ecs::NpcInventory, ecs::NpcCharacter>(g_trade_npc);
        if (!validTrade) {
            g_trade_npc = entt::null;
        } else {
            const auto& kind = w.reg.get<ecs::NPCKind>(g_trade_npc);
            auto& bag = w.reg.get<ecs::NpcInventory>(g_trade_npc);
            const auto& ch = w.reg.get<ecs::NpcCharacter>(g_trade_npc);
            const NPCType t = NPCType(kind.type);
            const auto& def = npc_def(t);
            if (!npc_trade_supported(t)) {
                g_trade_npc = entt::null;
            } else {
                const char* npcName = npc_display_name(def, ch);
                ImGui::SetNextWindowPos(ImVec2(float(viewW) * 0.5f, 190.0f),
                                        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("NPC Trade", nullptr,
                                 ImGuiWindowFlags_NoCollapse)) {
                    ImGui::Text("%s  Gold %d", npcName, gs.player.gold);
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
                                ? player_buy_price(item->value, gs.player.attributes.cha, 0)
                                : 0;
                            const bool canBuy = item && count > 0 && gs.player.gold >= price;
                            ImGui::PushID(int(i));
                            if (!canBuy) ImGui::BeginDisabled();
                            if (ImGui::Button("Buy", ImVec2(52, 0))) {
                                gs.player.gold -= price;
                                gs.player.inventory.add(id, 1);
                                bag.inv.remove(id, 1);
                                changed = true;
                            }
                            if (!canBuy) ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !item) {
                                ImGui::SetTooltip("Unknown item id");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s x%d  %d g",
                                        item ? item->name : id.c_str(), count, price);
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
                                ? player_sell_price(item->value, gs.player.attributes.cha, 0)
                                : 0;
                            ImGui::PushID(int(i));
                            if (!item) ImGui::BeginDisabled();
                            if (ImGui::Button("Sell", ImVec2(52, 0))) {
                                gs.player.gold += price;
                                bag.inv.add(id, 1);
                                gs.player.inventory.remove(id, 1);
                                changed = true;
                            }
                            if (!item) ImGui::EndDisabled();
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !item) {
                                ImGui::SetTooltip("Unknown item id");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s x%d  %d g",
                                        item ? item->name : id.c_str(), count, price);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();
                    ImGui::Columns(1);

                    if (ImGui::Button("Close", ImVec2(-FLT_MIN, 0))) {
                        g_trade_npc = entt::null;
                    }
                }
                ImGui::End();
            }
        }
    }

    draw_battle_pending_modal(gs, viewW);
}

} // namespace sm::ui

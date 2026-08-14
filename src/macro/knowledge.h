// The player's knowledge of the macro map — the fog-of-war layer.
//
// One byte per cell, three states:
//   0  Unknown   terra incognita — the world draws BLACK, nothing overlays.
//   1  Explored  memory — grey relief and roads, faded landmarks, markers.
//   2  Visible   in sight right now — the full living picture.
//
// Only the EXPLORED bit is persistent (the save clamps 2 → 1 on write, v40):
// "what I see now" is a projection of where the player stands, recomputed by
// update_player_sight whenever their cell changes — and therefore after every
// load, from the restored position.
//
// SIGHT IS LIGHT. The sweep is the same bounded terrain-optical Dijkstra the
// night-glow bake runs (macro/optics.h optical_sweep): open land carries the
// eye far, a road corridor further, canopy smothers it per unit of tree
// density, and a ridge walls it off while a hilltop overlooks the valley
// below. One physics, one tuning — a cell a town's glow cannot reach is a
// cell an eye standing in that town cannot see.
//
// The sight BUDGET (in cells of open ground) is the caller's: the app derives
// it from the standing eye's horizon and will later scale it by attributes
// and skills — this layer only spends whatever budget it is handed. Quests
// and events open the map through the same door (reveal_area), so "the elder
// marks your map" and "you walked there" are indistinguishable afterwards.
//
// L1 macro layer, TreeLayer's shape: grid + revision (renderer refresh gate,
// never serialized). GameState owns one; the renderer samples it as R8
// (binding 5) once Increment 2 lands.
#pragma once
#include <cstdint>
#include <vector>

#include "macro/optics.h"

namespace sm {

enum : std::uint8_t {
    kKnowledgeUnknown  = 0,
    kKnowledgeExplored = 1,
    kKnowledgeVisible  = 2,
};

struct KnowledgeLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;
    // Runtime dirty counter — bumped on every mutation so the renderer knows
    // to refresh its field texture. Never serialized.
    std::uint32_t revision = 0;

    std::size_t cell_count() const {
        std::size_t n = 0;
        return FeatureLayer::cell_count_for(width, height, n) ? n : 0u;
    }
    bool has_complete_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n;
    }
    // THE door every consumer asks — overlays, NPC draw filters, the map
    // screen. Torus-wrapped; an absent grid answers Unknown (fail closed:
    // a world with no knowledge layer shows nothing, not everything).
    std::uint8_t at(int x, int y) const {
        if (width <= 0 || height <= 0 || data.empty()) return kKnowledgeUnknown;
        const int wx = FeatureLayer::wrap_coord(x, width);
        const int wy = FeatureLayer::wrap_coord(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        return i < data.size() ? data[i] : std::uint8_t(kKnowledgeUnknown);
    }
};

// Session half of the player's sight: which cells are Visible right now (so
// a move can decay exactly those to Explored), the Dijkstra scratch, and the
// cell the last sweep ran from. App-owned, reset with the world — never saved.
struct SightRuntime {
    OpticalScratch scratch;
    std::vector<std::uint32_t> visibleCells;
    int lastCellX = INT32_MIN, lastCellY = INT32_MIN;
};

// All-Unknown grid of the given dims — a new world starts dark.
void knowledge_reset(KnowledgeLayer& k, int width, int height);

// Recompute the player's sight if their cell changed since the last call:
// previous Visible cells decay to Explored, then the optical sweep from the
// player's cell marks the new Visible set (Explored underneath forever).
// Returns true when the layer changed (revision bumped) — the renderer's cue
// to refresh. One integer compare when the cell is unchanged, so calling it
// every frame costs nothing between crossings.
bool update_player_sight(KnowledgeLayer& k, SightRuntime& rt,
                         const OpticalWorld& world,
                         float playerX, float playerY, float budgetCells);

// The quest/event door: mark Explored (never Visible) everything an eye at
// (cx, cy) with this budget would see — same physics as the player's own
// sight, so a revealed area looks exactly like a remembered one.
void reveal_area(KnowledgeLayer& k, const OpticalWorld& world,
                 OpticalScratch& scratch, float cx, float cy,
                 float budgetCells);

} // namespace sm

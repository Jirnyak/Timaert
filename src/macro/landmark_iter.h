// THE landmark enumeration — the one place that knows which GameState
// collections hold placed landmarks. Every consumer (map draw loop, hover
// pick, minimap, collect_landmarks) walks THIS visitor and dispatches on the
// registry row (landmark_registry.h), so a new landmark kind = its registry
// row + one yield here — no consumer is touched. Yields POD views in
// registry-row order per kind (settlements, villages, spires — the same
// priority resolve_context scans in, so "first hit at a cell" agrees with
// the subworld's idea of who owns the cell).
//
// Split from landmark_registry.h because the visitor needs the full
// GameState definition, and the registry table is included by far lighter
// headers (map_data.h, seasons.h).
#pragma once
#include "macro/landmark_registry.h"
#include "macro/state.h"

namespace sm {

struct LandmarkView {
    LandmarkType type;
    int  id;          // unique within its kind (the collection's own id)
    int  x, y;
    const char* name; // display name; never null, may be ""
    int  population;  // 0 where the kind has none
    SettlementMood mood = SettlementMood::Stable; // meaningful for City/Village
    bool depleted = false;                        // meaningful for Spire
};

template <class F>
void for_each_landmark(const GameState& gs, F&& fn) {
    for (const auto& s : gs.settlements) {
        fn(LandmarkView{LandmarkType::City, s.id, s.x, s.y,
                        s.name.c_str(), s.population, s.mood, false});
    }
    for (const auto& v : gs.villages) {
        fn(LandmarkView{LandmarkType::Village, v.id, v.x, v.y,
                        v.name.c_str(), v.population, v.mood, false});
    }
    for (const auto& sp : gs.spires) {
        fn(LandmarkView{LandmarkType::Spire, sp.id, sp.x, sp.y,
                        sp.depleted ? "Depleted Spire" : "Spire",
                        0, SettlementMood::Stable, sp.depleted});
    }
}

} // namespace sm

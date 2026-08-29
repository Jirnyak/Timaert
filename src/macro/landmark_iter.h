// THE landmark enumeration over the ONE roster (gs.landmarks, CANON S9
// 2026-08-29). Every consumer (map draw loop, hover pick, minimap,
// collect_landmarks, the cell grid) walks THIS visitor and dispatches on the
// registry row (landmark_registry.h), so a new landmark kind = its registry
// row + its entry in the yield order below — no consumer is touched. The
// yield order is the ONE cell-ownership priority ("first hit at a cell"
// agrees with the subworld's idea of who owns the cell).
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

// Cell-ownership priority (CANON S9): the order kinds are yielded IS the one
// law of who owns a contested cell — the same order the old three-vector walk
// had (cities first, then villages, then spires). Storage is one vector in
// creation order (state.h gs.landmarks); the priority lives here, once.
inline constexpr LandmarkType kLandmarkYieldOrder[] = {
    LandmarkType::City, LandmarkType::Village, LandmarkType::Spire,
    LandmarkType::Ruin, LandmarkType::Lair, LandmarkType::Shrine,
    LandmarkType::Mine, LandmarkType::Tower,
};

template <class F>
void for_each_landmark(const GameState& gs, F&& fn) {
    for (LandmarkType t : kLandmarkYieldOrder) {
        for (const auto& lm : gs.landmarks) {
            if (lm.type != t) continue;
            const char* name = lm.name.c_str();
            if (t == LandmarkType::Spire && lm.name.empty()) {
                name = lm.depleted ? "Depleted Spire" : "Spire";
            }
            fn(LandmarkView{lm.type, lm.id, lm.x, lm.y, name,
                            lm.population, lm.mood, lm.depleted});
        }
    }
}

} // namespace sm

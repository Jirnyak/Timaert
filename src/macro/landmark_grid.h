// THE baked "cell → landmark" index (CANON S6/S9, 2026-08-24).
//
// Before this grid, "what stands on this cell" was answered by linear scans
// over settlements / villages / spires — written at least three times
// (resolve_context, fauna's landmark_kind_at, the spire placer), each with its
// own priority order, one of them already drifted (canon-audit C2). The scan
// order IS a world fact — who owns a cell two landmarks would share — so it
// must exist once. It exists in for_each_landmark (landmark_iter.h); this grid
// is that order, baked: first landmark yielded at a cell wins it.
//
// The grid answers position → {type, id} and NOTHING else. Live facts —
// population, spell tier, kingdom, depleted — drift daily and are resolved
// from GameState by the {type, id} the grid returns, at the moment of asking.
// A stale grid can therefore mis-answer only "who stands here", which changes
// exactly when a landmark is born, dies or transmutes (CANON S9) — the rebake
// points. Today that is world-gen and load; the living-landmarks track adds
// its transitions here and nowhere else.
//
// u16 slot per cell into a compact ref list: 2 MiB per 1024² world, and the
// cap of 65534 landmarks speaks loudly instead of truncating (S26).
#pragma once
#include "core/torus.h"
#include "macro/landmark_iter.h"

#include <cstdio>
#include <cstdint>
#include <vector>

namespace sm {

struct LandmarkRef {
    LandmarkType type = LandmarkType::None;
    std::int32_t id = -1;   // WORLD-unique landmark ordinal (v54); -1 = none
};

struct LandmarkGrid {
    static constexpr std::uint16_t kNoLandmark = 0xFFFF;

    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> slot;   // per cell: index into refs, or kNoLandmark
    std::vector<LandmarkRef>   refs;

    // Torus-wrapped, fail-closed: an unbuilt grid answers "nothing stands
    // here" — the zero contribution, never a crash.
    LandmarkRef at(int x, int y) const {
        if (width <= 0 || height <= 0
            || slot.size() != std::size_t(width) * std::size_t(height)) {
            return {};
        }
        if (!world_shape_ok(width, height)) return {};
        const std::uint16_t s = slot[cell_of(x, y, width)];
        return s == kNoLandmark ? LandmarkRef{} : refs[s];
    }
};

inline LandmarkGrid build_landmark_grid(const GameState& gs) {
    LandmarkGrid g;
    g.width = gs.mapW;
    g.height = gs.mapH;
    if (g.width <= 0 || g.height <= 0) return g;
    g.slot.assign(std::size_t(g.width) * std::size_t(g.height),
                  LandmarkGrid::kNoLandmark);
    g.refs.clear();
    for_each_landmark(gs, [&](const LandmarkView& lv) {
        auto& s = g.slot[cell_of(lv.x, lv.y, g.width)];
        // First landmark yielded at a cell owns it — the iterator's order is
        // the ONE priority (it is the same order resolve_context used to scan).
        if (s != LandmarkGrid::kNoLandmark) return;
        // THE CAP SPEAKS OUT LOUD (CANON S26). This used to be an `assert`,
        // which is nothing at all in the build the player runs: past 65 534
        // landmarks the u16 slot would have taken kNoLandmark's own value and
        // every cell of that place would have answered «nothing stands here»
        // — a world silently missing a town, in release only. The refusal is
        // said and the cell is honestly left empty instead.
        if (g.refs.size() >= LandmarkGrid::kNoLandmark) {
            std::fprintf(stderr,
                         "[landmark-grid] slot space exhausted at %zu "
                         "landmarks — cell %d,%d left unowned\n",
                         g.refs.size(), cell_x(cell_of(lv.x, lv.y, g.width),
                                               g.width),
                         cell_y(cell_of(lv.x, lv.y, g.width), g.width));
            return;
        }
        s = std::uint16_t(g.refs.size());
        g.refs.push_back(LandmarkRef{lv.type, lv.id});
    });
    return g;
}

} // namespace sm

#include "macro/ruins.h"
#include "macro/landmark_registry.h"
#include "macro/state.h"
#include "macro/zones.h"
#include "macro/map_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <cstdio>

namespace sm {

namespace {

// Candidate draws per ruin — the same budget the spire pass runs
// (spires.cpp kSpireCandidateTries): the ruin band (51..229) covers most
// non-civilized land, so misses are rare and handled by simply placing
// fewer ruins, out loud.
constexpr int kRuinCandidateTries = 64;

// How many ruins a world carries: one per city plus this base handful —
// density follows civilization's own scale instead of a map-size constant
// (a bigger politik raises more cities AND leaves more dead ones behind).
constexpr int kRuinBaseCount = 4;

int torus_chebyshev_(int ax, int ay, int bx, int by, int w, int h) {
    int dx = std::abs(ax - bx);
    dx = std::min(dx, w - dx);
    int dy = std::abs(ay - by);
    dy = std::min(dy, h - dy);
    return std::max(dx, dy);
}

// Pre-grid occupancy scan, exactly like the spire pass (spires.cpp): the
// baked cell→landmark index does not exist yet during worldgen, and a
// co-located ruin would be silently shadowed by whatever stands on top.
bool cell_occupied_(const GameState& gs, int x, int y) {
    for (const auto& lm : gs.landmarks)
        if (lm.x == x && lm.y == y) return true;
    return false;
}

} // namespace

void generate_ruins(GameState& gs, const ZoneLayer& zones,
                    const TerrainData& terrain, std::uint8_t seaLevel8) {
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_complete_storage()) {
        return;
    }
    const LandmarkDef& def = landmark_def(LandmarkType::Ruin);
    int cities = 0;
    for (const auto& lm : gs.landmarks) {
        if (lm.type == LandmarkType::City) ++cities;
    }
    const int target = kRuinBaseCount + cities;
    // Own deterministic stream, distinct from the spire pass (0x59B12E50)
    // and the landmark-naming salt (0xC1A05E1D).
    Rng rng(gs.worldSeed ^ 0x2A15DEADu);
    gs.landmarks.reserve(gs.landmarks.size() + std::size_t(target));

    int placed = 0;
    for (int n = 0; n < target; ++n) {
        // Best-candidate (Mitchell) pick over the row's own zone band: among
        // admissible draws take the one farthest from every ruin already
        // placed — even spread over the dangerous middle of the world.
        int bestX = -1, bestY = -1, bestScore = -1;
        for (int t = 0; t < kRuinCandidateTries; ++t) {
            const int x = int(rng.next_u32() % std::uint32_t(gs.mapW));
            const int y = int(rng.next_u32() % std::uint32_t(gs.mapH));
            if (terrain.is_water(x, y, seaLevel8)) continue;
            const int z = int(zones.at(x, y));
            if (z < int(def.minZone) || z > int(def.maxZone)) continue;
            if (cell_occupied_(gs, x, y)) continue;
            int score = gs.mapW + gs.mapH;   // no ruins yet: any site wins
            for (const auto& lm : gs.landmarks) {
                if (lm.type != LandmarkType::Ruin) continue;
                score = std::min(score, torus_chebyshev_(x, y, lm.x, lm.y,
                                                         gs.mapW, gs.mapH));
            }
            if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }
        }
        if (bestScore < 0) {
            // A world can genuinely lack admissible land — fewer ruins then,
            // and the shortfall is said, never silent (CANON S28).
            std::fprintf(stderr,
                         "[worldgen] ruin %d/%d: no admissible site in zone "
                         "band %d..%d\n",
                         n + 1, target, int(def.minZone), int(def.maxZone));
            continue;
        }
        Landmark ruin{};
        ruin.type = LandmarkType::Ruin;
        ruin.id   = int(gs.nextLandmarkOrdinal++);
        ruin.x    = bestX;
        ruin.y    = bestY;
        // Born with its haunt: the row's born columns × the site's own
        // danger byte, a discrete bell around the mean — redder land, harder
        // haunt. Its own stream per ruin ordinal, so placement draws above
        // stay untangled from the roll.
        {
            Rng popRng(gs.worldSeed ^ 0xB0125EEDu
                       ^ (std::uint32_t(ruin.id) * 2654435761u));
            ruin.population = landmark_born_population(
                int(def.bornPopBase), int(def.bornPopPerScore),
                int(zones.at(bestX, bestY)), popRng);
        }
        gs.landmarks.push_back(std::move(ruin));
        ++placed;
    }
    std::printf("[worldgen] ruins: %d of %d placed (%d cities)\n",
                placed, target, cities);
}

} // namespace sm

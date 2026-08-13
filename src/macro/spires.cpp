#include "macro/spires.h"
#include "macro/landmark_registry.h"
#include "macro/map_generator.h"
#include "macro/state.h"
#include "macro/zones.h"
#include "core/rng.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace sm {

namespace {

// Candidate draws per gate step. The gate admits the wild tail of the danger
// field (zone >= 5); the fbm base minus civ pull keeps a healthy fraction of
// land there on every non-degenerate world, so 64 draws miss an admissible
// band only when the band is genuinely rare — and a rare band is handled by
// relaxing the gate one zone, not by drawing harder.
constexpr int kSpireCandidateTries = 64;

int torus_chebyshev(int ax, int ay, int bx, int by, int w, int h) {
    int dx = std::abs(ax - bx);
    dx = std::min(dx, w - dx);
    int dy = std::abs(ay - by);
    dy = std::min(dy, h - dy);
    return std::max(dx, dy);
}

// A spire may not share a cell with any named place: resolve_context and
// fauna's landmark_kind_at scan settlements -> villages -> spires in order,
// so a co-located spire would be silently shadowed by the town on top of it.
bool cell_occupied(const GameState& gs, int x, int y) {
    for (const auto& s : gs.settlements)
        if (s.x == x && s.y == y) return true;
    for (const auto& v : gs.villages)
        if (v.x == x && v.y == y) return true;
    for (const auto& sp : gs.spires)
        if (sp.x == x && sp.y == y) return true;
    return false;
}

} // namespace

void generate_spires(GameState& gs, const ZoneLayer& zones,
                     const TerrainData& terrain, std::uint8_t seaLevel8,
                     std::span<const SpireSpellSpec> spells) {
    if (gs.mapW <= 0 || gs.mapH <= 0 || !terrain.has_rgba_storage()
        || !zones.has_data_storage()) {
        return;
    }
    const LandmarkDef& def = landmark_def(LandmarkType::Spire);
    // Own deterministic stream, distinct from the landmark-naming salt in
    // populate_landmarks_from_politik (0xC1A05E1D).
    Rng rng(gs.worldSeed ^ 0x59B12E50u);
    gs.spires.reserve(gs.spires.size() + spells.size());

    for (const SpireSpellSpec& spec : spells) {
        // The tier walks the gate through the table's own band: tier 1 opens
        // at minZone ("Untamed"), tier 5 demands maxZone ("Hellgate") — the
        // spire of a doom spell stands where the world is at its worst, and
        // its garrison strength follows from the site's own spawn context,
        // not from any per-spire scaling.
        const int tier = std::clamp(int(spec.tier), 1,
                                    int(def.maxZone) - int(def.minZone) + 1);
        int bestX = -1, bestY = -1, bestScore = -1;
        int gate = std::min<int>(def.maxZone, int(def.minZone) + tier - 1);
        for (; gate >= int(def.minZone); --gate) {
            // Best-candidate (Mitchell) pick: among admissible draws take the
            // one farthest from every spire already placed — even spread over
            // the wild band without a tuned separation constant.
            for (int t = 0; t < kSpireCandidateTries; ++t) {
                const int x = int(rng.next_u32() % std::uint32_t(gs.mapW));
                const int y = int(rng.next_u32() % std::uint32_t(gs.mapH));
                if (terrain.is_water(x, y, seaLevel8)) continue;
                if (int(zones.at(x, y)) < gate) continue;
                if (cell_occupied(gs, x, y)) continue;
                int score = gs.mapW + gs.mapH;   // no spires yet: any site wins
                for (const auto& sp : gs.spires) {
                    score = std::min(score, torus_chebyshev(x, y, sp.x, sp.y,
                                                            gs.mapW, gs.mapH));
                }
                if (score > bestScore) {
                    bestScore = score;
                    bestX = x;
                    bestY = y;
                }
            }
            if (bestScore >= 0) break;
        }
        if (bestScore < 0) {
            // A world can genuinely lack admissible land (all-ocean or fully
            // civilized test maps). The spell is then not offered — say so.
            std::fprintf(stderr,
                         "[worldgen] spire for spell ordinal %u: no admissible "
                         "site at any zone >= %d\n",
                         spec.spellOrdinal, int(def.minZone));
            continue;
        }
        Spire sp{};
        sp.id       = int(gs.spires.size());
        sp.x        = bestX;
        sp.y        = bestY;
        sp.spellId  = spec.spellOrdinal;
        sp.tier     = std::uint8_t(tier);
        sp.depleted = false;
        gs.spires.push_back(sp);
    }
}

} // namespace sm

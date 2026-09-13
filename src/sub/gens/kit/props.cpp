#include "sub/gens/kit/props.h"

#include "core/rng.h"

#include <cmath>

namespace sm::sub::kit {

// Street lanterns: posts on the verge of paved ground, spaced by the light's
// own reach so their pools just meet. Placed on plain ground ADJACENT to a
// road/square tile — on the verge, never in the lane, so nothing a body walks
// down is blocked by a post.
void scatter_street_lanterns(SubworldMapData& out, int center,
                                    int radius, std::uint32_t seed) {
    const float reach = structure_kind_row(Structure::Lantern).lightRadiusTiles;
    // Spacing is HALF the reach, not the whole of it. A pool falls off
    // quadratically to nothing at `reach`, so posts a full reach apart meet
    // where both give nothing — a street of beads with dark between them.
    // At half, every point of the lane lies inside two pools and the street
    // is lit. The cost is the light FIELD's (sub/lighting.h): a splat of a
    // few dozen 3 m cells per lamp, rebuilt every 8th frame, so density here
    // is bought with arithmetic nobody can feel.
    const int step = std::max(8, int(reach) / 2);
    Rng r(seed);
    auto is_lane = [&](int x, int y) {
        if (x < 1 || y < 1 || x >= kCellSize - 1 || y >= kCellSize - 1) {
            return false;
        }
        const std::uint8_t t = out.tiles[std::size_t(y) * kCellSize + x];
        return t == TILE_ROAD || t == TILE_SQUARE;
    };
    // Start from the LANE and step off it, rather than from a lattice hoping
    // to land on a verge: a dense town's verges are mostly house footprint,
    // so the hopeful version lit three posts in a city of four hundred houses
    // (caught by prop_interaction_test, 2026-08-12).
    auto placeable = [&](int x, int y) {
        if (x < 2 || y < 2 || x >= kCellSize - 2 || y >= kCellSize - 2) {
            return false;
        }
        const std::uint8_t t = out.tiles[std::size_t(y) * kCellSize + x];
        return t == TILE_GRASS || t == TILE_EMPTY || t == TILE_FIELD;
    };
    for (int y = center - radius; y <= center + radius; y += step) {
        for (int x = center - radius; x <= center + radius; x += step) {
            // Jitter along the lattice so the town does not look surveyed.
            const int lx = x + int(r.next_u32() % 5u) - 2;
            const int ly = y + int(r.next_u32() % 5u) - 2;
            if (!is_lane(lx, ly)) continue;      // find the lane…
            // …then step onto its verge, whichever side has room. Three
            // tiles is the widest a post may stand from the lane it lights
            // before it stops reading as street furniture.
            int px = -1, py = -1;
            for (int d = 1; d <= 3 && px < 0; ++d) {
                if      (placeable(lx + d, ly)) { px = lx + d; py = ly; }
                else if (placeable(lx - d, ly)) { px = lx - d; py = ly; }
                else if (placeable(lx, ly + d)) { px = lx; py = ly + d; }
                else if (placeable(lx, ly - d)) { px = lx; py = ly - d; }
            }
            if (px < 0) continue;
            Structure s{};
            s.kind = Structure::Lantern;
            s.x = float(px) + 0.5f;
            s.y = float(py) + 0.5f;
            s.hx = structure_min_half_xy(Structure::Lantern);
            s.hy = s.hx;
            s.radius = s.hx;
            s.height = structure_min_height(Structure::Lantern);
            s.shape = Structure::Cylinder;      // a post, not a crate
            out.structures.push_back(s);
        }
    }
}

// The heart of a settlement: a well on the square and a signboard beside it.
// Both stand on the paved centre every settlement generator lays down, so the
// rule is the same for a city and a village and neither generator has to know
// what the other did.
void stamp_village_green(SubworldMapData& out, int center,
                                std::uint32_t seed) {
    Rng r(seed);
    // The well sits a few paces off the exact centre — a square with a hole
    // dead in the middle is a diagram, not a place.
    const float ang = r.next_f01() * 6.2831853f;
    const float off = 3.0f + r.next_f01() * 3.0f;
    Structure w{};
    w.kind = Structure::Well;
    w.x = float(center) + std::cos(ang) * off;
    w.y = float(center) + std::sin(ang) * off;
    w.hx = structure_min_half_xy(Structure::Well);
    w.hy = w.hx;
    w.radius = w.hx;
    w.height = structure_min_height(Structure::Well);
    w.shape = Structure::Cylinder;      // a curb is round
    out.structures.push_back(w);

    // The board faces the well across the square, so whoever reads it is
    // standing where the place can be seen.
    Structure sg{};
    sg.kind = Structure::Sign;
    sg.x = float(center) - std::cos(ang) * (off + 4.0f);
    sg.y = float(center) - std::sin(ang) * (off + 4.0f);
    sg.hx = structure_min_half_xy(Structure::Sign);
    sg.hy = sg.hx * 0.25f;
    sg.radius = sg.hx;
    sg.height = structure_min_height(Structure::Sign);
    sg.yaw = ang;
    out.structures.push_back(sg);
}
} // namespace sm::sub::kit

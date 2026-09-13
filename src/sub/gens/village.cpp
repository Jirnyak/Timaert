// THE VILLAGE — a green, a handful of lanes and the fields that feed it.
//
// Same build order as the city, for the same reason: the tract arrives, the
// wall (if this place is big enough to keep one) is raised across it and opens
// on it, the lanes grow inside, the houses line them, the fields take the rest.
// A hamlet raises no wall at all and its outline is simply its core.
#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/city_layout.h"
#include "sub/gens/kit/growth.h"
#include "sub/gens/kit/outline.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/village_palisade.h"

#include "core/rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sm::sub {
// The kit is this module's vocabulary — primitives, never a sibling module.
using namespace kit;

void gen_village(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;

    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    clear_decor_tiles(out);

    const int centre = kCellSize / 2;
    const float cf = float(centre);
    const int population = std::max(kSettlementFootprint.villagePopFloor,
                                    ctx.landmark.size);
    // How tall the village's wall stands — asked of the trunk the village
    // fells, because that is what the wall IS (gens/village_palisade.h). The
    // berm and the postern's reach are measured off it, so a stockade keeps a
    // strip clear of itself in ITS own terms rather than a city curtain's.
    const float palisadeH = structure_min_height(Structure::Palisade);

    // Separate streams, same reason as the city: the wall's shape must not be
    // a function of how many houses the placement loop happened to try.
    Rng rWall(ctx.seed ^ 0x7A11u);
    Rng rBuild(ctx.seed ^ 0xABCDEFu);

    // ── 1. The tract ──────────────────────────────────────────────────────
    const RoadAxisSet axes = settlement_road_axes(nbFeature);
    carve_settlement_main_roads(out, ctx, axes, centre, ctx.seed ^ 0xA115EEDu);

    // Village footprint from the same authority the city uses and the citizen
    // populator reads (sub/city_layout.h).
    const float settleR = village_core_radius(population);
    const bool walled = village_is_walled(population);
    // Grown, not drawn — same three laws as a city (slope, wet ground, the
    // pull of the tract). A hamlet strung along a road comes out a ribbon,
    // which is what a hamlet on a road IS.
    const Outline grown = grow_outline(out, cf, cf,
                                       village_core_radius_guaranteed(population),
                                       village_target_area(population),
                                       village_max_radius(population));
    const Outline area = walled
        ? wall_ring_noise(grown.scaled(village_wall_radius(population)
                                       / std::max(1.0f, settleR)),
                          kSettlementWallRing.villageRoughness, rWall)
        : grown;

    // A walled village with no road neighbour still needs one way out to its
    // own fields — otherwise the ring finds no paving and closes solid.
    if (walled && !axes.anchored) {
        const float ang = float(ctx.seed & 0xFFFFu) / 65535.0f * 6.2831853f;
        const float reach = area.at(ang) + palisadeH;
        carve_organic_road(out, centre, centre,
                           int(std::floor(cf + std::cos(ang) * reach)),
                           int(std::floor(cf + std::sin(ang) * reach)),
                           ctx.seed ^ 0x9057E54u);
    }

    // ── 2. The palisade ───────────────────────────────────────────────────
    // The village's OWN wall, raised by the village's own module: trunks set
    // shoulder to shoulder, a timber frame where the road comes through, one
    // watch platform over it. It used to call the city's masonry primitive
    // with the height dialled down, and a hamlet stood behind eight metres of
    // stone with round towers (owner's ruling, 2026-09-13 — see
    // gens/village_palisade.h).
    std::array<WallGate, 8> gates{};
    int gateCount = 0;
    if (walled) {
        gateCount = std::min(stamp_palisade(out, area, gates.data(),
                                            int(gates.size())),
                             int(gates.size()));
    }

    // ── 3. The green and the lanes ────────────────────────────────────────
    // Short radials from the centre so houses have lanes to line even when no
    // main road reaches a neighbour. Bounded to the core, so a lane never runs
    // off toward a neighbour nobody built a road to ("into the void").
    {
        const int spokes = std::clamp(4 + population / 40, 4, 8);
        const float base = float(ctx.seed & 0xFFFFu) / 65535.0f * 6.2831853f;
        for (int i = 0; i < spokes; ++i) {
            const float a = base + (float(i) / float(spokes)) * 6.2831853f;
            const float reach = std::max(20.0f, settleR * 0.95f);
            const int ex = int(std::floor(cf + std::cos(a) * reach));
            const int ey = int(std::floor(cf + std::sin(a) * reach));
            carve_organic_road(out, centre, centre, ex, ey,
                               ctx.seed + std::uint32_t(0x51EE7u + i * 41));
        }
    }
    const int squareSize = 3 + int(rBuild.next_u32() % 4u);
    stamp_rect(out, centre - squareSize / 2, centre - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);
    stamp_village_green(out, centre, ctx.seed ^ 0x3E11A0u);

    // ── 4. The houses ─────────────────────────────────────────────────────
    // The inset is the same idea as a city's: nobody builds against the wall.
    // An unwalled hamlet's outline is its core, so the inset is zero there —
    // its last cottage IS its edge.
    const float houseInset = walled ? kSettlementFootprint.cityHouseInset : 0.0f;
    const int houses = village_house_target(population);
    int placedHouses = 0;
    for (int attempt = 0; placedHouses < houses && attempt < houses * 64; ++attempt) {
        if (try_add_roadside_house(out, rBuild, area, houseInset, 2, 3,
                                   4.0f + rBuild.next_f01() * 2.5f)) {
            ++placedHouses;
        }
    }

    // ── 5. The fields ─────────────────────────────────────────────────────
    const float berm = walled ? palisadeH : 5.0f;
    const float belt = std::min(float(kCellSize) * 0.08f,
                                30.0f + float(population) * 0.3f);
    const int targetFields = std::min(40, std::max(2, population / 20));
    std::array<int, 40> fieldX{};
    std::array<int, 40> fieldY{};
    int fields = 0;
    for (int attempt = 0; fields < targetFields && attempt < targetFields * 25; ++attempt) {
        const float a = rBuild.next_f01() * 6.2831853f;
        const float inner = area.at(a) + berm;
        const float d = inner + rBuild.next_f01() * std::max(1.0f, belt);
        const int fw = 8 + int(rBuild.next_u32() % 13u);
        const int fh = 6 + int(rBuild.next_u32() % 11u);
        const int fx = int(std::floor(cf + std::cos(a) * d));
        const int fy = int(std::floor(cf + std::sin(a) * d));
        if (add_field_rect(out, fx, fy, fw, fh)) {
            fieldX[std::size_t(fields)] = fx;
            fieldY[std::size_t(fields)] = fy;
            ++fields;
        }
    }
    // Farm tracks leave by a gate when there is a wall to leave by, and
    // straight off the green when there is not.
    const int farmRoads = std::min(fields, std::max(1, population / 80));
    for (int i = 0; i < farmRoads; ++i) {
        const int idx = (i * fields) / std::max(1, farmRoads);
        const float fx = float(fieldX[std::size_t(idx)]);
        const float fy = float(fieldY[std::size_t(idx)]);
        int sx = centre, sy = centre;
        if (gateCount > 0) {
            const float want = std::atan2(fy - cf, fx - cf);
            int best = 0;
            float bestGap = 7.0f;
            for (int g = 0; g < gateCount; ++g) {
                float gap = std::fabs(gates[std::size_t(g)].angle - want);
                if (gap > 3.14159265f) gap = 6.2831853f - gap;
                if (gap < bestGap) { bestGap = gap; best = g; }
            }
            sx = int(gates[std::size_t(best)].x);
            sy = int(gates[std::size_t(best)].y);
        }
        carve_organic_road(out, sx, sy, int(fx), int(fy),
                           ctx.seed + std::uint32_t(0xBEEFu + i * 53));
    }

    // Trees keep the same berm off the edge of the place that the plough does.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount,
        /*clearRadius*/ int(area.max_radius() + berm), ctx.seed);
    // The village's field plots grow the same wheat as open farmland.
    scatter_field_crops(ctx, out);
}

} // namespace sm::sub

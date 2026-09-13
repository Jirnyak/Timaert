// THE CITY — a walled place with a heart, a street plan and a hinterland.
//
// The order the module builds in IS the design, because each step reads what
// the previous one left on the ground:
//
//   1. the TRACT arrives first (it was there before the town — the macro map
//      stamps a road on every city cell), aimed at the seam anchors its
//      neighbours will aim back at;
//   2. the WALL is raised across it, and opens exactly where it finds paving
//      under itself — so a gate is a gate onto a real road, once, and the
//      empty arch beside a second opening is structurally impossible;
//   3. the STREETS grow inside the wall, unable to breach it (a lane does not
//      cut masonry — sub/gens/kit/streets.cpp);
//   4. the HOUSES fill the blocks, bounded by the wall's own outline per
//      bearing, so none of them can stand where the ring is going to run;
//   5. the FIELDS take the hinterland beyond the berm, and their tracks come
//      back to a GATE rather than to an arbitrary point of the compass.
//
// Until 2026-09-13 this ran almost backwards — houses and fields first, wall
// last and yielding to both — which is why the owner's walls had holes in them
// and his gates came in pairs.
#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/city_layout.h"
#include "sub/gens/kit/growth.h"
#include "sub/gens/kit/lanes.h"
#include "sub/gens/kit/outline.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/kit/wall.h"

#include "core/rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {
// The kit is this module's vocabulary — primitives, never a sibling module.
using namespace kit;

namespace {

// A city's curtain is three courses of the wall module; each ring outward from
// the core is a course taller, because the newest ring is the one that has to
// answer the current age's siege. The module itself is the prop table's
// minimum wall height — the one brick this world builds masonry out of.
inline float city_curtain_height(int ring) {
    return structure_min_height(Structure::Wall) * float(3 + ring);
}

// The BERM: the clear strip a wall keeps outside its own foot — its ditch, its
// footing, and the room to walk around it. One curtain height, because that is
// the distance the wall itself occupies when it falls, and it is what stops a
// besieger's cover (a ploughed strip or an orchard) touching the masonry.
inline float city_berm(int ring) { return city_curtain_height(ring); }

// THE street plan a city grows (see sub/gens/kit/lanes.h). Every number is a
// statement about traffic, not a look:
//
//   · a tip decides where to go every 6 tiles — about a house frontage, the
//     scale at which a street can actually bend around something;
//   · it branches every 24 — four frontages, which is a BLOCK: shorter and
//     the town is all junctions, longer and the interior of a block is
//     unreachable;
//   · it wanders ±0.25 rad, enough that no street is a ruler line and little
//     enough that one still goes somewhere;
//   · it joins paving within 5 tiles rather than running beside it;
//   · and it counts ground served 18 tiles either side — a plot's depth plus
//     its back yard, i.e. exactly the ground one lane can actually front.
LanePlan city_lane_plan(int population, float frontageTiles, float heartRadius) {
    LanePlan p{};
    p.stepTiles   = 6.0f;
    p.branchEvery = 24.0f;
    p.wanderRad   = 0.25f;
    p.mergeRadius = 5.0f;
    p.serveRadius = 18.0f;
    // Streets leaving the market grow with the town: a great city's square is
    // a hub of many, a small one's a crossroads.
    p.heartSpokes = std::clamp(3 + population / 2000, 3, 8);
    p.heartRadius = heartRadius;
    // The network is finished when it can front the town's buildings.
    p.frontageTiles = frontageTiles;
    p.maxSegs       = 4000;   // backstop against a pathological seed only
    return p;
}

} // namespace

void gen_city(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;

    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    clear_decor_tiles(out);

    const int centre = kCellSize / 2;
    const float cf = float(centre);
    const int population = ctx.landmark.size;   // floored by city_wall_radius
    const int ringCount = city_wall_rings(population);

    // The wall draws its shape from its OWN stream. It used to share one with
    // the house and field loops and was stamped after them, so the ring's
    // geometry was a function of how many placement attempts those loops
    // happened to consume — change a house size band and every wall in the
    // world moved.
    Rng rWall(ctx.seed ^ 0x3A11u);
    Rng rBuild(ctx.seed ^ 0xC1C1C1u);

    // ── 1. The tract ──────────────────────────────────────────────────────
    const RoadAxisSet axes = settlement_road_axes(nbFeature);
    carve_settlement_main_roads(out, ctx, axes, centre, ctx.seed ^ 0x7711AAu);

    // ── The SHAPE ─────────────────────────────────────────────────────────
    // Grown, not drawn: from the core the town holds regardless, outward over
    // the cheapest ground — down the tract it just carved, along the flat,
    // never into the wet — until it covers the area its population needs. This
    // is why the town is long where the road runs and pinched where the hill
    // is, instead of being the circle it was until 2026-09-13.
    //
    // Everything downstream already reads an Outline, so the shape changing
    // from a circle to this cost them no change at all.
    const Outline shape = grow_outline(out, cf, cf,
                                       city_core_radius(population),
                                       city_target_area(population),
                                       city_max_radius(population));

    // Outlines first: the wall's shape is what every later step is measured
    // against, so it is decided before a single tile is built on. Inner rings
    // are the SAME shape scaled — a town's older cores stood on the same
    // ground and were bent by the same hills.
    const float nominal = std::max(1.0f, float(city_wall_radius(population)));
    std::array<Outline, 5> rings{};
    static_assert(5 == 1 + 4, "city_wall_rings caps at five concentric rings");
    for (int ring = 0; ring < ringCount; ++ring) {
        rings[std::size_t(ring)] = wall_ring_noise(
            shape.scaled(city_ring_wall_radius(population, ring) / nominal),
            city_wall_roughness(ring), rWall);
    }
    const Outline& rim = rings[std::size_t(ringCount - 1)];

    // A town with no road neighbour is still a town people leave: without one
    // track out, the ring would find no paving under itself and close solid.
    // The bearing is the cell's own coin, so the postern is where this town
    // put it and nowhere else.
    if (!axes.anchored) {
        const float ang = float(ctx.seed & 0xFFFFu) / 65535.0f * 6.2831853f;
        const float reach = rim.at(ang) + city_berm(ringCount - 1);
        carve_organic_road(out, centre, centre,
                           int(std::floor(cf + std::cos(ang) * reach)),
                           int(std::floor(cf + std::sin(ang) * reach)),
                           ctx.seed ^ 0x9057E54u);
    }

    // ── 2. The wall ───────────────────────────────────────────────────────
    // Inner rings are the town's older cores; the outermost is what encloses
    // it, and its gates are the ones the hinterland comes back to.
    std::array<WallGate, 16> gates{};
    int gateCount = 0;
    for (int ring = 0; ring < ringCount; ++ring) {
        const bool outermost = ring == ringCount - 1;
        const int cut = stamp_wall(out, rings[std::size_t(ring)],
                                   WallStyle{city_curtain_height(ring), true},
                                   outermost ? gates.data() : nullptr,
                                   outermost ? int(gates.size()) : 0);
        if (outermost) gateCount = std::min(cut, int(gates.size()));
    }

    // ── 3. The streets, grown ─────────────────────────────────────────────
    // From the tract and the market outward, branching and joining, dying on
    // ground another lane already serves (sub/gens/kit/lanes.h).
    std::array<float, 16> gx{}, gy{};
    for (int g = 0; g < gateCount; ++g) {
        gx[std::size_t(g)] = gates[std::size_t(g)].x;
        gy[std::size_t(g)] = gates[std::size_t(g)].y;
    }
    // How much street this town needs: every house wants a street face, and a
    // lane offers two of them — one down each side.
    const int houses = city_house_target(population);
    constexpr float kPlotFace = 6.0f;   // mean of widthMin..Max + gapMin..Max
    const float frontage = float(houses) * kPlotFace * 0.5f;
    const int squareSize = std::clamp(5 + population / 5000, 5, 10);
    Rng rLanes(ctx.seed ^ 0x51A7E11u);
    LaneNet lanes = grow_lanes(out, rim, city_layout().streetWallInset,
                               gx.data(), gy.data(), gateCount, cf, cf,
                               city_lane_plan(population, frontage,
                                              float(squareSize) * 0.5f + 2.0f),
                               rLanes);
    // The wall lane: how the garrison reaches any stretch of its own curtain
    // without threading a yard. An alley, just inside the building line.
    carve_pomerium(out, rim, city_layout().streetWallInset, lanes);

    // The market and the keep. The plaza grows with the town it serves, and
    // the keep stands ON its edge rather than in it — stamped clear, so the
    // paving no longer eats the citadel's own footprint tiles.
    stamp_rect(out, centre - squareSize / 2, centre - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    const int keepBase = std::clamp(4 + population / 1500, 6, 16);
    const int keepW = keepBase + int(rBuild.next_u32() % 3u);
    const int keepH = keepBase + int(rBuild.next_u32() % 3u);
    const float keepY = cf - float(squareSize) * 0.5f - float(keepH) * 0.5f - 1.0f;
    const bool keepPlaced = stamp_landmark_house(out, rBuild, cf, keepY,
                                                 keepW, keepH,
                                                 city_curtain_height(0));

    // ── 4. The houses, FRONTING the streets ───────────────────────────────
    // A town is its street frontage. Plots are laid down both sides of every
    // lane, doors onto the lane, neighbours a yard apart — instead of being
    // scattered anywhere within ten tiles of paving, which from the air read
    // as buildings dropped on a meadow.
    // Frontage is taken in GROWTH ORDER — the order the streets themselves
    // appeared, which starts at the tract and the market. Nothing needs
    // sorting, because the network is grown to exactly the length the town's
    // houses can line (lanes.h frontageTiles): every lane gets its frontage,
    // and a lane that would have stood empty was never laid.
    std::vector<float> fx0, fy0, fx1, fy1, fhw;
    fx0.reserve(lanes.segs.size());
    for (const LaneSeg& sg : lanes.segs) {
        fx0.push_back(sg.x0); fy0.push_back(sg.y0);
        fx1.push_back(sg.x1); fy1.push_back(sg.y1);
        fhw.push_back(lane_half_width(sg.rank));
    }
    FrontagePlan fp{};
    fp.setback   = 1.0f;     // a step off the carriageway, no more
    fp.depthMin  = 4.0f;     // a room deep…
    fp.depthMax  = 8.0f;     // …to a workshop with a back room
    fp.widthMin  = 3.0f;     // a burgage plot's narrow street face
    fp.widthMax  = 6.0f;
    fp.gapMin    = 0.0f;     // terraced where the town is dense…
    fp.gapMax    = 3.0f;     // …to a gated yard between
    fp.heightMin = 5.0f;
    fp.heightMax = 9.0f;
    int placedHouses = (keepPlaced ? 1 : 0)
        + lay_frontage(out, rBuild, fx0.data(), fy0.data(),
                       fx1.data(), fy1.data(), fhw.data(),
                       int(fx0.size()), fp,
                       std::max(0, houses - (keepPlaced ? 1 : 0)));
    // Whatever frontage could not seat (a town with few lanes, a seed where
    // the plots collided) falls back to the scatter, so the house COUNT the
    // population law asks for is still met.
    for (int attempt = 0; placedHouses < houses && attempt < houses * 24; ++attempt) {
        if (try_add_roadside_house(out, rBuild, rim,
                                   kSettlementFootprint.cityHouseInset,
                                   3, 5, 5.0f + rBuild.next_f01() * 4.0f)) {
            ++placedHouses;
        }
    }

    // Street lighting: a town that keeps a wall keeps lamps along its
    // thoroughfares. Spacing is the lantern's own reach from the prop table.
    scatter_street_lanterns(out, centre, int(rim.max_radius()),
                            ctx.seed ^ 0x1A47E27u);
    stamp_village_green(out, centre, ctx.seed ^ 0x3E11A0u);

    // ── 5. The hinterland ─────────────────────────────────────────────────
    const float berm = city_berm(ringCount - 1);
    const int targetFields = std::min(80, std::max(6, population / 50));
    std::array<int, 80> fieldX{};
    std::array<int, 80> fieldY{};
    int fields = 0;
    for (int attempt = 0; fields < targetFields && attempt < targetFields * 18; ++attempt) {
        const float a = rBuild.next_f01() * 6.2831853f;
        const float inner = rim.at(a) + berm;
        const float outer = float(kCellSize) * 0.48f;
        const float d = inner + rBuild.next_f01() * std::max(1.0f, outer - inner);
        const int fw = 8 + int(rBuild.next_u32() % 15u);
        const int fh = 6 + int(rBuild.next_u32() % 13u);
        const int fx = int(std::floor(cf + std::cos(a) * d));
        const int fy = int(std::floor(cf + std::sin(a) * d));
        if (add_field_rect(out, fx, fy, fw, fh)) {
            fieldX[std::size_t(fields)] = fx;
            fieldY[std::size_t(fields)] = fy;
            ++fields;
        }
    }

    // Field tracks run from a GATE. They used to start at a cardinal point
    // outside the wall that belonged to no opening at all — a road from
    // nowhere to a field, passing the ring wherever it happened to cross.
    if (gateCount > 0) {
        const int fieldRoads = std::min(fields, std::max(2, population / 1500));
        for (int i = 0; i < fieldRoads; ++i) {
            const int idx = (i * fields) / std::max(1, fieldRoads);
            const float fx = float(fieldX[std::size_t(idx)]);
            const float fy = float(fieldY[std::size_t(idx)]);
            // The gate this field would actually be carted through: the one
            // whose bearing is closest to the field's.
            const float want = std::atan2(fy - cf, fx - cf);
            int best = 0;
            float bestGap = 7.0f;   // larger than any angular gap (π)
            for (int g = 0; g < gateCount; ++g) {
                float gap = std::fabs(gates[std::size_t(g)].angle - want);
                if (gap > 3.14159265f) gap = 6.2831853f - gap;
                if (gap < bestGap) { bestGap = gap; best = g; }
            }
            carve_organic_road(out,
                int(gates[std::size_t(best)].x), int(gates[std::size_t(best)].y),
                int(fx), int(fy),
                ctx.seed + std::uint32_t(0xF00Du + i * 97));
        }
    }

    // Trees keep the same berm off the masonry as the plough does — and off
    // the OUTERMOST ring, which a four-ring city's old clear radius did not
    // reach, so a metropolis grew woods inside its own walls.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount,
        /*clearRadius*/ int(rim.max_radius() + berm), ctx.seed);
    // The city's field plots grow the same wheat as open farmland — one door.
    scatter_field_crops(ctx, out);
}

} // namespace sm::sub

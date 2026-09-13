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
#include "sub/gens/city_wall.h"

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

// A city's curtain is three courses of the wall module — the prop table's
// minimum wall height, the one brick this world builds masonry out of. There
// is ONE curtain: a town moved its wall outward as it grew and pulled the old
// one down, it did not keep a set of nested rings.
inline float city_curtain_height() {
    return structure_min_height(Structure::Wall) * 3.0f;
}

// The BERM: the clear strip a wall keeps outside its own foot — its ditch, its
// footing, and the room to walk around it. One curtain height, because that is
// the distance the wall itself occupies when it falls, and it is what stops a
// besieger's cover (a ploughed strip or an orchard) touching the masonry.
inline float city_berm() { return city_curtain_height(); }

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
    // The garrison's houses stand in the upper quarter, not on the town's
    // streets: the town lays the REST (sub/city_layout.h).
    const int upperHouses = city_upper_houses(population);

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

    // ONE curtain. The wall's shape is what every later step is measured
    // against, so it is decided before a single tile is built on.
    const Outline rim = wall_ring_noise(shape, city_wall_roughness(), rWall);

    // ── THE UPPER QUARTER ─────────────────────────────────────────────────
    // A walled DISTRICT inside the city, sized by the garrison that lives in
    // it (sub/city_layout.h), with its own gates, its own streets and its own
    // houses — not a castle-sized ring stuck to the curtain, which is what the
    // first cut produced and what the owner rightly called ridiculous.
    //
    // It backs onto the wall at the highest ground the curtain reaches. That
    // is where upper towns were built and why: the high ground, and a gate
    // OUTWARD past the city — supply under siege, and a way out when it is the
    // townsmen doing the besieging.
    int castleBin = 0;
    {
        float best = -1.0f;
        for (int b = 0; b < Outline::kBearings; ++b) {
            const float ang = float(b) * Outline::kTwoPi / float(Outline::kBearings);
            // Sample where the castle would actually sit: a little inside the
            // curtain, since that is the ground it must stand on.
            const float d = std::max(1.0f, rim.r[std::size_t(b)]
                                         - city_upper_radius(population));
            const int x = std::clamp(int(cf + std::cos(ang) * d), 0, kCellSize - 1);
            const int y = std::clamp(int(cf + std::sin(ang) * d), 0, kCellSize - 1);
            const float h = out.heightmap[std::size_t(y) * kCellSize + x];
            if (h > best) { best = h; castleBin = b; }
        }
    }
    const float castleAng = float(castleBin) * Outline::kTwoPi
                          / float(Outline::kBearings);
    const float castleR = city_upper_radius(population);
    const float castleDist = std::max(1.0f, rim.at(castleAng) - castleR * 0.75f);
    const float castleX = cf + std::cos(castleAng) * castleDist;
    const float castleY = cf + std::sin(castleAng) * castleDist;
    const Outline castle = wall_ring_noise(
        Outline::disk(castleX, castleY, castleR),
        city_wall_roughness(), rWall);

    // Its road: from the castle's own yard, outward past the curtain. One
    // carve opens a gate in BOTH walls — the castle's and the city's — because
    // a gate is wherever a ring finds paving under itself.
    {
        const float reach = castleR + city_berm() * 2.0f
                          + rim.at(castleAng) - castleDist;
        carve_organic_road(out,
            int(castleX), int(castleY),
            int(std::floor(castleX + std::cos(castleAng) * reach)),
            int(std::floor(castleY + std::sin(castleAng) * reach)),
            ctx.seed ^ 0xCA571E1u);
        // …and its road INTO the town, so the castle is not a sealed island:
        // the lord's men ride down to the market.
        carve_organic_road(out, int(castleX), int(castleY), centre, centre,
                           ctx.seed ^ 0xCA57102u);
    }

    // A town with no road neighbour is still a town people leave: without one
    // track out, the ring would find no paving under itself and close solid.
    // The bearing is the cell's own coin, so the postern is where this town
    // put it and nowhere else.
    if (!axes.anchored) {
        const float ang = float(ctx.seed & 0xFFFFu) / 65535.0f * 6.2831853f;
        const float reach = rim.at(ang) + city_berm();
        carve_organic_road(out, centre, centre,
                           int(std::floor(cf + std::cos(ang) * reach)),
                           int(std::floor(cf + std::sin(ang) * reach)),
                           ctx.seed ^ 0x9057E54u);
    }

    // ── 2. The walls ──────────────────────────────────────────────────────
    // The curtain first — its gates are the ones the hinterland comes back to
    // — then the castle's enceinte, which is taller: a castle out-tops the
    // town it watches, or it cannot watch it.
    std::array<WallGate, 16> gates{};
    int gateCount = std::min(
        stamp_city_wall(out, rim, CurtainStyle{city_curtain_height(), true},
                   gates.data(), int(gates.size())),
        int(gates.size()));
    // …clipped by the curtain, so the quarter's own wall is only the arc that
    // stands INSIDE the town. Its outer side IS the city wall — that is what
    // "backed into the curtain" means, and stamping a full circle there gave
    // two walls running alongside each other with the quarter bulging out past
    // the town (owner, 2026-09-13).
    std::array<WallGate, 8> upperGates{};
    const int upperGateCount = std::min(
        stamp_city_wall(out, castle,
            CurtainStyle{city_curtain_height()
                         + structure_min_height(Structure::Wall), true},
            upperGates.data(), int(upperGates.size()), &rim),
        int(upperGates.size()));

    // ── THE MOUTHS OF THE GATEWAYS ────────────────────────────────────────
    // Ground the town has already spoken for, handed to its own plot layer
    // below. A gateway is a way THROUGH, and a way through needs room on both
    // sides of the masonry — but the frontage lines every lane the town has,
    // so without being told, a plot lands across the opening. The owner
    // photographed exactly that at two gates of the upper quarter
    // (2026-09-13): a house standing in the mouth, with the garrison's road
    // running into its back wall.
    //
    // The reach is the GATEWAY'S OWN WIDTH (kit/outline.h WallGate::span), so
    // no distance is invented here. And it is the CITY that states it, not the
    // placer that infers it: the wall and the plots are the same module's
    // work, which is the whole reason this is data and not a special case
    // somewhere else (owner's ruling on module encapsulation, 2026-09-13).
    std::array<KeepOut, 24> mouths{};
    int mouthCount = 0;
    auto claim_mouth = [&](const WallGate& g) {
        if (mouthCount >= int(mouths.size())) return;
        mouths[std::size_t(mouthCount++)] = {g.x, g.y, g.span};
    };
    for (int g = 0; g < gateCount; ++g)      claim_mouth(gates[std::size_t(g)]);
    for (int g = 0; g < upperGateCount; ++g) claim_mouth(upperGates[std::size_t(g)]);

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
    const int houses = std::max(0, city_house_target(population) - upperHouses);
    constexpr float kPlotFace = 6.0f;   // mean of widthMin..Max + gapMin..Max
    const float frontage = float(houses) * kPlotFace * 0.5f;
    // THE MARKET, sized by the sellers it must hold (sub/city_layout.h) —
    // ~40 tiles across for a town of six thousand, not the 6x6 stamp it was.
    const int squareSize = std::max(5, int(std::sqrt(
        city_market_area(city_house_target(population)))));
    Rng rLanes(ctx.seed ^ 0x51A7E11u);
    LaneNet lanes = grow_lanes(out, rim, city_layout().streetWallInset,
                               gx.data(), gy.data(), gateCount, cf, cf,
                               city_lane_plan(population, frontage,
                                              float(squareSize) * 0.5f + 2.0f),
                               rLanes);
    // The wall lane: how the garrison reaches any stretch of its own curtain
    // without threading a yard. An alley, just inside the building line.
    carve_pomerium(out, rim, city_layout().streetWallInset, lanes);

    // The MARKET stays where the roads meet, in the middle of the town. That
    // is what a market is: the place everyone's road passes through.
    stamp_rect(out, centre - squareSize / 2, centre - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    // ── The UPPER QUARTER's own life ──────────────────────────────────────
    // Its streets grow from its gates toward its own heart, exactly as the
    // city's grow from theirs — the same kit, a second call. That is what
    // makes it a quarter rather than a walled yard: lanes, frontage and houses
    // of its own, for the garrison that lives in it.
    LaneNet upperLanes;
    if (upperHouses > 0) {
        std::array<float, 8> ugx{}, ugy{};
        int ugc = 0;
        for (int g = 0; g < gateCount && ugc < int(ugx.size()); ++g) {
            const float dx = gates[std::size_t(g)].x - castleX;
            const float dy = gates[std::size_t(g)].y - castleY;
            if (dx * dx + dy * dy > castleR * castleR * 4.0f) continue;
            ugx[std::size_t(ugc)] = gates[std::size_t(g)].x;
            ugy[std::size_t(ugc)] = gates[std::size_t(g)].y;
            ++ugc;
        }
        LanePlan up = city_lane_plan(population,
                                     float(upperHouses) * kPlotFrontage * 0.5f,
                                     castleR * 0.25f);
        up.heartSpokes = 3;
        upperLanes = grow_lanes(out, castle, city_layout().streetWallInset * 0.5f,
                                ugx.data(), ugy.data(), ugc,
                                castleX, castleY, up, rLanes);
        // …and its own wall lane. A ring of masonry without one is a ring its
        // garrison cannot reach: the frontage below lines every lane there is,
        // so with no alley behind the enceinte the houses close over the
        // approach to the quarter's own gates and the men who hold that wall
        // walk through somebody's yard to get to it (owner, 2026-09-13 — two
        // gates of the quarter photographed with a house in the mouth).
        //
        // This step existed for the curtain and was simply missing here: the
        // quarter is a hand-written second copy of the town's build order, and
        // a copy loses a field silently. city_wall_integrity_test now asserts
        // the property for EVERY ring, so the next district cannot lose it.
        carve_pomerium(out, castle, city_layout().streetWallInset * 0.5f,
                       upperLanes);
    }

    // The KEEP stands in the upper quarter and out-tops the curtain by two
    // courses — the whole point of a keep is that the last defence is also the
    // highest one.
    const int keepBase = std::clamp(4 + population / 1500, 6, 16);
    const int keepW = keepBase + int(rBuild.next_u32() % 3u);
    const int keepH = keepBase + int(rBuild.next_u32() % 3u);
    const bool keepPlaced = stamp_landmark_house(
        out, rBuild, castleX, castleY, keepW, keepH,
        city_curtain_height() + structure_min_height(Structure::Wall) * 2.0f);

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
                       std::max(0, houses - (keepPlaced ? 1 : 0)),
                       mouths.data(), mouthCount);
    // Whatever frontage could not seat (a town with few lanes, a seed where
    // the plots collided) falls back to the scatter, so the house COUNT the
    // population law asks for is still met.
    for (int attempt = 0; placedHouses < houses && attempt < houses * 24; ++attempt) {
        if (try_add_roadside_house(out, rBuild, rim,
                                   kSettlementFootprint.cityHouseInset,
                                   3, 5, 5.0f + rBuild.next_f01() * 4.0f,
                                   mouths.data(), mouthCount)) {
            ++placedHouses;
        }
    }
    // …and the rest go on the BACKLANDS. Both placers above want a lane — one
    // fronts it, the other wants paving within ten tiles — so between them
    // they seated about three quarters of the town and a quarter of its
    // households had no door at all. The remainder now takes the bald ground
    // inside the walls, biggest patch first (kit/plots.h lay_backland_houses);
    // owner, 2026-09-13: «пустыри всё равно без улиц есть, пусть там будут
    // дома». It costs the yards about a twelfth of their ground, which is what
    // the measurement said before the argument did.
    placedHouses += lay_backland_houses(out, rBuild, rim,
                                        kSettlementFootprint.cityHouseInset,
                                        fp, houses - placedHouses,
                                        mouths.data(), mouthCount);

    // …and the garrison's own houses, along the quarter's own lanes.
    if (!upperLanes.segs.empty()) {
        std::vector<float> ux0, uy0, ux1, uy1, uhw;
        for (const LaneSeg& sg : upperLanes.segs) {
            ux0.push_back(sg.x0); uy0.push_back(sg.y0);
            ux1.push_back(sg.x1); uy1.push_back(sg.y1);
            uhw.push_back(lane_half_width(sg.rank));
        }
        const int upperPlaced = lay_frontage(
            out, rBuild, ux0.data(), uy0.data(), ux1.data(), uy1.data(),
            uhw.data(), int(ux0.size()), fp, upperHouses,
            mouths.data(), mouthCount);
        // The quarter owes its garrison as many roofs as the town owes its
        // townsmen, so its backlands fill by the same law — inside its OWN
        // ring, which is what keeps the barracks in the compartment.
        lay_backland_houses(out, rBuild, castle,
                            city_layout().streetWallInset * 0.5f,
                            fp, upperHouses - upperPlaced,
                            mouths.data(), mouthCount);
    }

    // ── THE YARDS ─────────────────────────────────────────────────────────
    // The ground between the houses is BUDGETED as yard by the land-per-house
    // law (eighty-one tiles of a hundred and seventeen), and until now nothing
    // stood on it — which is why a town read as bald from the street though
    // its density was right to three per cent. Gardens, hurdles and a well per
    // block (kit/plots.h lay_yards).
    //
    // A well serves the households that can carry water from it, and how many
    // that is belongs to the STREET PLAN: a block is branchEvery tiles of
    // lane, a plot takes kPlotFace of frontage, and a lane fronts two sides.
    const int housesPerBlock = std::max(1,
        int(city_lane_plan(population, 0.0f, 0.0f).branchEvery / kPlotFace) * 2);
    lay_yards(out, rBuild, housesPerBlock);

    // Street lighting: a town that keeps a wall keeps lamps along its
    // thoroughfares. Spacing is the lantern's own reach from the prop table.
    scatter_street_lanterns(out, centre, int(rim.max_radius()),
                            ctx.seed ^ 0x1A47E27u);
    stamp_village_green(out, centre, ctx.seed ^ 0x3E11A0u);

    // ── 5. The hinterland ─────────────────────────────────────────────────
    const float berm = city_berm();
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

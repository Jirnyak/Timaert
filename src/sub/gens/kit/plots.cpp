#include "sub/gens/kit/plots.h"

#include "sub/gens/kit/noise.h"
#include "sub/gens/kit/tiles.h"

#include <algorithm>
#include <cmath>

namespace sm::sub::kit {

// How many houses this cell has raised so far — the ordinal the next one
// gets. The interior behind a door is found by counting Houses in the door's
// window cell (engine.cpp enter_dungeon_by_door), so the number a door is
// stamped with must be produced by the same counting rule.
std::uint16_t house_count(const SubworldMapData& out) {
    std::uint16_t n = 0;
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::House) ++n;
    }
    return n;
}

// Oriented house: the universal placement primitive. Stamps the ROTATED
// footprint (every tile whose centre falls inside the yawed box, slightly
// fattened so the tile stamp fully covers the solid), flattens exactly those
// cells to their mean (cut-vs-fill: a pad is carved as much as built),
// and emits ONE oriented Structure record — independent width/length/yaw, the
// silhouette the 3D pass draws and the collision index blocks with. Clearance
// is checked on the footprint's AABB plus a 1-tile ring (conservative for a
// rotated box, which only costs a few rejected attempts).
bool add_house_obb(SubworldMapData& out, float cx, float cy,
                          float hx, float hy, float yaw, float height,
                          bool requireClear) {
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    const float ex = std::fabs(hx * cs) + std::fabs(hy * sn);
    const float ey = std::fabs(hx * sn) + std::fabs(hy * cs);
    const int x0 = int(std::floor(cx - ex));
    const int y0 = int(std::floor(cy - ey));
    const int x1 = int(std::ceil(cx + ex));
    const int y1 = int(std::ceil(cy + ey));
    if (x0 < 2 || y0 < 2 || x1 >= kCellSize - 2 || y1 >= kCellSize - 2) {
        return false;
    }
    if (requireClear) {
        for (int yy = y0 - 1; yy <= y1 + 1; ++yy) {
            for (int xx = x0 - 1; xx <= x1 + 1; ++xx) {
                const std::uint8_t t = out.tiles[std::size_t(yy) * kCellSize + xx];
                // Nothing a generator already decided: no lane, no plaza, no
                // neighbour's wall, no plough, no other house.
                if (tile_is(t, kTileBuilt)) {
                    return false;
                }
            }
        }
    }
    auto inside = [&](float px, float py) {
        const float dx = px - cx;
        const float dy = py - cy;
        const float lx = dx * cs + dy * sn;
        const float ly = -dx * sn + dy * cs;
        return std::fabs(lx) <= hx + 0.35f && std::fabs(ly) <= hy + 0.35f;
    };
    double sum = 0.0;
    int cnt = 0;
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            if (!inside(float(xx) + 0.5f, float(yy) + 0.5f)) continue;
            sum += out.heightmap[std::size_t(yy) * kCellSize + xx];
            ++cnt;
        }
    }
    if (cnt == 0) return false;
    const float level = float(sum / double(cnt));
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            if (!inside(float(xx) + 0.5f, float(yy) + 0.5f)) continue;
            const std::size_t idx = std::size_t(yy) * kCellSize + xx;
            out.tiles[idx] = TILE_HOUSE;
            out.trav[idx] = 0;
            out.heightmap[idx] = level;
        }
    }
    Structure s{};
    s.kind = Structure::House;
    s.x = cx;
    s.y = cy;
    s.radius = std::max(hx, hy);
    s.height = height;
    s.yaw = yaw;
    s.hx = hx;
    s.hy = hy;
    // Every house is built with its ordinal already known — it is simply how
    // many houses this cell has raised so far, which is exactly the number
    // the composite will count back when a door is opened.
    const std::uint16_t ordinal = house_count(out);
    out.structures.push_back(s);

    // ...and the door it is entered by. A building without a visible way in
    // is a building the player cannot read: the door hangs flush on the
    // +local-Y face, its own leaf, carrying the ordinal that names this house.
    Structure d{};
    d.kind = Structure::Door;
    // A leaf, not a gate: 1.5 tiles across and half a tile deep, HUNG ON the
    // wall — its centre is pushed out by its own half-depth so the whole leaf
    // stands proud of the facade. Centred flush (the first cut) it was half
    // swallowed by the house box and, on a wall of the same colour, invisible.
    d.hx = std::min(hx * 0.6f, 0.75f);
    d.hy = structure_min_half_xy(Structure::Door);
    const float out_ = hy + d.hy;
    d.x = cx - out_ * sn;      // outward normal of the +local-Y face
    d.y = cy + out_ * cs;
    d.yaw = yaw;
    d.radius = std::max(d.hx, d.hy);
    d.height = structure_min_height(Structure::Door);
    d.tag = ordinal;
    out.structures.push_back(d);
    return true;
}

bool stamp_landmark_house(SubworldMapData& out, Rng& r,
                                 float cx, float cy, int w, int h,
                                 float height) {
    // The keep gets a modest random lean — enough to break the axis-aligned
    // grid look without turning the citadel diagonal to its own plaza.
    const float yaw = (r.next_f01() * 2.0f - 1.0f) * 0.35f;
    return add_house_obb(out, cx, cy, float(w) * 0.5f, float(h) * 0.5f,
                         yaw, height, /*requireClear=*/false);
}

bool try_add_roadside_house(SubworldMapData& out, Rng& r,
                            const Outline& area, float inset,
                            int minSize, int maxSize,
                            float height) {
    // Sample the outline's bounding box and reject by bearing, rather than by
    // a scalar radius: a place is not a disk, and a house that obeyed the MEAN
    // radius of a wandering wall stood in the masonry wherever the wall dipped
    // inward — which is how a wall got holes it never stamped.
    const int radius = int(std::floor(area.max_radius()));
    if (radius <= 0) return false;
    const int cxT = int(area.cx);
    const int cyT = int(area.cy);
    const int span = radius * 2 + 1;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int hxT = cxT + int(r.next_u32() % std::uint32_t(span)) - radius;
        const int hyT = cyT + int(r.next_u32() % std::uint32_t(span)) - radius;
        if (!area.contains(float(hxT), float(hyT), inset)) continue;
        if (!has_tile_near(out, hxT, hyT, 10, TILE_ROAD)) continue;
        // Independent continuous width / length and a free orientation: houses
        // stop being one quantised square. Width and length draw from the same
        // band, so proportions range from square to ~1:2 barns.
        const float sizeRange = float(std::max(0, maxSize - minSize));
        const float w = float(minSize) + r.next_f01() * sizeRange;
        const float h = float(minSize) + r.next_f01() * sizeRange;
        const float yaw = r.next_f01() * 3.14159265f;
        if (add_house_obb(out, float(hxT), float(hyT),
                          w * 0.5f, h * 0.5f, yaw, height,
                          /*requireClear=*/true)) {
            return true;
        }
    }
    return false;
}

int lay_frontage(SubworldMapData& out, Rng& r,
                 const float* x0, const float* y0,
                 const float* x1, const float* y1,
                 const float* halfWidth, int segCount,
                 const FrontagePlan& plan, int maxHouses) {
    int placed = 0;
    for (int s = 0; s < segCount && placed < maxHouses; ++s) {
        const float dx = x1[s] - x0[s];
        const float dy = y1[s] - y0[s];
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0f) continue;
        const float ux = dx / len, uy = dy / len;      // along the street
        const float nx = -uy,      ny =  ux;           // across it

        for (int side = 0; side < 2; ++side) {
            const float sgn = side == 0 ? 1.0f : -1.0f;
            float along = 0.0f;
            while (along < len && placed < maxHouses) {
                const float w = plan.widthMin
                    + r.next_f01() * (plan.widthMax - plan.widthMin);
                const float d = plan.depthMin
                    + r.next_f01() * (plan.depthMax - plan.depthMin);
                const float gap = plan.gapMin
                    + r.next_f01() * (plan.gapMax - plan.gapMin);
                const float centreAlong = along + w * 0.5f;
                if (centreAlong > len) break;

                // The plot sits a setback back from the carriageway, and its
                // own half-depth beyond that.
                const float off = halfWidth[s] + plan.setback + d * 0.5f;
                const float cx = x0[s] + ux * centreAlong + nx * sgn * off;
                const float cy = y0[s] + uy * centreAlong + ny * sgn * off;

                // THE orientation: local +Y is where the door hangs
                // (add_house_obb), so aim it back at the street. Without this
                // the door of a frontage house opens onto its own back yard.
                const float inX = -nx * sgn, inY = -ny * sgn;
                const float yaw = std::atan2(-inX, inY);

                const float height = plan.heightMin
                    + r.next_f01() * (plan.heightMax - plan.heightMin);
                if (add_house_obb(out, cx, cy, w * 0.5f, d * 0.5f, yaw, height,
                                  /*requireClear=*/true)) {
                    ++placed;
                }
                along += w + gap;
            }
        }
    }
    return placed;
}

bool add_field_rect(SubworldMapData& out, int cx, int cy, int w, int h) {
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    if (x < 2 || y < 2 || x + w >= kCellSize - 2 || y + h >= kCellSize - 2) {
        return false;
    }
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            const std::uint8_t t = out.tiles[std::size_t(yy) * kCellSize + xx];
            // Built ground, and open water besides: a plough turns bare earth,
            // and it may turn LAST YEAR's field again (TILE_FIELD is not in
            // this mask) — that is how neighbouring strips share a balk.
            if (tile_is(t, (kTileBuilt & ~TILE_M_FIELD) | TILE_M_WATER)) {
                return false;
            }
        }
    }
    stamp_rect(out, x, y, w, h, TILE_FIELD, 1);
    return true;
}
// Crop stands on ploughed ground — ONE scatterer for every producer of
// TILE_FIELD (the FT_Field module below and the settlement field plots), so
// a village garden and open farmland grow the same wheat through the same
// door. Deterministic per GLOBAL lattice node (the glade-scan idiom): a
// coarse lattice with hash jitter, gated per node, planted only where the
// tile really is ploughed. A stand is a Structure::Crop — non-solid, drawn
// by the billboard pass (sprite row kCropSpriteRow), harvested through the
// same loot door as a tree (map_data.h kStructureKindRows).
void scatter_field_crops(const CellContext& ctx, SubworldMapData& out) {
    constexpr int   kCropStepTiles = 8;     // lattice pitch (po2)
    // Node survival scales with the cell's FERTILITY (the same moisture
    // channel the field stamp scored by) — a lush river meadow stands thick,
    // a dry margin stands thin. The owner's law: crop density is a function
    // of the cell's own fertility, settlements included, no special case.
    constexpr float kCropGateMax   = 0.90f; // survival at fertility 1.0
    constexpr float kCropMinH      = 0.9f;  // stand height band, metres
    constexpr float kCropMaxH      = 1.5f;
    // Stand footprint as a fraction of its height — the billboard aspect the
    // renderer preserves (same law as trees: radius authored here once).
    constexpr float kCropWidthRatio = 0.45f;

    const float gate = kCropGateMax * std::clamp(ctx.fertility01, 0.0f, 1.0f);
    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    const int startGX = gox + ((kCropStepTiles - (gox % kCropStepTiles))
                               % kCropStepTiles);
    const int startGY = goy + ((kCropStepTiles - (goy % kCropStepTiles))
                               % kCropStepTiles);
    // The harvest scar (crop_count row): the sickle took this many stands
    // and regrowth has not returned them yet. The LAST `scar` stands in
    // lattice order stay down, so a harvested field comes back thinner and
    // returning to it never resurrects the wheat. Deterministic: same scar
    // → same survivors.
    int quota = -1;
    if (ctx.cropHarvested > 0) {
        int natural = 0;
        for (int gy = startGY; gy < goy + kCellSize; gy += kCropStepTiles) {
            for (int gx = startGX; gx < gox + kCellSize; gx += kCropStepTiles) {
                if (noise01(gx, gy, ctx.seed ^ 0xc50f5eedu) > gate) continue;
                const int jx = int(noise01(gx + 7, gy + 3, ctx.seed) * 7.0f) - 3;
                const int jy = int(noise01(gx + 1, gy + 9, ctx.seed) * 7.0f) - 3;
                const int x = gx - gox + jx;
                const int y = gy - goy + jy;
                if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
                if (out.tiles[std::size_t(y) * kCellSize + x] != TILE_FIELD)
                    continue;
                ++natural;
            }
        }
        quota = std::max(0, natural - ctx.cropHarvested);
    }

    for (int gy = startGY; gy < goy + kCellSize; gy += kCropStepTiles) {
        for (int gx = startGX; gx < gox + kCellSize; gx += kCropStepTiles) {
            if (quota == 0) return;
            if (noise01(gx, gy, ctx.seed ^ 0xc50f5eedu) > gate) continue;
            // Hash jitter inside the lattice cell so rows of wheat do not
            // stand on a visible grid.
            const int jx = int(noise01(gx + 7, gy + 3, ctx.seed) * 7.0f) - 3;
            const int jy = int(noise01(gx + 1, gy + 9, ctx.seed) * 7.0f) - 3;
            const int x = gx - gox + jx;
            const int y = gy - goy + jy;
            if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] != TILE_FIELD) continue;
            const float h = kCropMinH
                + noise01(gx + 5, gy + 5, ctx.seed) * (kCropMaxH - kCropMinH);
            Structure s{};
            s.kind   = Structure::Crop;
            s.x      = float(x) + 0.5f;
            s.y      = float(y) + 0.5f;
            s.radius = h * kCropWidthRatio;
            s.height = h;
            out.structures.push_back(s);
            if (quota > 0) --quota;
        }
    }
}
} // namespace sm::sub::kit

// Cave interior — the first dungeon that is nobody's property: no household,
// no landlord, no store. Self-contained like every dgn module; dispatch.cpp
// only routes.
//
// A cavern is NOT a house with rougher walls. A house is a rectangle you can
// take in from its doorway; a cave is a place you must walk to learn, which is
// the whole reason the layer exists. So the shape is grown, not stamped: a
// chain of chambers joined by winding galleries, carved OUT of solid rock —
// everything the walk does not open stays stone.
#include "sub/dgn/dispatch.h"

#include "core/rng.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace sm::sub {

// ── Cavern constants ───────────────────────────────────────────────────────
// The mouth chamber's half-span, and the floor every other chamber is drawn
// against. A body is 3 tiles across and swings 5 (kPlayerMeleeRange): 10 keeps
// a fight in a chamber from being a corridor brawl, which is the same
// manoeuvre floor the house partitions respect.
constexpr float kChamberMinHalf = 10.0f;
constexpr float kChamberMaxHalf = 22.0f;
// Galleries are two bodies wide: one to walk, one to be met in. Narrower and
// the mass-battle steering has nowhere to resolve separation.
constexpr float kGalleryHalf = 3.0f;
// How many chambers a cavern grows. Not a difficulty knob — depth is what a
// cave HAS; the danger comes from the cell's own fauna stock and zone.
constexpr int kChambersMin = 3;
constexpr int kChambersMax = 7;
// The rock is the ceiling: one solid slab over the whole cell, its underside
// at the gallery height, so a cave is sealed from above by the same mechanism
// a house is (structure lid), and projectiles die on it.
constexpr float kCaveHeadroomM = 5.0f;
constexpr float kCeilingSlabM = 1.0f;
// The cell's edge must stay solid rock: the dungeon window's ring cells are
// sealed Void filler and nothing playable may touch them.
constexpr float kCellApronTiles = 48.0f;

namespace {

inline std::size_t cell_at(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

// Open a disc of floor. Solid rock is TILE_WALL and carved floor is TILE_ROCK
// (scree underfoot): the two must differ in the TILE, not only in `trav`,
// because the composite carries tiles and every consumer that places anything
// — the vermin spawner, the minimap, movement cost — reads them.
void carve_disc(SubworldMapData& out, float cx, float cy, float r) {
    const int x0 = std::max(2, int(cx - r));
    const int x1 = std::min(kCellSize - 3, int(cx + r));
    const int y0 = std::max(2, int(cy - r));
    const int y1 = std::min(kCellSize - 3, int(cy + r));
    const float r2 = r * r;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = float(x) + 0.5f - cx;
            const float dy = float(y) + 0.5f - cy;
            if (dx * dx + dy * dy > r2) continue;
            const std::size_t i = cell_at(x, y);
            out.tiles[i] = TILE_ROCK;
            out.trav[i] = 1;
        }
    }
}

// Open a gallery between two chambers as a chain of discs along a wandering
// line. Overlapping discs are what make it read as carved rather than drilled,
// and they guarantee connectivity by construction: consecutive discs always
// share ground.
void carve_gallery(SubworldMapData& out, Rng& rng,
                   float ax, float ay, float bx, float by) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) return;
    // One disc per half-radius of travel: the step is derived from the width
    // so a wider gallery is not sampled more finely than it needs.
    const int steps = std::max(2, int(len / (kGalleryHalf * 0.5f)));
    // The wander is perpendicular and bounded by the gallery's own width, so
    // a passage bends without ever pinching itself shut.
    const float sway = kGalleryHalf * (rng.next_f01() * 2.0f - 1.0f);
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const float bend = std::sin(t * 3.14159265f) * sway;
        const float px = ax + dx * t - dy / len * bend;
        const float py = ay + dy * t + dx / len * bend;
        carve_disc(out, px, py, kGalleryHalf);
    }
}

} // namespace

DungeonRoom dungeon_cave_room(const DungeonRef& ref) {
    // The "room" of a cave is its MOUTH chamber — the part the shared rules
    // (entry point, vermin scatter box) need to know about. The rest of the
    // cavern is discovered, not addressed.
    DungeonRoom room;
    room.cx = float(kCellSize) / 2.0f;
    room.cy = float(kCellSize) / 2.0f;
    // A mouth is as wide as the opening in the rock allows, within the span a
    // fight needs. The footprint travels on the ref exactly as a house's does.
    const float maxHalf = float(kCellSize) / 2.0f - kCellApronTiles;
    room.hx = std::clamp(ref.footHx * 4.0f, kChamberMinHalf, maxHalf);
    room.hy = std::clamp(ref.footHy * 4.0f, kChamberMinHalf, maxHalf);
    return room;
}

void gen_dungeon_cave(const CellContext& ctx, SubworldMapData& out) {
    const std::size_t n = std::size_t(kCellSize) * kCellSize;
    // Solid rock, then carve. Nothing is walkable until the walk opens it.
    out.heightmap.assign(n, ctx.macroHeight);
    out.tiles.assign(n, std::uint8_t(TILE_WALL));
    out.trav.assign(n, 0);
    out.structures.clear();
    out.waterLevel = 0.0f;      // no sea under a hill

    Rng rng(ctx.seed ^ 0xCAFE0DEu);
    const DungeonRoom mouth = dungeon_cave_room(ctx.dungeon);

    // The mouth chamber, on the threshold the shared rule places.
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);
    const float mouthHalf = std::min(mouth.hx, mouth.hy);
    carve_disc(out, mouth.cx, mouth.cy, mouthHalf);
    carve_gallery(out, rng, px, py, mouth.cx, mouth.cy);
    carve_disc(out, px, py, kGalleryHalf);

    // Chambers wander outward from the mouth, each joined to the one before
    // it — a chain, so the cave is always fully connected and always has a
    // deepest point worth reaching.
    const float apron = float(kCellSize) / 2.0f - kCellApronTiles;
    const int chambers = kChambersMin
        + int(rng.next_u32() % std::uint32_t(kChambersMax - kChambersMin + 1));
    float lastX = mouth.cx;
    float lastY = mouth.cy;
    float angle = rng.next_f01() * 6.2831853f;
    for (int i = 0; i < chambers; ++i) {
        // Turn by up to a right angle each time: a cave meanders, it does not
        // zigzag on the spot and it does not run straight.
        angle += (rng.next_f01() - 0.5f) * 3.14159265f;
        const float half = kChamberMinHalf
            + rng.next_f01() * (kChamberMaxHalf - kChamberMinHalf);
        // Reach far enough that the galleries are worth walking, and clamp
        // the result inside the apron so the cavern never touches the seal.
        const float reach = half * 2.0f + 24.0f + rng.next_f01() * 48.0f;
        const float nx = std::clamp(lastX + std::cos(angle) * reach,
                                    float(kCellSize) / 2.0f - apron + half,
                                    float(kCellSize) / 2.0f + apron - half);
        const float ny = std::clamp(lastY + std::sin(angle) * reach,
                                    float(kCellSize) / 2.0f - apron + half,
                                    float(kCellSize) / 2.0f + apron - half);
        carve_gallery(out, rng, lastX, lastY, nx, ny);
        carve_disc(out, nx, ny, half);
        lastX = nx;
        lastY = ny;
    }

    // Somebody came before you: a torch by the mouth and another at the far
    // end, still burning. They are the ordinary lantern prop, so they light
    // through the one point-light path and cost the cave no code of its own —
    // and they are why a cavern reads as a place rather than as a black
    // screen with a monster in it.
    for (const auto& spot : {std::pair<float, float>{mouth.cx, mouth.cy},
                             std::pair<float, float>{lastX, lastY}}) {
        Structure torch{};
        torch.kind = Structure::Lantern;
        torch.x = spot.first;
        torch.y = spot.second;
        torch.hx = structure_min_half_xy(Structure::Lantern);
        torch.hy = torch.hx;
        torch.radius = torch.hx;
        torch.height = structure_min_height(Structure::Lantern);
        torch.shape = Structure::Cylinder;
        out.structures.push_back(torch);
    }

    // The deepest chamber keeps a chest — a cave's reward is what somebody
    // left in it. It is the same prop as a household coffer; what it pays is
    // decided by whoever owns the place (engine search_chest), and a cave has
    // no owner, so this one answers with what the DEAD left: nothing until a
    // hoard row exists. Placed anyway, because the shape of the promise is
    // the shape of the thing.
    Structure hoard{};
    hoard.kind = Structure::Chest;
    hoard.x = lastX;
    hoard.y = lastY;
    hoard.hx = structure_min_half_xy(Structure::Chest);
    hoard.hy = hoard.hx * 0.75f;
    hoard.radius = hoard.hx;
    hoard.height = structure_min_height(Structure::Chest);
    out.structures.push_back(hoard);

    // The rock overhead: one lid across the whole cell at head height. Sealed
    // from above by the same mechanism a house is, so nothing can be shot or
    // flown out through the hill.
    Structure lid{};
    lid.kind = Structure::Wall;
    lid.x = float(kCellSize) / 2.0f;
    lid.y = float(kCellSize) / 2.0f;
    lid.hx = float(kCellSize) / 2.0f;
    lid.hy = lid.hx;
    lid.radius = lid.hx;
    lid.zBase = kCaveHeadroomM;
    lid.height = kCeilingSlabM;
    out.structures.push_back(lid);

    // The way out is a prop you can see, exactly as it is in a house.
    Structure door{};
    door.kind = Structure::Door;
    door.hx = 0.75f;
    door.hy = structure_min_half_xy(Structure::Door);
    door.x = px;
    door.y = py + kGalleryHalf - door.hy;
    door.radius = std::max(door.hx, door.hy);
    door.height = structure_min_height(Structure::Door);
    out.structures.push_back(door);
    // …and the threshold under it is paved, so it reads from inside.
    for (int y = int(py) - 1; y <= int(py) + 1; ++y) {
        for (int x = int(px) - 1; x <= int(px) + 1; ++x) {
            const std::size_t i = cell_at(x, y);
            out.tiles[i] = TILE_ROAD;
            out.trav[i] = 1;
        }
    }
}

} // namespace sm::sub

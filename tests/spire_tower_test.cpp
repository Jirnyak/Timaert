// Locks the SPIRE TOWER dungeon generator (sub/dgn/spire_tower.cpp routed via
// sub/dgn/dispatch.cpp). The tower is the spire spell's climb: ordinal = the
// spell's tier = the storey count, levels 0..tier-1, W shaft up / E shaft
// down, a roof hatch on the top storey. Nothing below pins layout literals —
// every geometric claim reads the same dispatch rules the engine reads.
//
// Promised and asserted:
//   1. Determinism — same context twice ⇒ identical tiles and structures.
//   2. The hall is ROUND: every walkable tile lies inside the room circle
//      (dungeon_spire_tower_room), sealed rock outside it.
//   3. Storey furniture follows the ladder rule for every tier × level:
//      up-shaft pad iff a storey above exists, down-shaft pad iff one below,
//      ground gate iff level 0, roof hatch iff top storey — each pad paved,
//      walkable, and carrying its prop (Stairs tag up/down, SpireGate tag =
//      ordinal).
//   4. Connectivity — flood fill from a pad reaches every walkable tile.
//   5. The perimeter is sealed by GEOMETRY: ring samples one wall
//      half-thickness outside the hall lie inside a ground-level solid; the
//      NEGATIVE CONTROL removes one ring segment and the same detector must
//      see the hole.
//   6. Exactly one lifted structure (the ceiling), seated on the wall crowns.
//   7. Shaft arrival mapping — a tower's up-shaft tops out on the target
//      storey's DOWN pad (and vice versa), and that pad exists there.
//   8. The floors clamp holds for foreign ordinals (0 ⇒ 1, 99 ⇒ 5).
//   9. NEGATIVE CONTROL for the flood fill — a rock row severing the hall
//      must yield unreachable walkable tiles.
#include "check.h"
#include "sub/dgn/dispatch.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

std::size_t cell_index(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

CellContext make_ctx(int tier, int level) {
    CellContext ctx{};
    ctx.cx = 5;
    ctx.cy = -3;
    ctx.macroHeight = 0.58f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.landmarkSettlementId = -1;
    ctx.landmarkSize = 0;
    ctx.seed = 0x5B12E7EAu;
    ctx.worldSeed = 0xC0FFEE11u;
    ctx.dungeon.kind = DungeonRef::SpireTower;
    ctx.dungeon.level = std::int8_t(level);
    ctx.dungeon.ordinal = std::uint16_t(tier);
    return ctx;
}

void generate(const CellContext& ctx, SubworldMapData& out) {
    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = ctx.macroHeight;
        nbB[i] = ctx.biome;
        nbF[i] = std::uint8_t(FT_None);
    }
    dispatch_generate(ctx, nbH, nbB, nbF, out);
}

bool walkable(std::uint8_t t) {
    return t == std::uint8_t(TILE_SQUARE) || t == std::uint8_t(TILE_ROAD);
}

// 4-neighbour flood fill over walkable paint from (sx, sy); returns count.
int flood_from(const std::vector<std::uint8_t>& tiles, int sx, int sy) {
    std::vector<std::uint8_t> seen(tiles.size(), 0);
    std::vector<std::pair<int, int>> stack;
    if (!walkable(tiles[cell_index(sx, sy)])) return 0;
    stack.emplace_back(sx, sy);
    seen[cell_index(sx, sy)] = 1;
    int reached = 0;
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        ++reached;
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : nb) {
            const int nx = x + d[0], ny = y + d[1];
            if (nx < 0 || ny < 0 || nx >= kCellSize || ny >= kCellSize)
                continue;
            const std::size_t i = cell_index(nx, ny);
            if (seen[i] || !walkable(tiles[i])) continue;
            seen[i] = 1;
            stack.emplace_back(nx, ny);
        }
    }
    return reached;
}

int count_walkable(const std::vector<std::uint8_t>& tiles) {
    int n = 0;
    for (std::uint8_t t : tiles) n += walkable(t) ? 1 : 0;
    return n;
}

// Ground-level solid coverage at a point (the house test's seal detector).
bool covered_by_ground_solid(const SubworldMapData& map, float px, float py) {
    for (const Structure& s : map.structures) {
        if (!structure_is_solid(s.kind) && s.kind != Structure::Wall) continue;
        if (s.zBase > 0.0f) continue;
        if (structure_surface_dist2(s, px, py) <= 0.0f) return true;
    }
    return false;
}

void assert_pad(const SubworldMapData& map, float x, float y,
                const char* what) {
    int paved = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            paved += walkable(
                map.tiles[cell_index(int(x) + dx, int(y) + dy)]) ? 1 : 0;
    CHECK(paved == 9, what);
}

const Structure* find_prop(const SubworldMapData& map, Structure::Kind kind,
                           float x, float y) {
    for (const Structure& s : map.structures) {
        if (s.kind != kind) continue;
        const float dx = s.x - x, dy = s.y - y;
        if (dx * dx + dy * dy <= 4.0f * 4.0f) return &s;
    }
    return nullptr;
}

void test_determinism() {
    SubworldMapData a, b;
    generate(make_ctx(3, 1), a);
    generate(make_ctx(3, 1), b);
    CHECK(a.tiles == b.tiles, "same identity, same tiles, byte for byte");
    CHECK(a.structures.size() == b.structures.size(),
          "same identity, same prop census");
}

void test_every_storey_of_every_tier() {
    for (int tier = 1; tier <= 5; ++tier) {
        for (int level = 0; level < tier; ++level) {
            const CellContext ctx = make_ctx(tier, level);
            SubworldMapData map;
            generate(ctx, map);
            const DungeonRoom room = dungeon_spire_tower_room(ctx.dungeon);
            CHECK(room.hx == room.hy, "a tower hall is round");

            // 2. Walkable paint lies inside the hall circle.
            int outside = 0, inside = 0;
            for (int y = 0; y < kCellSize; ++y) {
                for (int x = 0; x < kCellSize; ++x) {
                    if (!walkable(map.tiles[cell_index(x, y)])) continue;
                    const float dx = float(x) + 0.5f - room.cx;
                    const float dy = float(y) + 0.5f - room.cy;
                    (dx * dx + dy * dy <= room.hx * room.hx) ? ++inside
                                                             : ++outside;
                }
            }
            CHECK(inside > 0 && outside == 0,
                  "every walkable tile lies inside the round hall");

            // 3. The ladder rule, pad by pad.
            const bool wantUp = level < tier - 1;
            const bool wantDown = level > 0;
            const bool wantGate = level == 0;
            const bool wantHatch = level == tier - 1;
            float ux = 0, uy = 0, dxp = 0, dyp = 0, hx = 0, hy = 0;
            dungeon_stair_point(ctx.dungeon, true, ux, uy);
            dungeon_stair_point(ctx.dungeon, false, dxp, dyp);
            dungeon_roof_hatch_point(ctx.dungeon, hx, hy);
            const Structure* up = find_prop(map, Structure::Stairs, ux, uy);
            const Structure* down =
                find_prop(map, Structure::Stairs, dxp, dyp);
            const Structure* hatch =
                find_prop(map, Structure::SpireGate, hx, hy);
            CHECK((up != nullptr) == wantUp,
                  "an up shaft stands iff a storey above exists");
            CHECK((down != nullptr) == wantDown,
                  "a down shaft stands iff a storey below exists");
            CHECK((hatch != nullptr) == wantHatch,
                  "the roof hatch stands only on the top storey");
            if (up) {
                CHECK(up->tag == 1, "the W shaft climbs");
                assert_pad(map, ux, uy, "the up pad's 3x3 is walkable");
            }
            if (down) {
                CHECK(down->tag == 0, "the E shaft descends");
                assert_pad(map, dxp, dyp, "the down pad's 3x3 is walkable");
            }
            if (wantHatch) {
                assert_pad(map, hx, hy, "the hatch pad's 3x3 is walkable");
            }
            float ex = 0, ey = 0;
            dungeon_entry_point(ctx.dungeon, ex, ey);
            const Structure* gate =
                find_prop(map, Structure::SpireGate, ex, room.cy + room.hx);
            CHECK((gate != nullptr) == wantGate,
                  "the ground gate stands only on the storey it opens onto");
            if (gate) {
                CHECK(int(gate->tag) == tier,
                      "the gate's tag carries the tier back out");
                assert_pad(map, ex, ey, "the threshold's 3x3 is walkable");
            }

            // 4. One region: a pad reaches every walkable tile.
            const float sx = wantGate ? ex : (wantDown ? dxp : ux);
            const float sy = wantGate ? ey : (wantDown ? dyp : uy);
            const int total = count_walkable(map.tiles);
            const int reached = flood_from(map.tiles, int(sx), int(sy));
            CHECK(total > 0 && reached == total,
                  "the hall is one connected region from its pad");

            // 5. Sealed perimeter: samples one wall half outside the hall.
            const float wallHalf = structure_min_half_xy(Structure::Wall);
            int samples = 0, uncovered = 0;
            for (int k = 0; k < 128; ++k) {
                const float a = float(k) * 6.2831853f / 128.0f;
                const float px = room.cx + std::cos(a) * (room.hx + wallHalf);
                const float py = room.cy + std::sin(a) * (room.hx + wallHalf);
                ++samples;
                uncovered += covered_by_ground_solid(map, px, py) ? 0 : 1;
            }
            CHECK(samples > 0 && uncovered == 0,
                  "the masonry ring is sealed by geometry");

            // 6. Exactly one lifted solid: the ceiling on the wall crowns.
            int lifted = 0;
            for (const Structure& s : map.structures) {
                if (s.zBase <= 0.0f) continue;
                ++lifted;
                CHECK(s.zBase == structure_min_height(Structure::Wall),
                      "the ceiling sits exactly on the wall crowns");
                CHECK(s.shape == Structure::Cylinder,
                      "a round hall wears a round lid");
            }
            CHECK(lifted == 1, "one ceiling, nothing else floats");
        }
    }
}

void test_seal_detector_sees_a_hole() {
    const CellContext ctx = make_ctx(2, 0);
    SubworldMapData map;
    generate(ctx, map);
    // Remove one ground-level wall chord; the ring detector must notice.
    for (std::size_t i = 0; i < map.structures.size(); ++i) {
        const Structure& s = map.structures[i];
        if (s.kind == Structure::Wall && s.zBase <= 0.0f
            && s.shape == Structure::Box && s.hx > s.hy) {
            map.structures.erase(map.structures.begin()
                                 + std::ptrdiff_t(i));
            break;
        }
    }
    const DungeonRoom room = dungeon_spire_tower_room(ctx.dungeon);
    const float wallHalf = structure_min_half_xy(Structure::Wall);
    int uncovered = 0;
    for (int k = 0; k < 128; ++k) {
        const float a = float(k) * 6.2831853f / 128.0f;
        uncovered += covered_by_ground_solid(
                         map, room.cx + std::cos(a) * (room.hx + wallHalf),
                         room.cy + std::sin(a) * (room.hx + wallHalf))
                         ? 0
                         : 1;
    }
    CHECK(uncovered > 0, "control: a removed chord is a visible hole");
}

void test_flood_detector_sees_a_severed_hall() {
    const CellContext ctx = make_ctx(1, 0);
    SubworldMapData map;
    generate(ctx, map);
    const DungeonRoom room = dungeon_spire_tower_room(ctx.dungeon);
    // Paint a rock row straight across the hall's midline.
    for (int x = 0; x < kCellSize; ++x) {
        map.tiles[cell_index(x, int(room.cy))] = std::uint8_t(TILE_ROCK);
    }
    float ex = 0, ey = 0;
    dungeon_entry_point(ctx.dungeon, ex, ey);
    const int total = count_walkable(map.tiles);
    const int reached = flood_from(map.tiles, int(ex), int(ey));
    CHECK(total > 0 && reached < total,
          "control: a severed hall leaves unreachable floor");
}

void test_shaft_arrival_lands_on_the_partner_pad() {
    // Climb 0→1 in a tier-3 tower: the arrival point on storey 1 must be
    // storey 1's DOWN pad — same xy the module paves and props there.
    const CellContext below = make_ctx(3, 0);
    const CellContext above = make_ctx(3, 1);
    float ax = 0, ay = 0;
    dungeon_shaft_arrival_point(above.dungeon, /*wentUp*/true, ax, ay);
    float dx = 0, dy = 0;
    dungeon_stair_point(above.dungeon, /*up*/false, dx, dy);
    CHECK(ax == dx && ay == dy,
          "an up shaft tops out on the target storey's down pad");
    SubworldMapData map;
    generate(above, map);
    CHECK(find_prop(map, Structure::Stairs, ax, ay) != nullptr,
          "the arrival pad exists and carries its stair");
    // And descending 1→0 lands on storey 0's UP pad.
    float bx = 0, by = 0;
    dungeon_shaft_arrival_point(below.dungeon, /*wentUp*/false, bx, by);
    float ux = 0, uy = 0;
    dungeon_stair_point(below.dungeon, /*up*/true, ux, uy);
    CHECK(bx == ux && by == uy,
          "a down shaft bottoms out on the target storey's up pad");
}

void test_floors_clamp() {
    DungeonRef weird{};
    weird.kind = DungeonRef::SpireTower;
    weird.ordinal = 0;
    CHECK(dungeon_spire_tower_floors(weird) == 1,
          "a foreign ordinal degrades to a legal tower, floor 1");
    weird.ordinal = 99;
    CHECK(dungeon_spire_tower_floors(weird) == 5,
          "the ceiling of the clamp is the spell tier domain");
}

} // namespace

int main() {
    test_determinism();
    test_every_storey_of_every_tier();
    test_seal_detector_sees_a_hole();
    test_flood_detector_sees_a_severed_hall();
    test_shaft_arrival_lands_on_the_partner_pad();
    test_floors_clamp();
    return sm::test::report("spire_tower_test");
}

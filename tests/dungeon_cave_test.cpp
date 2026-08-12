// Locks the CAVE INTERIOR dungeon generator (sub/dgn/cave.cpp, routed by
// sub/dgn/dispatch.cpp and the dispatch_generate early return in
// sub/gens/dispatch.cpp).
//
// A cave is not a house with rougher walls: it is CARVED out of solid rock by
// a walk — a mouth chamber, a gallery from the threshold, then a chain of
// further chambers joined by wandering galleries. Nothing about that shape is
// pinnable per seed, so nothing below pins a tile: every claim is an invariant
// that must hold for ANY seed.
//
// What is promised and asserted here:
//   1. CONNECTIVITY — the headline. A 4-neighbour flood fill from the entry
//      threshold over `trav == 1` reaches EVERY walkable tile of the cell, for
//      several seeds (the sweep asserts it swept). A chain of chambers is only
//      a cave if the chain is unbroken; a chamber the gallery missed is a room
//      full of loot behind solid rock.
//   2. NEGATIVE CONTROL for that detector, itself asserted: ring the threshold
//      with rock and the SAME fill must report unreached walkable floor — and
//      the mutation must be shown to have removed walkable tiles, so the
//      control cannot silently do nothing.
//   3. FLOOR / WALL DISTINCTION: carved floor and solid rock differ in the
//      TILE, not only in `trav`. Every walkable tile carries the module's own
//      floor paint (dungeon_floor_tile) or the threshold's road; every
//      TILE_WALL tile is impassable. This is the invariant the composite's
//      consumers (vermin spawner, minimap, movement cost) read.
//   4. APRON: nothing playable touches the cell edge — the dungeon window's
//      ring cells are sealed Void filler.
//   5. PROPS: the cave is furnished by the ONE structure vocabulary — one
//      chest in the deepest chamber, one door on the threshold within the
//      table's own reach, lanterns that light through the one lit-prop column,
//      and exactly one lifted body (the rock lid) roofing the cell.
//   6. NO STOREYS: a cave has depth, and depth is walked, not climbed.
//   7. DETERMINISM: the interior is a pure projection of {context, ref} — same
//      context twice ⇒ identical grid and identical props; a different seed ⇒
//      a different cavern (asserted, or the seed would be decoration).
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

// Per-cell buffers are kCellSize-strided (the convention every open-air
// generator writes and seamless_manager's composite blit reads) — NOT the
// kFullSize stride of the 3×3 composite.
std::size_t cell_index(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

// The seeds the sweeps run. Spread across the u32 range so a cavern that
// happens to grow in one direction cannot carry the suite.
constexpr std::uint32_t kSeeds[] = {1u, 7u, 999u, 12345u, 0xD00DCAFEu};
constexpr int kSeedCount = int(sizeof(kSeeds) / sizeof(kSeeds[0]));
static_assert(kSeedCount >= 4, "connectivity must be proven on several seeds");

// A real cave door is a CaveMouth prop, and the engine hands the interior that
// prop's own half-extents (engine.cpp: ref.footHx = structure_half_x(shape)).
// So the fixture's footprint is the mouth's table row and the placement rule
// beside it (gens/dispatch.cpp scatter_cave_mouths: hy = hx × 0.5), never a
// number typed here — a retune of the row retunes the fixture with it.
CellContext make_cave_ctx(std::uint32_t seed) {
    CellContext ctx{};
    ctx.cx = 3;
    ctx.cy = -2;
    ctx.macroHeight = 0.62f;
    ctx.biome = Mountain;
    ctx.feature = FT_None;
    ctx.landmarkSettlementId = -1;
    ctx.landmarkSize = 0;
    ctx.seed = seed;
    ctx.dungeon.kind = DungeonRef::Cave;
    ctx.dungeon.level = 0;
    ctx.dungeon.ordinal = 1;
    ctx.dungeon.footHx = structure_min_half_xy(Structure::CaveMouth);
    ctx.dungeon.footHy = ctx.dungeon.footHx * 0.5f;
    return ctx;
}

// Full production route: dispatch_generate must itself divert into the dungeon
// pipeline when the context carries a door.
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

bool structures_identical(const std::vector<Structure>& a,
                          const std::vector<Structure>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Structure& s = a[i];
        const Structure& t = b[i];
        if (s.kind != t.kind || s.shape != t.shape) return false;
        if (s.x != t.x || s.y != t.y) return false;
        if (s.radius != t.radius || s.height != t.height) return false;
        if (s.yaw != t.yaw || s.hx != t.hx || s.hy != t.hy) return false;
        if (s.zBase != t.zBase) return false;
    }
    return true;
}

// Connectivity detector over TRAVERSABILITY (the channel every mover reads).
// `totalWalkable` recounts the grid it was GIVEN, so a mutated grid is
// recounted and never trusted from before the mutation; `reached` is what the
// fill actually visited. The two can agree only if the chain is unbroken.
struct Flood {
    int totalWalkable = 0;
    int reached = 0;
};
Flood flood_trav(const std::vector<std::uint8_t>& trav, int sx, int sy) {
    Flood f{};
    for (std::uint8_t t : trav) {
        if (t == 1) ++f.totalWalkable;
    }
    if (sx < 0 || sy < 0 || sx >= kCellSize || sy >= kCellSize) return f;
    if (trav[cell_index(sx, sy)] != 1) return f;
    std::vector<std::uint8_t> seen(trav.size(), 0);
    std::vector<int> stack;
    seen[cell_index(sx, sy)] = 1;
    stack.push_back(sy * kCellSize + sx);
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        ++f.reached;
        const int x = i % kCellSize;
        const int y = i / kCellSize;
        auto push = [&](int nx, int ny) {
            if (nx < 0 || ny < 0 || nx >= kCellSize || ny >= kCellSize) return;
            const std::size_t j = cell_index(nx, ny);
            if (seen[j] || trav[j] != 1) return;
            seen[j] = 1;
            stack.push_back(ny * kCellSize + nx);
        };
        push(x - 1, y);
        push(x + 1, y);
        push(x, y - 1);
        push(x, y + 1);
    }
    return f;
}

// ── 1. Connectivity, several seeds ─────────────────────────────────────────
void test_connectivity() {
    int seedsTested = 0;
    for (int si = 0; si < kSeedCount; ++si) {
        const CellContext ctx = make_cave_ctx(kSeeds[si]);
        SubworldMapData out{};
        generate(ctx, out);
        ++seedsTested;

        float px = 0.0f, py = 0.0f;
        dungeon_entry_point(ctx.dungeon, px, py);
        CHECK(out.trav[cell_index(int(px), int(py))] == 1,
              "the threshold the engine spawns a body on is walkable floor");

        const Flood f = flood_trav(out.trav, int(px), int(py));
        CHECK(f.totalWalkable > 0,
              "a carved cavern has walkable floor at all");
        CHECK(f.reached == f.totalWalkable,
              "every walkable tile is reachable from the cave mouth");
    }
    CHECK(seedsTested == kSeedCount,
          "the connectivity sweep tested every seed it was given");
}

// ── 2. Negative control for the connectivity detector ──────────────────────
void test_connectivity_negative_control() {
    const CellContext ctx = make_cave_ctx(kSeeds[0]);
    SubworldMapData out{};
    generate(ctx, out);
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);

    // Fixture: untouched, the detector reports full connectivity — the state
    // section 1 calls green.
    const Flood before = flood_trav(out.trav, int(px), int(py));
    CHECK(before.totalWalkable > 0 && before.reached == before.totalWalkable,
          "fixture: the untouched cavern is fully connected");

    // Wall the threshold in: an annulus of rock around it, thick enough that
    // no 4-neighbour step can cross it, and small enough to sit well inside
    // the mouth chamber (whose half-span is dungeon_room's, so the radius is
    // derived from the room rather than guessed).
    const DungeonRoom mouth = dungeon_room(ctx.dungeon);
    const float ringR = std::min(mouth.hx, mouth.hy) * 0.5f;
    const float ringHalfBand = 1.5f;
    CHECK(ringR > ringHalfBand + 1.0f,
          "fixture: the ring radius clears its own band around the threshold");
    int removed = 0;
    const int lo = int(px - ringR - ringHalfBand) - 1;
    const int hi = int(px + ringR + ringHalfBand) + 1;
    const int loY = int(py - ringR - ringHalfBand) - 1;
    const int hiY = int(py + ringR + ringHalfBand) + 1;
    for (int y = loY; y <= hiY; ++y) {
        for (int x = lo; x <= hi; ++x) {
            if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
            const float dx = float(x) + 0.5f - px;
            const float dy = float(y) + 0.5f - py;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(d - ringR) > ringHalfBand) continue;
            const std::size_t i = cell_index(x, y);
            if (out.trav[i] != 1) continue;
            out.trav[i] = 0;
            ++removed;
        }
    }
    // The control must be shown to have DONE something: a ring that removed
    // nothing would "prove" connectivity broken by proving nothing at all.
    CHECK(removed > 0,
          "the control actually removed walkable tiles from the cavern");

    const Flood after = flood_trav(out.trav, int(px), int(py));
    CHECK(after.totalWalkable == before.totalWalkable - removed,
          "the detector recounts the mutated grid, not the original");
    CHECK(after.reached > 0,
          "the threshold's own pocket is still reachable after the ring");
    CHECK(after.reached < after.totalWalkable,
          "with the mouth ringed off the detector must SEE unreached floor");
}

// ── 3. Floor / wall distinction ────────────────────────────────────────────
void test_floor_and_wall_paint() {
    // The module says what its floor is; the test reads that, never a literal.
    // A cave is bare scree where a house is flagged — the distinction the
    // placement passes (vermin, minimap, movement cost) key off.
    const CellContext ctx = make_cave_ctx(kSeeds[1]);
    const Tile floorTile = dungeon_floor_tile(ctx.dungeon);
    DungeonRef houseRef = ctx.dungeon;
    houseRef.kind = DungeonRef::House;
    CHECK(floorTile != dungeon_floor_tile(houseRef),
          "a cave's floor paint differs from a house's — scree, not flagstone");
    CHECK(floorTile == TILE_ROCK, "a cave's carved floor is scree");

    SubworldMapData out{};
    generate(ctx, out);
    CHECK(out.tiles.size() == std::size_t(kCellSize) * kCellSize &&
              out.trav.size() == out.tiles.size(),
          "the module fills the whole per-cell grid, tiles and trav alike");

    int walkableSamples = 0, badFloorPaint = 0;
    int wallSamples = 0, walkableWalls = 0;
    for (std::size_t i = 0; i < out.tiles.size(); ++i) {
        if (out.trav[i] == 1) {
            ++walkableSamples;
            if (out.tiles[i] != floorTile && out.tiles[i] != TILE_ROAD) {
                ++badFloorPaint;
            }
        }
        if (out.tiles[i] == TILE_WALL) {
            ++wallSamples;
            if (out.trav[i] != 0) ++walkableWalls;
        }
    }
    CHECK(walkableSamples > 0 && badFloorPaint == 0,
          "every walkable tile carries the cave's floor paint or the threshold road");
    CHECK(wallSamples > 0 && walkableWalls == 0,
          "solid rock is impassable — TILE_WALL never carries trav");
    // Both populations exist, so neither aggregate above is vacuous: a cavern
    // is carved OUT of rock, so most of the cell must still be rock.
    CHECK(wallSamples > walkableSamples,
          "the cell is mostly solid rock — the cavern is carved, not stamped");
}

// ── 4. Apron — nothing playable touches the cell edge ──────────────────────
void test_apron() {
    // The generator keeps the cavern inside a 48-tile apron so the dungeon
    // window's sealed ring cells are unreachable. The bound asserted here is
    // deliberately looser than that (a body is 3 tiles across and the seam
    // blit needs margin either side) — the claim is "nothing playable is near
    // the edge", not the apron's exact retune.
    constexpr int kEdgeSafeTiles = 16;
    int seedsTested = 0, walkableSamples = 0, tooCloseToEdge = 0;
    int minMargin = kCellSize;
    for (int si = 0; si < kSeedCount; ++si) {
        SubworldMapData out{};
        generate(make_cave_ctx(kSeeds[si]), out);
        ++seedsTested;
        for (int y = 0; y < kCellSize; ++y) {
            for (int x = 0; x < kCellSize; ++x) {
                if (out.trav[cell_index(x, y)] != 1) continue;
                ++walkableSamples;
                const int margin = std::min(std::min(x, y),
                                            std::min(kCellSize - 1 - x,
                                                     kCellSize - 1 - y));
                if (margin < minMargin) minMargin = margin;
                if (margin < kEdgeSafeTiles) ++tooCloseToEdge;
            }
        }
    }
    CHECK(seedsTested == kSeedCount, "the apron sweep tested every seed");
    CHECK(walkableSamples > 0 && tooCloseToEdge == 0,
          "no walkable tile lies within the edge-safe band of the cell border");
    CHECK(minMargin >= kEdgeSafeTiles,
          "the closest carved tile to the cell edge still clears the band");
}

// ── 5. Props ───────────────────────────────────────────────────────────────
void test_props() {
    const CellContext ctx = make_cave_ctx(kSeeds[2]);
    SubworldMapData out{};
    generate(ctx, out);
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);

    int chests = 0, doors = 0, lanterns = 0, lifted = 0;
    int unlitLanterns = 0;
    Structure lid{}, door{};
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::Chest) ++chests;
        if (s.kind == Structure::Door) { ++doors; door = s; }
        if (s.kind == Structure::Lantern) {
            ++lanterns;
            // The lit column of kStructureKindRows is what hangs a light on a
            // prop through the ONE point-light path; a torch that is not a lit
            // KIND would leave the cavern a black screen with a monster in it.
            if (!structure_is_lit(s.kind)) ++unlitLanterns;
        }
        if (s.zBase > 0.0f) { ++lifted; lid = s; }
    }
    CHECK(chests == 1, "a cave keeps exactly one chest — its deepest reward");
    CHECK(doors == 1, "exactly one door: the way back out");
    CHECK(lanterns >= 2 && unlitLanterns == 0,
          "somebody's torches burn at both ends of the cavern, and they are lit props");
    CHECK(lifted == 1, "exactly one lifted structure — the rock lid overhead");
    CHECK(std::fabs(float(kCellSize) / 2.0f - lid.x) <= structure_half_x(lid) &&
              std::fabs(float(kCellSize) / 2.0f - lid.y) <= structure_half_y(lid),
          "the rock lid roofs the cell centre, where the mouth chamber is");
    CHECK(lid.kind == Structure::Wall && structure_is_solid(lid.kind),
          "the lid is a solid body — nothing is shot or flown out through the hill");

    // The door must be usable from the threshold the engine puts the body on:
    // the reach is the table's own column, measured to the prop's SURFACE
    // exactly as the interaction path measures it.
    const float reach = interact_row(InteractId::Door).reachTiles;
    CHECK(reach > 0.0f, "contract: the Door verb has a reach in the table");
    CHECK(structure_surface_dist2(door, px, py) <= reach * reach,
          "the exit door is within the table's reach of the entry threshold");
    CHECK(structure_interact(Structure::Door) == InteractId::Door,
          "contract: the Door prop carries the Door verb");

    // No two SOLID props may share a square. The deep torch used to be placed
    // on the very tile of the hoard — a lantern wholly inside a chest, i.e. a
    // body nothing could walk around — which is exactly the class of defect a
    // grown layout hides well, because nothing looks wrong from a distance.
    int solidPairs = 0, overlaps = 0;
    for (std::size_t i = 0; i < out.structures.size(); ++i) {
        const Structure& a = out.structures[i];
        if (!structure_is_solid(a.kind) || a.zBase > 0.0f) continue;
        for (std::size_t j = i + 1; j < out.structures.size(); ++j) {
            const Structure& b = out.structures[j];
            if (!structure_is_solid(b.kind) || b.zBase > 0.0f) continue;
            ++solidPairs;
            // Surface distance is zero exactly when one footprint reaches
            // into the other — the same measure the reach tests use.
            if (structure_surface_dist2(a, b.x, b.y) <= 0.0f) ++overlaps;
        }
    }
    CHECK(solidPairs > 0 && overlaps == 0,
          "no two grounded solid props stand inside one another");

    // The mouth's footprint DRIVES its cavern, as a house's drives its rooms:
    // a wider opening in the rock opens on a wider chamber. Derived from the
    // module's own rule (dungeon_cave_room), never from a pinned number.
    DungeonRef narrow = make_cave_ctx(kSeeds[0]).dungeon;
    DungeonRef wide = narrow;
    narrow.footHx = 1.5f;
    narrow.footHy = 1.0f;
    wide.footHx = narrow.footHx * 3.0f;
    wide.footHy = narrow.footHy * 3.0f;
    const DungeonRoom narrowRoom = dungeon_room(narrow);
    const DungeonRoom wideRoom = dungeon_room(wide);
    CHECK(wideRoom.hx > narrowRoom.hx && wideRoom.hy > narrowRoom.hy,
          "a wider mouth opens on a wider chamber — the footprint is not dead");

    // The chest sits on carved floor, or its reward is inside the rock.
    int chestsOnFloor = 0, chestSamples = 0;
    for (const Structure& s : out.structures) {
        if (s.kind != Structure::Chest && s.kind != Structure::Lantern) continue;
        ++chestSamples;
        const int tx = int(s.x), ty = int(s.y);
        if (tx >= 0 && ty >= 0 && tx < kCellSize && ty < kCellSize &&
            out.trav[cell_index(tx, ty)] == 1) {
            ++chestsOnFloor;
        }
    }
    CHECK(chestSamples > 0 && chestsOnFloor == chestSamples,
          "every chest and torch stands on carved floor, not inside the rock");
}

// ── 6. No storeys ──────────────────────────────────────────────────────────
void test_no_storeys() {
    int seedsTested = 0, upper = 0, cellars = 0;
    for (int si = 0; si < kSeedCount; ++si) {
        const CellContext ctx = make_cave_ctx(kSeeds[si]);
        ++seedsTested;
        if (dungeon_has_upper(ctx.dungeon)) ++upper;
        if (dungeon_has_cellar(ctx.dungeon, ctx.seed, ctx.cx, ctx.cy)) ++cellars;
    }
    CHECK(seedsTested == kSeedCount, "the storey sweep tested every seed");
    CHECK(upper == 0, "a cave never grows an upper storey — depth is walked");
    CHECK(cellars == 0, "a cave never grows a cellar shaft");
    // The sweep would be vacuous if the same rule denied a HOUSE its storeys:
    // the cellar coin must be capable of coming up heads for some house.
    const CellContext ctx = make_cave_ctx(kSeeds[0]);
    DungeonRef houseRef = ctx.dungeon;
    houseRef.kind = DungeonRef::House;
    houseRef.footHx = houseRef.footHy = 8.0f;
    int houseCellars = 0, houseSeeds = 0;
    for (int si = 0; si < kSeedCount; ++si) {
        ++houseSeeds;
        for (std::uint16_t ord = 0; ord < 8; ++ord) {
            DungeonRef r = houseRef;
            r.ordinal = ord;
            if (dungeon_has_cellar(r, kSeeds[si], 3, -2)) ++houseCellars;
        }
    }
    CHECK(houseSeeds == kSeedCount, "the house control swept every seed");
    CHECK(houseCellars > 0,
          "control: the same cellar rule DOES answer yes for houses");
    CHECK(dungeon_has_upper(houseRef),
          "control: the same storey rule DOES answer yes for a roomy house");
}

// ── 7. Determinism ─────────────────────────────────────────────────────────
void test_determinism() {
    const CellContext ctx = make_cave_ctx(kSeeds[3]);
    CHECK(resolve_mode(ctx) == SubworldMode::Dungeon,
          "a context carrying a cave door resolves to SubworldMode::Dungeon");

    SubworldMapData a{}, b{};
    generate(ctx, a);
    generate(ctx, b);
    CHECK(a.tiles == b.tiles, "same cave twice: identical tile grid");
    CHECK(a.trav == b.trav, "same cave twice: identical traversability");
    CHECK(a.heightmap == b.heightmap, "same cave twice: identical heightmap");
    CHECK(structures_identical(a.structures, b.structures),
          "same cave twice: identical props, field for field");

    // A different seed must carve a different cavern, or the seed is
    // decoration and every hill in the world hides the same hole.
    SubworldMapData c{};
    generate(make_cave_ctx(kSeeds[4]), c);
    CHECK(a.tiles.size() == c.tiles.size(),
          "both seeds fill the same per-cell grid");
    int differing = 0;
    for (std::size_t i = 0; i < a.tiles.size() && i < c.tiles.size(); ++i) {
        if (a.tiles[i] != c.tiles[i]) ++differing;
    }
    CHECK(differing > 0, "a different seed carves a different cavern");
}

} // namespace

int main() {
    test_connectivity();
    test_connectivity_negative_control();
    test_floor_and_wall_paint();
    test_apron();
    test_props();
    test_no_storeys();
    test_determinism();
    return sm::test::report("dungeon_cave_test");
}

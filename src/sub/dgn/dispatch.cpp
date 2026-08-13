// Dungeon dispatch — routes an interior CellContext to its module and owns
// the shared Void filler (the sealed ring around the one real interior cell;
// it is scenery for nobody and a generator for nothing, so it lives here).
#include "sub/dgn/dispatch.h"

#include <algorithm>

namespace sm::sub {

// FNV-style mixing with the spatial primes the engine's other per-cell
// streams use (kCellSeedX/Y idiom). The salt separates the dungeon namespace
// from every overworld seed stream so an interior can never cache-collide
// with its own cell's terrain.
std::uint32_t dungeon_scene_seed(std::uint32_t worldSeed, int cx, int cy,
                                 std::uint16_t ordinal, std::int8_t level) {
    constexpr std::uint32_t kSalt  = 2147483647u + 347922777u;
    constexpr std::uint32_t kPrime = 16777619u;
    constexpr std::uint32_t kMixX  = 73856093u;
    constexpr std::uint32_t kMixY  = 19349663u;
    std::uint32_t h = worldSeed ^ kSalt;
    h = (h ^ (std::uint32_t(cx) * kMixX)) * kPrime;
    h = (h ^ (std::uint32_t(cy) * kMixY)) * kPrime;
    h = (h ^ (std::uint32_t(ordinal) << 8
                | std::uint32_t(std::uint8_t(level)))) * kPrime;
    return h;
}

// Sealed filler: flat rock at the door cell's altitude, nothing walkable,
// nothing built. The player can never reach it (the interior is walled), so
// its only job is to be cheap and deterministic.
static void gen_dungeon_void(const CellContext& ctx, SubworldMapData& out) {
    const std::size_t n = std::size_t(kCellSize) * kCellSize;
    out.tiles.assign(n, std::uint8_t(TILE_ROCK));
    out.trav.assign(n, 0);
    out.heightmap.assign(n, ctx.macroHeight);
    out.structures.clear();
    // Below any floor: an interior has no sea.
    out.waterLevel = 0.0f;
}

Tile dungeon_floor_tile(const DungeonRef& ref) {
    switch (ref.kind) {
        case DungeonRef::Cave: return TILE_ROCK;    // scree
        default:               return TILE_SQUARE;  // flagged hall
    }
}

DungeonRoom dungeon_room(const DungeonRef& ref) {
    switch (ref.kind) {
        case DungeonRef::House:      return dungeon_house_room(ref);
        case DungeonRef::Cave:       return dungeon_cave_room(ref);
        case DungeonRef::SpireTower: return dungeon_spire_tower_room(ref);
        default:                     return DungeonRoom{};
    }
}

void dungeon_entry_point(const DungeonRef& ref, float& x, float& y) {
    // THE exterior threshold — the tile you step onto from the street and
    // leave from, on the storey the door opens onto. 2 tiles inside the south
    // wall's midpoint: deep enough that a body radius (1.5 tiles, sub/body.h)
    // never spawns inside the wall solid, close enough that the door reads as
    // "right there". Stair arrivals use dungeon_stair_point instead — which
    // shaft you took is the caller's knowledge, not the level's.
    const DungeonRoom room = dungeon_room(ref);
    x = room.cx;
    y = room.cy + room.hy - 2.0f;
}

bool dungeon_has_upper(const DungeonRef& ref) {
    // A cave has no storeys: it has depth, and depth is walked, not climbed.
    if (ref.kind == DungeonRef::Cave) return false;
    // A tower climbs 0..floors-1; every storey below the top has a way up.
    if (ref.kind == DungeonRef::SpireTower) {
        return int(ref.level) < dungeon_spire_tower_floors(ref) - 1;
    }
    // A storey is worth climbing only if it seats a room you can fight in:
    // both interior half-spans at least the manoeuvre floor the partitions
    // are cut to (sub/dgn/house.cpp kMinRoomSpanTiles = 12 — a doorway plus
    // melee reach on either side). Smaller houses are one room and a roof.
    const DungeonRoom room = dungeon_room(ref);
    return std::min(room.hx, room.hy) >= 12.0f;
}

bool dungeon_has_cellar(const DungeonRef& ref, std::uint32_t worldSeed,
                        int cx, int cy) {
    if (ref.kind == DungeonRef::Cave) return false;   // see above
    // A tower rises; nothing of it is dug. Its downward shafts live BETWEEN
    // storeys (E pad, dungeon_stair_point), never below the ground floor.
    if (ref.kind == DungeonRef::SpireTower) return false;
    // Design parameter, not an invariant: every second hearth keeps a
    // cellar. Rolled from the LEVEL-0 stream so every storey of one house
    // agrees on whether the shaft below exists.
    const std::uint32_t s0 =
        dungeon_scene_seed(worldSeed, cx, cy, ref.ordinal, 0);
    return ((s0 >> 16) & 1u) != 0u;
}

void dungeon_stair_point(const DungeonRef& ref, bool up, float& x, float& y) {
    const DungeonRoom room = dungeon_room(ref);
    // A tower hall is ROUND: the rect's corners lie outside the circle, so
    // its pads sit on the W/E axis instead — 6 tiles in from the wall (the
    // 3×3 pad plus a body radius, sub/body.h, clear of the masonry ring).
    if (ref.kind == DungeonRef::SpireTower) {
        x = up ? room.cx - room.hx + 6.0f : room.cx + room.hx - 6.0f;
        y = room.cy;
        return;
    }
    // One vertical line per shaft, level-independent: NW climbs, NE
    // descends. 4 tiles off the corner — one doorway width — so the pad
    // clears the outer walls and any partition ever nudged against them.
    x = up ? room.cx - room.hx + 4.0f : room.cx + room.hx - 4.0f;
    y = room.cy - room.hy + 4.0f;
}

void dungeon_shaft_arrival_point(const DungeonRef& ref, bool wentUp,
                                 float& x, float& y) {
    // A tower's up-shaft tops out at the new storey's DOWN pad (and the
    // down-shaft lands on the UP pad) — the ladder alternates sides. A
    // house shaft is one vertical line: you arrive on the pad you took.
    const bool pad = ref.kind == DungeonRef::SpireTower ? !wentUp : wentUp;
    dungeon_stair_point(ref, pad, x, y);
}

void dungeon_roof_hatch_point(const DungeonRef& ref, float& x, float& y) {
    // North point of the hall's axis, the same 6-tile wall clearance the
    // stair pads keep — the one spot on the top storey no shaft occupies.
    const DungeonRoom room = dungeon_room(ref);
    x = room.cx;
    y = room.cy - room.hy + 6.0f;
}

void dispatch_generate_dungeon(const CellContext& ctx, SubworldMapData& out) {
    switch (ctx.dungeon.kind) {
        case DungeonRef::House:      gen_dungeon_house(ctx, out);       break;
        case DungeonRef::Cave:       gen_dungeon_cave(ctx, out);        break;
        case DungeonRef::SpireTower: gen_dungeon_spire_tower(ctx, out); break;
        case DungeonRef::Void:
        default:                     gen_dungeon_void(ctx, out);        break;
    }
}

} // namespace sm::sub

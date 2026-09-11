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
        case DungeonRef::Cave:         return TILE_ROCK;  // scree
        case DungeonRef::PrologueRoad: return TILE_ROAD;  // the paved bed
        default:                       return TILE_SQUARE; // flagged hall
    }
}

// kind, id, death node, household≥0, vermin≥0, den family, ladder, hatch,
// water, ring, scene biome (rock ring for interiors), wrap block (0 = static)
constexpr DungeonKindRow kDungeonKindRows[] = {
    { DungeonRef::None,       "none",   nullptr,
                              false, false, /*garrison*/false,
                              LandmarkType::Ruin,
                              false, false, 0.0f, DungeonRef::Void,
                              Biome::Mountain, 0 },
    { DungeonRef::House,      "house",  nullptr,
                              true,  false, /*garrison*/false,
                              LandmarkType::Ruin,
                              false, false, 0.0f, DungeonRef::Void,
                              Biome::Mountain, 0 },
    // A cave garrisons from its place the day a Lair stands on the map; on
    // a wild cell (no landmark) the same row falls through to FaunaCount.
    { DungeonRef::Cave,       "cave",   nullptr,
                              false, true,  /*garrison*/true,
                              LandmarkType::Ruin,
                              false, false, 0.0f, DungeonRef::Void,
                              Biome::Mountain, 0 },
    { DungeonRef::SpireTower, "spire_tower", nullptr,
                              false, true,  /*garrison*/true,
                              LandmarkType::Spire,
                              true,  true,  0.0f, DungeonRef::Void,
                              Biome::Mountain, 0 },
    // The one place in the world where dying is a story beat: the witch
    // takes the body the road took (release.md §3 scene 2).
    { DungeonRef::PrologueRoad, "prologue_road", "prologue_main",
                              false, false, /*garrison*/false,
                              LandmarkType::Ruin,
                              false, false, 0.0f, DungeonRef::Void,
                              Biome::Taiga, 3 },
};
static_assert(rows_in_enum_order(kDungeonKindRows, &DungeonKindRow::kind),
              "kDungeonKindRows must mirror DungeonRef::Kind");

const DungeonKindRow& dungeon_kind_row(std::uint8_t kind) {
    return kind < std::size(kDungeonKindRows) ? kDungeonKindRows[kind]
                                              : kDungeonKindRows[0];
}

std::uint8_t dungeon_kind_from_token(const char* token) {
    if (token == nullptr) return DungeonRef::None;
    for (const DungeonKindRow& row : kDungeonKindRows) {
        if (row.id == nullptr) continue;
        const char* a = token;
        const char* b = row.id;
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return row.kind;
    }
    return DungeonRef::None;
}

DungeonRoom dungeon_room(const DungeonRef& ref) {
    switch (ref.kind) {
        case DungeonRef::House:        return dungeon_house_room(ref);
        case DungeonRef::Cave:         return dungeon_cave_room(ref);
        case DungeonRef::SpireTower:   return dungeon_spire_tower_room(ref);
        case DungeonRef::PrologueRoad: return dungeon_prologue_road_room(ref);
        default:                       return DungeonRoom{};
    }
}

int dungeon_storey_count(const DungeonRef& ref) {
    switch (ref.kind) {
        case DungeonRef::SpireTower: return dungeon_spire_tower_floors(ref);
        default:                     return 1;
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
    // A tower climbs 0..floors-1; every storey below the top has a way up.
    if (ref.kind == DungeonRef::SpireTower) {
        return int(ref.level) < dungeon_spire_tower_floors(ref) - 1;
    }
    // Only a house stacks rooms. A cave has depth, and depth is walked, not
    // climbed; an open pocket stands under the sky.
    if (ref.kind != DungeonRef::House) return false;
    // A storey is worth climbing only if it seats a room you can fight in:
    // both interior half-spans at least the manoeuvre floor the partitions
    // are cut to (sub/dgn/house.cpp kMinRoomSpanTiles = 12 — a doorway plus
    // melee reach on either side). Smaller houses are one room and a roof.
    const DungeonRoom room = dungeon_room(ref);
    return std::min(room.hx, room.hy) >= 12.0f;
}

bool dungeon_has_cellar(const DungeonRef& ref, std::uint32_t worldSeed,
                        int cx, int cy) {
    // Only a house digs. A cave IS the underground; a tower rises, nothing
    // of it is dug (its downward shafts live BETWEEN storeys — E pad,
    // dungeon_stair_point — never below the ground floor); an open pocket
    // has no door to dig under.
    if (ref.kind != DungeonRef::House) return false;
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
    // A ladder's up-shaft tops out at the new storey's DOWN pad (and the
    // down-shaft lands on the UP pad) — the sides alternate. A fixed-pair
    // shaft is one vertical line: you arrive on the pad you took.
    const bool pad = dungeon_kind_row(ref.kind).shaftLadder ? !wentUp : wentUp;
    dungeon_stair_point(ref, pad, x, y);
}

void dungeon_roof_hatch_point(const DungeonRef& ref, float& x, float& y) {
    // North point of the hall's axis, the same 6-tile wall clearance the
    // stair pads keep — the one spot on the top storey no shaft occupies.
    const DungeonRoom room = dungeon_room(ref);
    x = room.cx;
    y = room.cy - room.hy + 6.0f;
}

void spire_crown_hatch_point(float& x, float& y) {
    // North of the axis at half the radius — the derivation is in the header:
    // clear of the orb at the centre, clear of the parapet on the rim.
    x = kSpireTowerLocalCenter;
    y = kSpireTowerLocalCenter - kSpireTowerRadiusTiles * 0.5f;
}

void stamp_dungeon_shaft(SubworldMapData& out, bool up, float x, float y,
                         float ceilingM) {
    // Both ends of a shaft are square props on the pad's own axis; only the
    // kind, the lift and the run differ. The direction rides `tag`, which is
    // what the engine reads to know which way this end goes.
    const std::uint16_t tag = up ? std::uint16_t(1) : std::uint16_t(0);
    auto prop = [&](Structure::Kind kind, float zBase, float height) {
        Structure s{};
        s.kind = kind;
        s.x = x;
        s.y = y;
        s.hx = structure_min_half_xy(kind);
        s.hy = s.hx;
        s.radius = s.hx;
        s.zBase = zBase;
        s.height = height > 0.0f ? height : structure_min_height(kind);
        s.tag = tag;
        out.structures.push_back(s);
    };
    if (!up) {
        prop(Structure::Hatch, 0.0f, 0.0f);   // the lid at your feet
        return;
    }
    // Climbing: the ladder runs the storey's full height, and the opening it
    // climbs to is the same hatch leaf RECESSED into the slab — its top flush
    // with the ceiling, so from below the eye sees a hole in the masonry and
    // not a plank hanging under it. No thickness of its own: a lid you look up
    // at is the lid somebody upstairs stands on.
    prop(Structure::Ladder, 0.0f, ceilingM);
    prop(Structure::Hatch, ceilingM - structure_min_height(Structure::Hatch),
         0.0f);
}

void dispatch_generate_dungeon(const CellContext& ctx, SubworldMapData& out) {
    switch (ctx.dungeon.kind) {
        case DungeonRef::House:      gen_dungeon_house(ctx, out);       break;
        case DungeonRef::Cave:       gen_dungeon_cave(ctx, out);        break;
        case DungeonRef::SpireTower: gen_dungeon_spire_tower(ctx, out); break;
        case DungeonRef::PrologueRoad:
            gen_dungeon_prologue_road(ctx, out);
            break;
        case DungeonRef::Void:
        default:                     gen_dungeon_void(ctx, out);        break;
    }
    // The scene's floor catalog (CANON S28): every trav==1 tile becomes a
    // stand point, folded HERE — the one moment `trav` is alive and the one
    // place every interior passes through, so no module can forget it and
    // no spawner has to guess walkability back out of a tile byte. Only a
    // scene that can ever populate pays for the vector, and WHICH scenes
    // those are is the kind row's own columns (a cellar, level < 0, is
    // always a den) — never a branch on the kind here.
    out.standPoints.clear();
    const DungeonKindRow& kindRow = dungeon_kind_row(ctx.dungeon.kind);
    const bool populates = kindRow.householdAbove || kindRow.verminAbove
                        || ctx.dungeon.level < 0;
    if (populates && out.trav.size() == out.tiles.size()) {
        for (std::size_t i = 0; i < out.trav.size(); ++i) {
            if (!out.trav[i]) continue;
            out.standPoints.push_back(
                StandPoint{std::uint16_t(i % std::size_t(kCellSize)),
                           std::uint16_t(i / std::size_t(kCellSize)), 0u});
        }
    }
}

} // namespace sm::sub

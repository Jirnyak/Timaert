// Dungeon dispatch — routes an interior CellContext to its module and owns
// the shared Void filler (the sealed ring around the one real interior cell;
// it is scenery for nobody and a generator for nothing, so it lives here).
#include "sub/dgn/dispatch.h"

namespace sm::sub {

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

DungeonRoom dungeon_room(const DungeonRef& ref) {
    switch (ref.kind) {
        case DungeonRef::House: return dungeon_house_room(ref);
        default:                return DungeonRoom{};
    }
}

void dungeon_entry_point(const DungeonRef& ref, float& x, float& y) {
    // The pad sits 2 tiles inside the south wall's midpoint — deep enough
    // that a body radius (1.5 tiles, sub/body.h) never spawns inside the
    // wall solid, close enough that the door reads as "right there".
    const DungeonRoom room = dungeon_room(ref);
    x = room.cx;
    y = room.cy + room.hy - 2.0f;
}

void dispatch_generate_dungeon(const CellContext& ctx, SubworldMapData& out) {
    switch (ctx.dungeon.kind) {
        case DungeonRef::House: gen_dungeon_house(ctx, out); break;
        case DungeonRef::Void:
        default:                gen_dungeon_void(ctx, out);  break;
    }
}

} // namespace sm::sub

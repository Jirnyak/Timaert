// Subworld map data — tile types, structures, cell context.
// Mirrors subworld/map-data.ts (compact).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm::sub {

constexpr int kCellSize = 1024;          // tiles per macro cell
constexpr int kFullSize = kCellSize * 3; // 3×3 grid

// Tile constants for the subworld grid.
enum Tile : std::uint8_t {
    TILE_EMPTY = 0, TILE_GRASS, TILE_FIELD, TILE_TREE_DECOR,
    TILE_ROAD, TILE_HOUSE, TILE_WALL, TILE_WATER, TILE_SHORE,
    TILE_SQUARE, TILE_ROCK,
    TILE_COUNT,
};

// ── Ground movement table ──────────────────────────────────────────────────
// How fast a body crosses each ground type, as a multiplier on its own speed.
// ONE data row per tile id: this is the single source of truth for terrain cost,
// read by the mass-battle steering pass (sub/battle.h) through a plain pointer —
// no branch chain, no hardcoded "is it water" test anywhere in the engine.
// Adding a ground type = adding an enum value and a row here; the static_assert
// below refuses to build if the two ever drift apart.
//
// Hard blocking is NOT this table's job: solid structures (walls, houses)
// physically stop and carry bodies through sub/collide.h, which indexes the
// oriented Structure volumes below. The HOUSE/WALL rows here only price the
// unpainted verge tiles hugging a footprint — the solid itself refuses entry.
inline constexpr float kTileMovementSpeed[TILE_COUNT] = {
    1.00f,   // TILE_EMPTY      — untouched ground, treat as open
    1.00f,   // TILE_GRASS      — reference surface
    0.95f    // TILE_FIELD      — crops underfoot
    ,
    0.80f,   // TILE_TREE_DECOR — undergrowth
    1.15f,   // TILE_ROAD       — the reason roads exist
    0.35f,   // TILE_HOUSE      — squeezing through a building footprint
    0.30f,   // TILE_WALL       — same, worse
    0.45f,   // TILE_WATER      — wading (rivers are honest water cells)
    0.75f,   // TILE_SHORE      — wet sand / shallows
    1.10f,   // TILE_SQUARE     — paved settlement square
    0.65f,   // TILE_ROCK       — scree
};
static_assert(sizeof(kTileMovementSpeed) / sizeof(float) == std::size_t(TILE_COUNT),
              "every Tile id needs exactly one movement row");

enum class SubworldMode : std::uint8_t {
    Open, City, Village, Forest, Mountain, Swamp, Ruin, Water, Grassland, Road, Spire,
    Field,
    // An INTERIOR scene (house / castle / cave floor) projected behind a door
    // of some subworld structure — not the open air of its macro cell. Routed
    // to the self-contained sub/dgn/ generators; never produced by the macro
    // resolver (see CellContext::DungeonRef).
    Dungeon,
};

enum class CellLandmarkKind : std::uint8_t {
    None = 0, City, Village, Ruin, Spire,
};

// Dungeon door reference. When `kind` is non-None the window cell is an
// INTERIOR scene entered through a door standing on macro cell (cx, cy) —
// not that cell's open air. The subworld engine synthesises these contexts
// for its dungeon session (the dungeon is a projection OF the subworld, the
// same way the subworld is a projection of the macro map); the macro
// resolver never sets one. Identity is {cell, ordinal, level}: ordinal is
// the building's deterministic index within its cell's generation order, so
// the same seed always opens the same interior behind the same door.
struct DungeonRef {
    enum Kind : std::uint8_t {
        None = 0,
        House = 1,      // a settlement building's interior
        Void = 0xFF,    // sealed filler for the dungeon window's ring cells
    };
    std::uint8_t  kind = None;
    // Storey, SIGNED: 0 = the level the door opens onto, +1 up, -1 a cellar.
    // Stairs re-enter the same identity at level ± 1 (sub/dgn/dispatch.h).
    std::int8_t   level = 0;
    std::uint16_t ordinal = 0;  // building index within its owning cell
    // Exterior footprint half-extents (tiles) of the entered building — the
    // interior keeps the building's real proportions and scales them up
    // (M&M scale, owner ruling 2026-08-12: inside is roomier than outside).
    float footHx = 0.0f;
    float footHy = 0.0f;
};

// CellContext — what the macroworld knows about a single cell.
struct CellContext {
    int   cx, cy;
    float macroHeight;   // 0..1
    float macroTemperature = 0.5f; // 0..1, used for TS tree species bands
    Biome biome;
    FeatureType feature;
    int   landmarkSettlementId; // -1 = none
    int   landmarkSize;         // population / strength
    CellLandmarkKind landmarkKind = CellLandmarkKind::None;
    // Owning kingdom of the landmark on this cell (index into Politik::kingdoms;
    // -1 = none / unowned). The subworld does not know what a kingdom IS — it
    // carries the index so the engine can resolve the citizens' faction through
    // the one macro resolver (faction_index_for_kingdom), instead of every
    // settlement in the world fielding imperial guards.
    int   kingdomIdx = -1;
    std::uint32_t seed;
    // Macro TreeLayer count for this cell (0..16384); -1 = unknown — the
    // generator re-derives it from biome/features (tests, bare resolvers).
    int treeCount = -1;
    // Furrow orientation of this cell's ploughed-field material, resolved at
    // macro resolve from the WRAPPED cell coords (field_furrows_vertical,
    // sub/material.h — the twin of the map's macro.frag furrow hash). Carried
    // like macroTemperature: generation ignores it, the renderer's material
    // grid consumes it, so what the map paints is what lies underfoot.
    bool fieldFurrowsVert = false;
    // The cell's fertility (the macro moisture channel — the same number the
    // field stamp scored cells by). Crop density is contextual on it.
    float fertility01 = 0.5f;
    // The WORLD seed (not the per-cell hash above): seam-symmetric features
    // — the field-plot lattice — need a seed every neighbour derives
    // identically, or the two sides of a shared plot disagree. 0 in bare
    // test contexts keeps them deterministic.
    std::uint32_t worldSeed = 0;
    // Stands the sickle has taken from this cell and regrowth has not yet
    // returned (GameState::cropOverrides — the crop_count harvest scar).
    // The crop scatter plants its natural yield minus this, so returning to
    // a harvested field does NOT resurrect the wheat. 0 = unscarred.
    int cropHarvested = 0;
    // Interior-scene door (see DungeonRef). kind == None for every real
    // macro cell; the engine's dungeon session is the only writer.
    DungeonRef dungeon{};
};

// The effective landmark of a cell for terrain purposes. A macro settlement
// projects as a City cell even when landmarkKind is None (mirrors
// resolve_mode's `landmarkSettlementId >= 0` branch) — one helper so terrain
// flattening and mode resolution can never disagree.
inline CellLandmarkKind effective_landmark(const CellContext& ctx) {
    if (ctx.landmarkKind != CellLandmarkKind::None) return ctx.landmarkKind;
    if (ctx.landmarkSettlementId >= 0) return CellLandmarkKind::City;
    return CellLandmarkKind::None;
}

// ── What a prop DOES when the player presses E on it ────────────────────────
// The world is built in two passes: generators place PROPS (geometry — trees,
// walls, houses, doors, lanterns), then this one column says which of them are
// also INTERACTIVE and with what verb. Adding "drink from the well" is a row
// in the prop table plus a case in the one dispatcher — never a new system, and
// never a special case in the engine's input path.
enum class InteractId : std::uint8_t {
    None = 0,
    // Step through into the interior behind this door (sub/dgn). The prop's
    // `tag` names WHICH building: the house's ordinal within its cell.
    Door,
    // Take the shaft this prop stands on: `tag` 1 = up, 0 = down.
    Stairs,
    // The corpse of something you killed — not a composite prop but an ECS
    // body; it shares the verb so one prompt and one keypress serve both.
    Loot,
    Count,
};

struct InteractRow {
    // Shown in the HUD prompt as "[E] <verb>".
    const char* verb;
    // How far the player may stand from the prop's surface, in tiles. A door
    // is arm's length (the melee reach every other contact uses); a corpse is
    // picked up from a little further because you loot what fell around you.
    float reachTiles;
};
inline constexpr InteractRow kInteractRows[int(InteractId::Count)] = {
    /* None   */ {"",            0.0f},
    /* Door   */ {"Enter",       5.0f},
    /* Stairs */ {"Take stairs", 5.0f},
    /* Loot   */ {"Loot",       12.0f},
};
inline constexpr const InteractRow& interact_row(InteractId i) {
    return kInteractRows[int(i) < int(InteractId::Count) ? int(i) : 0];
}

struct Structure {
    enum Kind : std::uint8_t { Tree = 0, Rock, House, Wall, Bridge, Crop,
                               Fence, Furnish, Door, Lantern, Stairs } kind;
    static constexpr int kKindCount = int(Stairs) + 1;
    // Footprint silhouette. Box is the default; Cylinder renders (and collides)
    // as a round prism — wall towers, gate jambs, the spire. One byte, not a
    // new Kind: shape is orthogonal to what the thing IS.
    enum Shape : std::uint8_t { Box = 0, Cylinder = 1 };
    float x, y;
    float radius;        // XZ half-extent (bounding); billboard scale for Tree
    float height;        // metres; NEGATIVE = decayed/abandoned (map_factory.h)
    // ── Universal oriented-box extension (all zero ⇒ legacy square box). ──
    float yaw   = 0.0f;  // rotation about vertical, radians, tile-space CCW
    float hx    = 0.0f;  // half-extent along local X, tiles (0 ⇒ radius)
    float hy    = 0.0f;  // half-extent along local Y, tiles (0 ⇒ radius)
    float zBase = 0.0f;  // bottom lift above the terrain seat, metres — gate
                         // lintels / decks: bodies pass beneath, stand on top
    Shape shape = Box;
    // Per-INSTANCE payload for the prop's interaction (the kind decides the
    // verb, this decides which one of them this is): a door carries its
    // building's ordinal, a stair carries its direction. Meaningless — and
    // ignored — for a kind with no interaction.
    std::uint16_t tag = 0;
};

// ── Structure geometry — the ONE contract shared by the 3D renderer and the
// collision index (sub/collide.h). Both derive a structure's oriented footprint
// and vertical span from these helpers, so what you SEE is exactly what blocks
// and carries you. ──────────────────────────────────────────────────────────

inline float structure_half_x(const Structure& s) {
    return s.hx > 0.0f ? s.hx : s.radius;
}
inline float structure_half_y(const Structure& s) {
    return s.hy > 0.0f ? s.hy : s.radius;
}

// Squared 2D distance from a point to the SURFACE of a prop's oriented
// footprint (0 inside it). Reach is measured to the surface, never to the
// centre, or a wide building would be unreachable from its own doorstep.
inline float structure_surface_dist2(const Structure& s, float px, float py) {
    const float dx = px - s.x;
    const float dy = py - s.y;
    const float c = std::cos(s.yaw);
    const float sn = std::sin(s.yaw);
    const float lx = dx * c + dy * sn;
    const float ly = -dx * sn + dy * c;
    const float qx = std::max(0.0f, std::abs(lx) - structure_half_x(s));
    const float qy = std::max(0.0f, std::abs(ly) - structure_half_y(s));
    return qx * qx + qy * qy;
}

// ── The ONE per-kind prop row. The size-floor / loot forks that used to live
// here promised to collapse into a table "when environment props land" — the
// Crop kind is that third prop, so here is the table. Adding a structure kind
// = one row; every per-kind question below reads its column. ──
struct StructureKindRow {
    // Key into the ONE loot registry (`roll_loot_profile`, macro/items.h) —
    // the same resolver a kill goes through. Empty id = drops nothing (yet),
    // and the harvest door skips the kind entirely.
    const char* lootId;
    // Degenerate-record floors keeping legacy records visible/solid.
    // Formerly renderer literals; shared so collision can never disagree
    // with the pixels.
    float minHalfXy;   // tiles
    float minHeight;   // metres
    // Height at which the kind's loot profile pays verbatim; taller and
    // shorter records scale off it (a 20 m mast outpays a tundra scrub, a
    // wheat stand pays a stalk's worth). 0 = no yield scaling defined.
    float yieldRefHeightM;
    // Enters the collision index (sub/collide.h). Trees are deliberately
    // non-solid — undergrowth is a battle speed cost, not a wall — and a
    // stand of wheat is walked through; Rock/Bridge are not part of the 3D
    // structure pass yet and stay transparent with it.
    bool solid;
    // Status line the harvest door prints ("" = not harvestable).
    const char* harvestMsg;
    // Which pass draws this prop. Two hardcoded kind lists used to live in
    // the renderer (one per pass), so every new prop was invisible until
    // somebody remembered to edit both — the door bug that started this
    // system. Now the row says it.
    enum class Draw : std::uint8_t {
        None = 0,   // not drawn yet (Rock, Bridge)
        Billboard,  // camera-facing quad from the tree atlas
        Solid,      // the oriented box / cylinder structure pass
    };
    Draw draw;
    // Solid-pass material: wood grain vs masonry.
    bool wood;
    // What pressing E on this prop does (InteractId::None = scenery).
    InteractId interact;
    // Light this prop casts, as 0xRRGGBB + reach in tiles (0 radius = dark).
    // The engine hangs a LightEmitter on every lit prop through the ONE
    // point-light path (sub/lighting.h), so a lantern lights the street with
    // the same code a carried torch lights a corridor.
    std::uint32_t lightRgb;
    float lightRadiusTiles;
    // Metres above the prop's seat the light hangs — a lantern burns at its
    // crown, not at the foot of its post.
    float lightHeightM;
};
inline constexpr StructureKindRow kStructureKindRows[Structure::kKindCount] = {
    /* Tree   */ {"tree", 1.6f, 3.5f, 14.0f, false, "You fell a tree",
                  StructureKindRow::Draw::Billboard, true,
                  InteractId::None, 0u, 0.0f, 0.0f},
    /* Rock   */ {"",     1.6f, 3.5f,  0.0f, false, "",
                  StructureKindRow::Draw::None, false,
                  InteractId::None, 0u, 0.0f, 0.0f},
    /* House  */ {"",     1.6f, 3.5f,  0.0f, true,  "",
                  StructureKindRow::Draw::Solid, true,
                  InteractId::None, 0u, 0.0f, 0.0f},
    /* Wall   */ {"",     1.2f, 4.0f,  0.0f, true,  "",
                  StructureKindRow::Draw::Solid, false,
                  InteractId::None, 0u, 0.0f, 0.0f},
    /* Bridge */ {"",     1.6f, 3.5f,  0.0f, false, "",
                  StructureKindRow::Draw::None, true,
                  InteractId::None, 0u, 0.0f, 0.0f},
    /* Crop   */ {"crop", 0.4f, 0.5f,  1.2f, false, "You harvest the crop",
                  StructureKindRow::Draw::Billboard, true,
                  InteractId::None, 0u, 0.0f, 0.0f},
    // Fence: the field balks' boulder walls — knee-high, honest to walk
    // around (solid), drawn by the same box pass as walls in stone flavour.
    /* Fence  */ {"",     0.3f, 0.4f,  0.0f, true,  "",
                  StructureKindRow::Draw::Solid, false,
                  InteractId::None, 0u, 0.0f, 0.0f},
    // Furnish: interior furniture (beds, tables, chests — sub/dgn/). Solid so
    // a room fights around its furniture; waist-high floor (0.4 m) so a chest
    // is never inflated to a pillar by the legacy stub rule. Drawn wood-
    // flavoured in the box pass. No loot row yet — a chest that PAYS is a
    // future increment through this same row.
    /* Furnish*/ {"",     0.5f, 0.4f,  0.0f, true,  "",
                  StructureKindRow::Draw::Solid, true,
                  InteractId::None, 0u, 0.0f, 0.0f},
    // Door: the way in. Not solid — it hangs flush on a wall that already
    // blocks, and a door you bump into instead of opening is a door that
    // fights the player. A leaf is 2 m tall (a body plus its hat) and half a
    // tile wide, which is what makes it READ as a door from the street.
    /* Door   */ {"",     0.5f, 2.0f,  0.0f, false, "",
                  StructureKindRow::Draw::Solid, true,
                  InteractId::Door, 0u, 0.0f, 0.0f},
    // Lantern: a post with a flame on top. Solid so it is a real obstacle you
    // walk around, knee-thin. Warm 0xFFB060 at 24 tiles — the carried-torch
    // family (sub/lighting.h), hung at 3 m: above a body's head, so it lights
    // the street rather than the walker's boots.
    /* Lantern*/ {"",     0.3f, 3.0f,  0.0f, true,  "",
                  StructureKindRow::Draw::Solid, true,
                  InteractId::None, 0xFFB060u, 24.0f, 3.0f},
    // Stairs: the shaft between storeys, drawn as a low block you step onto.
    // Not solid (you stand ON its tile and press E), knee-high so it reads as
    // a flight of steps and not a table.
    /* Stairs */ {"",     1.5f, 0.5f,  0.0f, false, "",
                  StructureKindRow::Draw::Solid, true,
                  InteractId::Stairs, 0u, 0.0f, 0.0f},
};

inline constexpr const StructureKindRow& structure_kind_row(Structure::Kind k) {
    return kStructureKindRows[int(k) < Structure::kKindCount ? int(k) : 0];
}
inline constexpr float structure_min_half_xy(Structure::Kind k) {
    return structure_kind_row(k).minHalfXy;
}
inline constexpr float structure_min_height(Structure::Kind k) {
    return structure_kind_row(k).minHeight;
}
inline constexpr const char* structure_loot_id(Structure::Kind k) {
    return structure_kind_row(k).lootId;
}
inline constexpr bool structure_is_solid(Structure::Kind k) {
    return structure_kind_row(k).solid;
}
inline constexpr bool structure_is_lootable(Structure::Kind k) {
    return structure_kind_row(k).lootId[0] != '\0';
}
inline constexpr StructureKindRow::Draw structure_draw(Structure::Kind k) {
    return structure_kind_row(k).draw;
}
inline constexpr bool structure_is_wood(Structure::Kind k) {
    return structure_kind_row(k).wood;
}
inline constexpr InteractId structure_interact(Structure::Kind k) {
    return structure_kind_row(k).interact;
}
inline constexpr bool structure_is_lit(Structure::Kind k) {
    return structure_kind_row(k).lightRadiusTiles > 0.0f;
}

// Visible/solid height in metres. A decayed record (negative height — the
// map_factory.h sign-bit overlay) collapses to the per-kind stub floor, which
// is exactly how the renderer has always drawn it. The floor exists to keep
// degenerate LEGACY grounded records visible; a zBase-lifted body (gate
// lintel, deck) states its thickness explicitly and is never inflated —
// otherwise a 3 m lintel would grow a phantom metre that blocks whoever
// stands on it.
inline float structure_visible_height(const Structure& s) {
    const float minH = structure_min_height(s.kind);
    if (s.height < 0.0f) return minH;
    if (s.zBase > 0.0f) return s.height;
    return std::max(s.height, minH);
}

// Vertical solid span in absolute metres given the terrain sample at the
// structure's centre (`seatM`). Grounded bodies rest on the seat; zBase lifts
// the bottom clear of it (gate lintels, decks).
inline void structure_solid_span(const Structure& s, float seatM,
                                 float& z0, float& z1) {
    z0 = seatM + s.zBase;
    z1 = z0 + structure_visible_height(s);
}

struct SubworldMapData {
    std::vector<std::uint8_t> tiles;     // FullSize × FullSize
    std::vector<std::uint8_t> trav;      // 0 = wall, 1 = walkable
    std::vector<float>        heightmap; // FullSize × FullSize
    std::vector<Structure>    structures;
    float waterLevel = 0.4f;
};

inline std::size_t tile_index(int x, int y) { return std::size_t(y) * kFullSize + x; }

// ── Geometry helpers (port of subworld/map-data.ts) ──────────────────

// Compass direction index — clockwise from north. Matches the 8 neighbour
// slots in NeighborGrid (N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7).
enum class Dir : std::uint8_t {
    N = 0, NE = 1, E = 2, SE = 3,
    S = 4, SW = 5, W = 6, NW = 7,
};

inline constexpr int kDirOffsets[8][2] = {
    { 0, -1}, { 1, -1}, { 1,  0}, { 1,  1},
    { 0,  1}, {-1,  1}, {-1,  0}, {-1, -1},
};

} // namespace sm::sub

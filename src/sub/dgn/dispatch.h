// Dungeon (interior-scene) generators. Mirrors sub/gens/dispatch: each
// interior kind is a self-contained module TU; this header is the registry.
// A dungeon cell is generated INSTEAD of the open-air pipeline — the module
// writes the whole kCellSize² cell (tiles, trav, heightmap, structures) and
// owes the base terrain generator nothing.
#pragma once
#include "sub/map_data.h"

namespace sm::sub {

// THE dungeon seed rule — one identity {door cell, ordinal, level} ⇒ one
// scene, forever. Lives in the dungeon module so the engine (session
// resolver) and the generators (level-independent rolls, e.g. the cellar
// coin, which must read the level-0 stream) share one hash.
std::uint32_t dungeon_scene_seed(std::uint32_t worldSeed, int cx, int cy,
                                 std::uint16_t ordinal, std::int8_t level);

// ── The spire tower's exterior dimensions ───────────────────────────────────
// Shared between the OPEN-AIR generator (gens/dispatch gen_spire stamps the
// cylinder) and the dungeon session (the roof exit stands the player at the
// crown) — one authority or the roof and the tower disagree by metres.
// 128 m = 32 courses of the 4 m wall module (structure_min_height(Wall)):
// tall enough to crest the treeline (~30 m species tops) from anywhere in
// the 3×3 window — a spire the player cannot see is not a landmark.
inline constexpr float kSpireTowerHeightM = 128.0f;
// 12-tile exterior radius (24-tile crown). The floor is what the CROWN must
// seat, because the crown is a place a body stands: a parapet one wall module
// deep on the rim (1.2 tiles), the orb's plinth at the axis, and the hatch
// between them at half radius — 6 tiles of clear walking either side of it.
// A 7-tile crown seated none of that, which is why standing on it meant
// falling off it (owner, 2026-09-09). The aspect stays a spire's: 128 / 24 is
// still five and a half times as tall as it is wide.
inline constexpr float kSpireTowerRadiusTiles = 12.0f;
// WHERE in its cell the tower stands (cell-local tile coord of the axis,
// same on both axes): gen_spire stamps it at the cell's midpoint. Placement
// is the generator module's policy (CANON S17) — the engine (roof exit,
// power-circle zone) reads this instead of re-deriving it.
inline constexpr float kSpireTowerLocalCenter = float(kCellSize / 2);

// ── The crown ───────────────────────────────────────────────────────────────
// The tower top is a PLACE, not a lid: the orb at the axis, a parapet on the
// rim, and a hatch between them — the way back in. The open-air generator
// stamps all three (gens/dispatch gen_spire); the dungeon session reads the
// hatch point to stand the body coming out of it, so the two ends of one
// doorway can never disagree about where it is.
//
// Chest-high rail (above kStepUpM = 0.9 m, sub/collide.h — a body steps over
// a kerb and cannot step over this). It is the whole answer to a 128 m drop:
// leaving the crown is a decision, not an accident.
inline constexpr float kSpireCrownParapetHeightM = 1.2f;
// The hatch's seat on the crown: NORTH of the axis at half the radius — clear
// of the orb's plinth at the centre and clear of the rail on the rim, so a
// body materialising on it touches neither.
void spire_crown_hatch_point(float& x, float& y);

// ── The manoeuvre floor ─────────────────────────────────────────────────────
// The span an interior room needs to be a room you FIGHT in rather than a
// corridor you clinch in: melee reach (kPlayerMeleeRange = 5 tiles) on both
// sides of a doorway, with furniture clearance to spare. Shared, because it
// is one quantity: the house splits its hall while both halves keep it, and
// the tower's hall is guarded against it below.
inline constexpr float kInteriorFightSpanTiles = 12.0f;

// ── Storeys ─────────────────────────────────────────────────────────────────
// A house is up to three levels: cellar (-1) ↔ ground (0) ↔ upper (+1),
// joined by two stair SHAFTS at fixed corners of the room — NW climbs
// (0↔+1), NE descends (0↔-1). Fixed corners make the shafts a shared rule
// (generator stamps the pads, engine reads E on them, tests assert both)
// instead of plumbed data.
// A SPIRE TOWER climbs 0..tier-1 (ordinal = the spell's tier = the storey
// count): its W pad is the shaft to the storey ABOVE, its E pad the shaft
// to the storey BELOW — every storey between has both, so the climb is a
// ladder of shafts, not one vertical line.
bool dungeon_has_upper(const DungeonRef& ref);
bool dungeon_has_cellar(const DungeonRef& ref, std::uint32_t worldSeed,
                        int cx, int cy);
// Pad centre (cell-local tiles) of the climbing (NW / tower-W) / descending
// (NE / tower-E) shaft for this footprint's room. Positions are
// level-independent.
void dungeon_stair_point(const DungeonRef& ref, bool up, float& x, float& y);
// Where a body ARRIVING by shaft stands on its new storey. For a house the
// shaft is one vertical line (arrive on the pad you took); for a tower the
// up-shaft tops out at the new storey's DOWN pad and vice versa. One
// dispatch, so the engine's placement can never disagree with the module's
// pads.
void dungeon_shaft_arrival_point(const DungeonRef& ref, bool wentUp,
                                 float& x, float& y);
// The roof hatch pad of a spire tower's TOP storey — the way OUT onto the
// crown (the open-air scene, standing on the cylinder). Shared by the
// module (stamps the prop) and the engine (reach check + exit placement).
void dungeon_roof_hatch_point(const DungeonRef& ref, float& x, float& y);

// ── A shaft, as the eye reads it ────────────────────────────────────────────
// A shaft is a PAIR of props, not one block: climbing, you see a ladder rising
// to a dark hatch in the ceiling; descending, you see the hatch in the floor at
// your feet. One knee-high block served both directions until 2026-09-09 and
// read as a floor hatch in both, so "up" was a trapdoor underfoot leading
// nowhere the eye could follow (owner: «лестницы наверх выглядят как люки»).
// Every storey stamps its OWN end of a shaft, which is what keeps the pair
// agreeing: the hatch you climb to from below is the hatch you stand on above.
// `ceilingM` is where this storey's lid begins — the ladder's full run.
void stamp_dungeon_shaft(SubworldMapData& out, bool up, float x, float y,
                         float ceilingM);

// The tile an interior's WALKABLE ground is paved with. Placement code (the
// resident and vermin spawners) reads the composite's tiles, so the module
// that carved the place is the one that must say which of them is floor —
// a house is flagged and paved, a cave is bare scree.
Tile dungeon_floor_tile(const DungeonRef& ref);

// ── What a kind IS, in columns ──────────────────────────────────────────────
// The behavioural facts of an interior kind, one row per DungeonRef::Kind.
// The session reads these instead of comparing kinds, so a new kind — an
// open pocket scene under real sky (CANON S17) — is a row and a module, not
// a hunt for scattered branches. Geometry stays in the dispatches above: a
// column says what a kind IS, a dispatch says where its footprint puts
// things. (The sky itself needs no column: a ceiling is module masonry, and
// a module that stamps none stands under the honest sky.)
struct DungeonKindRow {
    std::uint8_t kind;       // DungeonRef::Kind — rows_in_enum_order guard
    // The kind's NAME, the way every registry in the game is addressed
    // (an npc row's id, an item's, a faction's). It is what lets a scene be
    // authored in content/ — which never includes sub/ — without a magic
    // ordinal: the scene says "prologue_road", this layer resolves it.
    const char* id;
    // What DYING here means. nullptr = death is death (the default and the
    // CANON law). A name = the logic node death ACTIVATES instead: the
    // scene's own story beat, keyed on the place rather than on the player,
    // so the damage door and the game-over path stay untouched. Its node
    // fires on the next logic tick — a story shown from gameplay goes
    // through a node, never a raw event (see the runtime's intercept).
    const char* deathNode;
    bool householdAbove;     // storeys ≥0 borrow RESIDENTS from Population
    bool verminAbove;        // storeys ≥0 draw VERMIN from FaunaCount
                             //   (a cellar, level<0, is always a den)
    LandmarkType denFamily;  // whose monster-table family the den draws
    bool shaftLadder;        // pads are directional (W climbs, E descends)
                             //   instead of fixed storey pairs
    bool roofHatch;          // the top storey opens a hatch onto open air
    float waterLevel;        // the scene's sea plane (an interior has none)
    std::uint8_t ringFiller; // DungeonRef::Kind sealing the window's ring
    Biome sceneBiome;        // the biome the synthetic resolver reports —
                             //   the ground's material (rock ring vs green)
    std::uint8_t wrapCells;  // 0 = static walled window (interiors).
                             //   N = TOROIDAL pocket: the scene is an N×N
                             //   block of variant cells, every window cell
                             //   resolves to its (x mod N, y mod N) variant,
                             //   and the seam re-centres — walk on forever,
                             //   the block meets itself. The ring filler is
                             //   unused under wrap. The module reads this
                             //   same column for its periodicity law.
};
// Void (0xFF) and anything unknown read the None row: no people, no beasts,
// no hatch — filler is inert by the same columns that make a house lively.
const DungeonKindRow& dungeon_kind_row(std::uint8_t kind);

// The kind a name addresses (DungeonRef::None when nothing answers) — the
// same token→row scan every registry here uses.
std::uint8_t dungeon_kind_from_token(const char* token);

// Interior room rectangle (cell-local tile coords) derived from the door's
// exterior footprint — the ONE geometry rule shared by the generator, the
// engine's entry placement, and the tests. Dispatches on ref.kind.
struct DungeonRoom {
    float cx = 0.0f, cy = 0.0f; // room centre, tiles within the cell
    float hx = 0.0f, hy = 0.0f; // interior half-extents, tiles
};
DungeonRoom dungeon_room(const DungeonRef& ref);

// Where a body entering this interior stands, and where E walks back out —
// the exit pad (cell-local tiles). One point on both sides of the door, so
// the engine and the generator can never disagree about the threshold.
void dungeon_entry_point(const DungeonRef& ref, float& x, float& y);

// Single generation entry point — dispatches on ctx.dungeon.kind. Void fills
// a sealed filler ring cell; every real kind routes to its module.
void dispatch_generate_dungeon(const CellContext& ctx, SubworldMapData& out);

// House interior (sub/dgn/house.cpp — self-contained module).
void gen_dungeon_house(const CellContext& ctx, SubworldMapData& out);
DungeonRoom dungeon_house_room(const DungeonRef& ref);

// Cave interior (sub/dgn/cave.cpp — self-contained module). Its "room" is
// the MOUTH chamber: the part the shared rules address, the rest is walked.
void gen_dungeon_cave(const CellContext& ctx, SubworldMapData& out);
DungeonRoom dungeon_cave_room(const DungeonRef& ref);

// Spire tower interior (sub/dgn/spire_tower.cpp — self-contained module).
// One round hall per storey; the "room" is the hall's bounding square.
void gen_dungeon_spire_tower(const CellContext& ctx, SubworldMapData& out);
DungeonRoom dungeon_spire_tower_room(const DungeonRef& ref);
// The hall's radius in tiles, STATED — not derived from the exterior.
//
// It used to be the exterior radius × 4, the house's law copied verbatim. It
// never belonged here. A house's facade VARIES, so its interior must be a
// multiple to guarantee the smallest one still seats a fight; every spire
// raises the same tower, so multiplying one constant by another only hid the
// fight floor inside a product — and balance living in a product of two knobs
// drifts silently, so widening the silhouette would have quietly rebuilt the
// battlefield. Two questions, two numbers. This one answers "how much room is
// a storey worth", and the guard below is the only thing it owes anyone.
inline constexpr float kSpireTowerHallRadiusTiles = 48.0f;
static_assert(kSpireTowerHallRadiusTiles >= kInteriorFightSpanTiles,
              "a tower storey must seat a fight, not a clinch");
// Storey count of the tower (= clamped ordinal = the spell's tier).
int dungeon_spire_tower_floors(const DungeonRef& ref);

// Prologue road (sub/dgn/prologue_road.cpp — self-contained module). The
// demo's opening pocket: a forest road under the honest sky, TOROIDALLY
// continuous (every height and tile is a periodic function of the cell, so
// the wrapped window walks forever). No door, no exit, no ceiling.
void gen_dungeon_prologue_road(const CellContext& ctx, SubworldMapData& out);
DungeonRoom dungeon_prologue_road_room(const DungeonRef& ref);

} // namespace sm::sub

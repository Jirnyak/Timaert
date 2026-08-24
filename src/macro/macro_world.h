// THE envelope of world layers (CANON S6, grown 2026-08-24). Everything a
// consumer may need in order to ask "what is this cell / what stands here". It
// grows by a FIELD when a new system needs one — never by a new argument at a
// call site. That law is the whole point: before the door, enter(), the two AI
// drivers and a dozen call sites each carried their own parallel list of layer
// arguments, three of them forgot `deposits`, and the deposit rows refused
// silently (canon-audit C4). One envelope, assembled ONCE per owner (the app
// for the live game, a test for its fixture), handed everywhere by reference.
//
// Every pointer is optional and fail-closed: a null layer reads as "that
// system contributes nothing here" — the zero contribution of S6, expressed by
// data, not by a second code path.
//
// Deliberately ecs-free and POD: the lightest consumers (hand-curated test
// executables, pure-data routers like fauna) take the envelope without
// dragging entt or GameState into their translation units.
#pragma once

namespace sm {

struct GameState;
struct TreeLayer;
struct DepositLayer;
struct TerrainData;
struct FeatureLayer;
struct ZoneLayer;
struct PathCostData;
struct TreeGrid;
struct LandmarkGrid;
namespace ecs { struct World; }

struct MacroWorld {
    GameState*  gs    = nullptr;
    TreeLayer*  trees = nullptr;
    ecs::World* world = nullptr;   // the roster row lives on squad entities
    const TerrainData* terrain = nullptr;   // the fauna row derives its
                                            //   baseline from the cell's biome
    DepositLayer* deposits = nullptr;       // the Clay/Iron/Stone carrier
    const FeatureLayer* features = nullptr; // roads / dirt roads / fields
    const ZoneLayer*    zones    = nullptr; // danger 0-9 (macro/zones.h)
    const PathCostData* pathCost = nullptr; // baked SP-weight grid + water flag
    const TreeGrid*     treeGrid = nullptr; // tree-point buckets (npc_ai.h) —
                                            //   the woodcutter's target search
    const LandmarkGrid* landmarks = nullptr; // baked cell → landmark index
                                             //   (macro/landmark_grid.h)
};

} // namespace sm

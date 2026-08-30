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
#include <cstdint>

namespace sm {

// A fact the macro layer produces about a fight it resolved. One kind so far;
// it grows like every other row-shaped thing here. `victim` and `killer` are
// entity bits (0 = a roster row, which has no entity of its own — it is named
// by its record id in `detail`), so the app can raise this as the ordinary
// NpcDeath the whole story layer already listens to: an auto-resolved death
// and a fought death are the SAME fact, which is exactly the point (CANON
// S13: one law of battle at both scales).
struct BattleFact {
    enum class Kind : std::uint8_t { Death = 0 };
    Kind          kind    = Kind::Death;
    std::uint16_t npcType = 0;       // the fallen body's row (NpcDeath.ix)
    std::uint32_t victim  = 0;       // entity bits, 0 for a roster member
    std::uint32_t killer  = 0;       // entity bits of the victorious leader
    std::int32_t  detail  = -1;      // the roster record id, -1 for a leader
    int           level   = 1;
    const char*   factionId = "";    // whose colours the fallen wore
};
using BattleFactSink = void (*)(void* user, const BattleFact& fact);

// The economy's fact record lives in econ_day.h (its writer); the envelope
// only carries the pointer, so a forward declaration keeps this header POD.
struct EconFact;
using EconFactSink = void (*)(void* user, const EconFact& fact);

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
    const ZoneLayer*    zones    = nullptr; // danger byte 0..255 (zones.h)
    const PathCostData* pathCost = nullptr; // baked SP-weight grid + water flag
    const TreeGrid*     treeGrid = nullptr; // tree-point buckets (npc_ai.h) —
                                            //   the woodcutter's target search
    const LandmarkGrid* landmarks = nullptr; // baked cell → landmark index
                                             //   (macro/landmark_grid.h)

    // ── The way OUT: facts the macro layer produces ───────────────────────
    // The macro layer must not see the event bus (that is L3; econ_day
    // already reports through a POD sink for the same reason), but a system
    // that stays SILENT is invisible to the story layer — «система обязана
    // объявить, какие факты она эмитит» (work_vector §1). So the envelope
    // carries the channel: the app plugs the bus in once, and every macro
    // system that has something to report finds it here rather than growing
    // an out-parameter. Null = nobody is listening, which is the honest
    // state of a headless fixture and costs a null check.
    BattleFactSink facts     = nullptr;
    void*          factsUser = nullptr;
    // The economy's fact channel (econ_day.h EconFact): gathered / produced /
    // consumed / famine, stamped with the landmark id by the world_tick relay.
    // Same law as `facts` above — null means nobody is listening. The balance
    // harness is the first listener; the app may plug the bus in later.
    EconFactSink   econFacts     = nullptr;
    void*          econFactsUser = nullptr;
};

} // namespace sm

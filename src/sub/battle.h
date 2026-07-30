// Mass battle steering — ONE algorithm for every subworld fight, from a single
// bandit to 16384 combatants.
//
// WHY THIS EXISTS
// ---------------
// The previous combat tick asked every actor, every frame: "who is the nearest
// hostile within 200 units?". In a battle 100 units wide that query returns
// *everyone*, so the cost was O(N²) — and each pair re-resolved hostility
// through two std::map<std::string> lookups plus strcmp. Two armies also
// collapsed into a point, because every actor steered at the exact centre of
// the same nearest enemy (and, with no enemy in range, at the player scalar —
// a second global attractor).
//
// The fix is not a faster long-range query; the long-range query is removed as
// a class of work:
//
//   1. Hostility is one integer op. Faction identity is the id STRING (the
//      universal key of this codebase), interned per tick into a dense index; a
//      unit carries a 64-bit `enemyMask` and "is j my enemy" is a shift and an
//      AND. No vocabulary, no roster limit, no strings and no maps in the loop.
//   2. Navigation reads a FIELD, not neighbours. A coarse per-faction influence
//      grid (`InfluenceField`) holds, for each cell, the position of the nearest
//      hostile concentration. An actor reads one cell. Armies flow toward each
//      other organically instead of converging on one body — the Bannerlord
//      approach phase — and the same field drives a lone bandit walking at the
//      player, because the player side is just another faction in it.
//   3. Advance is gated by SIGHT, not by a distance leash, and sight RELAYS
//      through comrades (the alert chain, below): a deep army charges as one
//      mass while scattered animals mind their own business.
//   4. Two bucket grids at two scales, both sized from the crowd's own data:
//      bodies are ~1 unit wide while weapons reach up to 25, so one cell size
//      cannot serve both queries without going quadratic or blind.
//   5. Bodies push each other apart (separation, capped at `maxSepNeighbors`),
//      attackers stand on a ring around their target instead of inside it, and
//      velocity is acceleration-limited — mass, not teleport.
//   6. Terrain is a TABLE (sub/map_data.h kTileMovementSpeed) plus a height
//      gradient. This module has no idea what "water" is; adding a ground type
//      is one enum value and one data row.
//
// Every pass is O(N) (+ O(cells), independent of N). `BattleStats.neighborVisits`
// is instrumented so a test can PROVE the bound instead of trusting a comment.
//
// Vulkan-free and entt-free by design: this is a plain struct-of-arrays over
// PODs, so `battle_ai_test` exercises the real shipping code headless. The ECS
// gather/scatter and all gameplay authority (damage, deaths, loot, XP, events)
// stay in sub/engine.cpp — this module only decides where bodies want to be.
#pragma once
#include <cstdint>
#include <vector>

namespace sm::sub {

// ── Universal capacity ─────────────────────────────────────────────────────
// One 2^14 ceiling for subworld actors, deliberately equal to the renderer's
// kMaxEntityInstances: a body that cannot be drawn must not be simulated.
constexpr int kMaxBattleUnits = 16384;

// Sentinel for a squared-distance cache meaning "no such body anywhere".
// Comfortably past the squared diagonal of the 3×3 window and deliberately
// FINITE — the engine translation unit is built with -ffast-math.
constexpr float kNoThreatDistance2 = 1.0e18f;

// ── Factions ───────────────────────────────────────────────────────────────
// There is NO faction vocabulary in this module and no fixed roster. The
// universal faction identity in this codebase is the id STRING — the same key
// gs->factions, player reputation and the loot profiles use — so battle code
// never names a faction and never needs editing when one is added.
//
// Per tick the engine INTERNS the faction id of every body present into a dense
// index, and asks the macro relation matrix once per interned pair. Hostility is
// then one shift and one AND (`BattleUnits::hostile`), while the world may hold
// any number of factions: only the ones actually standing in this 3×3 window
// occupy a slot.
//
// The cap is on SIMULTANEOUSLY PRESENT factions, not on the world's roster; 64
// lets one enemy mask stay a single std::uint64_t. A 65th distinct faction in
// one window degrades to neutral (it fights nobody) rather than misbehaving.
constexpr int kMaxBattleFactions = 64;

struct FactionSet {
    const char* ids[kMaxBattleFactions]{};
    // enemyMask[f] has bit g set iff faction f treats faction g as an enemy.
    // Asymmetry is allowed and used: the macro matrix is not required to be
    // symmetric, and a body may carry a private grudge on top (see
    // BattleUnitDesc::enemyMask).
    std::uint64_t enemyMask[kMaxBattleFactions]{};
    int count = 0;

    void clear();
    // Dense index for a faction id, adding it on first sight. Returns -1 for a
    // null/empty id or once the table is full. Comparison is pointer-first: the
    // ids come from static tables, so the common path never reaches strcmp.
    int intern(const char* id);
    // Index of an already-interned id, or -1.
    int find(const char* id) const;
};

// Fill enemyMask[] for every interned faction by asking `relation` about each
// ordered pair; a pair is hostile when the relation is < hostileBelow. A faction
// is never hostile to itself.
//
// MUST BE CALLED EVERY TICK, right after interning. This is not an optimisation
// point: the matrix is K² over the factions PRESENT in the window (2–5 in
// practice, so a couple of dozen lookups), while `clear()` necessarily wipes the
// masks. An earlier build amortised this behind a 0.5 s timer and produced a
// vicious bug — for 29 ticks out of 30 every enemyMask was zero, so armies had no
// enemies to advance on and simply stood, yet still struck a body that walked
// into contact, because the attack cooldown (1 s) outlasted the stale window.
// Standing armies that hit you when you touch them: that was this.
void build_faction_masks(FactionSet& fs,
                         int (*relation)(void* user, const char* a, const char* b),
                         void* user, int hostileBelow);

enum BattleUnitFlags : std::uint8_t {
    BU_None    = 0,
    BU_Missile = 1u << 0,   // fights at range: holds stand-off, never closes
    BU_Pinned  = 1u << 1,   // a real target and a real obstacle, but NOT steered
                            // here (the player body; fleeing wildlife owned by
                            // sub/ai.cpp). Its position is never written back.
    BU_Flying  = 1u << 2,   // owns its Z: terrain water/slope must not touch it
};

// One unit as handed to the battle pass. POD; the caller fills it from the ECS.
struct BattleUnitDesc {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float radius = 0.55f;       // body radius, world units (1 unit ≈ 1 m)
    float speed = 0.0f;         // world units / s
    float reach = 0.0f;         // attack range, world units (surface-to-centre)
    float sight = 200.0f;       // own detection range; relays through comrades
    std::uint64_t enemyMask = 0;
    std::int16_t  faction = -1; // interned index, -1 = factionless (fights none)
    std::uint8_t  flags = BU_None;
};

// ── Struct of arrays ───────────────────────────────────────────────────────
// One array per attribute. Storage is reserved once and reused every frame, so
// the hot path never allocates.
struct BattleUnits {
    int count = 0;
    std::vector<float> x, y, z;
    std::vector<float> vx, vy;
    std::vector<float> radius, speed, reach, sight;
    std::vector<std::uint64_t> enemyMask;
    std::vector<std::int16_t>  faction;
    std::vector<std::uint8_t>  flags;
    // Outputs, one per unit.
    std::vector<std::int32_t> target;    // unit index, -1 = nobody in reach-scan
    std::vector<std::uint8_t> inReach;   // 1 => target is strikeable this tick
    // Crowd extremes, maintained by add(). The grids size themselves from these
    // instead of from a constant, so the SAME code is correctly scaled for
    // 0.55-unit peasants and for a dragon.
    float maxRadius = 0.0f;
    float maxReach = 0.0f;

    void reserve(int n);
    void clear();
    // Appends a unit; returns its index, or -1 once kMaxBattleUnits is hit.
    int add(const BattleUnitDesc& d);

    inline bool hostile(int i, int j) const noexcept {
        const std::int16_t fj = faction[std::size_t(j)];
        if (fj < 0) return false;
        return ((enemyMask[std::size_t(i)] >> fj) & 1ull) != 0ull;
    }
};

// ── Fine grid: contact picking + body separation ───────────────────────────
// Uniform buckets over the units' ACTUAL bounding box, so cost tracks the real
// spread instead of the 3072² window. Counting sort: O(N + cells), two passes,
// no per-bucket allocation.
struct UnitGrid {
    float cell = 1.0f;
    float originX = 0.0f, originY = 0.0f;
    int cols = 1, rows = 1;
    std::vector<std::uint32_t> begin;   // cols*rows + 1 prefix sums
    std::vector<std::int32_t>  items;   // unit indices, bucket-sorted
    // Scatter cursors, kept as a member purely so the build allocates NOTHING in
    // the per-frame path (project rule: no heap churn in a hot loop). Two grids
    // rebuilt every tick over a 256² cell space would otherwise be half a
    // megabyte of malloc/free per frame.
    std::vector<std::uint32_t> cursor;

    inline int col_of(float wx) const noexcept {
        const int c = int((wx - originX) / cell);
        return c < 0 ? 0 : (c >= cols ? cols - 1 : c);
    }
    inline int row_of(float wy) const noexcept {
        const int r = int((wy - originY) / cell);
        return r < 0 ? 0 : (r >= rows ? rows - 1 : r);
    }
};

// `cell` is the intended query radius (queries then touch 2×2 cells). `maxDim`
// caps the grid resolution: if the units are spread so wide that the bbox needs
// more cells, the cell size grows instead of the allocation.
void build_unit_grid(UnitGrid& g, const BattleUnits& u, float cell, int maxDim);

// ── Coarse influence field: navigation without neighbour queries ───────────
// Per faction slot, per cell: how many units of that slot are here and where
// their centroid is; then, per slot, the position of the nearest cell holding
// that slot's ENEMIES (a nearest-site distance transform, two sweeps).
//
// An actor reads exactly one cell to know which way the enemy army is. This is
// what makes the pass O(N) and what makes armies converge as masses instead of
// funnelling into one body.
// The alert chain, and why it exists.
//
// A body charges when it can SEE an enemy — its own data-driven `sight`. That
// alone is wrong for armies: a rear rank 500 units behind the front cannot see
// anything and would stand still while its front rank fights (observed exactly
// that: "части армии просто группируются в кучу и никуда не бегут, особенно те
// кто стояли далеко"). The previous stop-gap, a flat maxApproachDist leash, was
// an arbitrary constant that produced the same bug from the other side.
//
// So awareness RELAYS: a cell is alerted if a body standing in it sees an enemy,
// and the alert then spreads to neighbouring cells that hold friends of the same
// faction — a chain of comrades, unlimited in length but only through the
// formation itself. Consequences, all emergent from one rule:
//   • an army charges as one mass, however deep, because its ranks are contiguous;
//   • scattered lone animals do NOT swarm — if the wolf near you noticed nothing,
//     the wolf a kilometre away is not connected to anything and stays home;
//   • it is free: the spread is a flood fill over the coarse grid, O(cells).
struct InfluenceField {
    float cell = 1.0f;
    // Like UnitGrid, the field covers the OCCUPIED bounding box, not the whole
    // 3072² window. Every unit stands inside that box by construction, and a
    // unit only ever reads its own cell, so a bbox-local transform is exact —
    // while a skirmish stops paying to clear and sweep ~9000 empty cells.
    float originX = 0.0f, originY = 0.0f;
    int cols = 1, rows = 1;
    std::uint64_t presentMask = 0;                        // factions with a body
    std::uint64_t factionEnemyMask[kMaxBattleFactions]{};  // OR of member masks
    // Planes are allocated ONLY for factions actually standing in the window. A
    // two-army battle pays for two planes — the per-frame clear was the whole
    // fixed cost of this pass, and a lone bandit must not pay a roster price.
    std::int16_t planeIdx[kMaxBattleFactions]{};          // -1 = faction absent
    int planeCount = 0;
    std::vector<std::uint64_t> occMask;   // [cell] factions present in the cell
    std::vector<std::uint32_t> count;     // [plane*cells + cell]
    std::vector<float> sumX, sumY;        // [plane*cells + cell] centroid accum
    // Nearest hostile concentration for a faction, propagated by the transform.
    std::vector<float> siteX, siteY;      // [plane*cells + cell]
    std::vector<std::uint8_t> hasSite;    // [plane*cells + cell]
    // Alert chain: 1 => bodies of this faction in this cell know where the enemy
    // is, either by their own eyes or relayed through comrades.
    std::vector<std::uint8_t> alert;      // [plane*cells + cell]

    inline std::size_t cell_count() const noexcept {
        return std::size_t(cols) * std::size_t(rows);
    }
    inline int col_of(float wx) const noexcept {
        const int c = int((wx - originX) / cell);
        return c < 0 ? 0 : (c >= cols ? cols - 1 : c);
    }
    inline int row_of(float wy) const noexcept {
        const int r = int((wy - originY) / cell);
        return r < 0 ? 0 : (r >= rows ? rows - 1 : r);
    }
    // Centre of a cell in world space (the transform measures from here).
    inline float cell_centre_x(int cx) const noexcept {
        return originX + (float(cx) + 0.5f) * cell;
    }
    inline float cell_centre_y(int cy) const noexcept {
        return originY + (float(cy) + 0.5f) * cell;
    }
    inline bool has_faction(int f) const noexcept {
        return f >= 0 && f < kMaxBattleFactions && planeIdx[f] >= 0;
    }
    // Valid only when has_faction(f).
    inline std::size_t plane(int f) const noexcept {
        return std::size_t(planeIdx[f]) * cell_count();
    }
};

void build_influence_field(InfluenceField& f, const BattleUnits& u,
                           float cell, float worldSize);

// ── Terrain ────────────────────────────────────────────────────────────────
// Two inputs, both data:
//   • the ground TYPE grid plus its movement table — the single source of
//     terrain cost (sub/map_data.h kTileMovementSpeed). Adding a ground type is
//     one enum value and one row; this module has no idea what "water" is.
//   • the height field, sampled through a function pointer (not a virtual) so
//     this translation unit stays free of the renderer and of Vulkan.
// Every field is optional: all-null is a flat featureless plain, which is
// exactly what a headless test wants as its control.
struct BattleTerrain {
    float (*heightAt)(void* user, float x, float y) = nullptr;
    void* user = nullptr;
    const std::uint8_t* tiles = nullptr;   // tileDim² ground-type ids
    int tileDim = 0;
    const float* tileSpeed = nullptr;      // movement multiplier per tile id
    int tileSpeedCount = 0;
    float worldMax = 0.0f;                 // movement clamp bound (kFullSize)
};

// ── Tunables ───────────────────────────────────────────────────────────────
// Every knob of the fight lives here; nothing is hardcoded in the loop.
struct BattleParams {
    // ── Grids ──────────────────────────────────────────────────────────────
    // The fine cell is NOT a constant: it is derived from the crowd's own body
    // size (BattleUnits::maxRadius), because that is what bounds occupancy. A
    // cell of side S holds at most ~(S / 2r)² bodies of radius r, so sizing the
    // cell to the bodies makes the neighbour count per query a GEOMETRIC bound
    // rather than a hope. 0.55-unit peasants get a ~2.5-unit cell; a dragon-sized
    // crowd gets a proportionally larger one. Same code, no tuning per content.
    float fineCellPerRadius = 4.0f; // fine cell = maxRadius * this
    float fineCellMin = 2.0f;
    // Contact scan cell, sized to the longest weapon in the crowd so a query
    // reaches its target within 2×2 cells (an archer's 25 units of reach must not
    // sweep a hundred fine cells).
    float pickCellPerReach = 1.0f;  // pick cell = maxReach + 2*maxRadius, scaled
    float pickCellMin = 8.0f;
    float influenceCell = 32.0f;    // coarse navigation / alert-chain resolution
    // ── Contact ────────────────────────────────────────────────────────────
    float ringFactor = 0.9f;        // stand at 90 % of reach ⇒ attackers form a
                                    // RING around a target instead of a point
    // ── Bodies ─────────────────────────────────────────────────────────────
    float sepRadiusScale = 1.15f;   // separate while d < (ri+rj) * this
    float sepWeight = 1.8f;         // separation vs. seek authority
    int   maxSepNeighbors = 8;      // accumulate at most this many pushes
    // Hard visit ceilings. Without them a DEGENERATE start (an army spawned on
    // one spot) would make the first frames quadratic before separation unpacks
    // it. Truncation is deterministic (the bucket walk is a pure function of the
    // unit index) and harmless: skipped candidates share a cell with taken ones.
    int   maxPickVisits = 64;
    int   maxSepVisits = 48;
    // ── Approach ───────────────────────────────────────────────────────────
    // No aggro leash. A body advances iff its cell is ALERTED — its own sight or
    // a comrade's, relayed through the formation (see InfluenceField::alert).
    float accelPerSpeed = 4.0f;     // full speed in 1/4 s ⇒ mass, not teleport
    float arriveEpsilon = 0.5f;     // inside this, stop and swing
    // ── Terrain ────────────────────────────────────────────────────────────
    // Ground cost itself is data (kTileMovementSpeed); these only shape slopes.
    //
    // CAUTION, learned the hard way. Grades here are LARGE by construction: the
    // height field spans kHeightScale (1500 m) across a 3072-unit window, so
    // ordinary rolling terrain reads grade > 1. The first version pushed bodies
    // away from any non-zero slope with weight 1.2 — and folded that push into
    // the separation accumulator, which is then scaled by sepWeight (1.8), so its
    // real authority was 2.16 against an advance of 1.0. Terrain beat the order:
    // measured on 120 m of relief per 200 units, all 2000 bodies ran at full
    // speed FOREVER (mean |v| 34.7 of 35), formation extent blew from 39 to 134
    // and engagement fell by a third. In game that read as armies dissolving into
    // clumps that slide between each other.
    //
    // The rule now: ordinary ground only COSTS speed; only genuinely un-walkable
    // steepness steers, and it steers with less authority than the advance.
    float slopeProbe = 8.0f;        // height sampling arm, world units
    float slopeWalkGrade = 1.0f;    // at/below this, terrain never steers (≈45°)
    float slopeAvoid = 0.6f;        // push authority — deliberately < the advance
    float slopeSpeedDrop = 0.6f;    // uphill speed penalty scale
    float slopeSpeedFloor = 0.35f;  // a charge is slowed by a hill, never stalled
};

// ── Deployment ─────────────────────────────────────────────────────────────
// Position of body `i` of `count` in side `side` (0 or 1), deployed as a block
// facing the opposing side across `centreX/centreY`. Pure geometry — shared by
// the console `test_battle`, the smoke harness and the unit test so all three
// stage the SAME fight. Writes {x, y} into `outXY`; false if the arguments are
// degenerate.
bool deploy_army_slot(float centreX, float centreY, int side, int i, int count,
                      float* outXY);

struct BattleStats {
    std::uint64_t neighborVisits = 0;   // O(N) proof: bounded by c·N
    std::uint32_t engaged = 0;          // units with a strikeable target
    std::uint32_t steered = 0;          // units the pass actually moved
    std::uint32_t advancing = 0;        // units alerted and closing on an enemy
};

// Grid cell sizes for this crowd, derived from its own body/weapon extremes.
float fine_cell_for(const BattleUnits& u, const BattleParams& prm);
float pick_cell_for(const BattleUnits& u, const BattleParams& prm);

// One pass over the SoA: pick contact, read the field, separate, apply terrain,
// accelerate, integrate. Writes x/y/vx/vy (except BU_Pinned) plus target and
// inReach for the caller's damage pass. `fine` bounds body separation, `pick`
// bounds the contact search — two structures at two scales, as the geometry
// demands (a 1-unit separation query and a 25-unit weapon reach cannot share one
// cell size without one of them becoming quadratic or blind).
void steer_battle(BattleUnits& u, const UnitGrid& fine, const UnitGrid& pick,
                  const InfluenceField& field, const BattleTerrain& terrain,
                  const BattleParams& prm, float dt, BattleStats* stats);

} // namespace sm::sub

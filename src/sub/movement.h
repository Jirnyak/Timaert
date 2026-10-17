// THE MOVER — how a body moves in the subworld. One algorithm for every living
// thing down here, from a browsing deer to a full kMaxSubObjects in a battle
// line, and for the player among them.
//
// It was called "mass battle steering" until 2026-08-30, and the name was a
// lie the owner caught: «БОЙ В ИГРЕ НИЧЕМ НЕ ОСОБЕННЫЙ, ЭТО ПРОСТО РЯДОМ
// ОКАЗАЛИСЬ ВРАГИ… нет никакого боя, просто если НПЦ видят врага, они бегут
// на него». There is no battle MODE (CANON S13): this pass moves townsfolk on
// errands, wildlife, fleeing prey, projected macro figures and the player, and
// war is merely one of the things that can claim a body's legs — the influence
// field below is an input to movement, not a mode of it. The lie had a cost
// worth remembering: because "battle" sounded optional, the player was pinned
// OUT of this pass and moved by his own input path, and so he alone paid
// nothing for ground or slope while every peasant did.
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
//      AND. No vocabulary, no fixed list, no strings and no maps in the loop.
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
//      velocity is acceleration-limited — mass, not teleport. A body is a
//      vertical CAPSULE (feet to eye line, height.h kBodyEyeM), not its ground
//      shadow: two bodies push only while their columns overlap, so a flier
//      above the crowd's heads is touched by nobody, while everyone standing
//      on the same ground keeps the exact planar law (gap 0).
//   6. Ground is a TABLE (sub/map_data.h kTileGroundWeight — the macro step
//      law's own weights, converted by its own 1/√weight) plus a height
//      gradient. This module has no idea what "water" is; adding a ground type
//      is one enum value and one data row. What the ground IS under a body is
//      asked of its SUPPORT (`standTile`), so a bridge deck is walked as road
//      while the river runs three metres below.
//
// Every pass is O(N) (+ O(cells), independent of N). `MoveStats.neighborVisits`
// is instrumented so a test can PROVE the bound instead of trusting a comment.
//
// Vulkan-free and entt-free by design: this is a plain struct-of-arrays over
// PODs, so `movement_steering_test` exercises the real shipping code headless. The ECS
// gather/scatter and all gameplay authority (damage, deaths, loot, XP, events)
// stay in sub/engine.cpp — this module only decides where bodies want to be.
#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include "sub/caps.h"         // kMaxSubObjects — the subworld scene ceiling
#include "tables/faction.h"   // kMaxFactions — THE world faction limit

namespace sm::sub {

// ── Universal capacity ─────────────────────────────────────────────────────
// The subworld ceiling is its OWN cap, `kMaxSubObjects@src/sub/caps.h`
// (65536 = 2^16, the reach of the u16 slot index; owner correction 2026-10-09
// split it from the macro cap AND raised it — step 0 of M-150; the former
// local name kMaxBodyCrowd and the shared reading of kUnifiedCap both died,
// and quoting 16k for subworld bodies is an ERROR, owner 2026-10-13).
// Locally it means what it always meant: this
// SoA, the renderer's instance buffer (vk_renderer_3d.cpp) and both
// broad-phase snapshot buffers (spell contact, melee swing) all read THIS one
// name. A body that cannot be drawn must not be simulated, and a snapshot OF
// bodies can never hold more than exist. Whatever embodied the body — a cell's
// module design, a squad off the 3×3 window, a field's head count, a scripted
// event, a console spawn — it lands in this one crowd: the ceiling stands on
// the BODY, never on its source.

// Sentinel for a squared-distance cache meaning "no such body anywhere".
// Comfortably past the squared diagonal of the 3×3 window and deliberately
// FINITE — the engine translation unit is built with -ffast-math.
constexpr float kNoThreatDistance2 = 1.0e18f;

// ── Factions ───────────────────────────────────────────────────────────────
// There is NO faction vocabulary in this module and no fixed creatures. The
// universal faction identity in this codebase is the id STRING — the same key
// gs->factions, player reputation and the loot profiles use — so battle code
// never names a faction and never needs editing when one is added.
//
// Per tick the engine INTERNS the faction id of every body present into a dense
// index, and asks the macro relation matrix once per interned pair. Hostility is
// then one shift and one AND (`BodyCrowd::hostile`), while the world may hold
// any number of factions: only the ones actually standing in this 3×3 window
// occupy a slot.
//
// The cap is on SIMULTANEOUSLY PRESENT factions; one enemy mask stays a single
// std::uint64_t. A 65th distinct faction in one window degrades to neutral (it
// fights nobody) rather than misbehaving.
//
// ИМЯ ЧИСЛУ ОДНО — `kMaxFactions@src/tables/faction.h`, И ЗДЕСЬ ЕГО НЕТ
// (M-182, вердикт владельца 2026-09-28: «число фракций заменить на 64 (к
// одному числу единому)»). Тут стоял алиас `kMaxCrowdFactions`, и он врал не
// значением, а ВОПРОСОМ: читатель видел два имени и достраивал к ним два
// разных вопроса — «сколько народов бывает в мире» против «сколько их сошлось
// в одном окне». Ответ у них общий и причина общая: 64 есть ШИРИНА
// `std::uint64_t` маски враждебности, которой `hostile` ниже отвечает «враг ли
// j» одним сдвигом и одним И. Вывод стоит под `static_assert` у самого
// объявления числа, а не здесь в прозе.

struct FactionSet {
    const char* ids[kMaxFactions]{};
    // enemyMask[f] has bit g set iff faction f treats faction g as an enemy.
    // Asymmetry is allowed and used: the macro matrix is not required to be
    // symmetric, and a body may carry a private grudge on top (see
    // BodyDesc::enemyMask).
    std::uint64_t enemyMask[kMaxFactions]{};
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
// The callback is handed INTERNED INDICES plus the set they index, not two bare
// `const char*`. Indices are what this module actually deals in, and they are
// what a caller can compare: an id string arriving from data or a save is a
// different pointer than the identical literal, so any callback that recognised
// a faction by pointer identity was correct only by accident. Ids remain
// available through `set.ids[]` for whoever genuinely needs the string (the
// macro relation matrix is keyed by id).
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
                         int (*relation)(void* user, const FactionSet& set,
                                         int a, int b),
                         void* user, int hostileBelow);

enum BodyFlags : std::uint8_t {
    B_None    = 0,
    B_Missile = 1u << 0,   // fights at range: holds stand-off, never closes
    B_Pinned  = 1u << 1,   // a real target and a real obstacle, but NOT steered
                            // here (the player body — its mover is the input).
                            // Its position is never written back.
    B_Flying  = 1u << 2,   // owns its Z: terrain water/slope must not touch it
    B_Passive = 1u << 3,   // the mind refuses the war drive (a Flee brain):
                            // never contact-scans, never answers the field,
                            // never strikes — its intent rules its legs. Still
                            // a real target, still separates, still blocks.
};

// One unit as handed to the battle pass. POD; the caller fills it from the ECS.
struct BodyDesc {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float radius = 0.55f;       // body radius, world units (1 unit ≈ 1 m)
    // How TALL this body's column stands, metres — the OTHER half of its
    // room, from the same table row the renderer draws (sub/body.h
    // body_height_m × shape). Separation touches two bodies only while
    // their columns overlap vertically. ≤ 0 = unstated → add() fills the
    // man-column default, so a bare fixture still takes a person's room.
    float height = 0.0f;
    float speed = 0.0f;         // world units / s
    float reach = 0.0f;         // attack range, world units (surface-to-centre)
    float sight = 200.0f;       // own detection range; relays through comrades
    // The brain's intent (SubworldAi.wantVx/Vy): the velocity this body's own
    // mind asks for. Executed by the steering pass ONLY while no combat drive
    // claims the body (no contact target, no alerted field cell), so a wander
    // amble yields to a battle the moment one starts. The vector carries its
    // own pace — a fleeing deer runs at flee speed, not at its combat sprint.
    float intentVx = 0.0f, intentVy = 0.0f;
    std::uint64_t enemyMask = 0;
    std::int16_t  faction = -1; // interned index, -1 = factionless (fights none)
    std::uint8_t  flags = B_None;
};

// ── Struct of arrays ───────────────────────────────────────────────────────
// One FIXED column per attribute, every column the length of THE ceiling:
// 64 B per body × kMaxSubObjects = exactly 1 MiB, in ONE block. Nothing is ever
// reserved, grown or reallocated — `add` refuses past the last slot, `clear`
// only rewinds `count` — so no index taken during a pass can dangle, and a
// capacity that has drifted out of step with the cap cannot exist. That promise
// used to be a discipline spread over two files (a `reserve` call here, a
// refusal there); now it is the type.
//
// The block is far too big for a stack frame, so the crowd is NON-COPYABLE,
// NON-MOVABLE and born only through `make_body_crowd()` — the shape
// `make_macro_store` already uses one layer up. Deliberate: "1 MiB on the
// stack" stops being a rule somebody must remember and becomes a line that
// does not compile.
struct BodyCrowd {
    int count = 0;
    std::array<float, kMaxSubObjects> x, y, z;
    std::array<float, kMaxSubObjects> vx, vy;
    std::array<float, kMaxSubObjects> intentVx, intentVy;
    std::array<float, kMaxSubObjects> radius, height, speed, reach, sight;
    std::array<std::uint64_t, kMaxSubObjects> enemyMask;
    std::array<std::int16_t,  kMaxSubObjects> faction;
    std::array<std::uint8_t,  kMaxSubObjects> flags;
    // Outputs, one per unit.
    std::array<std::int32_t, kMaxSubObjects> target;  // unit index, -1 = nobody in reach-scan
    std::array<std::uint8_t, kMaxSubObjects> inReach; // 1 => target is strikeable this tick
    // Crowd extremes, maintained by add(). The grids size themselves from these
    // instead of from a constant, so the SAME code is correctly scaled for
    // 0.55-unit peasants and for a dragon.
    float maxRadius = 0.0f;
    float maxReach = 0.0f;

    // Born ONLY through make_body_crowd(), i.e. only in its own heap block: the
    // constructor is private and the factory is its only friend, so `BodyCrowd
    // u;` in a function does not compile. Copy and move are gone too — a 1 MiB
    // memberwise copy is never what anybody meant.
    BodyCrowd(const BodyCrowd&) = delete;
    BodyCrowd& operator=(const BodyCrowd&) = delete;
    BodyCrowd(BodyCrowd&&) = delete;
    BodyCrowd& operator=(BodyCrowd&&) = delete;

    // Rewinds to empty. The columns keep their (stale) contents on purpose:
    // every slot below `count` is overwritten by `add` before anyone reads it,
    // and wiping 1 MiB per tick would buy nothing but a memset.
    void clear();
    // Takes the next slot; returns its index, or -1 once kMaxSubObjects is full.
    int add(const BodyDesc& d);

    inline bool hostile(int i, int j) const noexcept {
        const std::int16_t fj = faction[std::size_t(j)];
        if (fj < 0) return false;
        return ((enemyMask[std::size_t(i)] >> fj) & 1ull) != 0ull;
    }

private:
    BodyCrowd() = default;
    friend std::unique_ptr<BodyCrowd> make_body_crowd();
};
// 17 columns × 64 B per body over the ceiling, plus count and the two extremes.
// Named per DOD п.10: a size that is stated out loud is nailed down here, so it
// can never quietly drift from the picture of memory.
static_assert(sizeof(BodyCrowd) == std::size_t(kMaxSubObjects) * 64u + 16u,
              "one flat MiB: 64 B/body, nothing hidden but four bytes of pad");

// Births the crowd in its own heap block. Defined in movement.cpp on purpose:
// a 1 MiB object named with an initializer inside a HEADER is paid by every
// translation unit that includes it (AGENTS §5 п.13 — measured at 74× on
// make_macro_store, 55.6 s for one empty TU).
std::unique_ptr<BodyCrowd> make_body_crowd();

// ── Grid resolution ceilings ───────────────────────────────────────────────
// The two scratch structures below used to be vectors whose length was set IN
// THE TICK (`begin.assign(cells + 1u, 0u)`) — the last allocation-on-the-hot-
// path of the scene (DOD п.4, слом 8 M-150). Now the length is the CAP and the
// columns are fixed arrays, the BodyCrowd shape: a size that has drifted out of
// step with the cap cannot exist, and a reallocation in the tick is
// inexpressible rather than avoided.
//
// Resolution ceiling of a bucket grid. When the crowd is spread wider than this
// many cells the CELL grows instead of the allocation — density is low in that
// case, so the neighbour bound holds either way. (The number lived in
// engine.cpp as kBattleGridMaxDim while the arrays could grow; now the arrays
// ARE the ceiling, so the one name lives with them.)
constexpr int kUnitGridMaxDim = 256;
constexpr std::size_t kUnitGridCells =
    std::size_t(kUnitGridMaxDim) * std::size_t(kUnitGridMaxDim);
// Influence-field ceiling: the whole 3×3 window (kFullSize = 3072 world units)
// at the field's design resolution, MoveParams::influenceCell = 32 ⇒
// 3072 / 32 = 96 cells, +1 for the origin snap. Builds asked for a finer cell
// than the window affords are clamped to this many cells per axis (col_of /
// row_of bin outliers into the edge cells, as they always did). Tuning
// influenceCell below 32 therefore needs this cap raised first (ЗАКОН КАПА).
constexpr int kInfluenceGridMaxDim = 97;
constexpr std::size_t kInfluenceGridCells =
    std::size_t(kInfluenceGridMaxDim) * std::size_t(kInfluenceGridMaxDim);
// One plane per faction that can possibly stand in the window.
constexpr std::size_t kInfluencePlaneCap =
    kInfluenceGridCells * std::size_t(kMaxFactions);

// ── Fine grid: contact picking + body separation ───────────────────────────
// Uniform buckets over the units' ACTUAL bounding box, so cost tracks the real
// spread instead of the 3072² window. Counting sort: O(N + cells), two passes,
// no allocation anywhere — every column is a fixed array of the cap.
struct UnitGrid {
    float cell = 1.0f;
    float originX = 0.0f, originY = 0.0f;
    int cols = 1, rows = 1;
    // How many unit indices the last build wrote into `items` (== crowd count).
    // With fixed columns the array length is the CAP, so the honest length must
    // be its own number — two witnesses used to read items.size() for this.
    int itemCount = 0;
    std::array<std::uint32_t, kUnitGridCells + 1> begin; // cols*rows + 1 prefix sums
    std::array<std::int32_t, kMaxSubObjects> items;      // unit indices, bucket-sorted
    // Scatter cursors, a member for the same reason every buffer here is: the
    // per-frame build must allocate nothing.
    std::array<std::uint32_t, kUnitGridCells> cursor;

    inline int col_of(float wx) const noexcept {
        const int c = int((wx - originX) / cell);
        return c < 0 ? 0 : (c >= cols ? cols - 1 : c);
    }
    inline int row_of(float wy) const noexcept {
        const int r = int((wy - originY) / cell);
        return r < 0 ? 0 : (r >= rows ? rows - 1 : r);
    }

    // ~0.75 MiB: too big for a stack frame, so like BodyCrowd it is
    // NON-COPYABLE, NON-MOVABLE and born only through make_unit_grid().
    UnitGrid(const UnitGrid&) = delete;
    UnitGrid& operator=(const UnitGrid&) = delete;
    UnitGrid(UnitGrid&&) = delete;
    UnitGrid& operator=(UnitGrid&&) = delete;

private:
    UnitGrid() = default;
    friend std::unique_ptr<UnitGrid> make_unit_grid();
};
// Six 4-byte scalars + three u32/i32 columns over the caps, no padding.
static_assert(sizeof(UnitGrid) == 24u + (kUnitGridCells + 1u) * 4u
                                      + std::size_t(kMaxSubObjects) * 4u
                                      + kUnitGridCells * 4u,
              "UnitGrid is three flat columns over its caps and nothing hidden");

// Births the grid in its own heap block (defined in movement.cpp — a cap-sized
// named initializer in a header is paid by every includer, AGENTS §5 п.13).
std::unique_ptr<UnitGrid> make_unit_grid();

// `cell` is the intended query radius (queries then touch 2×2 cells). `maxDim`
// caps the grid resolution: if the units are spread so wide that the bbox needs
// more cells, the cell size grows instead of the allocation.
void build_unit_grid(UnitGrid& g, const BodyCrowd& u, float cell, int maxDim);

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
    std::uint64_t factionEnemyMask[kMaxFactions]{};       // OR of member masks
    // Planes are CLEARED only for factions actually standing in the window. A
    // two-army battle sweeps two planes — the per-frame clear was the whole
    // fixed cost of this pass, and a lone bandit must not pay a creatures
    // price. The MEMORY behind all 64 possible planes is fixed and paid once
    // at birth (ЗАКОН СТАБИЛЬНОСТИ: the profile of an empty field and a full
    // one is the same); only the SWEEP tracks the factions present.
    std::int16_t planeIdx[kMaxFactions]{};               // -1 = faction absent
    int planeCount = 0;
    std::array<std::uint64_t, kInfluenceGridCells> occMask; // [cell] factions here
    std::array<std::uint32_t, kInfluencePlaneCap> count;    // [plane*cells + cell]
    // ТРИ ОСИ, ОДНА ФОРМА (ЗАКОН ИЗОТРОПИИ + вердикт владельца 2026-10-12:
    // «могут быть армии со всеми: наземные, летающие… и уже есть в субмире
    // летающие мобы типа драконов, их можно отстрелять и они стреляют»).
    // Вертикаль нужна ГЕЙТУ ЗРЕНИЯ ниже: без неё драконья стая в 200 м НАД
    // строем будила его как из-под ног, а строй под утёсом будился от врага
    // на утёсе и бежал в скалу. ШАГ ПОДХОДА остаётся плоским — ноги по земле.
    std::array<float, kInfluencePlaneCap> sumX, sumY, sumZ;  // centroid accum
    // Nearest hostile concentration for a faction, propagated by the transform.
    std::array<float, kInfluencePlaneCap> siteX, siteY, siteZ;
    std::array<std::uint8_t, kInfluencePlaneCap> hasSite;
    // Alert chain: 1 => bodies of this faction in this cell know where the enemy
    // is, either by their own eyes or relayed through comrades.
    std::array<std::uint8_t, kInfluencePlaneCap> alert;
    // The alert flood's work queue, a member for the same reason every buffer
    // above it is: the per-frame pass allocates nothing. A cell enters the
    // queue at most once per faction (the alert flag gates the push), so the
    // cell cap bounds it by construction.
    std::array<std::uint32_t, kInfluenceGridCells> alertQueue;

    // ~17.3 MiB: heap-only, like BodyCrowd — born through make_influence_field().
    InfluenceField(const InfluenceField&) = delete;
    InfluenceField& operator=(const InfluenceField&) = delete;
    InfluenceField(InfluenceField&&) = delete;
    InfluenceField& operator=(InfluenceField&&) = delete;

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
        return f >= 0 && f < kMaxFactions && planeIdx[f] >= 0;
    }
    // Valid only when has_faction(f).
    inline std::size_t plane(int f) const noexcept {
        return std::size_t(planeIdx[f]) * cell_count();
    }

private:
    InfluenceField() = default;
    friend std::unique_ptr<InfluenceField> make_influence_field();
};
// Seven 4-byte columns + two byte columns over the plane cap, the occupancy
// mask and the flood queue over the cell cap, the faction headers, and three
// 4-byte pads to the u64 alignment. Nothing hidden.
static_assert(sizeof(InfluenceField) ==
                  20u                                       // cell, origin, cols, rows
                + 8u + 8u * std::size_t(kMaxFactions)       // presentMask + enemy masks
                + 2u * std::size_t(kMaxFactions) + 4u       // planeIdx + planeCount
                + 8u * kInfluenceGridCells                  // occMask
                + 4u * kInfluencePlaneCap * 7u              // count + 6 float columns
                + 1u * kInfluencePlaneCap * 2u              // hasSite + alert
                + 4u * kInfluenceGridCells                  // alertQueue
                + 12u,                                      // three 4-byte pads (u64 align)
              "InfluenceField is flat columns over its caps and nothing hidden");

// Births the field in its own heap block (defined in movement.cpp — AGENTS §5
// п.13: a cap-sized named initializer in a header is paid by every includer).
std::unique_ptr<InfluenceField> make_influence_field();

void build_influence_field(InfluenceField& f, const BodyCrowd& u,
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
struct MoveGround {
    float (*heightAt)(void* user, float x, float y) = nullptr;
    void* user = nullptr;
    const std::uint8_t* tiles = nullptr;   // tileDim² ground-type ids
    int tileDim = 0;
    const float* tileSpeed = nullptr;      // movement multiplier per tile id
    int tileSpeedCount = 0;
    // What the feet get to push and brake against, per tile id (map_data.h
    // kTileGroundGrip). Null = perfect footing everywhere, which is what a
    // headless fixture wants. Indexed by the SAME id `tileSpeed` is, and that
    // id is the SUPPORT's (standTile below): a bridge deck grips like the
    // stone it is, not like the river under it.
    const float* tileGrip = nullptr;
    float worldMax = 0.0f;                 // movement clamp bound (kFullSize)
    // Solid-structure gate (sub/collide.h, wired by the engine; optional like
    // everything here): may a body of radius r with feet at z occupy (x, y)?
    // Walls/houses stop being wadeable speed bumps and become real walls; the
    // steering pass slides along them (axis fallback) and NEVER traps a body
    // that is already inside a solid (it may always move — the escape rule).
    bool (*canStand)(void* user, float x, float y, float r, float z) = nullptr;
    void* solidUser = nullptr;
    // THE GROUND UNDER THE FEET (sub/collide.h walk_tile_at): given the tile
    // the terrain paints at (x, y), what is the body at feet-height `z`
    // ACTUALLY standing on? A bridge deck answers "road" while the river
    // runs below it. Null → the terrain tile stands, which is what a
    // headless fixture with no solids wants. Shares `solidUser`: it is the
    // same index answering both questions about the same solids.
    std::uint8_t (*standTile)(void* user, float x, float y, float r, float z,
                              std::uint8_t terrainTile) = nullptr;
};

// ── Tunables ───────────────────────────────────────────────────────────────
// Every knob of the fight lives here; nothing is hardcoded in the loop.
struct MoveParams {
    // ── Grids ──────────────────────────────────────────────────────────────
    // The fine cell is NOT a constant: it is derived from the crowd's own body
    // size (BodyCrowd::maxRadius), because that is what bounds occupancy. A
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
    // Stopping distance in BODY RADII: a body pulls up inside its own length.
    // Not a feel knob — the shape of the number is the physics (a = v²/2d), so
    // it holds at any pace, and a wide body needs more room than a small one.
    // Divided by the ground's grip, so slick footing lengthens the skid.
    float stopBodies    = 2.0f;
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

struct MoveStats {
    std::uint64_t neighborVisits = 0;   // O(N) proof: bounded by c·N
    std::uint32_t engaged = 0;          // units with a strikeable target
    std::uint32_t steered = 0;          // units the pass actually moved
    std::uint32_t advancing = 0;        // units alerted and closing on an enemy
};

// Grid cell sizes for this crowd, derived from its own body/weapon extremes.
float fine_cell_for(const BodyCrowd& u, const MoveParams& prm);
float pick_cell_for(const BodyCrowd& u, const MoveParams& prm);

// One pass over the SoA: pick contact, read the field, separate, apply terrain,
// accelerate, integrate. Writes x/y/vx/vy (except B_Pinned) plus target and
// inReach for the caller's damage pass. `fine` bounds body separation, `pick`
// bounds the contact search — two structures at two scales, as the geometry
// demands (a 1-unit separation query and a 25-unit weapon reach cannot share one
// cell size without one of them becoming quadratic or blind).
void steer_bodies(BodyCrowd& u, const UnitGrid& fine, const UnitGrid& pick,
                  const InfluenceField& field, const MoveGround& terrain,
                  const MoveParams& prm, float dt, MoveStats* stats);

} // namespace sm::sub

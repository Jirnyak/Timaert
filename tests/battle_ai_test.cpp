// Mass-battle AI contract — the ONE steering algorithm for every subworld
// fight (src/sub/battle.{h,cpp}).
//
// This test exists because every bug it pins was shipped and VISIBLE:
//   • battles lagged — a per-agent "nearest hostile within 200 units" scan
//     returns everyone in a 100-unit-wide melee, and each pair re-resolved
//     hostility through two std::map<std::string> lookups;
//   • two armies collapsed into a single point — everyone steered at the exact
//     centre of the same body, with no separation and two global attractors;
//   • parts of an army stood still and clumped, "особенно те кто стояли далеко"
//     — a flat maxApproachDist leash meant a rear rank could not see the enemy
//     and therefore never advanced.
//
// What is pinned here:
//   1. Factions are interned STRINGS with no vocabulary and no roster limit:
//      dedup, pointer-vs-strcmp identity, empty/null rejection, full-table
//      degradation. Hostility is asymmetric-capable (private grudges ride it).
//   2. UnitGrid is a correct counting sort; it fits the crowd's bbox; a crowd
//      spread wider than maxDim grows the CELL, not the allocation.
//   3. Grid cell sizes come from the crowd's own body/weapon data, so the same
//      code is correctly scaled for a peasant and for a dragon.
//   4. InfluenceField: presence + centroid per faction, and a nearest-hostile
//      site propagated across empty space — the whole long-range navigation
//      system, and the reason a lone bandit walks at the player with no player
//      special case anywhere.
//   5. THE ALERT CHAIN, three ways: a deep army's rear ranks charge though they
//      cannot see anything (the reported bug); awareness relays through comrades;
//      and a disconnected group does NOT swarm — if the wolf near you noticed
//      nothing, the wolf a kilometre away stays home.
//   6. NO COLLAPSE: 2×200 bodies charge, meet and stay unstacked — with a
//      NEGATIVE CONTROL that reproduces the old pile, so the assertion cannot
//      pass vacuously.
//   7. O(N) BOUND, measured not asserted: visits are counted by the pass, must
//      respect the per-unit ceilings, and must scale ~linearly when the army
//      doubles — including from a DEGENERATE all-on-one-spot start.
//   8. Terrain is a TABLE: a body crossing a slow ground type covers less than
//      one on fast ground, and uphill costs more than flat. This module knows
//      nothing about "water".
//   9. Single bandit vs a pinned player: identical code path; the pinned body
//      never moves.
//  10. Determinism: two identical runs are bitwise identical.
//
// Pure — links only battle.cpp. No Vulkan, no ECS, no window.
#include "sub/battle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

int fails = 0;
void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL battle_ai_test: %s\n", msg);
        ++fails;
    }
}

constexpr float kWorld = 3072.0f;
// Real shipping numbers: 1 world unit ≈ 1 metre, a humanoid is 0.55 wide, a
// guard's sword reaches 3 and it walks at 35 (macro/npc.h kGuardCombat).
constexpr float kBodyRadius = 0.55f;
constexpr float kReach = 3.0f;
constexpr float kSpeed = 35.0f;
constexpr float kSight = 200.0f;

// Faction ids are ordinary strings — exactly what the shipping gather interns.
const char* const kEmpire = "empire";
const char* const kBandits = "bandits";

BattleUnitDesc soldier(float x, float y, int faction, std::uint64_t enemyMask,
                       float sight = kSight) {
    BattleUnitDesc d{};
    d.x = x; d.y = y; d.z = 0.0f;
    d.radius = kBodyRadius;
    d.speed = kSpeed;
    d.reach = kReach;
    d.sight = sight;
    d.faction = std::int16_t(faction);
    d.enemyMask = enemyMask;
    return d;
}

constexpr std::uint64_t mask_of(int faction) {
    return 1ull << faction;
}

// One tick of the real shipping sequence: two grids at two scales, the field,
// then the steering pass.
void advance(BattleUnits& u, UnitGrid& fine, UnitGrid& pick, InfluenceField& f,
             const BattleTerrain& t, const BattleParams& prm, float dt,
             BattleStats* st) {
    build_unit_grid(fine, u, fine_cell_for(u, prm), 256);
    build_unit_grid(pick, u, pick_cell_for(u, prm), 256);
    build_influence_field(f, u, prm.influenceCell, kWorld);
    steer_battle(u, fine, pick, f, t, prm, dt, st);
}

struct RunResult {
    BattleUnits units;
    BattleStats lastStats;
    std::uint32_t peakEngaged = 0;
    std::uint32_t peakAdvancing = 0;
};

RunResult run_battle(int perSide, int ticks, const BattleParams& prm,
                     const BattleTerrain& terrain, float dt = 1.0f / 60.0f,
                     bool stackAll = false, float sight = kSight) {
    RunResult r{};
    r.units.reserve(perSide * 2);
    const float cx = kWorld * 0.5f, cy = kWorld * 0.5f;
    // Faction 0 = empire, 1 = bandits (the indices the engine would intern).
    for (int side = 0; side < 2; ++side) {
        const int me = side, foe = 1 - side;
        for (int i = 0; i < perSide; ++i) {
            float pos[2] = {cx, cy};
            if (!stackAll) deploy_army_slot(cx, cy, side, i, perSide, pos);
            r.units.add(soldier(pos[0], pos[1], me, mask_of(foe), sight));
        }
    }
    UnitGrid fine{}, pick{};
    InfluenceField field{};
    for (int t = 0; t < ticks; ++t) {
        advance(r.units, fine, pick, field, terrain, prm, dt, &r.lastStats);
        r.peakEngaged = std::max(r.peakEngaged, r.lastStats.engaged);
        r.peakAdvancing = std::max(r.peakAdvancing, r.lastStats.advancing);
    }
    return r;
}

// Mean distance to the nearest OTHER body. O(N²) — fine in a test, and it is
// exactly the number the "collapse into a point" bug destroys.
float mean_nearest_neighbour(const BattleUnits& u) {
    if (u.count < 2) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < u.count; ++i) {
        float best = 1.0e9f;
        for (int j = 0; j < u.count; ++j) {
            if (i == j) continue;
            const float dx = u.x[std::size_t(i)] - u.x[std::size_t(j)];
            const float dy = u.y[std::size_t(i)] - u.y[std::size_t(j)];
            best = std::min(best, std::sqrt(dx * dx + dy * dy));
        }
        acc += double(best);
    }
    return float(acc / double(u.count));
}

// Radius of gyration of one faction: the RMS distance of its bodies from their
// own centroid. This is the quantity a point attractor destroys, and the one the
// first version of this test failed to measure — a thousand bodies squeezed into
// a ball still pass a nearest-neighbour check, because the ball's internal
// spacing is exactly the separation distance. Extent is what must survive.
float spread_of(const BattleUnits& u, int faction) {
    double cx = 0, cy = 0;
    int n = 0;
    for (int i = 0; i < u.count; ++i) {
        if (u.faction[std::size_t(i)] != faction) continue;
        cx += u.x[std::size_t(i)]; cy += u.y[std::size_t(i)]; ++n;
    }
    if (n < 2) return 0.0f;
    cx /= n; cy /= n;
    double acc = 0.0;
    for (int i = 0; i < u.count; ++i) {
        if (u.faction[std::size_t(i)] != faction) continue;
        const double dx = u.x[std::size_t(i)] - cx;
        const double dy = u.y[std::size_t(i)] - cy;
        acc += dx * dx + dy * dy;
    }
    return float(std::sqrt(acc / double(n)));
}

float centroid_gap(const BattleUnits& u) {
    double ax = 0, ay = 0, bx = 0, by = 0;
    int an = 0, bn = 0;
    for (int i = 0; i < u.count; ++i) {
        if (u.faction[std::size_t(i)] == 0) {
            ax += u.x[std::size_t(i)]; ay += u.y[std::size_t(i)]; ++an;
        } else {
            bx += u.x[std::size_t(i)]; by += u.y[std::size_t(i)]; ++bn;
        }
    }
    if (an == 0 || bn == 0) return 0.0f;
    const double dx = ax / an - bx / bn, dy = ay / an - by / bn;
    return float(std::sqrt(dx * dx + dy * dy));
}

// ── Terrain fixtures ───────────────────────────────────────────────────────
float flat_height(void*, float, float) { return 100.0f; }
float ramp_height(void*, float x, float) { return x; }   // 45° climb in +x

BattleTerrain flat_terrain() {
    BattleTerrain t{};
    t.heightAt = &flat_height;
    t.worldMax = kWorld;
    return t;
}

// ── 1. Factions: interned strings, no vocabulary, no roster limit ──────────
void test_factions() {
    FactionSet fs{};
    const int a = fs.intern(kEmpire);
    const int b = fs.intern(kBandits);
    check(a == 0 && b == 1, "interning assigns dense indices in order");
    check(fs.intern(kEmpire) == a, "same pointer dedups");
    char copy[16];
    std::strcpy(copy, "empire");
    check(fs.intern(copy) == a, "same text dedups even from a different pointer");
    check(fs.count == 2, "no duplicate slots created");
    check(fs.intern(nullptr) < 0 && fs.intern("") < 0,
          "null / empty faction id is refused, not interned");

    // A faction the battle code has never heard of is just another string: this
    // is the whole point — new factions cost zero code.
    check(fs.intern("some_brand_new_kingdom") == 2, "unknown faction interns fine");

    // NOTE ON THE CONTRACT: FactionSet stores the POINTER, not a copy, so every
    // interned id must outlive the tick. The shipping caller only ever passes
    // string literals from the fighter tables, and this loop must therefore hand
    // it distinct, stable storage too — reusing one stack buffer would make every
    // id the same pointer and silently dedup to a single faction.
    FactionSet full{};
    std::vector<std::vector<char>> ids;
    ids.resize(std::size_t(kMaxBattleFactions));
    for (int i = 0; i < kMaxBattleFactions; ++i) {
        ids[std::size_t(i)].resize(16);
        std::snprintf(ids[std::size_t(i)].data(), 16, "f%d", i);
        full.intern(ids[std::size_t(i)].data());
    }
    check(full.count == kMaxBattleFactions, "table fills to its capacity");
    check(full.intern("one_too_many") < 0,
          "beyond capacity degrades to factionless, never overruns");

    FactionSet cleared{};
    cleared.intern(kEmpire);
    cleared.clear();
    check(cleared.count == 0 && cleared.find(kEmpire) < 0, "clear resets fully");

    // Hostility is per-unit and may be asymmetric: that is how a private grudge
    // (TempHostileToPlayer) rides along without a branch in the hot loop.
    BattleUnits u{};
    u.add(soldier(0, 0, 2, mask_of(3)));    // wildlife with a grudge on the player
    u.add(soldier(0, 0, 3, 0));             // player side, no standing quarrel
    check(u.hostile(0, 1), "grudge-bearing beast sees the player as an enemy");
    check(!u.hostile(1, 0), "hostility is not implicitly reciprocal");
    // A factionless body (-1) fights nobody and is nobody's enemy.
    BattleUnits none{};
    none.add(soldier(0, 0, -1, ~0ull));
    none.add(soldier(0, 0, 0, ~0ull));
    check(!none.hostile(1, 0), "a factionless body is never a valid enemy");
}

// ── 1b. Mask freshness — the staleness bug, pinned ─────────────────────────
// Symptom in-game: two armies stood facing each other doing nothing, yet either
// side would strike the player the moment he touched them. Cause: the gather
// re-interns the faction set every tick (which clears the masks), while the
// matrix rebuild was amortised behind a 0.5 s timer — so for 29 ticks in 30 every
// enemyMask was zero. No enemies meant nothing to advance on; the strike still
// landed because the 1 s attack cooldown outlasted the stale window. Empty masks
// are silent: nothing crashes, nothing warns, the army just stops fighting.
int fake_relation(void* user, const char* a, const char* b) {
    // "empire" and "bandits" are at war; anything else is indifferent.
    (void)user;
    const bool ab = (std::strcmp(a, "empire") == 0 && std::strcmp(b, "bandits") == 0);
    const bool ba = (std::strcmp(a, "bandits") == 0 && std::strcmp(b, "empire") == 0);
    return (ab || ba) ? -80 : 0;
}

void test_faction_mask_freshness() {
    FactionSet fs{};
    const int emp = fs.intern(kEmpire);
    const int ban = fs.intern(kBandits);

    // Interning ALONE leaves every mask empty. This is the trap: a caller that
    // interns and forgets to rebuild gets a silent ceasefire.
    check(fs.enemyMask[emp] == 0ull && fs.enemyMask[ban] == 0ull,
          "a freshly interned set has no hostility yet (the trap)");

    build_faction_masks(fs, &fake_relation, nullptr, -50);
    check((fs.enemyMask[emp] >> ban) & 1ull, "war resolves into the mask");
    check((fs.enemyMask[ban] >> emp) & 1ull, "and it resolves both ways");
    check(((fs.enemyMask[emp] >> emp) & 1ull) == 0ull,
          "a faction is never hostile to itself");

    // The lifecycle the engine actually performs, twice: clear -> intern ->
    // rebuild. After every iteration the masks must be live, never empty.
    for (int tick = 0; tick < 3; ++tick) {
        fs.clear();
        const int e2 = fs.intern(kEmpire);
        const int b2 = fs.intern(kBandits);
        check(fs.enemyMask[e2] == 0ull, "clear() wipes the masks (documented)");
        build_faction_masks(fs, &fake_relation, nullptr, -50);
        check(((fs.enemyMask[e2] >> b2) & 1ull) != 0ull,
              "masks are live again after every clear+intern+rebuild cycle");
    }

    // A null relation function must not fabricate hostility.
    fs.clear();
    fs.intern(kEmpire);
    build_faction_masks(fs, nullptr, nullptr, -50);
    check(fs.enemyMask[0] == 0ull, "no relation source => no hostility invented");
}

// ── 2–3. Grid correctness and data-derived cell sizes ──────────────────────
void test_grid() {
    BattleUnits u{};
    for (int i = 0; i < 97; ++i) {
        u.add(soldier(1000.0f + float(i % 10) * 2.0f,
                      1000.0f + float(i / 10) * 2.0f, 0, 0));
    }
    const BattleParams prm{};
    UnitGrid g{};
    build_unit_grid(g, u, fine_cell_for(u, prm), 256);

    check(g.items.size() == std::size_t(u.count), "grid holds every unit once");
    std::vector<int> seen(std::size_t(u.count), 0);
    bool cellOk = true;
    for (int cy = 0; cy < g.rows; ++cy) {
        for (int cx = 0; cx < g.cols; ++cx) {
            const std::size_t ci = std::size_t(cy) * std::size_t(g.cols)
                                 + std::size_t(cx);
            for (std::uint32_t k = g.begin[ci]; k < g.begin[ci + 1u]; ++k) {
                const int j = g.items[k];
                ++seen[std::size_t(j)];
                if (g.col_of(u.x[std::size_t(j)]) != cx
                    || g.row_of(u.y[std::size_t(j)]) != cy) cellOk = false;
            }
        }
    }
    check(cellOk, "every unit sits in its own bucket");
    check(std::count(seen.begin(), seen.end(), 1) == u.count,
          "no unit duplicated or dropped");

    // Cell sizes are DATA-derived: bodies set the fine cell, weapons the pick
    // cell. A dragon-sized crowd must get proportionally larger cells with no
    // code change — that is the whole "радиус из контекста" requirement.
    const float fineSmall = fine_cell_for(u, prm);
    BattleUnits big{};
    BattleUnitDesc dragon = soldier(1000.0f, 1000.0f, 0, 0);
    dragon.radius = 8.0f;
    dragon.reach = 20.0f;
    big.add(dragon);
    check(fine_cell_for(big, prm) > fineSmall * 3.0f,
          "a wider body grows the separation grid");
    check(pick_cell_for(big, prm) > pick_cell_for(u, prm),
          "a longer weapon grows the contact grid");
    check(pick_cell_for(u, prm) >= fine_cell_for(u, prm),
          "contact grid is never finer than the separation grid");

    BattleUnits wide{};
    wide.add(soldier(1.0f, 1.0f, 0, 0));
    wide.add(soldier(3000.0f, 3000.0f, 0, 0));
    UnitGrid gw{};
    build_unit_grid(gw, wide, 4.0f, 16);
    check(gw.cols <= 16 && gw.rows <= 16, "maxDim respected");
    check(gw.cell > 4.0f, "cell grew instead of the allocation");

    UnitGrid ge{};
    BattleUnits empty{};
    build_unit_grid(ge, empty, 4.0f, 256);
    check(ge.begin.size() == 2u && ge.items.empty(), "empty crowd degrades safely");
}

// ── 4. Influence field ─────────────────────────────────────────────────────
void test_influence_field() {
    BattleUnits u{};
    u.add(soldier(200.0f, 1000.0f, 0, mask_of(1)));
    for (int i = 0; i < 5; ++i)
        u.add(soldier(1000.0f + float(i) * 2.0f, 1000.0f, 1, mask_of(0)));

    InfluenceField f{};
    build_influence_field(f, u, 32.0f, kWorld);
    check(f.has_faction(0) && f.has_faction(1), "both factions get a plane");
    check(f.planeCount == 2, "only present factions are allocated");

    // The lone body must find a site pointing at the enemy mass 800 units away,
    // with nothing but empty cells between. No neighbour query is involved.
    const std::size_t fi = f.plane(0)
        + std::size_t(f.row_of(1000.0f)) * std::size_t(f.cols)
        + std::size_t(f.col_of(200.0f));
    check(f.hasSite[fi] != 0u, "site propagated across empty space");
    check(f.siteX[fi] > 950.0f && f.siteX[fi] < 1050.0f, "site is the enemy mass");

    // A faction with no enemies anywhere gets no site: nothing to charge.
    BattleUnits lonely{};
    lonely.add(soldier(500.0f, 500.0f, 0, 0));
    InfluenceField f2{};
    build_influence_field(f2, lonely, 32.0f, kWorld);
    const std::size_t li = f2.plane(0)
        + std::size_t(f2.row_of(500.0f)) * std::size_t(f2.cols)
        + std::size_t(f2.col_of(500.0f));
    check(f2.hasSite[li] == 0u, "no enemies => no site");
}

// ── 5. The alert chain: the reported "far ranks just clump" bug ────────────
void test_alert_chain() {
    const BattleParams prm{};
    const BattleTerrain flat = flat_terrain();

    // A DEEP army: 2000 per side, deployed as a block ~45 ranks deep. The rear
    // ranks are ~430 units from the enemy front — far beyond their own 200-unit
    // sight. Before the alert chain they stood still and piled up; they must now
    // charge because their front ranks saw the enemy.
    const int perSide = 2000;
    BattleUnits u{};
    u.reserve(perSide * 2);
    const float cx = kWorld * 0.5f, cy = kWorld * 0.5f;
    int rearmost = -1;
    float rearmostX = cx;
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < perSide; ++i) {
            float pos[2];
            deploy_army_slot(cx, cy, side, i, perSide, pos);
            const int idx = u.add(soldier(pos[0], pos[1], side,
                                          mask_of(1 - side)));
            if (side == 0 && pos[0] < rearmostX) { rearmostX = pos[0]; rearmost = idx; }
        }
    }
    // Sanity: the body we picked really is out of its own sight range.
    check(rearmost >= 0 && (cx - rearmostX) > kSight * 0.9f,
          "test setup: rearmost body is beyond its own sight");

    UnitGrid fine{}, pick{};
    InfluenceField f{};
    BattleStats st{};
    const float startX = u.x[std::size_t(rearmost)];
    for (int t = 0; t < 240; ++t) {
        advance(u, fine, pick, f, flat, prm, 1.0f / 60.0f, &st);
    }
    const float moved = u.x[std::size_t(rearmost)] - startX;
    std::fprintf(stderr, "[battle_ai] rear rank at %.0f from centre advanced %.1f, "
                         "advancing=%u/%d\n",
                 cx - rearmostX, moved, st.advancing, u.count);
    check(moved > 20.0f, "a rear rank that cannot see the enemy still charges");
    check(st.advancing > std::uint32_t(u.count / 4),
          "most of a deep army is advancing, not standing in a clump");

    // NEGATIVE CONTROL for the gate itself: blind everyone (sight 1) and nothing
    // seeds the chain, so the army must NOT advance. If it moved anyway, the
    // assertion above would be proving nothing about the alert chain.
    RunResult blind = run_battle(400, 120, prm, flat, 1.0f / 60.0f, false, 1.0f);
    check(blind.peakAdvancing == 0u, "with nobody able to see, nobody advances");
    check(centroid_gap(blind.units) > 100.0f, "a blind army holds its ground");

    // DISCONNECTED GROUP: exactly the wolves case. One wolf close enough to see
    // the player charges; a second wolf far away, with no comrade between, must
    // stay home — awareness relays through a formation, not by telepathy.
    BattleUnits wolves{};
    wolves.add(soldier(1000.0f, 1000.0f, 0, 0));           // the quarry
    wolves.flags[0] |= BU_Pinned;
    wolves.add(soldier(1080.0f, 1000.0f, 1, mask_of(0)));  // near wolf: sees it
    wolves.add(soldier(2200.0f, 1000.0f, 1, mask_of(0)));  // far wolf: blind, alone
    const float nearStart = wolves.x[1], farStart = wolves.x[2];
    for (int t = 0; t < 300; ++t) {
        advance(wolves, fine, pick, f, flat, prm, 1.0f / 60.0f, &st);
    }
    check(std::fabs(wolves.x[1] - nearStart) > 20.0f, "the near wolf charges");
    check(std::fabs(wolves.x[2] - farStart) < 1.0f,
          "an unconnected far wolf does not swarm");

    // RELAY: put a chain of comrades between the far body and the fighting. Now
    // the far body advances although it never saw anything itself.
    BattleUnits chain{};
    chain.add(soldier(1000.0f, 1000.0f, 0, 0));
    chain.flags[0] |= BU_Pinned;
    chain.add(soldier(1080.0f, 1000.0f, 1, mask_of(0)));
    for (float x = 1100.0f; x <= 2200.0f; x += 25.0f)   // link spacing < field cell
        chain.add(soldier(x, 1000.0f, 1, mask_of(0), 1.0f));   // all blind
    const int last = chain.count - 1;
    const float linkStart = chain.x[std::size_t(last)];
    for (int t = 0; t < 300; ++t) {
        advance(chain, fine, pick, f, flat, prm, 1.0f / 60.0f, &st);
    }
    check(chain.x[std::size_t(last)] < linkStart - 20.0f,
          "a blind body advances when comrades relay the alert to it");
}

// ── 6. No collapse, with a negative control ────────────────────────────────
void test_no_collapse_and_convergence() {
    const BattleParams prm{};
    const BattleTerrain flat = flat_terrain();
    const int perSide = 200;

    // Measure the deployed extent BEFORE the fight, then again after: a formation
    // that squeezes itself into a ball is the bug the screenshot showed.
    RunResult start = run_battle(perSide, 1, prm, flat);
    const float spread0 = spread_of(start.units, 0);

    RunResult r = run_battle(perSide, 1200, prm, flat);
    const float mnn = mean_nearest_neighbour(r.units);
    const float spread1 = spread_of(r.units, 0);
    std::fprintf(stderr,
                 "[battle_ai] mnn=%.2f gap=%.1f engaged=%u spread %.1f -> %.1f\n",
                 mnn, centroid_gap(r.units), r.peakEngaged, spread0, spread1);
    // Bodies of radius 0.55 must not interpenetrate.
    check(mnn > kBodyRadius, "bodies stay apart (no collapse into a point)");
    // ...and the ARMY must not implode. This is the assertion the first version of
    // this test lacked, which let a point-attractor bug ship: bodies were 1.44
    // apart (pass) while the whole thousand-strong deployment had balled up into a
    // few metres (the reported "сгрудились в кучки").
    check(spread1 > spread0 * 0.6f,
          "the formation keeps its extent (no implosion into clumps)");
    check(centroid_gap(r.units) < 200.0f, "armies converged");
    check(r.peakEngaged > std::uint32_t(perSide / 4),
          "a real front line formed (bodies reached strike contact)");

    // Kill separation and the engagement ring — i.e. steer every body at the
    // exact centre of its nearest enemy, which is what the shipped code did —
    // and the pile must come back.
    BattleParams naive = prm;
    naive.sepWeight = 0.0f;
    naive.maxSepNeighbors = 0;
    naive.ringFactor = 0.0f;
    naive.arriveEpsilon = 0.0f;
    RunResult bad = run_battle(perSide, 1200, naive, flat);
    check(mean_nearest_neighbour(bad.units) < mnn * 0.5f,
          "negative control reproduces the collapse (control is meaningful)");
}

// ── 6b. The reported scenario, at the reported scale ───────────────────────
// `test_battle 1000` in-game showed almost every body squeezed into a handful of
// tight clumps that then sat there. This reproduces that exact deployment and
// asserts the formation survives its approach, with a negative control that
// brings the bug back — no test-only knob: shrinking the field cell to ~a body
// width degenerates the flow read into per-body homing on the enemy centroid,
// which IS the defect.
void test_thousand_per_side_keeps_formation() {
    const BattleTerrain flat = flat_terrain();
    const int perSide = 1000;
    // The approach phase is what the flow read governs, so it must be LONG
    // enough to matter: deploy the blocks 600 units apart and give the fighters
    // eyes to match (both are ordinary data — a table column in the shipping
    // build). At the default 120-unit staging the armies contact almost at once
    // and the approach regime barely runs, which is why the first attempt at this
    // test could not tell flow from point-homing at all.
    auto deploy = [&](BattleUnits& u, float gap) {
        const float cx = kWorld * 0.5f, cy = kWorld * 0.5f;
        const int cols = int(std::sqrt(float(perSide)) + 0.5f);
        for (int side = 0; side < 2; ++side) {
            const float facing = side == 0 ? -1.0f : 1.0f;
            for (int i = 0; i < perSide; ++i) {
                const int col = i % cols, row = i / cols;
                const float x = cx + facing * (gap * 0.5f + float(row) * 3.0f);
                const float y = cy + (float(col) - float(cols - 1) * 0.5f) * 3.0f;
                u.add(soldier(x, y, side, mask_of(1 - side), 700.0f));
            }
        }
    };
    auto approach = [&](const BattleParams& prm, int ticks, float* outSpread0) {
        BattleUnits u{};
        u.reserve(perSide * 2);
        deploy(u, 600.0f);
        if (outSpread0) *outSpread0 = spread_of(u, 0);
        UnitGrid fine{}, pick{};
        InfluenceField f{};
        BattleStats st{};
        for (int t = 0; t < ticks; ++t)
            advance(u, fine, pick, f, flat, prm, 1.0f / 60.0f, &st);
        return spread_of(u, 0);
    };

    const BattleParams prm{};
    float spread0 = 0.0f;
    const float flow = approach(prm, 420, &spread0);      // ~7 s of closing
    // HYPOTHESIS TESTED AND REJECTED, recorded so nobody re-derives it: the first
    // diagnosis of the in-game clumping blamed the influence site for being a
    // POINT attractor (every body steering at one enemy centroid). Degenerating
    // the flow read back into per-body homing — influenceCell ≈ a body width —
    // measurably does NOT implode a formation: over a 600-unit approach the
    // bearing to a distant point is near-identical for every body in a 100-unit
    // block, so convergence is negligible (38.8 -> 38.8). The real cause was the
    // terrain slope term, pinned in test_terrain_does_not_outvote_the_advance.
    // The cell-quantised flow read is kept because it is the principled way to
    // read a field and costs nothing measurable, NOT because it fixed this bug.
    BattleParams pointy = prm;
    pointy.influenceCell = 1.0f;
    const float point = approach(pointy, 420, nullptr);

    std::fprintf(stderr,
                 "[battle_ai] 1000/side approach spread %.1f -> %.1f (flow) "
                 "vs %.1f (per-body homing)\n", spread0, flow, point);
    check(flow > spread0 * 0.7f,
          "1000-per-side formation survives a long approach");
    check(point > spread0 * 0.7f,
          "the rejected hypothesis stays rejected: homing alone does not implode");
}

// ── 6c. Terrain must not outvote the order ─────────────────────────────────
// The height field spans 1500 m over a 3072-unit window, so ordinary rolling
// terrain reads grade > 1. A slope term that pushes on any non-zero grade — and
// worse, one folded into the separation accumulator so sepWeight scales it —
// beats the advance everywhere: bodies slide downhill forever, formations tear
// into terrain-pooled clusters and the battle never joins. That shipped and was
// visible in-game. Pinned here on relief of the same magnitude as the real
// generator's. Two further defects of the same family are pinned with it: a body
// must still CROSS a steep hill to reach its enemy, and an IDLE body must not
// slide downhill at all (victorious guards were observed pooling in a pit once
// their enemies were dead — terrain steers movement, it is not a force on rest).
float rolling_hills(void*, float x, float y) {
    return 600.0f + 120.0f * std::sin(x / 200.0f) * std::sin(y / 200.0f);
}

float mean_speed(const BattleUnits& u) {
    if (u.count <= 0) return 0.0f;
    double acc = 0.0;
    for (int i = 0; i < u.count; ++i) {
        acc += std::sqrt(double(u.vx[std::size_t(i)]) * u.vx[std::size_t(i)]
                       + double(u.vy[std::size_t(i)]) * u.vy[std::size_t(i)]);
    }
    return float(acc / double(u.count));
}

void test_terrain_does_not_outvote_the_advance() {
    BattleTerrain hills{};
    hills.heightAt = &rolling_hills;
    hills.worldMax = kWorld;

    const BattleParams prm{};
    const int perSide = 500;
    const float spread0 = spread_of(run_battle(perSide, 1, prm, hills).units, 0);
    RunResult r = run_battle(perSide, 600, prm, hills);
    const float spread1 = spread_of(r.units, 0);
    const float v1 = mean_speed(r.units);

    std::fprintf(stderr,
                 "[battle_ai] hills spread %.1f -> %.1f  |v|=%.1f engaged=%u\n",
                 spread0, spread1, v1, r.lastStats.engaged);

    check(spread1 < spread0 * 2.0f, "hills do not tear the formation apart");
    check(r.lastStats.engaged > std::uint32_t(perSide / 2),
          "the battle still joins on broken ground");
    check(v1 < kSpeed * 0.25f,
          "bodies settle into the melee instead of sliding forever");

    // AUTHORITY PROBE. One body ordered straight across a steep hill towards a
    // distant pinned enemy. With the shipping numbers terrain bends its path a
    // little; with the old saturating authority (push on any grade, weight above
    // the advance) it is deflected off course entirely. This is the mechanism that
    // dissolved armies, measured in isolation instead of by proxy.
    auto crossing_error = [&](const BattleParams& p) {
        BattleUnits u{};
        u.add(soldier(kWorld * 0.5f - 150.0f, kWorld * 0.5f, 0, mask_of(1), 400.0f));
        u.add(soldier(kWorld * 0.5f + 150.0f, kWorld * 0.5f, 1, 0, 400.0f));
        u.flags[1] |= BU_Pinned;
        UnitGrid fine{}, pick{};
        InfluenceField f{};
        for (int t = 0; t < 900; ++t)
            advance(u, fine, pick, f, hills, p, 1.0f / 60.0f, nullptr);
        const float dx = u.x[0] - u.x[1], dy = u.y[0] - u.y[1];
        return std::sqrt(dx * dx + dy * dy);      // how far short it stopped
    };
    // NO PARAMETER CONTROL IS POSSIBLE ANY MORE, and that is the point: the
    // runaway needed the slope push to be AMPLIFIED by sepWeight (1.2 x 1.8 =
    // 2.16 against an advance of 1.0, enough to reverse it). Terrain now owns a
    // separate accumulator, so no setting of slopeAvoid can outvote the advance —
    // measured: even slopeAvoid 1.2 with no walkable threshold still lands the
    // body on its enemy (2.4 units short vs 2.9 with the shipping numbers). The
    // defect is structurally gone, so what is asserted is the OUTCOME.
    const float shipErr = crossing_error(prm);
    std::fprintf(stderr, "[battle_ai] hill crossing miss: %.1f\n", shipErr);
    check(shipErr < kReach + 2.0f * kBodyRadius + 2.0f,
          "a body crosses a steep hill and reaches its enemy");

    // AFTER the battle: bodies with no enemy left must STAND. Terrain steers a
    // body that is going somewhere; it is not a force acting on one at rest.
    // Ungated, victorious troops slide downhill and collect in the nearest
    // depression — reported in-game as guards gathering in a pit once the bandits
    // were dead.
    BattleUnits idle{};
    for (int i = 0; i < 60; ++i) {
        // On a slope (sin' is steepest near x/200 = 0), no enemies at all.
        idle.add(soldier(kWorld * 0.5f + float(i % 10) * 3.0f,
                         kWorld * 0.5f + float(i / 10) * 3.0f, 0, 0));
    }
    UnitGrid fine{}, pick{};
    InfluenceField f{};
    const float ix0 = idle.x[0], iy0 = idle.y[0];
    for (int t = 0; t < 600; ++t)
        advance(idle, fine, pick, f, hills, prm, 1.0f / 60.0f, nullptr);
    const float drift = std::sqrt((idle.x[0] - ix0) * (idle.x[0] - ix0)
                                + (idle.y[0] - iy0) * (idle.y[0] - iy0));
    std::fprintf(stderr, "[battle_ai] idle-on-slope drift after 10 s: %.2f\n", drift);
    check(drift < 2.0f, "idle bodies do not slide downhill into a pit");
    check(mean_speed(idle) < 0.5f, "idle bodies come to rest");
}

// ── 7. The O(N) bound, measured ────────────────────────────────────────────
void test_linear_scaling() {
    const BattleParams prm{};
    const BattleTerrain flat = flat_terrain();

    RunResult a = run_battle(200, 600, prm, flat);
    RunResult b = run_battle(400, 600, prm, flat);

    const std::uint64_t perUnitA = a.lastStats.neighborVisits / 400u;
    const std::uint64_t perUnitB = b.lastStats.neighborVisits / 800u;
    const std::uint64_t ceiling =
        std::uint64_t(prm.maxPickVisits + prm.maxSepVisits) + 8u;
    check(perUnitA <= ceiling, "visits per unit respect the ceiling (N=400)");
    check(perUnitB <= ceiling, "visits per unit respect the ceiling (N=800)");

    const double ratio = double(b.lastStats.neighborVisits)
                       / double(std::max<std::uint64_t>(1u,
                                a.lastStats.neighborVisits));
    check(ratio < 2.6, "work scales linearly with army size");
    std::fprintf(stderr,
                 "[battle_ai] visits/unit N=400:%llu N=800:%llu ratio=%.2f "
                 "ceiling=%llu\n",
                 (unsigned long long)perUnitA, (unsigned long long)perUnitB,
                 ratio, (unsigned long long)ceiling);

    // DEGENERATE start: both armies spawned on one spot. Without the visit
    // ceilings the first frames would be quadratic; with them the bound holds
    // even here, and separation still unpacks the pile.
    RunResult stacked = run_battle(300, 1, prm, flat, 1.0f / 60.0f, true);
    check(stacked.lastStats.neighborVisits / 600u <= ceiling,
          "degenerate all-stacked start still respects the ceiling");
    RunResult unpacked = run_battle(300, 600, prm, flat, 1.0f / 60.0f, true);
    check(mean_nearest_neighbour(unpacked.units) > kBodyRadius * 0.5f,
          "separation unpacks a degenerate stack");
}

// ── 8. Terrain is a data table ─────────────────────────────────────────────
void test_terrain_table() {
    const BattleParams prm{};
    constexpr int kDim = 256;
    // Two ground types, id 0 fast and id 1 slow — the module never learns which
    // one is "water"; sub/map_data.h owns that meaning.
    const float speeds[2] = {1.0f, 0.25f};
    std::vector<std::uint8_t> ground(std::size_t(kDim) * std::size_t(kDim), 0u);
    std::vector<std::uint8_t> slow(std::size_t(kDim) * std::size_t(kDim), 1u);

    auto chase = [&](const std::uint8_t* tiles,
                     float (*height)(void*, float, float)) {
        BattleUnits u{};
        u.add(soldier(20.0f, 128.0f, 0, mask_of(1)));
        u.add(soldier(200.0f, 128.0f, 1, 0));
        u.flags[1] |= BU_Pinned;                 // the quarry stands still
        BattleTerrain t{};
        t.heightAt = height;
        t.tiles = tiles;
        t.tileDim = tiles ? kDim : 0;
        t.tileSpeed = speeds;
        t.tileSpeedCount = 2;
        t.worldMax = float(kDim);
        UnitGrid fine{}, pick{};
        InfluenceField f{};
        const float x0 = u.x[0];
        for (int i = 0; i < 180; ++i)
            advance(u, fine, pick, f, t, prm, 1.0f / 60.0f, nullptr);
        return u.x[0] - x0;
    };

    const float fast = chase(ground.data(), &flat_height);
    const float slowed = chase(slow.data(), &flat_height);
    const float uphill = chase(ground.data(), &ramp_height);
    check(fast > 0.0f, "the chase advances on fast ground");
    check(slowed < fast * 0.5f, "a slow ground row slows the advance");
    check(uphill < fast * 0.95f, "uphill slows and deflects the advance");
    // No tile grid at all must degrade to "no ground effect", not to a crash.
    check(chase(nullptr, &flat_height) > 0.0f, "absent ground grid is harmless");
}

// ── 9. One bandit, one player: same code, no special case ──────────────────
void test_single_bandit() {
    const BattleParams prm{};
    const BattleTerrain flat = flat_terrain();

    BattleUnits u{};
    u.add(soldier(1000.0f, 1000.0f, 0, 0));      // player body: pinned, no enemies
    u.flags[0] |= BU_Pinned;
    u.add(soldier(1150.0f, 1000.0f, 1, mask_of(0)));

    UnitGrid fine{}, pick{};
    InfluenceField f{};
    BattleStats st{};
    const float px0 = u.x[0], py0 = u.y[0];
    for (int i = 0; i < 900; ++i)
        advance(u, fine, pick, f, flat, prm, 1.0f / 60.0f, &st);

    check(u.x[0] == px0 && u.y[0] == py0, "pinned player body never moved");
    const float dx = u.x[1] - u.x[0], dy = u.y[1] - u.y[0];
    const float d = std::sqrt(dx * dx + dy * dy);
    check(d < kReach + 2.0f * kBodyRadius + 1.0f,
          "lone bandit closed to contact through the same path");
    check(st.engaged == 1u && u.inReach[1] != 0u && u.target[1] == 0,
          "lone bandit is strikeable on the player");
}

// ── 10. Determinism + the universal capacity ceiling ───────────────────────
void test_determinism_and_capacity() {
    const BattleParams prm{};
    const BattleTerrain flat = flat_terrain();
    RunResult a = run_battle(120, 400, prm, flat);
    RunResult b = run_battle(120, 400, prm, flat);
    bool same = a.units.count == b.units.count;
    for (int i = 0; same && i < a.units.count; ++i) {
        same = a.units.x[std::size_t(i)] == b.units.x[std::size_t(i)]
            && a.units.y[std::size_t(i)] == b.units.y[std::size_t(i)]
            && a.units.vx[std::size_t(i)] == b.units.vx[std::size_t(i)]
            && a.units.vy[std::size_t(i)] == b.units.vy[std::size_t(i)];
    }
    check(same, "two identical runs are bitwise identical");
    check(a.lastStats.neighborVisits == b.lastStats.neighborVisits,
          "visit counts match exactly");

    BattleUnits u{};
    u.reserve(kMaxBattleUnits);
    int refused = 0;
    for (int i = 0; i < kMaxBattleUnits + 16; ++i) {
        if (u.add(soldier(float(i % 3000) + 1.0f, 1.0f, 0, 0)) < 0) ++refused;
    }
    check(u.count == kMaxBattleUnits, "capacity is exactly 2^14");
    check(refused == 16, "overflow refused, never grown");
}

} // namespace

int main() {
    test_factions();
    test_faction_mask_freshness();
    test_grid();
    test_influence_field();
    test_alert_chain();
    test_no_collapse_and_convergence();
    test_thousand_per_side_keeps_formation();
    test_terrain_does_not_outvote_the_advance();
    test_linear_scaling();
    test_terrain_table();
    test_single_bandit();
    test_determinism_and_capacity();

    if (fails == 0) std::fprintf(stderr, "battle_ai_test: OK\n");
    return fails == 0 ? 0 : 1;
}

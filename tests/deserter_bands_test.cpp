// The deserter pool's conservation law, both directions.
//
// A beaten army's survivors fall into GameState::deserterPool
// (macro/squad.h drain_dead_leader_squads) and walk back out of it as bands
// (macro/npc_spawn.h raise_deserter_bands). Before the outflow existed the pool
// was a grave: men fell in and nothing ever came out, so "nothing is lost" was
// a claim no code made true.
//
// What is asserted here is the LAW, not the numbers: every man who leaves the
// pool stands on the map, nobody is minted, nobody evaporates, and the pile
// empties in finite time. The size of a day's exodus (√pool) is checked only as
// a SHAPE — it must shrink with the pool and never exceed it — because the day
// a better law replaces it, this test should still be right.
#include "check.h"

#include "ecs/world.h"
#include "macro/npc_spawn.h"
#include "macro/squad.h"
#include "macro/state.h"

#include <cmath>
#include <vector>

namespace {

// A small all-land world: the site draw must always find ground, so no case
// below can pass or fail for want of a coastline.
sm::TerrainData all_land(int w, int h) {
    sm::TerrainData t{};
    t.width = w;
    t.height = h;
    t.rgba.assign(std::size_t(w) * std::size_t(h) * 4u, std::uint8_t(255));
    return t;
}

sm::GameState make_world(int men) {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 12345u;
    for (int i = 0; i < men; ++i) {
        gs.deserterPool.members.push_back(sm::make_soldier(
            std::uint8_t(sm::NPCType::Peasant),
            /*level*/1 + (i % 4), std::uint32_t(i + 1)));
    }
    return gs;
}

// Every man the world holds on the map, in squads: leaders plus their rosters.
int men_on_map(sm::ecs::World& w) {
    int n = 0;
    for (auto [e, roster] : w.reg.view<sm::ecs::SquadRoster>().each()) {
        (void)e;
        n += 1 + int(roster.members.size());
    }
    return n;
}

int bands_on_map(sm::ecs::World& w) {
    int n = 0;
    for (auto e : w.reg.view<sm::ecs::SquadRoster>()) { (void)e; ++n; }
    return n;
}

} // namespace

int main() {
    using namespace sm;

    // ── 1. Conservation: what leaves the pool stands on the map ──────────
    {
        const int kMen = 100;
        GameState gs = make_world(kMen);
        const TerrainData terrain = all_land(gs.mapW, gs.mapH);
        ecs::World w{};

        const int left = raise_deserter_bands(gs, w, terrain, /*day*/1);
        CHECK(left > 0, "a non-empty pool must raise a band");
        CHECK(int(gs.deserterPool.members.size()) == kMen - left,
                        "the pool shrinks by exactly the men who left");
        CHECK(men_on_map(w) == left,
                        "every man who left the pool stands on the map");
        CHECK(bands_on_map(w) == 1,
                        "one day raises one band");
    }

    // ── 2. The exodus shrinks with the pile, and never outruns it ────────
    // Shape, not value: a bigger rout throws off bigger bands, and no draw may
    // ever take more men than the pool holds (the pool is the only bound).
    {
        const TerrainData terrain = all_land(64, 64);
        int prev = 0;
        for (const int men : {4, 100, 10000}) {
            GameState gs = make_world(men);
            ecs::World w{};
            const int left = raise_deserter_bands(gs, w, terrain, /*day*/7);
            CHECK(left >= 1 && left <= men,
                            "a day's exodus is bounded by the pool itself");
            CHECK(left >= prev,
                            "a bigger pile throws off a band at least as big");
            prev = left;
        }
    }

    // ── 3. A pool empties in finite time, and then the world is quiet ────
    // The negative control for a runaway: once the pile is gone, no further day
    // may invent a band out of nothing.
    {
        GameState gs = make_world(64);
        const TerrainData terrain = all_land(gs.mapW, gs.mapH);
        ecs::World w{};

        int day = 1;
        int raisedTotal = 0;
        while (!gs.deserterPool.members.empty() && day < 200) {
            raisedTotal += raise_deserter_bands(gs, w, terrain, day);
            ++day;
        }
        CHECK(gs.deserterPool.members.empty(),
                        "the pool drains in finite time");
        CHECK(raisedTotal == 64,
                        "exactly the men who fell in walked out — none minted");
        CHECK(men_on_map(w) == 64,
                        "and all of them are standing on the map");

        const int bandsBefore = bands_on_map(w);
        for (int d = day; d < day + 10; ++d) {
            CHECK(raise_deserter_bands(gs, w, terrain, d) == 0,
                            "an empty pool raises nobody");
        }
        CHECK(bands_on_map(w) == bandsBefore,
                        "and no band appears out of an empty pool");
    }

    // ── 4. The full circle: rout → pool → band ───────────────────────────
    // The two halves must agree about the same men. A squad whose leader died
    // pays its survivors into the pool, and the pool hands them back to the
    // world; nobody is duplicated on the way.
    {
        GameState gs = make_world(0);
        const TerrainData terrain = all_land(gs.mapW, gs.mapH);
        ecs::World w{};

        auto dead = w.reg.create();
        ecs::SquadRoster roster{};
        for (int i = 0; i < 9; ++i) {
            roster.members.push_back(make_soldier(
                std::uint8_t(NPCType::Guard), 2, std::uint32_t(100 + i)));
        }
        w.reg.emplace<ecs::SquadRoster>(dead, roster);
        w.reg.emplace<ecs::Dead>(dead);

        const int drained = drain_dead_leader_squads(w, gs.deserterPool);
        CHECK(drained == 9, "the fallen squad pays its survivors in");
        CHECK(int(gs.deserterPool.members.size()) == 9,
                        "and the pool is exactly that many men deep");

        // The dead leader's own (now empty) roster must not be counted as a
        // band, so measure the delta the raiser adds.
        const int before = men_on_map(w);
        const int left = raise_deserter_bands(gs, w, terrain, /*day*/3);
        CHECK(men_on_map(w) - before == left,
                        "the men the pool released are the men that appeared");
    }

    // ── 5. Deterministic from (worldSeed, day) ───────────────────────────
    // A reload may not re-roll the day: no RNG state is consumed, so the same
    // world on the same day must raise the same band in the same place.
    {
        const TerrainData terrain = all_land(64, 64);
        float ax = -1.0f, ay = -1.0f, bx = -2.0f, by = -2.0f;
        int aLeft = 0, bLeft = 0;
        for (int pass = 0; pass < 2; ++pass) {
            GameState gs = make_world(50);
            ecs::World w{};
            const int left = raise_deserter_bands(gs, w, terrain, /*day*/42);
            float x = 0.0f, y = 0.0f;
            for (auto [e, pos, roster] :
                 w.reg.view<sm::ecs::Position, sm::ecs::SquadRoster>().each()) {
                (void)e;
                (void)roster;
                x = pos.x;
                y = pos.y;
            }
            if (pass == 0) { ax = x; ay = y; aLeft = left; }
            else           { bx = x; by = y; bLeft = left; }
        }
        CHECK(aLeft == bLeft, "same day, same exodus");
        CHECK(ax == bx && ay == by, "same day, same ground");
    }

    return sm::test::report("deserter_bands_test");
}

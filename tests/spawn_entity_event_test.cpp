// Locks the SpawnEntity consumer half (macro/npc_spawn.cpp spawn_npc_at).
//
// Before 2026-08-05 the SpawnEntity event had two producers (quest onAccept,
// content/quests/procedural.cpp) and ZERO consumers — it was emitted into the
// void and kill-contracts never produced their targets (history/audit.md II.5). This
// test pins the consumer's contract:
//   * a known token spawns exactly one macro NPC of that registry type;
//   * an Aggressive type joins the "bandits" faction (boot-spawner parity);
//   * level > 0 pins NpcLevel;
//   * the body lands within the spawn scatter of the named cell (torus);
//   * the MacroSpawnId ordinal continues past the existing maximum
//     (possession identity stays unique);
//   * an unknown token spawns NOTHING and returns false (no silent Bandit
//     fallback here — that is the subworld console's own historical rule).

#include "check.h"
#include "macro/npc_spawn.h"
#include "macro/npc.h"
#include "macro/faction.h"
#include "macro/state.h"
#include "core/torus.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/store.h"

#include <cstdio>

namespace {

int count_npcs(sm::ecs::World& w) {
    int n = 0;
    for ([[maybe_unused]] auto e :
         w.reg.view<sm::ecs::MacroSlot>()) ++n;
    return n;
}

// The whole scenario lives in a VOID function so CHECK_OR_RETURN can bail
// out of a broken fixture without inventing a verdict: `main` only reports.
void run_spawn_contract() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 777u;
    sm::ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());
    sm::TerrainData terrain{};  // no RGBA storage => everywhere is land

    // NEGATIVE CONTROL FIRST, and asserted in two halves: a token the
    // catalogue does not know must be REFUSED, and — the half that is easy to
    // forget — must leave nothing behind. A spawner that half-built a body
    // and then said no would pass the first check alone.
    CHECK(!sm::spawn_npc_at(gs, w, sm::store_of(w), terrain, "grue", 10, 12, 3),
          "a token the catalogue does not know is REFUSED");
    CHECK(count_npcs(w) == 0, "...and left no half-built entity behind");

    // Known token, case-insensitive, level pinned.
    CHECK_OR_RETURN(
        sm::spawn_npc_at(gs, w, sm::store_of(w), terrain, "Bandit", 10, 12, 3),
        "a known token spawns — and the lookup ignores case");
    CHECK(count_npcs(w) == 1, "exactly one body was raised, not two");

    std::uint32_t firstOrdinal = 0;
    for (auto e : w.reg.view<sm::ecs::MacroSlot>()) {
        const auto& kind = (*sm::body_state<sm::ecs::NPCKind>(w.reg, e));
        CHECK(kind.type == std::uint16_t(sm::NPCType::Bandit),
              "the body wears the ROW the token named");
        CHECK(kind.factionIdx == std::uint16_t(sm::faction_index("bandits")),
              "and the faction comes from the ONE registry, resolved by name "
              "at the border and carried as an ordinal");
        const auto& lvl = (*sm::body_state<sm::ecs::NpcLevel>(w.reg, e));
        CHECK(lvl.value == 3, "the level asked for is the level pinned");
        const auto& pc = (*sm::body_state<sm::ecs::MacroCell>(w.reg, e));
        // find_valid_spawn scatters within +-6 cells of the wrapped target.
        const float d2 = sm::torus_dist_sq(
            float(sm::ecs::cell_x(pc, gs.mapW)),
            float(sm::ecs::cell_y(pc, gs.mapW)), 10.0f, 12.0f,
            float(gs.mapW), float(gs.mapH));
        CHECK(d2 <= 2.0f * 6.0f * 6.0f,
              "the body landed within find_valid_spawn's scatter of the cell "
              "asked for — measured by TORUS distance, so the seam is near");
        firstOrdinal = (*sm::body_state<sm::ecs::MacroSpawnId>(w.reg, e)).index;
    }

    // Second spawn: ordinal strictly continues (possession identity unique).
    CHECK_OR_RETURN(
        sm::spawn_npc_at(gs, w, sm::store_of(w), terrain, "bandit", 40, 40, 2),
        "the same token spawns again in lower case");
    CHECK(count_npcs(w) == 2, "two bodies stand, not one and not three");
    bool sawSecond = false;
    int wentBackwards = 0;
    for (auto e : w.reg.view<sm::ecs::MacroSlot>()) {
        const std::uint32_t idx = (*sm::body_state<sm::ecs::MacroSpawnId>(w.reg, e)).index;
        if (idx == firstOrdinal) continue;
        if (idx <= firstOrdinal) ++wentBackwards;
        sawSecond = true;
    }
    CHECK(sawSecond, "the second body carries an ordinal of its own");
    CHECK(wentBackwards == 0,
          "and it CONTINUES the sequence — a spawn ordinal is an identity, so "
          "it may never be reused or walked back");
}

} // namespace

int main() {
    run_spawn_contract();
    return sm::test::report("spawn_entity_event_test");
}

// Locks the SpawnEntity consumer half (macro/npc_spawn.cpp spawn_npc_at).
//
// Before 2026-08-05 the SpawnEntity event had two producers (quest onAccept,
// content/quests/procedural.cpp) and ZERO consumers — it was emitted into the
// void and kill-contracts never produced their targets (audit.md II.5). This
// test pins the consumer's contract:
//   * a known token spawns exactly one macro NPC of that registry type;
//   * an Aggressive type joins the "bandits" faction (boot-spawner parity);
//   * level > 0 pins NpcLevel;
//   * the body lands within the spawn scatter of the named cell (torus);
//   * the MacroSpawnId ordinal continues past the existing maximum
//     (possession identity stays unique);
//   * an unknown token spawns NOTHING and returns false (no silent Bandit
//     fallback here — that is the subworld console's own historical rule).

#include "macro/npc_spawn.h"
#include "macro/npc.h"
#include "macro/faction.h"
#include "macro/state.h"
#include "core/torus.h"
#include "ecs/components.h"
#include "ecs/world.h"

#include <cstdio>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL spawn_entity_event_test: %s\n", msg);
    return 1;
}

int count_npcs(sm::ecs::World& w) {
    int n = 0;
    for ([[maybe_unused]] auto e :
         w.reg.view<sm::ecs::NPCKind, sm::ecs::MacroNpcRuntime>()) ++n;
    return n;
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 777u;
    sm::ecs::World w;
    sm::TerrainData terrain{};  // no RGBA storage => everywhere is land

    // Unknown token: refuses, spawns nothing.
    if (sm::spawn_npc_at(gs, w, terrain, "grue", 10, 12, 3)) {
        return fail("unknown token must not spawn");
    }
    if (count_npcs(w) != 0) return fail("unknown token left an entity behind");

    // Known token, case-insensitive, level pinned.
    if (!sm::spawn_npc_at(gs, w, terrain, "Bandit", 10, 12, 3)) {
        return fail("'Bandit' did not spawn");
    }
    if (count_npcs(w) != 1) return fail("expected exactly one body");

    std::uint32_t firstOrdinal = 0;
    for (auto e : w.reg.view<sm::ecs::NPCKind>()) {
        const auto& kind = w.reg.get<sm::ecs::NPCKind>(e);
        if (kind.type != std::uint16_t(sm::NPCType::Bandit)) {
            return fail("spawned body is not a Bandit");
        }
        if (kind.factionIdx != std::uint16_t(sm::faction_index("bandits"))) {
            return fail("aggressive type must join the bandits faction");
        }
        const auto& lvl = w.reg.get<sm::ecs::NpcLevel>(e);
        if (lvl.value != 3) return fail("level 3 was not pinned");
        const auto& pos = w.reg.get<sm::ecs::Position>(e);
        // find_valid_spawn scatters within +-6 cells of the wrapped target.
        const float d2 = sm::torus_dist_sq(pos.x, pos.y, 10.0f, 12.0f,
                                           float(gs.mapW), float(gs.mapH));
        if (d2 > 2.0f * 6.0f * 6.0f) {
            return fail("body landed outside the spawn scatter of the cell");
        }
        firstOrdinal = w.reg.get<sm::ecs::MacroSpawnId>(e).index;
    }

    // Second spawn: ordinal strictly continues (possession identity unique).
    if (!sm::spawn_npc_at(gs, w, terrain, "bandit", 40, 40, 2)) {
        return fail("second spawn failed");
    }
    if (count_npcs(w) != 2) return fail("expected two bodies");
    bool sawSecond = false;
    for (auto e : w.reg.view<sm::ecs::MacroSpawnId>()) {
        const std::uint32_t idx = w.reg.get<sm::ecs::MacroSpawnId>(e).index;
        if (idx != firstOrdinal) {
            if (idx <= firstOrdinal) return fail("ordinal did not continue");
            sawSecond = true;
        }
    }
    if (!sawSecond) return fail("second body carries no fresh ordinal");

    std::printf("spawn_entity_event_test: refuse=ok spawn=ok faction=ok "
                "level=ok scatter=ok ordinal=ok\n");
    return 0;
}

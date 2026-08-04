// Settlement allegiance contract — a town's people belong to its KINGDOM.
//
// The bug this pins: both the subworld citizen spawn and the procedural quest
// generator hardcoded faction_index("empire"), so a city of Old Magica fielded
// imperial guards on foot while the very same city fielded Magica guards on the
// map, and a quest for a Magica town paid reputation to the empire. Ownership
// was never unknown — Settlement/Village::kingdomIdx is computed by the politik
// layer and persisted in the save; the spawn sites simply ignored it.
//
// There is now exactly ONE resolver (macro/politik.h faction_index_for_kingdom)
// and this test comes at it from both ends:
//
//   1. The resolver itself — real kingdom, unowned settlement, out-of-range
//      index, and a kingdom whose id is not a registry row.
//   2. The shipping subworld spawn path (spawn_cell_npcs → citizens): a city
//      handed a non-empire faction produces citizens of THAT faction and NOT
//      ONE imperial body. That negative control is the whole point — a test
//      that only checked "guards exist" passed happily while every guard in the
//      world served the empire.
//
// Pure ECS + data assertions: no Vulkan, no window, no GameState needed beyond a
// hand-built Politik. Manager construction mirrors subworld_spawn_parity_test.
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/faction.h"
#include "macro/politik.h"
#include "sub/seamless_manager.h"
#include "sub/spawn.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL settlement_faction_test: %s\n", msg);
    return 1;
}

// A flat meadow cell — the resolver the manager calls for each window cell.
sm::sub::CellContext meadow_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    c.macroHeight = 0.62f;
    c.biome = sm::Biome::Meadow;
    c.feature = sm::FT_None;
    c.landmarkSettlementId = -1;
    c.landmarkSize = 0;
    c.landmarkKind = sm::sub::CellLandmarkKind::None;
    c.seed = 0x13570000u
        ^ (std::uint32_t(cx) * 73856093u)
        ^ (std::uint32_t(cy) * 19349663u);
    return c;
}

sm::Kingdom make_kingdom(const char* id) {
    sm::Kingdom k{};
    k.id = id;
    k.name = id;
    k.temperament = sm::Temperament::Magical;
    k.capitalCityIdx = 0;
    k.color = 0u;
    return k;
}

// ── 1. The resolver ─────────────────────────────────────────────────────────
bool run_resolver_contract() {
    sm::Politik politik{};
    politik.kingdoms.push_back(make_kingdom("old_magica"));
    politik.kingdoms.push_back(make_kingdom("timaert"));
    // A kingdom id with no registry row: a data error that must degrade to the
    // documented fallback, never to a garbage index.
    politik.kingdoms.push_back(make_kingdom("atlantis"));

    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    if (sm::faction_index_for_kingdom(politik, 0)
        != std::uint16_t(sm::faction_index("old_magica"))) return false;
    if (sm::faction_index_for_kingdom(politik, 1)
        != std::uint16_t(sm::faction_index("timaert"))) return false;
    if (sm::faction_index_for_kingdom(politik, 2) != empire) return false; // unknown id
    if (sm::faction_index_for_kingdom(politik, -1) != empire) return false; // unowned
    if (sm::faction_index_for_kingdom(politik, 99) != empire) return false; // out of range

    // And the fallback is a real registry row, not a lucky zero.
    if (sm::faction_index("old_magica") == sm::faction_index("empire")) return false;
    return true;
}

// ── 2. The shipping spawn path, with the negative control ───────────────────
// Populate a City cell for a non-empire realm and demand that EVERY citizen
// wears that realm's colours. Fauna is excluded: a creature's faction is its own
// FaunaEntry row, not the town's (NPCKind.type < NPCType::Count is the
// humanoid/creature discriminator used everywhere else).
bool run_citizens_wear_their_realm(const sm::sub::SeamlessSubworldManager& mgr) {
    const std::uint16_t magica = std::uint16_t(sm::faction_index("old_magica"));
    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    sm::ecs::World world{};
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             sm::sub::LandmarkKind::City,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xB105A11u,
                             /*settlementFaction*/magica,
                             /*landmarkPop*/2000,
                             /*zoneLevel*/0);

    int citizens = 0;
    int wrongFaction = 0;
    int imperial = 0;
    auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        if (kind.type >= std::uint16_t(sm::NPCType::Count)) continue;  // fauna
        ++citizens;
        if (kind.factionIdx != magica) ++wrongFaction;
        if (kind.factionIdx == empire) ++imperial;
    }

    if (citizens < 10) return false;      // a 2000-soul city fields a crowd
    if (wrongFaction != 0) return false;  // all of them Magica
    if (imperial != 0) return false;      // NEGATIVE CONTROL: not one imperial
    return true;
}

// The empire itself must still work — the fix must not invert the bug.
bool run_imperial_city_still_imperial(const sm::sub::SeamlessSubworldManager& mgr) {
    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    sm::ecs::World world{};
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             sm::sub::LandmarkKind::Village,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xB105A12u,
                             /*settlementFaction*/empire,
                             /*landmarkPop*/400,
                             /*zoneLevel*/0);

    int citizens = 0;
    auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        if (kind.type >= std::uint16_t(sm::NPCType::Count)) continue;
        ++citizens;
        if (kind.factionIdx != empire) return false;
    }
    return citizens > 0;
}

}  // namespace

int main() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty();

    if (!run_resolver_contract()) {
        sm::sub::clear_saved_subworlds();
        return fail("faction_index_for_kingdom wrong "
                    "(kingdom id not resolved / fallback not empire)");
    }
    if (!run_citizens_wear_their_realm(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("citizens of a non-empire city are not that city's faction "
                    "(imperial bodies spawned in a Magica town)");
    }
    if (!run_imperial_city_still_imperial(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("an imperial settlement stopped spawning imperial citizens");
    }

    std::printf("OK settlement_faction_test resolver=1 realm_citizens=1 "
                "empire_unbroken=1\n");
    sm::sub::clear_saved_subworlds();
    return 0;
}

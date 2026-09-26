// Settlement allegiance contract — a town's people belong to its FACTION
// (owner 2026-09-11: «королевств нет, только фракции — одна система»; the
// column is Landmark::factionIdx, persisted in the save since v94).
//
// The bug this pins: both the subworld citizen spawn and the procedural quest
// generator hardcoded faction_index("empire"), so a city of Old Magica fielded
// imperial guards on foot while the very same city fielded Magica guards on the
// map, and a quest for a Magica town paid reputation to the empire.
//
// The resolver is faction_or_freefolk over the stored index (the ownerless
// fallback law, macro/faction.h) and faction_index_for_cell for the ground;
// this test comes at both from both ends:
//
//   1. The fallback law — a real index, an unowned -1, an out-of-range byte.
//   2. The shipping subworld spawn path (spawn_cell_npcs → citizens): a city
//      handed a non-empire faction produces citizens of THAT faction and NOT
//      ONE imperial body. That negative control is the whole point — a test
//      that only checked "guards exist" passed happily while every guard in the
//      world served the empire.
//
// Pure ECS + data assertions: no Vulkan, no window, no GameState needed beyond a
// hand-built Politik. Manager construction mirrors subworld_spawn_parity_test.
#include "check.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/faction.h"
#include "macro/politik.h"
#include "sub/seamless_manager.h"
#include "sub/spawn.h"
#include "macro/store.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// A flat meadow cell — the resolver the manager calls for each window cell.
sm::sub::CellContext meadow_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    c.macroHeight = 0.62f;
    c.biome = sm::Biome::Meadow;
    c.feature = sm::FT_None;
    c.landmark.id = -1;
    c.landmark.size = 0;
    c.landmark.kind = sm::LandmarkType::None;
    c.seed = 0x13570000u
        ^ sm::sub::cell_seed(0u, cx, cy);
    return c;
}

// ── 1. The fallback law over the stored column ──────────────────────────────
void run_resolver_contract() {
    // An ownerless place belongs to the free folk — never quietly to the empire.
    const int freeIdx = sm::faction_index("freefolk");
    CHECK_OR_RETURN(freeIdx >= 0, "the free folk are a row of the ONE registry");
    const std::uint16_t freefolk = std::uint16_t(freeIdx);

    const int magica = sm::faction_index("old_magica");
    CHECK(sm::faction_or_freefolk(magica) == std::uint16_t(magica),
          "a place with an owner keeps that owner");
    CHECK(sm::faction_or_freefolk(-1) == freefolk,
          "an UNOWNED place belongs to the free folk");
    CHECK(sm::faction_or_freefolk(9999) == freefolk,
          "a garbage owner byte fails closed to the free folk, never to a crown");
    CHECK(sm::faction_or_freefolk(int(sm::kNoFaction)) == freefolk,
          "the explicit no-faction sentinel resolves to the free folk too");
    // The unruled are their own realm, not an alias of a crown.
    CHECK(freeIdx != sm::faction_index("empire"),
          "the free folk are their OWN realm, not an alias of the empire");
    CHECK(magica != sm::faction_index("empire"),
          "old_magica is its own realm — the registry has no two rows for one");
}

// ── 1b. The same question asked of the GROUND ───────────────────────────────
// A body that appears with no owner of its own (a scripted encounter, a console
// spawn) inherits the realm that holds the cell it stands on. This is the answer
// to "no context", so it must be exactly as honest as the settlement rule — and
// must degrade, never guess, when the world has no ownership map yet.
void run_ground_owner_contract() {
    const std::uint16_t freefolk = std::uint16_t(sm::faction_index("freefolk"));

    // The byte IS the faction registry index now (kingdoms cut 2026-09-11).
    sm::Politik politik{};
    politik.mapW = 4;
    politik.mapH = 2;
    politik.cellOwner.assign(8, 0xffu);
    politik.cellOwner[0] =
        std::uint8_t(sm::faction_index("old_magica"));   // (0,0)
    politik.cellOwner[5] =
        std::uint8_t(sm::faction_index("timaert"));      // (1,1)

    CHECK(sm::faction_index_for_cell(politik, 0, 0)
              == std::uint16_t(sm::faction_index("old_magica")),
          "a claimed cell answers with the realm that holds it");
    CHECK(sm::faction_index_for_cell(politik, 1, 1)
              == std::uint16_t(sm::faction_index("timaert")),
          "a second claimed cell answers with ITS realm, not the first's");
    CHECK(sm::faction_index_for_cell(politik, 2, 0) == freefolk,
          "the unclaimed wilds belong to the free folk");
    // ЗАКОН АДРЕСА: the map is a torus — coordinates WRAP, they never read
    // out of bounds, and the far side of the seam is the same cell.
    CHECK(sm::faction_index_for_cell(politik, 4, 2)
              == std::uint16_t(sm::faction_index("old_magica")),
          "a coordinate past the far edge wraps to the same cell");
    CHECK(sm::faction_index_for_cell(politik, -4, -2)
              == std::uint16_t(sm::faction_index("old_magica")),
          "a negative coordinate wraps the same way — the torus has no edge");

    // No ownership map at all (a world mid-generation, a bare test fixture):
    // unclaimed, not a garbage index off the end of the vector.
    sm::Politik empty{};
    CHECK(sm::faction_index_for_cell(empty, 0, 0) == freefolk,
          "a world with no ownership map yet answers UNCLAIMED, not garbage");
    // A truncated map is rejected the same way rather than indexed into.
    sm::Politik torn = politik;
    torn.cellOwner.resize(3);
    CHECK(sm::faction_index_for_cell(torn, 0, 0) == freefolk,
          "a truncated map degrades to unclaimed instead of being indexed into");
}

// ── 2. The shipping spawn path, with the negative control ───────────────────
// Populate a City cell for a non-empire realm and demand that EVERY citizen
// wears that realm's colours. Fauna is excluded: a creature's faction is its own
// FaunaEntry row, not the town's (NPCKind.type < NPCType::Count is the
// humanoid/creature discriminator used everywhere else).
void run_citizens_wear_their_realm(const sm::sub::SeamlessSubworldManager& mgr) {
    const std::uint16_t magica = std::uint16_t(sm::faction_index("old_magica"));
    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    sm::ecs::World world{};

    auto worldStore_ = sm::make_macro_store();

    sm::store_attach(world, worldStore_.get());
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             sm::LandmarkType::City,
                             /*danger*/0,
                             /*depositsNear*/0,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xB105A11u,
                             /*worldSeed*/0xB105A11u,
                             /*settlementFaction*/magica,
                             /*landmarkPop*/2000,
/*landmarkSubjectId*/-1,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount*/-1, /*garrison*/nullptr,
                             sm::world_time_at(1, 12, 0));

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

    // The count is asserted FIRST: without it the two zero-checks below pass
    // on an empty street, which is the §8 п.3 trap exactly.
    CHECK_OR_RETURN(citizens >= 10,
                    "a 2000-soul city actually fielded a crowd to inspect");
    CHECK(wrongFaction == 0, "every citizen wears the realm that owns the town");
    // NEGATIVE CONTROL, and now its own verdict: the bug this test was written
    // for spawned IMPERIAL bodies in a Magica town. Folded into the bool above
    // it could not say which half had broken.
    CHECK(imperial == 0,
          "not one imperial body stands in a Magica town — the bug this test "
          "exists for");
}

// The empire itself must still work — the fix must not invert the bug.
void run_imperial_city_still_imperial(const sm::sub::SeamlessSubworldManager& mgr) {
    const std::uint16_t empire = std::uint16_t(sm::faction_index("empire"));

    sm::ecs::World world{};

    auto worldStore_ = sm::make_macro_store();

    sm::store_attach(world, worldStore_.get());
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             /*treeCount*/0,
                             sm::LandmarkType::Village,
                             /*danger*/0,
                             /*depositsNear*/0,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xB105A12u,
                             /*worldSeed*/0xB105A12u,
                             /*settlementFaction*/empire,
                             /*landmarkPop*/400,
                             /*landmarkSubjectId*/-1,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount*/-1, /*garrison*/nullptr,
                             sm::world_time_at(1, 12, 0));

    int citizens = 0;
    int foreign = 0;
    auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        if (kind.type >= std::uint16_t(sm::NPCType::Count)) continue;
        ++citizens;
        if (kind.factionIdx != empire) ++foreign;
    }
    CHECK_OR_RETURN(citizens > 0, "the imperial village fielded citizens at all");
    CHECK(foreign == 0,
          "and every one of them is imperial — the fix did not INVERT the bug");
}

}  // namespace

int main() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty();

    // Each contract asserts for itself. They used to hand `main` a bool that
    // became ONE message listing the three things it might have meant — and
    // the file's only COUNTED check was the un-failable `CHECK(true, "every
    // gate above held")` at the bottom (§8 ЗАКОН НУЛЕВОЙ п.6).
    run_resolver_contract();
    run_ground_owner_contract();
    run_citizens_wear_their_realm(mgr);
    run_imperial_city_still_imperial(mgr);

    sm::sub::clear_saved_subworlds();
    return sm::test::report("settlement_faction_test");
}

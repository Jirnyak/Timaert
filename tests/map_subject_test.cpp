// The one addressing door of the interaction menu (macro/map_subject.h).
//
// The fact under test is not "the functions return something" but the CLAIM
// the menu session stands on: a squad and a landmark hold the SAME types
// (Inventory / SoldierSquad), and store_of/roster_of return THE VERY OBJECTS
// the two old address books held — the ECS components on the leader entity,
// the bare fields of the landmark record — never copies, never a third store.
// And the landmark side must answer for ANY kind: the City filter of the old
// settlement panel (PLAY-2) is exactly what this door exists to end.
#include "check.h"

#include "ecs/world.h"
#include "macro/map_subject.h"
#include "macro/squad.h"
#include "macro/state.h"

#include <entt/entt.hpp>

namespace {

sm::GameState make_world() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    sm::Landmark city{};
    city.type = sm::LandmarkType::City;
    city.id = 7;
    city.name = "Testholm";
    city.x = 10;
    city.y = 10;
    city.population = 300;
    city.garrison.push(sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 2, 11u));
    gs.landmarks.push_back(city);
    // A VILLAGE and a SPIRE on the same one id space (v54): the door must
    // answer for them exactly as it does for the city — kind-blind.
    sm::Landmark village{};
    village.type = sm::LandmarkType::Village;
    village.id = 42;
    village.name = "Hamlet";
    village.x = 20;
    village.y = 20;
    village.population = 40;
    gs.landmarks.push_back(village);
    sm::Landmark spire{};
    spire.type = sm::LandmarkType::Spire;
    spire.id = 13;
    spire.x = 40;
    spire.y = 40;
    gs.landmarks.push_back(spire);
    return gs;
}

entt::entity make_squad(sm::ecs::World& w, std::uint32_t ordinal) {
    const auto e = w.reg.create();
    w.reg.emplace<sm::ecs::MacroSpawnId>(e, ordinal);
    w.reg.emplace<sm::ecs::NpcInventory>(e);
    auto& roster = w.reg.emplace<sm::ecs::SquadRoster>(e);
    roster.squad.push(sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 2, 21u));
    return e;
}

// The door answers with the SAME object the old path held — address equality,
// so "one store" is a fact of memory, not a convention of copies.
void test_the_door_opens_the_old_addresses() {
    using namespace sm;
    GameState gs = make_world();
    ecs::World world;
    // Grabla (ECS ref not across tick): every entity is created BEFORE any
    // pointer is taken — a later create() may reallocate component storage.
    const auto squad = make_squad(world, 5);
    const auto bare = world.reg.create();   // an entity with no bag, no men
    MacroWorld w{&gs, nullptr, &world};

    CHECK(store_of(w, subject_of_squad(squad))
              == &world.reg.get<ecs::NpcInventory>(squad).inv,
          "a squad's store IS its NpcInventory component, the very object");
    CHECK(roster_of(w, subject_of_squad(squad))
              == &world.reg.get<ecs::SquadRoster>(squad).squad,
          "a squad's roster IS its SquadRoster component, the very object");

    CHECK(store_of(w, subject_of_landmark(7))
              == &landmark_by_id(gs, 7)->inventory,
          "a landmark's store IS the record's inventory field, the very object");
    CHECK(roster_of(w, subject_of_landmark(7))
              == &landmark_by_id(gs, 7)->garrison,
          "a landmark's roster IS the record's garrison field, the very object");

    // PLAY-2's law: the door is KIND-BLIND. A village and a spire answer
    // through the same door a city does — no LandmarkType filter anywhere.
    CHECK(store_of(w, subject_of_landmark(42)) != nullptr
              && roster_of(w, subject_of_landmark(42)) != nullptr,
          "a village answers the door like any place");
    CHECK(store_of(w, subject_of_landmark(13)) != nullptr
              && roster_of(w, subject_of_landmark(13)) != nullptr,
          "a spire answers the door like any place");

    // An entity that carries no bag and no men is an honest "nothing", not
    // a crash and not somebody else's store.
    CHECK(store_of(w, subject_of_squad(bare)) == nullptr
              && roster_of(w, subject_of_squad(bare)) == nullptr,
          "a component-less entity answers nullptr, fail closed");
}

// One object behind the door: a write through the door is visible through
// the old path, and the two sides really do speak ONE pair of types.
void test_a_write_through_the_door_lands_in_the_world() {
    using namespace sm;
    GameState gs = make_world();
    ecs::World world;
    const auto squad = make_squad(world, 5);
    MacroWorld w{&gs, nullptr, &world};

    Inventory* store = store_of(w, subject_of_landmark(42));
    CHECK_OR_RETURN(store != nullptr, "the village store opens");
    store->add("bread", 3);
    CHECK(landmark_by_id(gs, 42)->inventory.count("bread") == 3,
          "bread added through the door sits in the village record itself");

    // The symmetry the menu will trade on: hire_npc already takes two
    // SoldierSquad& — the door's returns feed it directly, both ways.
    SoldierSquad* garrison = roster_of(w, subject_of_landmark(7));
    SoldierSquad* men = roster_of(w, subject_of_squad(squad));
    CHECK_OR_RETURN(garrison != nullptr && men != nullptr,
                    "both rosters open through the one door");
    const int before = total_soldiers(*garrison);
    men->push(garrison->members[0]);
    garrison->remove_at(0);
    CHECK(total_soldiers(*garrison) == before - 1 && total_soldiers(*men) == 2,
          "a garrison record moves into a squad roster: one type, no seam");
}

// The verbs door: data declares, the door answers — and the owner's verdicts
// of 2026-09-11 are pinned as LITERAL masks (the design numbers themselves,
// house style), so a drifting column trips here, not in a playtest.
void test_actions_are_declared_by_data() {
    using namespace sm;
    GameState gs = make_world();
    ecs::World world;
    const auto squad = make_squad(world, 5);
    MacroWorld w{&gs, nullptr, &world};

    CHECK(actions_of(w, subject_of_squad(squad))
              == (kMapActTalk | kMapActTrade | kMapActAttack),
          "a squad speaks talk/trade/attack — the macro NPC vocabulary");
    CHECK(actions_of(w, subject_of_landmark(7))
              == (kMapActTrade | kMapActHire | kMapActQuests | kMapActEnter),
          "a city offers trade, hire, its contract board and the walk in");
    CHECK(actions_of(w, subject_of_landmark(42))
              == (kMapActTrade | kMapActHire | kMapActQuests | kMapActEnter),
          "the village verdict: прилавок + найм + доска (and the walk in)");
    CHECK(actions_of(w, subject_of_landmark(13)) == kMapActEnter,
          "a spire declares no verbs of its own: the walk-in minimum");
    CHECK((actions_of(w, subject_of_landmark(7)) & (kMapActTalk | kMapActAttack))
              == 0,
          "no place talks or is attacked through this menu (war is a track)");

    CHECK(actions_of(w, MapSubject{}) == 0
              && actions_of(w, subject_of_landmark(9999)) == 0
              && actions_of(w, subject_of_squad(entt::null)) == 0,
          "nobody offers no verbs, fail closed");
}

// Fail closed: a malformed subject or an absent layer answers nullptr and
// touches nothing — the exact discipline of every macro door.
void test_the_door_fails_closed() {
    using namespace sm;
    GameState gs = make_world();
    ecs::World world;
    const auto squad = make_squad(world, 5);
    MacroWorld w{&gs, nullptr, &world};

    CHECK(store_of(w, MapSubject{}) == nullptr
              && roster_of(w, MapSubject{}) == nullptr,
          "a None subject names nobody");
    CHECK(store_of(w, subject_of_landmark(9999)) == nullptr
              && roster_of(w, subject_of_landmark(9999)) == nullptr,
          "an unknown landmark id names nobody");
    CHECK(store_of(w, subject_of_squad(entt::null)) == nullptr
              && roster_of(w, subject_of_squad(entt::null)) == nullptr,
          "a null entity names nobody");

    const auto dead = world.reg.create();
    world.reg.destroy(dead);
    CHECK(store_of(w, subject_of_squad(dead)) == nullptr,
          "a destroyed entity names nobody");

    MacroWorld headless{nullptr, nullptr, nullptr};
    CHECK(store_of(headless, subject_of_landmark(7)) == nullptr
              && roster_of(headless, subject_of_squad(squad)) == nullptr,
          "an absent layer answers nullptr, never a crash (S6 zero contribution)");
}

} // namespace

int main() {
    test_the_door_opens_the_old_addresses();
    test_a_write_through_the_door_lands_in_the_world();
    test_actions_are_declared_by_data();
    test_the_door_fails_closed();
    return sm::test::report("map_subject_test");
}

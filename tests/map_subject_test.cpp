// The one addressing door of the interaction menu (macro/map_subject.h).
//
// The fact under test is not "the functions return something" but the CLAIM
// the menu session stands on: a squad and a landmark hold the SAME types
// (Inventory / SoldierSquad), and store_of/roster_of return THE VERY OBJECTS
// the two address books hold — the store COLUMNS of the leader's slot (шаг
// 1г: субъект = {slot,gen}, двери отвечают колонками без entt), the bare
// fields of the landmark record — never copies, never a third store.
// And the landmark side must answer for ANY kind: the City filter of the old
// settlement panel (PLAY-2) is exactly what this door exists to end.
#include "check.h"
#include "macro/labour.h"   // settle_souls / souls_flock — двери душ

#include "ecs/world.h"
#include "macro/map_subject.h"
#include "macro/place_birth.h"   // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/world_row.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/store.h"

#include <entt/entt.hpp>
#include <cstdio>

namespace {

// Место есть неподвижный сквад (ломтик F): склад его — колонка ТЕЛА, род —
// колонка оси, ординал ВЫДАЁТ ЭМИТЕНТ. Рукописных `id` у фикстуры больше
// нет, поэтому она запоминает, кем родились её трое, и спрашивает дверь
// ровно этими ординалами.
struct Fixture {
    sm::GameState gs;
    int cityId = 0;
    int vilId = 0;
    int spireId = 0;
};

// Склад места — колонка инвентаря ЕГО ТЕЛА; резолв идёт той же дверью, что
// у самого меню (place_handle_by_ordinal), чтобы «тот же объект» значило
// тот же путь, а не второй спеллинг резолва.
sm::Inventory& place_store_of(sm::MacroStore& st, int ordinal) {
    const sm::MacroHandle h =
        sm::place_handle_by_ordinal(st, std::uint32_t(ordinal));
    return st.inventory[h.slot].inv;
}

Fixture make_world(sm::MacroStore& st) {
    Fixture fx{};
    sm::GameState& gs = fx.gs;
    gs.mapW = 64;
    gs.mapH = 64;
    // Эмитент поднят ВЫШЕ рукописных ординалов сквадов этой фикстуры (5, 6):
    // выдача с единицы столкнула бы место и сквад в одном ординале, и дверь
    // отвечала бы «тем же объектом» по совпадению.
    gs.nextMacroSpawnOrdinal = 7u;
    const sm::MacroHandle city = sm::birth_place(
        gs, st, sm::SquadType::City, 10, 10, -1, "Testholm");
    fx.cityId = int(st.spawnId[city.slot].index);
    sm::creatures_push(st.inventory[city.slot].inv,
        sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 2, 11u));
    // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число фичи,
    // головы в инвентарь ТЕЛА — тем же законом, что генезис.
    sm::settle_souls(gs, st, city.slot, 300);
    // A VILLAGE and a SPIRE on the same one id space (v54): the door must
    // answer for them exactly as it does for the city — kind-blind.
    const sm::MacroHandle village = sm::birth_place(
        gs, st, sm::SquadType::Village, 20, 20, -1, "Hamlet");
    fx.vilId = int(st.spawnId[village.slot].index);
    // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число
    // фичи, головы в инвентарь ТЕЛА — тем же законом, что генезис.
    sm::settle_souls(gs, st, village.slot, 40);
    const sm::MacroHandle spire =
        sm::birth_place(gs, st, sm::SquadType::Spire, 40, 40);
    fx.spireId = int(st.spawnId[spire.slot].index);
    return fx;
}

// Шаг 1г: субъект несёт хэндл {slot,gen}, и двери отвечают колонками store
// без entt вовсе — рождение сквада для этой двери есть рождение слота.
sm::MacroHandle make_squad(sm::MacroStore& st, std::uint32_t ordinal) {
    const sm::MacroHandle h = sm::store_birth(st);
    st.spawnId[h.slot] = sm::ecs::MacroSpawnId{ordinal};
    sm::creatures_push(st.inventory[h.slot].inv,
        sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 2, 21u));
    return h;
}

// The door answers with the SAME object the old path held — address equality,
// so "one store" is a fact of memory, not a convention of copies.
void test_the_door_opens_the_old_addresses() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);
    GameState& gs = fx.gs;
    const MacroHandle squad = make_squad(st, 5);
    MacroWorld w{.gs = &gs, .store = &st};

    CHECK(store_of(w, subject_of_squad(squad))
              == &st.inventory[squad.slot].inv,
          "a squad's store IS its inventory column, the very object");
    CHECK(roster_of(w, subject_of_squad(squad))
              == &st.inventory[squad.slot].inv,
          "a squad's roster IS its one container (M-71), the very object");

    CHECK(store_of(w, subject_of_landmark(fx.cityId))
              == &place_store_of(st, fx.cityId),
          "a landmark's store IS its BODY's inventory column, the very object");
    CHECK(roster_of(w, subject_of_landmark(fx.cityId))
              == &place_store_of(st, fx.cityId),
          "a landmark's roster IS its one container (M-71), the very object");

    // PLAY-2's law: the door is KIND-BLIND. A village and a spire answer
    // through the same door a city does — no SquadType filter anywhere.
    CHECK(store_of(w, subject_of_landmark(fx.vilId)) != nullptr
              && roster_of(w, subject_of_landmark(fx.vilId)) != nullptr,
          "a village answers the door like any place");
    CHECK(store_of(w, subject_of_landmark(fx.spireId)) != nullptr
              && roster_of(w, subject_of_landmark(fx.spireId)) != nullptr,
          "a spire answers the door like any place");

    // Хэндл, который никогда не рождался, — честное «ничего», не чужой
    // склад и не падение (наследник контроля «энтити без компонент»).
    CHECK(store_of(w, subject_of_squad(MacroHandle{})) == nullptr
              && roster_of(w, subject_of_squad(MacroHandle{})) == nullptr,
          "a never-born handle answers nullptr, fail closed");
}

// One object behind the door: a write through the door is visible through
// the old path, and the two sides really do speak ONE pair of types.
void test_a_write_through_the_door_lands_in_the_world() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);
    GameState& gs = fx.gs;
    const MacroHandle squad = make_squad(st, 5);
    MacroWorld w{.gs = &gs, .store = &st};

    Inventory* store = store_of(w, subject_of_landmark(fx.vilId));
    CHECK_OR_RETURN(store != nullptr, "the village store opens");
    store->add("food", 3);
    CHECK(place_store_of(st, fx.vilId).count("food") == 3,
          "food added through the door sits in the village's own body");

    // The symmetry the menu will trade on: hire_npc already takes two
    // Inventory& — the door's returns feed it directly, both ways (M-71).
    Inventory* garrison = roster_of(w, subject_of_landmark(fx.cityId));
    Inventory* men = roster_of(w, subject_of_squad(squad));
    CHECK_OR_RETURN(garrison != nullptr && men != nullptr,
                    "both rosters open through the one door");
    const int before = creature_heads(*garrison);
    SoldierRecord moved{};
    CHECK_OR_RETURN(
        creatures_take_at(*garrison, garrison->creature_first(), moved),
        "the garrison yields a soul");
    creatures_push(*men, moved);
    CHECK(creature_heads(*garrison) == before - 1
              && creature_heads(*men) == 2,
          "a garrison record moves into a squad roster: one type, no seam");
}

// The verbs door: data declares, the door answers — and the owner's verdicts
// of 2026-09-11 are pinned as LITERAL masks (the design numbers themselves,
// house style), so a drifting column trips here, not in a playtest.
void test_actions_are_declared_by_data() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);
    GameState& gs = fx.gs;
    const MacroHandle squad = make_squad(st, 5);
    MacroWorld w{.gs = &gs, .store = &st};

    CHECK(actions_of(w, subject_of_squad(squad))
              == (kMapActTalk | kMapActTrade | kMapActAttack),
          "a squad speaks talk/trade/attack — the macro NPC vocabulary");
    CHECK(actions_of(w, subject_of_landmark(fx.cityId))
              == (kMapActTrade | kMapActHire | kMapActQuests | kMapActEnter),
          "a city offers trade, hire, its contract board and the walk in");
    CHECK(actions_of(w, subject_of_landmark(fx.vilId))
              == (kMapActTrade | kMapActHire | kMapActQuests | kMapActEnter),
          "the village verdict: прилавок + найм + доска (and the walk in)");
    CHECK(actions_of(w, subject_of_landmark(fx.spireId)) == kMapActEnter,
          "a spire declares no verbs of its own: the walk-in minimum");
    CHECK((actions_of(w, subject_of_landmark(fx.cityId)) & (kMapActTalk | kMapActAttack))
              == 0,
          "no place talks or is attacked through this menu (war is a track)");

    CHECK(actions_of(w, MapSubject{}) == 0
              && actions_of(w, subject_of_landmark(9999)) == 0
              && actions_of(w, subject_of_squad(MacroHandle{})) == 0,
          "nobody offers no verbs, fail closed");
}

// Fail closed: a malformed subject or an absent layer answers nullptr and
// touches nothing — the exact discipline of every macro door.
void test_the_door_fails_closed() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);
    GameState& gs = fx.gs;
    const MacroHandle squad = make_squad(st, 5);
    MacroWorld w{.gs = &gs, .store = &st};

    CHECK(store_of(w, MapSubject{}) == nullptr
              && roster_of(w, MapSubject{}) == nullptr,
          "a None subject names nobody");
    CHECK(store_of(w, subject_of_landmark(9999)) == nullptr
              && roster_of(w, subject_of_landmark(9999)) == nullptr,
          "an unknown landmark id names nobody");
    CHECK(store_of(w, subject_of_squad(MacroHandle{})) == nullptr
              && roster_of(w, subject_of_squad(MacroHandle{})) == nullptr,
          "a never-born handle names nobody");

    // Поколение мертвит пережившую смерть ссылку: тот же слот, но старый
    // хэндл — «никто» (наследник контроля «уничтоженная энтити»).
    const MacroHandle doomed = make_squad(st, 6);
    sm::store_death(st, doomed);
    CHECK(store_of(w, subject_of_squad(doomed)) == nullptr,
          "a handle outliving its slot's death names nobody");

    MacroWorld headless{};
    CHECK(store_of(headless, subject_of_landmark(fx.cityId)) == nullptr
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

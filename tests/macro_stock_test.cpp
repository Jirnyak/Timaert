// The ledger between the layers (macro/macro_stock.{h,cpp}).
//
// The rule it serves: the subworld is a CONTEXT of the macro world, so anything
// borrowed down there — a citizen out of a town's population, a tree out of a
// cell's forest — is paid back UP. Before this system each quantity grew its
// own hand-made write-back, trees had one and population had none, and killing
// a town's people left the map still counting them as alive.
//
// So what is asserted here is not "the numbers came out right" but the
// PROPERTIES that make the system a system:
//   * the table is total — every stock has a row, or borrowing it is silent;
//   * borrowing and returning are the same row, so they cannot drift apart;
//   * a stock is bounded — a place can be emptied but never owe people;
//   * a debt names its subject, so one town's dead never bill its neighbour;
//   * a malformed receipt changes nothing (fail closed).
#include "check.h"

#include "macro/deposit_layer.h"
#include "macro/macro_stock.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <entt/entt.hpp>

namespace {

sm::GameState make_world() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    sm::Settlement city{};
    city.id = 7;
    city.name = "Testholm";
    city.x = 10;
    city.y = 10;
    city.population = 300;
    gs.settlements.push_back(city);
    sm::Settlement other{};
    other.id = 8;
    other.name = "Neighbour";
    other.x = 30;
    other.y = 30;
    other.population = 300;
    gs.settlements.push_back(other);
    sm::Village hamlet{};
    hamlet.id = 42;
    hamlet.name = "Hamlet";
    hamlet.x = 20;
    hamlet.y = 20;
    hamlet.population = 40;
    gs.villages.push_back(hamlet);
    return gs;
}

int population_of(const sm::GameState& gs, int id) {
    for (const auto& s : gs.settlements) if (s.id == id) return s.population;
    for (const auto& v : gs.villages)    if (v.id == id) return v.population;
    return -1;
}

// The table must answer for EVERY stock the enum declares. A row that goes
// missing does not crash — it silently stops paying the world back, which is
// the exact failure this system exists to end.
void test_the_table_is_total() {
    using namespace sm;
    int rows = 0, unnamed = 0;
    for (std::uint8_t i = 0; i < std::uint8_t(MacroStock::Count); ++i) {
        ++rows;
        const char* id = macro_stock_id(MacroStock(i));
        if (id == nullptr || id[0] == '\0' || id[0] == '?') ++unnamed;
    }
    CHECK(rows == int(MacroStock::Count) && rows > 0 && unnamed == 0,
          "every declared stock has a named row: the table is the system");
}

// The heart of it: `read` and `write` are two ends of ONE row, so what the
// world lends it can take back, exactly.
void test_borrow_and_return_are_symmetric() {
    using namespace sm;
    sm::GameState gs = make_world();
    sm::TreeLayer trees;
    trees.width = gs.mapW;
    trees.height = gs.mapH;
    trees.data.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH), 500);
    MacroWorld w{&gs, &trees};

    const MacroStockKey town{7, 10, 10};
    const MacroStockKey cell{-1, 3, 4};

    const int pop0  = macro_stock_read(w, MacroStock::Population, town);
    const int tree0 = macro_stock_read(w, MacroStock::TreeCount, cell);
    CHECK(pop0 == 300 && tree0 == 500,
          "read reports what the macro world actually holds");

    macro_stock_apply(w, MacroStock::Population, town, -12);
    macro_stock_apply(w, MacroStock::TreeCount,  cell, -30);
    CHECK(macro_stock_read(w, MacroStock::Population, town) == pop0 - 12
              && macro_stock_read(w, MacroStock::TreeCount, cell) == tree0 - 30,
          "spending a stock lowers exactly what was spent");

    macro_stock_apply(w, MacroStock::Population, town, +12);
    macro_stock_apply(w, MacroStock::TreeCount,  cell, +30);
    CHECK(macro_stock_read(w, MacroStock::Population, town) == pop0
              && macro_stock_read(w, MacroStock::TreeCount, cell) == tree0,
          "returning what was borrowed restores the world exactly");
}

// A place can be emptied. It can never owe people, and a cell can never hold
// more forest than a cell is allowed to hold.
void test_stocks_are_bounded() {
    using namespace sm;
    sm::GameState gs = make_world();
    sm::TreeLayer trees;
    trees.width = gs.mapW;
    trees.height = gs.mapH;
    trees.data.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH), 10);
    MacroWorld w{&gs, &trees};

    macro_stock_apply(w, MacroStock::Population, MacroStockKey{7, 10, 10}, -100000);
    CHECK(macro_stock_read(w, MacroStock::Population, MacroStockKey{7, 10, 10}) == 0,
          "a town can be emptied to zero and never below it");

    macro_stock_apply(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1}, -100000);
    CHECK(macro_stock_read(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1}) == 0,
          "a cell's forest bottoms out at bare ground, not at a negative count");

    macro_stock_apply(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1},
                      kMaxTreesPerCell * 4);
    CHECK(macro_stock_read(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1})
              == kMaxTreesPerCell,
          "a cell cannot be planted past the densest forest a cell can be");
}

// A receipt names its subject. One town's dead must never be billed to the
// town next door — the failure mode of every "nearest settlement" shortcut.
void test_debts_bill_their_own_subject() {
    using namespace sm;
    sm::GameState gs = make_world();
    MacroWorld w{&gs, nullptr};

    entt::registry reg;
    const auto citizen = reg.create();
    stamp_macro_debt(reg, citizen, MacroStock::Population,
                     MacroStockKey{7, 10, 10}, 1);
    const auto* debt = reg.try_get<ecs::MacroDebt>(citizen);
    CHECK_OR_RETURN(debt != nullptr, "stamping leaves a receipt on the body");

    settle_macro_debt(w, *debt, -1);
    CHECK(population_of(gs, 7) == 299, "the dead citizen's own town shrinks by one");
    CHECK(population_of(gs, 8) == 300, "the town next door is untouched");
    CHECK(population_of(gs, 42) == 40, "and so is the village");

    // A village is a named place with people too: same row, same id space.
    const auto villager = reg.create();
    stamp_macro_debt(reg, villager, MacroStock::Population,
                     MacroStockKey{42, 20, 20}, 3);
    settle_macro_debt(w, *reg.try_get<ecs::MacroDebt>(villager), -1);
    CHECK(population_of(gs, 42) == 37,
          "a village pays from its own people, by the amount the receipt says");

    // Signed both ways (owner's ruling): the same row settles creation.
    settle_macro_debt(w, *reg.try_get<ecs::MacroDebt>(villager), +1);
    CHECK(population_of(gs, 42) == 40,
          "handing the borrowed thing back credits the same place");
}

// A receipt that names nothing must change nothing: the system fails closed,
// so a spawner that half-stamps cannot quietly drain a random town.
void test_malformed_receipts_do_nothing() {
    using namespace sm;
    sm::GameState gs = make_world();
    MacroWorld w{&gs, nullptr};
    const int before = population_of(gs, 7);

    ecs::MacroDebt zeroAmount{std::uint8_t(MacroStock::Population), 7, 10, 10, 0};
    ecs::MacroDebt unknownStock{std::uint8_t(MacroStock::Count), 7, 10, 10, 5};
    ecs::MacroDebt noSubject{std::uint8_t(MacroStock::Population), -1, 10, 10, 5};
    ecs::MacroDebt strangerId{std::uint8_t(MacroStock::Population), 9999, 0, 0, 5};

    settle_macro_debt(w, zeroAmount,   -1);
    settle_macro_debt(w, unknownStock, -1);
    settle_macro_debt(w, noSubject,    -1);
    settle_macro_debt(w, strangerId,   -1);
    CHECK(population_of(gs, 7) == before,
          "a receipt for nothing, for an unknown stock or for nobody moves no stock");

    // And a world with no tree layer at all must not pretend it wrote one.
    MacroWorld headless{&gs, nullptr};
    macro_stock_apply(headless, MacroStock::TreeCount, MacroStockKey{-1, 0, 0}, -5);
    CHECK(macro_stock_read(headless, MacroStock::TreeCount, MacroStockKey{-1, 0, 0}) == 0,
          "a missing tree layer reads zero and swallows writes instead of crashing");
}

// ── The roster row: a squad's members are a stock like anyone else ─────────

// One squad the way the overworld shapes them: the entity IS the leader, the
// ordinal is its save-stable name, the roster holds everyone else.
entt::entity make_squad(sm::ecs::World& w, std::uint32_t ordinal,
                        std::initializer_list<std::uint32_t> memberIds) {
    const auto e = w.reg.create();
    w.reg.emplace<sm::ecs::MacroNpcRuntime>(e);
    w.reg.emplace<sm::ecs::MacroSpawnId>(e, ordinal);
    auto& roster = w.reg.emplace<sm::ecs::SquadRoster>(e);
    for (std::uint32_t id : memberIds) {
        roster.members.push_back(sm::make_soldier(
            std::uint8_t(sm::NPCType::Guard), 2, id));
    }
    return e;
}

// A member's death removes the very soldier who fell — named by the receipt,
// never "one of them" — and only from its own squad.
void test_the_roster_row_pays_by_name() {
    using namespace sm;
    ecs::World world;
    make_squad(world, 5, {11u, 22u, 0x80000021u});   // high-bit id: a garrison-
    const auto other = make_squad(world, 6, {77u});  // born soldier's shape
    MacroWorld w{nullptr, nullptr, &world};

    const MacroStockKey member11{5, 0, 0, 11};
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 3,
          "read reports how many members the squad actually holds");

    macro_stock_apply(w, MacroStock::Roster, member11, -1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 2,
          "a death removes exactly one member");
    macro_stock_apply(w, MacroStock::Roster, member11, -1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 2,
          "the same man cannot fall twice: a spent receipt moves nothing");

    // Ids are bit patterns: the high bit (garrison id space) must round-trip
    // through the signed detail field intact.
    macro_stock_apply(w, MacroStock::Roster,
                      MacroStockKey{5, 0, 0, std::int32_t(0x80000021u)}, -1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 1,
          "a high-bit entityId names its member exactly");

    CHECK(macro_stock_read(w, MacroStock::Roster, MacroStockKey{6, 0, 0}) == 1,
          "the squad next door never pays for this one's dead");

    // Fail closed, like every malformed receipt in this table.
    macro_stock_apply(w, MacroStock::Roster, MacroStockKey{5, 0, 0, -1}, -1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 1,
          "a nameless death removes nobody: the receipt names its member");
    macro_stock_apply(w, MacroStock::Roster, MacroStockKey{5, 0, 0, 22}, +1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 1,
          "a bare positive delta conjures nobody: recruitment brings real rows");
    macro_stock_apply(w, MacroStock::Roster, MacroStockKey{999, 0, 0, 22}, -1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 1
              && macro_stock_read(w, MacroStock::Roster,
                                  MacroStockKey{6, 0, 0}) == 1,
          "a receipt against an unknown squad moves no roster anywhere");

    // The full settle path — the same door a subworld death actually uses.
    entt::registry& reg = world.reg;
    const auto body = reg.create();
    stamp_macro_debt(reg, body, MacroStock::Roster,
                     MacroStockKey{5, 0, 0, 22}, 1);
    const auto* debt = reg.try_get<ecs::MacroDebt>(body);
    CHECK_OR_RETURN(debt != nullptr && debt->detail == 22,
                    "the receipt carries the member's name to the grave");
    settle_macro_debt(w, *debt, -1);
    CHECK(macro_stock_read(w, MacroStock::Roster, member11) == 0,
          "settling the receipt removes the named member");
    (void)other;
}

// Owner ruling 3: the survivors of a dead leader become deserters — once,
// when the fight ends, and never from a squad whose leader still stands.
void test_dead_leader_squads_fall_into_the_pool() {
    using namespace sm;
    ecs::World world;
    const auto fallen = make_squad(world, 10, {1u, 2u});
    make_squad(world, 11, {3u});
    world.reg.emplace<ecs::Dead>(fallen);

    SoldierSquad pool{};
    CHECK(drain_dead_leader_squads(world, pool) == 2,
          "the dead leader's survivors walk away, all of them");
    CHECK(total_soldiers(pool) == 2,
          "and they land in the deserter pool");
    MacroWorld w{nullptr, nullptr, &world};
    CHECK(macro_stock_read(w, MacroStock::Roster, MacroStockKey{10, 0, 0}) == 0,
          "the faceless squad is emptied: nothing left to pay twice");
    CHECK(macro_stock_read(w, MacroStock::Roster, MacroStockKey{11, 0, 0}) == 1,
          "a live leader keeps his men");

    CHECK(drain_dead_leader_squads(world, pool) == 0
              && total_soldiers(pool) == 2,
          "draining again pays nothing: the pool is never billed twice");
}

// The Trees row is a CARRIER row (resource_field.h): its live state is the
// dense grid the map renders, not a sparse scar map. The discipline that
// makes that safe for the 21 direct grid readers: every registry write lands
// in the grid AND moves its revision (the u_treeMap refresh driver), and the
// scar slot for Trees stays EMPTY — a scar there would be a second, silently
// diverging copy of the forest.
void test_trees_are_a_carrier_row() {
    using namespace sm;
    sm::GameState gs = make_world();
    sm::TreeLayer trees;
    trees.width = gs.mapW;
    trees.height = gs.mapH;
    trees.data.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH), 500);
    // Real terrain, so the daily-regrow walk below actually RUNS (it is
    // fail-closed without terrain, and a walk that never ran proves nothing).
    sm::TerrainData td;
    td.width = gs.mapW;
    td.height = gs.mapH;
    td.rgba.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH) * 4u, 128);
    MacroWorld w{&gs, &trees, nullptr, &td};

    const std::uint32_t rev0 = trees.revision;
    resource_field_apply(w, ResourceFieldId::Trees, 5, 6, -123);
    CHECK(int(trees.at(5, 6)) == 377,
          "a registry write lands in the very grid the renderer draws");
    CHECK(resource_field_read(w, ResourceFieldId::Trees, 5, 6) == 377,
          "the registry reads the same grid back");
    CHECK(trees.revision == rev0 + 1,
          "a registry write moves the grid revision (u_treeMap refresh)");
    CHECK(gs.resourceScars[std::size_t(ResourceFieldId::Trees)].empty(),
          "the Trees scar slot stays EMPTY - the grid is the only state");
    CHECK(resource_field_scar(gs, ResourceFieldId::Trees, 6u * 64u + 5u) == 0,
          "a carrier row reports no scar");

    // Behaviour frozen in Inc A: the daily heal walks sparse rows only —
    // a felled forest must NOT creep back until the growth law (Inc C)
    // arrives deliberately. (Also guards the null regrowPeriodDays row.)
    resource_fields_daily_regrow(w, /*day=*/32 * 64);
    CHECK(int(trees.at(5, 6)) == 377,
          "no silent regrowth: the felled cell holds until the growth law");
}

// The deposit rows (Clay/Iron/Stone) are carrier rows too: one registry
// door, per-kind maps underneath. What must hold: a write lands in ITS kind
// and no other, an absent vein refuses the write (mining invents no
// geology), and the scar slots stay empty.
void test_deposits_are_carrier_rows() {
    using namespace sm;
    sm::GameState gs = make_world();
    sm::DepositLayer deposits;
    deposits.width = gs.mapW;
    deposits.height = gs.mapH;
    deposits.cells[std::size_t(DepositKind::Stone)]
        [deposits.wrap_index(9, 9)] = 1000;
    deposits.cells[std::size_t(DepositKind::Iron)]
        [deposits.wrap_index(9, 9)] = 64;   // a vein IN the quarry
    MacroWorld w{&gs, nullptr, nullptr, nullptr, &deposits};

    CHECK(resource_field_read(w, ResourceFieldId::Stone, 9, 9) == 1000
              && resource_field_read(w, ResourceFieldId::Iron, 9, 9) == 64,
          "each kind's row reads its own map of the shared cell");

    resource_field_apply(w, ResourceFieldId::Iron, 9, 9, -60);
    CHECK(resource_field_read(w, ResourceFieldId::Iron, 9, 9) == 4
              && resource_field_read(w, ResourceFieldId::Stone, 9, 9) == 1000,
          "mining one kind never bleeds into the other kind's map");

    resource_field_apply(w, ResourceFieldId::Clay, 9, 9, +500);
    CHECK(resource_field_read(w, ResourceFieldId::Clay, 9, 9) == 0
              && deposits.cells[std::size_t(DepositKind::Clay)].empty(),
          "a kind the cell does not hold refuses the write: mining invents "
          "no geology");

    resource_field_apply(w, ResourceFieldId::Iron, 9, 9, -100);
    CHECK(resource_field_read(w, ResourceFieldId::Iron, 9, 9) == 0
              && deposits.cells[std::size_t(DepositKind::Iron)].count(
                     deposits.wrap_index(9, 9)),
          "an over-drained vein clamps to zero and stays a visible cell");

    for (std::size_t f : {std::size_t(ResourceFieldId::Clay),
                          std::size_t(ResourceFieldId::Iron),
                          std::size_t(ResourceFieldId::Stone)}) {
        CHECK(gs.resourceScars[f].empty(),
              "a deposit row's scar slot stays EMPTY - the cells are the "
              "only state");
    }
}

} // namespace

int main() {
    test_the_table_is_total();
    test_borrow_and_return_are_symmetric();
    test_stocks_are_bounded();
    test_debts_bill_their_own_subject();
    test_malformed_receipts_do_nothing();
    test_the_roster_row_pays_by_name();
    test_dead_leader_squads_fall_into_the_pool();
    test_trees_are_a_carrier_row();
    test_deposits_are_carrier_rows();
    return sm::test::report("macro_stock_test");
}

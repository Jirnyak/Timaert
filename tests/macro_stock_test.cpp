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

#include "macro/macro_stock.h"
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

} // namespace

int main() {
    test_the_table_is_total();
    test_borrow_and_return_are_symmetric();
    test_stocks_are_bounded();
    test_debts_bill_their_own_subject();
    test_malformed_receipts_do_nothing();
    return sm::test::report("macro_stock_test");
}

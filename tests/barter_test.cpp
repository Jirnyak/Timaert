// The package deal (owner ruling 2026-08-07): trade is barter by PACKAGE —
// both sides staged, ONE settlement, all-or-nothing. Pinned:
//   · barter_swap moves BOTH packages or neither, validated against the
//     PRE-DEAL bags — a short shelf on either side moves nothing;
//   · repeated ids in a package SUM for validation (no double-spend of one
//     stack across two lines);
//   · the same id may travel both ways (bread for bread is a legal deal);
//   · coin is not special: a currency row swaps like any ware, and item
//     totals CONSERVE across every deal — nothing is minted.
#include "check.h"

#include "macro/currency.h"

namespace {

using namespace sm;

void test_swap_moves_both_packages() {
    Inventory town, player;
    town.add("bread", 10);
    town.add("cloth", 4);
    player.add("wood", 6);
    player.add("coin_empire", 30);

    CHECK(barter_swap(player, town,
                      /*fromPlayer*/ {{"wood", 5}, {"coin_empire", 7}},
                      /*fromTown*/ {{"bread", 3}, {"cloth", 1}}),
          "a covered deal settles");
    CHECK(player.count("wood") == 1 && player.count("coin_empire") == 23,
          "the given lines left the player");
    CHECK(player.count("bread") == 3 && player.count("cloth") == 1,
          "the taken lines arrived");
    CHECK(town.count("bread") == 7 && town.count("cloth") == 3,
          "the town gave exactly the package");
    CHECK(town.count("wood") == 5 && town.count("coin_empire") == 7,
          "coin swaps like any ware - nothing minted, nothing lost");
}

void test_all_or_nothing() {
    Inventory a, b;
    a.add("bread", 2);
    b.add("wood", 8);

    CHECK(!barter_swap(a, b, {{"bread", 3}}, {{"wood", 1}}),
          "a short GIVE side refuses the whole deal");
    CHECK(!barter_swap(a, b, {{"bread", 1}}, {{"wood", 9}}),
          "a short TAKE side refuses the whole deal");
    CHECK(!barter_swap(a, b, {{"bread", 0}}, {{"wood", 1}}),
          "a non-positive line is not a deal");
    CHECK(a.count("bread") == 2 && b.count("wood") == 8
              && a.count("wood") == 0 && b.count("bread") == 0,
          "a refused deal moved NOTHING");
}

void test_repeated_lines_sum() {
    Inventory a, b;
    a.add("bread", 5);

    CHECK(!barter_swap(a, b, {{"bread", 3}, {"bread", 3}}, {}),
          "two lines of one stack SUM - 6 from 5 refuses");
    CHECK(a.count("bread") == 5, "and nothing moved");
    CHECK(barter_swap(a, b, {{"bread", 3}, {"bread", 2}}, {}),
          "5 from 5 across two lines settles");
    CHECK(a.count("bread") == 0 && b.count("bread") == 5,
          "both lines travelled");
}

void test_same_id_both_ways() {
    Inventory a, b;
    a.add("bread", 3);
    b.add("bread", 5);

    CHECK(barter_swap(a, b, {{"bread", 3}}, {{"bread", 5}}),
          "bread for bread is a legal deal");
    CHECK(a.count("bread") == 5 && b.count("bread") == 3,
          "the two stacks crossed whole");
}

void test_empty_packages_are_a_noop() {
    Inventory a, b;
    a.add("bread", 1);
    CHECK(barter_swap(a, b, {}, {}), "an empty deal is trivially settled");
    CHECK(a.count("bread") == 1 && b.count("bread") == 0, "and moved nothing");
}

} // namespace

int main() {
    test_swap_moves_both_packages();
    test_all_or_nothing();
    test_repeated_lines_sum();
    test_same_id_both_ways();
    test_empty_packages_are_a_noop();
    return sm::test::report("barter_test");
}

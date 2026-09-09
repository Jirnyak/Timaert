// The package deal (owner ruling 2026-08-07; PER-STACK lines, owner verdict
// 2026-09-07 «торговля пер-стак»): trade is barter by PACKAGE — both sides
// staged, ONE settlement, all-or-nothing. Lines are keyed by SLOT, so a
// rolled instance and its bare twin are two lines with two prices. Pinned:
//   · barter_swap moves BOTH packages or neither, validated against the
//     PRE-DEAL bags — a short shelf on either side moves nothing;
//   · repeated lines of one SLOT sum for validation (no double-spend of one
//     stack across two lines);
//   · the same ware may travel both ways (bread for bread is a legal deal);
//   · coin is not special: a currency row swaps like any ware, and item
//     totals CONSERVE across every deal — nothing is minted;
//   · a staged ROLLED stack travels AS ITSELF (seed + affixes), never as
//     «a sword by id» — the Wear-bug class, now impossible by key shape;
//   · a slot re-filled with a DIFFERENT row refuses the stale line (def
//     rides the BarterLine as the sanity byte).
#include "check.h"

#include "macro/currency.h"

namespace {

using namespace sm;

// The staging panels stand on the slot they drew; a test finds it the same
// honest way — by looking.
int slot_of(const Inventory& inv, const char* id) {
    const int def = item_index(id);
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        if (!inv.slots[std::size_t(i)].empty()
            && inv.slots[std::size_t(i)].def == std::uint16_t(def)) {
            return i;
        }
    }
    return -1;
}

BarterLine line(const Inventory& inv, const char* id, int n) {
    const int s = slot_of(inv, id);
    return BarterLine{s, n, std::uint16_t(item_index(id))};
}

void test_swap_moves_both_packages() {
    Inventory town, player;
    town.add("bread", 10);
    town.add("cloth", 4);
    player.add("wood", 6);
    player.add("coin_empire", 30);

    CHECK(barter_swap(player, town,
                      /*fromPlayer*/ {line(player, "wood", 5),
                                      line(player, "coin_empire", 7)},
                      /*fromTown*/ {line(town, "bread", 3),
                                    line(town, "cloth", 1)}),
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

    CHECK(!barter_swap(a, b, {line(a, "bread", 3)}, {line(b, "wood", 1)}),
          "a short GIVE side refuses the whole deal");
    CHECK(!barter_swap(a, b, {line(a, "bread", 1)}, {line(b, "wood", 9)}),
          "a short TAKE side refuses the whole deal");
    CHECK(!barter_swap(a, b, {line(a, "bread", 0)}, {line(b, "wood", 1)}),
          "a non-positive line is not a deal");
    CHECK(a.count("bread") == 2 && b.count("wood") == 8
              && a.count("wood") == 0 && b.count("bread") == 0,
          "a refused deal moved NOTHING");
}

void test_repeated_lines_sum() {
    Inventory a, b;
    a.add("bread", 5);

    CHECK(!barter_swap(a, b,
                       {line(a, "bread", 3), line(a, "bread", 3)}, {}),
          "two lines of one stack SUM - 6 from 5 refuses");
    CHECK(a.count("bread") == 5, "and nothing moved");
    CHECK(barter_swap(a, b, {line(a, "bread", 3), line(a, "bread", 2)}, {}),
          "5 from 5 across two lines settles");
    CHECK(a.count("bread") == 0 && b.count("bread") == 5,
          "both lines travelled");
}

void test_same_ware_both_ways() {
    Inventory a, b;
    a.add("bread", 3);
    b.add("bread", 5);

    CHECK(barter_swap(a, b, {line(a, "bread", 3)}, {line(b, "bread", 5)}),
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

// ── The rolled instance trades AS ITSELF (verdict 2026-09-07) ─────────────
void test_rolled_stack_travels_whole() {
    Inventory seller, buyer;
    ItemRef bare{};
    bare.def = std::uint16_t(item_index("wpn_sword"));
    bare.count = 1;
    ItemRef rolled = bare;
    rolled.seed = 99;
    rolled.set_affix(0, {std::uint8_t(BonusId::Str), 2});
    CHECK(seller.add_ref(bare) && seller.add_ref(rolled),
          "the bare blade and its rolled twin sit in two slots of one id");
    CHECK(seller.used_slots() == 2, "the stacking law kept them apart");

    CHECK(value_of(rolled) > value_of(bare),
          "the rolled instance is worth MORE than its bare row");
    CHECK(inventory_value(seller) == value_of(bare) + value_of(rolled),
          "the bag's universal value is the sum of its instances, not "
          "count x bare row");

    // Stage the ROLLED slot; the bare one stays.
    const int rolledSlot =
        seller.slots[0].seed == 99u ? 0 : 1;
    CHECK(barter_swap(seller, buyer,
                      {BarterLine{rolledSlot, 1, rolled.def}}, {}),
          "the rolled line settles");
    CHECK(buyer.used_slots() == 1
              && buyer.slots[std::size_t(slot_of(buyer, "wpn_sword"))]
                     .affix_at(0).value == 2,
          "the AFFIX arrived with the blade - it travelled as itself");
    CHECK(seller.count("wpn_sword") == 1
              && seller.slots[std::size_t(slot_of(seller, "wpn_sword"))]
                         .seed == 0u,
          "and the bare twin stayed exactly where it was");
}

// A shelf slot re-filled with a DIFFERENT row under an open panel: the
// stale line refuses instead of shipping the stranger.
void test_stale_slot_refuses() {
    Inventory a, b;
    a.add("bread", 2);
    const BarterLine stale = line(a, "bread", 1);
    a = Inventory{};
    a.add("wood", 5);   // wood now sits where bread sat
    CHECK(!barter_swap(a, b, {stale}, {}),
          "a re-filled slot refuses the stale line");
    CHECK(a.count("wood") == 5 && b.total() == 0, "and nothing moved");
}

} // namespace

int main() {
    test_swap_moves_both_packages();
    test_all_or_nothing();
    test_repeated_lines_sum();
    test_same_ware_both_ways();
    test_empty_packages_are_a_noop();
    test_rolled_stack_travels_whole();
    test_stale_slot_refuses();
    return sm::test::report("barter_test");
}

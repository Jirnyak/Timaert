// THE matter law's witness (owner verdicts 2026-09-11, CANON «Крафт/Скрап»).
// Composition lives on the catalog row (item_parts) and feeds four consumers
// — production, craft, scrap, auto-scrap. Pinned here:
//   · parts reference ONLY terminal rows (one-step reaction, no recursion),
//     with positive counts inside kMaxItemParts;
//   · coverage: every wearable and every produced-tier commodity carries a
//     composition; raw commodities, coins and monster matter stay terminal;
//   · NO-ARBITRAGE: a white base is worth at least its own scrap return —
//     the perpetual money machine (buy → scrap → sell) is impossible at any
//     stall by table law;
//   · the reaction doors: craft eats exactly the composition and emits a
//     WHITE base (seed 0, no affixes); scrap returns floor(count/2) per part
//     («крафт 2 железа → разбор 1 железо»), burns seed and affixes, refuses
//     terminals; both are all-or-nothing (a refused act leaves the bag
//     byte-identical, CANON S5);
//   · the material byte (1 + raw commodity row) substitutes part 0 at scrap;
//   · the craft door refuses coin (чеканка = право двора, CANON S10);
//   · auto_scrap_overflow frees slots down to the half mark, cheapest
//     non-fungible first, and never touches plain (fungible) stacks.
#include "check.h"

#include "macro/commodity.h"
#include "macro/currency.h"
#include "macro/items.h"

#include <cstring>

namespace {

using namespace sm;

bool is_terminal(int defIdx) { return item_parts(defIdx).empty(); }

// Plain white instance of a row — the craft door's promised output shape.
ItemRef plain(const char* id, int n) {
    ItemRef r{};
    r.def = std::uint16_t(item_index(id));
    r.count = n;
    return r;
}

int slot_of(const Inventory& inv, int defIdx) {
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        if (!inv.slots[std::size_t(i)].empty()
            && inv.slots[std::size_t(i)].def == std::uint16_t(defIdx)) {
            return i;
        }
    }
    return -1;
}

void test_table_laws() {
    const auto catalog = item_catalog();
    int rowsWithParts = 0;
    for (int i = 0; i < int(catalog.size()); ++i) {
        const auto parts = item_parts(i);
        if (!parts.empty()) ++rowsWithParts;
        CHECK(int(parts.size()) <= kMaxItemParts,
              "a row's parts fit the 4-slot ceiling");
        long scrapReturn = 0;
        for (const ItemPart& p : parts) {
            CHECK(int(p.count) > 0, "a part states a positive count");
            const ItemDef* mat = item_def_at(int(p.def));
            CHECK(mat != nullptr, "a part names a real catalog row");
            // One-step reaction: matter is terminal, so no chain and no
            // recursion can exist to be guarded elsewhere.
            CHECK(is_terminal(int(p.def)),
                  "parts reference ONLY terminal rows");
            if (mat) scrapReturn += long(int(p.count) / 2) * mat->value;
        }
        // NO-ARBITRAGE: white base value covers its own halved return.
        if (!parts.empty()) {
            CHECK(long(catalog[std::size_t(i)].value) >= scrapReturn,
                  "white base is worth >= its scrap return (no money machine)");
        }
        // Coverage by TYPE: what is worn is made of something.
        if (catalog[std::size_t(i)].slotMask != 0) {
            CHECK(!parts.empty(), "every wearable row carries a composition");
        }
    }
    CHECK(rowsWithParts > 0, "the matter table measured something");

    // Coverage by TIER: the economy's produced goods are made of matter; raw
    // rows ARE the matter (terminal, «состоит из себя»).
    for (int c = 0; c < kCommodityCount; ++c) {
        const int idx = item_index(kCommodities[c].id);
        CHECK(idx >= 0, "every commodity resolves in the catalog");
        if (kCommodities[c].tier == CommodityTier::Raw) {
            CHECK(is_terminal(idx), "raw commodity rows are terminal");
        } else {
            CHECK(!is_terminal(idx), "produced goods carry a composition");
        }
    }
    // Coins are terminal for the universal scrap: melting is the future
    // reverse of the MINT recipe (CANON S10), never the scrap door's.
    for (const CurrencyDef& c : kCurrencyDefs) {
        CHECK(is_terminal(item_index(c.itemId)), "coin rows are terminal");
        CHECK(item_is_currency(item_index(c.itemId)),
              "the coin gate knows its rows");
    }
    CHECK(!item_is_currency(item_index("bread")),
          "the coin gate refuses nothing else (negative control)");
    CHECK(item_parts(-1).empty() && item_parts(1 << 14).empty(),
          "out-of-catalog ordinals answer terminal, never UB");
}

void test_craft_door() {
    const int sword = item_index("wpn_sword");
    const int iron = item_index("iron");
    Inventory inv;
    inv.add("iron", 3);

    // Full price in, white base out.
    CHECK(craft_item(inv, sword, 1), "2 iron craft a sword");
    CHECK(inv.count("iron") == 1, "craft ate exactly the composition");
    const int s = slot_of(inv, sword);
    CHECK(s >= 0, "the sword landed");
    if (s >= 0) {
        const ItemRef& r = inv.slots[std::size_t(s)];
        CHECK(r.seed == 0 && affix_count(r) == 0 && r.material == 0,
              "craft emits a WHITE base (закон нулевых аффиксов)");
    }

    // Refusals are all-or-nothing: the bag stays byte-identical.
    const Inventory before = inv;
    CHECK(!craft_item(inv, sword, 1), "1 iron cannot craft a 2-iron sword");
    CHECK(!craft_item(inv, iron, 1), "terminal rows cannot be crafted");
    inv.add("silver", 8);
    CHECK(!craft_item(inv, item_index("coin_empire"), 1),
          "the bench never strikes coin — чеканка is the landmark's right");
    inv.remove("silver", 8);
    CHECK(std::memcmp(&before, &inv, sizeof(Inventory)) == 0,
          "a refused craft leaves the bag untouched (negative control)");
}

void test_scrap_door() {
    const int sword = item_index("wpn_sword");
    Inventory inv;
    inv.add_of(sword, 1);
    const int s = slot_of(inv, sword);

    // floor(2/2) = 1 iron back; the canon example, by arithmetic.
    CHECK(scrap_at(inv, s, 1), "a sword scraps");
    CHECK(inv.count("iron") == 1, "scrap returned floor(2/2) = 1 iron");
    CHECK(inv.count_of(sword) == 0, "the scrapped sword is gone");

    // A 1-count part burns whole («рукоять сгорает»): dagger = 1 iron → 0.
    inv.clear();
    inv.add("wpn_dagger", 1);
    CHECK(scrap_at(inv, slot_of(inv, item_index("wpn_dagger")), 1),
          "a dagger scraps even when everything burns");
    CHECK(inv.count("iron") == 0, "floor(1/2) = 0: the unit burnt to slag");
    CHECK(inv.used_slots() == 0, "the slot was freed regardless");

    // Affixes burn with no return: the rolled instance pays the same iron.
    inv.clear();
    ItemRef rolled = plain("wpn_sword", 1);
    rolled.seed = 77;
    rolled.set_affix(0, Bonus{std::uint8_t(BonusId::Str), 4});
    inv.add_ref(rolled);
    CHECK(value_of(rolled) > item_def("wpn_sword")->value,
          "the fixture's affix is worth money (negative control)");
    CHECK(scrap_at(inv, slot_of(inv, sword), 1), "a rolled sword scraps");
    CHECK(inv.count("iron") == 1 && inv.used_slots() == 1,
          "the affix burnt: same 1 iron as the bare twin (запрет реролла)");

    // The material byte substitutes part 0: a "stone" sword returns stone.
    inv.clear();
    ItemRef stoneSword = plain("wpn_sword", 1);
    int stoneRaw = -1;
    for (int c = 0; c < kRawCommodityCount; ++c) {
        if (std::strcmp(kCommodities[c].id, "stone") == 0) stoneRaw = c;
    }
    CHECK(stoneRaw >= 0, "stone is a raw commodity row (fixture)");
    stoneSword.material = std::uint8_t(1 + stoneRaw);
    inv.add_ref(stoneSword);
    CHECK(scrap_at(inv, slot_of(inv, sword), 1), "a variant sword scraps");
    CHECK(inv.count("stone") == 1 && inv.count("iron") == 0,
          "the instance material substituted part 0 (steel law)");

    // Terminals refuse: raw matter has no reverse.
    inv.clear();
    inv.add("iron", 4);
    const Inventory before = inv;
    CHECK(!scrap_at(inv, slot_of(inv, item_index("iron")), 1),
          "terminal rows refuse the scrap door");
    CHECK(std::memcmp(&before, &inv, sizeof(Inventory)) == 0,
          "a refused scrap leaves the bag untouched (negative control)");
}

void test_auto_scrap() {
    const int dagger = item_index("wpn_dagger");
    const int statue = item_index("statue");
    Inventory inv;
    // 130 distinct rolled daggers (each seed its own stack, the exact
    // non-fungible clog CANON describes), one dear rolled statue, and a plain
    // bread pile that must survive untouched.
    for (int i = 0; i < 130; ++i) {
        ItemRef r{};
        r.def = std::uint16_t(dagger);
        r.count = 1;
        r.seed = std::uint32_t(1000 + i);
        inv.add_ref(r);
    }
    ItemRef dear{};
    dear.def = std::uint16_t(statue);
    dear.count = 1;
    dear.seed = 7;
    inv.add_ref(dear);
    inv.add("bread", 40);
    const int used = inv.used_slots();
    CHECK(used > kAutoScrapSlots, "the fixture overflows (negative control)");

    const int scrapped = auto_scrap_overflow(inv);
    CHECK(scrapped > 0, "the overflow law acted");
    CHECK(inv.used_slots() <= kAutoScrapSlots,
          "the container came back to the half mark");
    CHECK(inv.count("iron") == 0,
          "cheap daggers melt to nothing: floor(1/2) burns the unit");
    CHECK(inv.count("bread") == 40, "plain stacks are never candidates");
    CHECK(inv.count_of(statue) == 1,
          "cheapest-first: the dear statue outlived every cheap dagger");

    // Below the mark the law is silent.
    Inventory calm;
    calm.add("bread", 10);
    CHECK(auto_scrap_overflow(calm) == 0 && calm.count("bread") == 10,
          "under half occupancy nothing is touched (negative control)");
}

} // namespace

int main() {
    test_table_laws();
    test_craft_door();
    test_scrap_door();
    test_auto_scrap();
    return sm::test::report("item_parts_test");
}

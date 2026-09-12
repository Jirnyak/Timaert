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
        const int yield = item_yield(i);
        CHECK(yield >= 1, "a row's yield is at least one item per batch");
        // LABOUR pairs with matter (owner 2026-09-12, «единая SP-система
        // труда»): what is made by work states its tempo; raw matter is not
        // made at all — so the hand's SP price (bar/labour) and the city's
        // day (labour × workers) can never divide by zero or invent work.
        if (parts.empty()) {
            CHECK(item_labour(i) == 0, "terminal rows carry no labour");
        } else {
            CHECK(item_labour(i) >= 1, "every made row states its labour");
        }
        long batchMatterValue = 0;
        for (const ItemPart& p : parts) {
            CHECK(int(p.count) > 0, "a part states a positive count");
            const ItemDef* mat = item_def_at(int(p.def));
            CHECK(mat != nullptr, "a part names a real catalog row");
            // One-step reaction: matter is terminal, so no chain and no
            // recursion can exist to be guarded elsewhere.
            CHECK(is_terminal(int(p.def)),
                  "parts reference ONLY terminal rows");
            if (mat) batchMatterValue += long(p.count) * mat->value;
        }
        // NO-ARBITRAGE, closed form over the POOLED entropy law: a scrap of
        // any n returns at most n×count/(2×yield) of each part, so the
        // return's value never exceeds items' value as long as half the
        // batch's matter value fits inside the batch's own price.
        if (!parts.empty()) {
            CHECK(batchMatterValue
                      <= 2L * yield * catalog[std::size_t(i)].value,
                  "half a batch's matter value fits its price (no money "
                  "machine at any n)");
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
    // Coins are ORDINARY matter (owner 2026-09-12, «БАРТЕРНАЯ ЭКОНОМИКА»):
    // every currency row carries the mint's composition and a >1 yield, and
    // the reaction is VALUE-NEUTRAL — striking coin creates nothing, so no
    // gate needs to exist (the static_assert beside the table pins the same
    // fact at compile time; this is the runtime belt over those braces).
    for (const CurrencyDef& c : kCurrencyDefs) {
        const int idx = item_index(c.itemId);
        const auto coinParts = item_parts(idx);
        CHECK(!coinParts.empty(), "a coin row carries the mint composition");
        CHECK(item_yield(idx) > 1, "a coin's batch strikes many from one");
        long in = 0;
        for (const ItemPart& p : coinParts) {
            const ItemDef* mat = item_def_at(int(p.def));
            if (mat) in += long(p.count) * mat->value;
        }
        CHECK(in == long(item_yield(idx))
                        * item_def_at(idx)->value,
              "the mint is value-neutral: batch matter == batch nominal");
    }
    CHECK(item_parts(-1).empty() && item_parts(1 << 14).empty(),
          "out-of-catalog ordinals answer terminal, never UB");
    CHECK(item_yield(-1) == 1 && item_yield(1 << 14) == 1,
          "out-of-catalog yield answers one, never UB");
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
    CHECK(std::memcmp(&before, &inv, sizeof(Inventory)) == 0,
          "a refused craft leaves the bag untouched (negative control)");

    // The MINT is this same door (owner 2026-09-12: «город делает монеты
    // через систему крафта» — and a hand may too; value-neutral, so a forge
    // mints nothing a market didn't already price): one batch of silver
    // strikes the row's whole yield.
    Inventory mint;
    mint.add("silver", 1);
    const int coin = item_index("coin_empire");
    CHECK(craft_item(mint, coin, 1), "one silver strikes a batch of coin");
    CHECK(mint.count("coin_empire") == item_yield(coin),
          "the batch struck exactly the row's yield");
    CHECK(mint.count("silver") == 0, "the metal was consumed whole");
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

    // A lone 1-count part burns whole («рукоять сгорает»): dagger = 1 iron
    // → 0 — but the entropy POOLS (owner 2026-09-12): TWO daggers scrapped
    // together are 2 iron of matter, and half of that survives.
    inv.clear();
    inv.add("wpn_dagger", 1);
    CHECK(scrap_at(inv, slot_of(inv, item_index("wpn_dagger")), 1),
          "a dagger scraps even when everything burns");
    CHECK(inv.count("iron") == 0, "floor(1/2) = 0: the unit burnt to slag");
    CHECK(inv.used_slots() == 0, "the slot was freed regardless");
    inv.clear();
    inv.add("wpn_dagger", 2);
    CHECK(scrap_at(inv, slot_of(inv, item_index("wpn_dagger")), 2),
          "two daggers scrap as one pool");
    CHECK(inv.count("iron") == 1,
          "pooled entropy: floor(2x1/2) = 1 iron survives");

    // COINS melt through this same door (owner 2026-09-12: no coin special
    // case): 64 coins embody 2 silver of matter, half survives; a single
    // coin is 1/32 silver and honestly burns to slag.
    inv.clear();
    inv.add("coin_empire", 64);
    const int coin = item_index("coin_empire");
    CHECK(scrap_at(inv, slot_of(inv, coin), 64), "a coin pile melts");
    CHECK(inv.count("silver") == 1 && inv.count("coin_empire") == 0,
          "64 coins -> floor(64x1/(2x32)) = 1 silver");
    inv.clear();
    inv.add("coin_empire", 1);
    CHECK(scrap_at(inv, slot_of(inv, coin), 1), "one coin may still melt");
    CHECK(inv.count("silver") == 0 && inv.count("coin_empire") == 0,
          "one coin is 1/32 silver: it burns whole to slag");

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

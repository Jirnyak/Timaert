// Every homeland the intro offers must lead to a real country.
//
// The defect it is bought with: "Barbarian Kingdoms" stored the value
// "barbarians", the faction registry has no such row (it has four —
// barbarian_north/south/west/east), and add_player_reputation was handed a name
// nothing answered to. One of the three opening buttons of the game awarded
// nothing, silently, for as long as it had been on screen. No test could see it
// because no test ever asked the registry whether the choices were real.
//
// So this asserts the TOTALITY the intro promises — every offered homeland
// resolves — plus the two properties of the group resolution the owner asked
// for: the world picks one of the group's own realms, and it picks the SAME one
// every time for a given world, because a reload must not move your birthplace.
#include "check.h"

#include "content/plot/intro.h"
#include "macro/faction.h"

#include <cstring>
#include <string_view>

namespace {

// The choices as the intro screen offers them. Kept as a literal list rather
// than read out of kRealmChoices: a test that walks the same table the code
// walks would pass on an intro that offers nothing at all.
constexpr const char* kOfferedHomelands[] = {"magika", "empire", "barbarians"};

// The registry rows "Barbarian Kingdoms" is allowed to resolve to.
constexpr const char* kBarbarianRealms[] = {
    "barbarian_north", "barbarian_south", "barbarian_west", "barbarian_east",
};

bool is_barbarian_realm(const char* id) {
    for (const char* r : kBarbarianRealms)
        if (id && std::strcmp(id, r) == 0) return true;
    return false;
}

void test_every_offered_homeland_is_real() {
    for (const char* choice : kOfferedHomelands) {
        const char* realm =
            sm::content::resolve_homeland_faction(choice, 12345u);
        CHECK(realm != nullptr,
              "every homeland the intro offers must resolve to a realm");
        if (!realm) continue;
        CHECK(sm::faction_index(realm) >= 0,
              "a resolved homeland must be a row of the faction registry");
    }
}

// A single-realm choice is passed through untouched — the group machinery must
// not start renaming countries that were already countries.
void test_a_plain_realm_passes_through() {
    const char* realm = sm::content::resolve_homeland_faction("empire", 7u);
    CHECK(realm != nullptr && std::strcmp(realm, "empire") == 0,
          "a choice that already names a realm must resolve to itself");
}

// The owner's ruling: the barbarian kingdoms are procedural, so the WORLD picks
// which one raised you — but it must pick one of THEIRS.
void test_the_group_resolves_within_itself() {
    int seen = 0;
    for (std::uint32_t seed = 1; seed <= 64u; ++seed) {
        const char* realm =
            sm::content::resolve_homeland_faction("barbarians", seed);
        CHECK(is_barbarian_realm(realm),
              "a barbarian homeland must resolve to a barbarian realm");
        ++seen;
    }
    CHECK(seen == 64, "the sampling loop must actually have run");
}

// ...and it must pick the SAME one every time for one world, or a reload would
// move the player's birthplace under him.
void test_one_world_answers_the_same_way_twice() {
    int compared = 0;
    for (std::uint32_t seed = 1; seed <= 64u; ++seed) {
        const char* a = sm::content::resolve_homeland_faction("barbarians", seed);
        const char* b = sm::content::resolve_homeland_faction("barbarians", seed);
        CHECK(a && b && std::strcmp(a, b) == 0,
              "one world must always name the same homeland");
        ++compared;
    }
    CHECK(compared == 64, "the determinism loop must actually have run");
}

// NEGATIVE CONTROL. Everything above would also pass if the resolver simply
// answered "barbarian_north" to absolutely anything. Two proofs that it does
// not: a name the registry has never heard of is REFUSED rather than
// substituted, and different worlds do not all give the same answer.
void test_the_resolver_can_say_no_and_can_disagree() {
    CHECK(sm::content::resolve_homeland_faction("not_a_place", 1u) == nullptr,
          "an unknown homeland must resolve to nothing, not to a guess");
    CHECK(sm::content::resolve_homeland_faction(nullptr, 1u) == nullptr,
          "a null homeland must resolve to nothing");
    CHECK(sm::content::resolve_homeland_faction("", 1u) == nullptr,
          "an empty homeland must resolve to nothing");

    const char* first = sm::content::resolve_homeland_faction("barbarians", 1u);
    bool sawAnother = false;
    for (std::uint32_t seed = 2; seed <= 256u && !sawAnother; ++seed) {
        const char* r = sm::content::resolve_homeland_faction("barbarians", seed);
        if (r && first && std::strcmp(r, first) != 0) sawAnother = true;
    }
    CHECK(sawAnother,
          "different worlds must be able to raise the player in different "
          "kingdoms — otherwise the pick is a constant wearing a seed");
}

} // namespace

int main() {
    test_every_offered_homeland_is_real();
    test_a_plain_realm_passes_through();
    test_the_group_resolves_within_itself();
    test_one_world_answers_the_same_way_twice();
    test_the_resolver_can_say_no_and_can_disagree();
    return sm::test::report("homeland_choice_test");
}

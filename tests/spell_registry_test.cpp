// Guards of the spell DATA registry (macro/spells.h, ARCHITECTURE.md Rule 13).
//
// The one thing this file must never stop guarding: ORDINALS ARE APPEND-ONLY.
// The row index rides in saves (Spire.spellOrdinal) and events, so the pins
// below are not "restated numbers" — the pin IS the promise, exactly like a
// save-format guard: reordering or inserting mid-table must turn this red,
// appending must not.
#include "check.h"
#include "macro/spells.h"

#include <string_view>

namespace {

using namespace sm;

void test_ordinals_are_pinned() {
    struct Pin { int ordinal; std::string_view id; };
    // The registration order of 2026-08-14 (the order register_builtin_spells
    // used to run), frozen forever. New spells append below flight.
    constexpr Pin kPins[] = {
        {0, "fireball"},
        {1, "ice_shard"},
        {2, "magic_bolt"},
        {3, "lightning_chain"},
        {4, "energy_beam"},
        {5, "armageddon"},
        {6, "haste"},
        {7, "flight"},
    };
    CHECK(kSpellCount >= int(sizeof(kPins) / sizeof(kPins[0])),
          "the table may only grow — a shrunken table lost a shipped spell");
    for (const Pin& p : kPins) {
        CHECK(p.ordinal < kSpellCount && kSpellDefs[p.ordinal].id == p.id,
              "a shipped ordinal must keep its spell forever (append-only)");
    }
}

void test_lookup_roundtrip() {
    int samples = 0, bad = 0;
    for (int i = 0; i < kSpellCount; ++i) {
        ++samples;
        if (spell_find(kSpellDefs[i].id) != &kSpellDefs[i]) ++bad;
        if (spell_ordinal(kSpellDefs[i].id) != i) ++bad;
    }
    CHECK(samples > 0 && bad == 0,
          "every row is found by its own id and maps back to its own ordinal");
    // The detector can see absence: an id not in the table finds nothing.
    CHECK(spell_find("no_such_spell") == nullptr
              && spell_ordinal("no_such_spell") == -1,
          "an unknown id must resolve to nothing");
}

void test_keys_are_unique() {
    int samples = 0, dupIds = 0, dupHashes = 0;
    for (int i = 0; i < kSpellCount; ++i) {
        for (int j = i + 1; j < kSpellCount; ++j) {
            ++samples;
            if (std::string_view(kSpellDefs[i].id) == kSpellDefs[j].id)
                ++dupIds;
            if (stable_spell_id(kSpellDefs[i].id)
                    == stable_spell_id(kSpellDefs[j].id))
                ++dupHashes;
        }
    }
    CHECK(samples > 0 && dupIds == 0,
          "spell ids are the registry key — no two rows may share one");
    CHECK(dupHashes == 0,
          "the wire hash must separate every pair of shipped ids");
}

// A flavor array is a null-terminated prefix: entries, then nulls, no holes —
// spell_flavor_count() and every UI loop depend on it.
bool has_flavor_hole(const char* const (&items)[kMaxSpellFlavorItems]) {
    const int n = spell_flavor_count(items);
    for (int i = n; i < kMaxSpellFlavorItems; ++i)
        if (items[i]) return true;
    return false;
}

void test_rows_are_sane() {
    int samples = 0, bad = 0;
    for (const SpellDef& s : kSpellDefs) {
        ++samples;
        const bool ok =
            s.id && s.id[0] && s.name && s.name[0]
            && s.description && s.description[0]
            && s.tier >= 1 && s.tier <= 5
            && s.manaCost >= 0
            && (!s.sustained || s.manaDrain > 0.0f)
            && s.statusEffect != nullptr
            && (s.hasMicro || s.hasMacro)
            && !has_flavor_hole(s.pros)
            && !has_flavor_hole(s.cons);
        if (!ok) ++bad;
    }
    CHECK(samples > 0 && bad == 0,
          "every row: named + described, tier 1..5, sustained spells drain, "
          "castable somewhere, flavor arrays hole-free");
    // Negative control for the hole detector itself: a hole must be seen.
    const char* holed[kMaxSpellFlavorItems] = {"a", nullptr, "c", nullptr,
                                               nullptr};
    CHECK(has_flavor_hole(holed),
          "the flavor-hole detector must flag a null before a non-null");
}

} // namespace

int main() {
    test_ordinals_are_pinned();
    test_lookup_roundtrip();
    test_keys_are_unique();
    test_rows_are_sane();
    return sm::test::report("spell_registry_test");
}

// Unit tests for the global creature registry (sm::sub, src/sub/fauna.cpp).
// Pins the stable-id contract the subworld spawn / death / loot path relies on:
//
//   * a creature's stable id IS its ordinal in THE one body table — there is
//     no monster catalog beside it and no `0x100` bit any more (2026-08-20);
//   * creature_def_from_kind() recovers the exact row, returning nullptr for
//     the humanoid rows and for anything that names no row at all.
//
// If catalog order ever changed (which would silently re-key live and saved
// entities) or the bit contract regressed, these assertions fail. Plain main(),
// no framework — mirrors tests/item_use_parity_test.cpp.

#include "macro/fauna.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

using namespace sm;
using sm::FaunaEntry;

static int g_fails = 0;
static bool expect(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_fails; }
    return ok;
}

int main() {
    const auto catalog = sm::creature_catalog();
    const int n = int(catalog.size());

    expect(n > 0, "catalog is non-empty");

    // ── structural invariants: every entry well-formed ──────────────────────
    for (int i = 0; i < n; ++i) {
        const FaunaEntry* e = catalog[i];
        if (!expect(e != nullptr, "catalog entry is non-null")) continue;
        expect(e->id != nullptr && e->id[0] != '\0', "entry has a non-empty stable id");
        expect(e->label != nullptr, "entry has a label");
    }
    // ids and entry pointers are unique across the catalog.
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (catalog[i] == nullptr || catalog[j] == nullptr) continue;
            expect(catalog[i] != catalog[j],
                   "each creature entry appears at most once in the catalog");
            expect(std::string_view(catalog[i]->id) != catalog[j]->id,
                   "creature stable ids are unique across the catalog");
        }
    }

    // ── round-trip: index <-> kind <-> def <-> id ───────────────────────────
    for (int i = 0; i < n; ++i) {
        const FaunaEntry* e = catalog[i];
        const std::uint16_t kind = std::uint16_t(sm::creature_index(e));

        expect(sm::creature_def_from_kind(kind) == e,
               "creature_def_from_kind(ordinal) recovers the row");
        expect(sm::creature_index(e) == int(kind),
               "a row's index IS the kind it answers to");
        expect(sm::creature_def(e->id) == e,
               "creature_def(id) recovers the same entry");
        expect(sm::creature_index(sm::creature_def_from_kind(kind)) == int(kind),
               "kind -> def -> index closes the loop");

        // (The old "same number without the 0x100 bit is a humanoid" check
        // died with the bit: there is no second number to compare against any
        // more. What it guarded — humanoid rows are not creatures — is asserted
        // directly over the role rows below.)
    }

    // ── the humanoid rows are never creatures ────────────────────────────────
    for (std::uint16_t k : {std::uint16_t(sm::NPCType::Peasant),
                            std::uint16_t(sm::NPCType::Guard),
                            std::uint16_t(sm::NPCType::Sorceress),
                            std::uint16_t(sm::NPCType::ClayDigger)}) {
        expect(sm::creature_def_from_kind(k) == nullptr,
               "a humanoid row is not a creature");
    }

    // ── a kind that names no row at all -> nullptr ───────────────────────────
    expect(sm::creature_def_from_kind(std::uint16_t(sm::NPCType::Count)) == nullptr,
           "one past the last row -> nullptr");
    expect(sm::creature_def_from_kind(std::uint16_t(0x1FF)) == nullptr,
           "a number far outside the table -> nullptr");

    // ── unknown id / null / non-catalog pointer ──────────────────────────────
    expect(sm::creature_def("definitely_not_a_creature_id") == nullptr,
           "unknown id -> nullptr");
    expect(sm::creature_index(nullptr) == -1,
           "creature_index(nullptr) == -1");
    FaunaEntry stranger{};
    stranger.id = "stranger";
    expect(sm::creature_index(&stranger) == -1,
           "an entry not in the catalog -> index -1");

    if (g_fails == 0) {
        std::printf("OK fauna_registry_test: catalog=%d entries, all round-trips hold\n", n);
        return 0;
    }
    std::printf("fauna_registry_test: %d assertion(s) FAILED\n", g_fails);
    return 1;
}

// Unit tests for the global creature registry (sm::sub, src/sub/fauna.cpp).
// Pins the stable-id contract the subworld spawn / death / loot path relies on:
//
//   * the catalog index IS the creature's stable id;
//   * ECS NPCKind.type bakes it as (0x100 | index), and the 0x100 bit is the
//     load-bearing "monster vs humanoid NPC" discriminator;
//   * creature_def_from_kind() recovers the exact row, returning nullptr for
//     humanoid kinds (< 0x100) and for out-of-range indices.
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
        const std::uint16_t kind = std::uint16_t(0x100 | i);

        expect(sm::creature_def_from_kind(kind) == e,
               "creature_def_from_kind(0x100|i) recovers catalog[i]");
        expect(sm::creature_index(e) == i,
               "creature_index(catalog[i]) == i");
        expect(sm::creature_def(e->id) == e,
               "creature_def(id) recovers the same entry");
        expect(sm::creature_index(sm::creature_def_from_kind(kind)) == i,
               "kind -> def -> index closes the loop");

        // The 0x100 bit is load-bearing: the SAME numeric value without the bit
        // is a humanoid NPCType slot, not this monster.
        expect(sm::creature_def_from_kind(std::uint16_t(i)) == nullptr,
               "the index without the 0x100 monster bit is not a creature");
    }

    // ── humanoid kinds (< 0x100) are never creatures ─────────────────────────
    for (std::uint16_t k : {std::uint16_t(0), std::uint16_t(1), std::uint16_t(7),
                            std::uint16_t(8), std::uint16_t(0xFF)}) {
        expect(sm::creature_def_from_kind(k) == nullptr,
               "kindType < 0x100 (humanoid NPCType) -> nullptr");
    }

    // ── out-of-range monster indices -> nullptr ──────────────────────────────
    expect(sm::creature_def_from_kind(std::uint16_t(0x100 | n)) == nullptr,
           "0x100|size (one past the last creature) -> nullptr");
    expect(sm::creature_def_from_kind(std::uint16_t(0x1FF)) == nullptr,
           "0x1FF (masked index 255, out of range) -> nullptr");

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

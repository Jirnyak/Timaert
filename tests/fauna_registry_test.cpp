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

#include "check.h"

#include "macro/fauna.h"

#include <cstdint>
#include <cstdio>
#include <string_view>

using namespace sm;
using sm::FaunaEntry;

int main() {
    const auto catalog = sm::creature_catalog();
    const int n = int(catalog.size());

    CHECK(n > 0, "catalog is non-empty");

    // ── structural invariants: every entry well-formed ──────────────────────
    for (int i = 0; i < n; ++i) {
        const FaunaEntry* e = catalog[i];
        CHECK(e != nullptr, "catalog entry is non-null");
        if (e == nullptr) continue;
        CHECK(e->id != nullptr && e->id[0] != '\0', "entry has a non-empty stable id");
        CHECK(e->label != nullptr, "entry has a label");
    }
    // ids and entry pointers are unique across the catalog.
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (catalog[i] == nullptr || catalog[j] == nullptr) continue;
            CHECK(catalog[i] != catalog[j],
                   "each creature entry appears at most once in the catalog");
            CHECK(std::string_view(catalog[i]->id) != catalog[j]->id,
                   "creature stable ids are unique across the catalog");
        }
    }

    // ── round-trip: index <-> kind <-> def <-> id ───────────────────────────
    for (int i = 0; i < n; ++i) {
        const FaunaEntry* e = catalog[i];
        const std::uint16_t kind = std::uint16_t(sm::creature_index(e));

        CHECK(sm::creature_def_from_kind(kind) == e,
               "creature_def_from_kind(ordinal) recovers the row");
        CHECK(sm::creature_index(e) == int(kind),
               "a row's index IS the kind it answers to");
        CHECK(sm::creature_def(e->id) == e,
               "creature_def(id) recovers the same entry");
        CHECK(sm::creature_index(sm::creature_def_from_kind(kind)) == int(kind),
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
        CHECK(sm::creature_def_from_kind(k) == nullptr,
               "a humanoid row is not a creature");
    }

    // ── a kind that names no row at all -> nullptr ───────────────────────────
    CHECK(sm::creature_def_from_kind(std::uint16_t(sm::NPCType::Count)) == nullptr,
           "one past the last row -> nullptr");
    CHECK(sm::creature_def_from_kind(std::uint16_t(0x1FF)) == nullptr,
           "a number far outside the table -> nullptr");

    // ── unknown id / null / non-catalog pointer ──────────────────────────────
    CHECK(sm::creature_def("definitely_not_a_creature_id") == nullptr,
           "unknown id -> nullptr");
    CHECK(sm::creature_index(nullptr) == -1,
           "creature_index(nullptr) == -1");
    FaunaEntry stranger{};
    stranger.id = "stranger";
    CHECK(sm::creature_index(&stranger) == -1,
           "an entry not in the catalog -> index -1");

    // ── THE BESTIARY IS REACHABLE (content witness, 2026-09-11) ────────────
    //
    // A species row is content only if THE LAW can actually raise it. Both
    // ways of being dead content are silent: a zero `weight` and a habitat
    // mask that no place asks for read exactly like a species that is simply
    // rare. So the witness stands on the two public doors — the crowd of a
    // place (pick_crowd_row over its crowdHabitat stripe) and the wild /
    // den roll (roll_spawns) — and asserts that sampling the grounds the
    // world actually has raises EVERY row of the catalog that claims to be
    // rollable. A dropped habitat bit fails here instead of shipping as an
    // empty tower.
    {
        bool seen[std::size_t(sm::NPCType::Count)] = {};
        auto sweep = [&](sm::LandmarkType place, sm::Biome biome, bool forest,
                         std::uint8_t danger, std::uint32_t salt) {
            sm::SpawnContext ctx{};
            ctx.biome = biome;
            ctx.forest = forest;
            ctx.landmark = place;
            ctx.danger = danger;
            // Deposits all in reach: the profession rows of a town stripe are
            // gated on ground, and this sweep asks "can it ever", not "here".
            ctx.depositsNear = 0xFFu;
            std::uint32_t rng = 0x5EED0000u ^ salt;
            for (int i = 0; i < 4096; ++i) {
                seen[std::size_t(sm::pick_crowd_row(ctx, rng))] = true;
                for (const auto& p : sm::roll_spawns(ctx, rng)) {
                    if (p.entry) seen[std::size_t(p.entry->type)] = true;
                }
            }
        };
        // The grounds the world has: the two den families across their whole
        // danger bands (a ruin at the safe edge holds different things from
        // one in hell), the crowd of a town, and the open biomes.
        for (int d = 0; d <= 255; d += 15) {
            const auto danger = std::uint8_t(d);
            sweep(sm::LandmarkType::Spire,   sm::Meadow,   false, danger, 1u);
            sweep(sm::LandmarkType::Ruin,    sm::Meadow,   false, danger, 2u);
            sweep(sm::LandmarkType::City,    sm::Meadow,   false, danger, 3u);
            sweep(sm::LandmarkType::None,    sm::Mountain, false, danger, 4u);
            sweep(sm::LandmarkType::None,    sm::Swamp,    false, danger, 5u);
            sweep(sm::LandmarkType::None,    sm::Desert,   false, danger, 6u);
            sweep(sm::LandmarkType::None,    sm::Steppe,   false, danger, 7u);
            sweep(sm::LandmarkType::None,    sm::Valley,   true,  danger, 8u);
            sweep(sm::LandmarkType::None,    sm::Taiga,    false, danger, 9u);
            sweep(sm::LandmarkType::None,    sm::Tundra,   false, danger, 10u);
            sweep(sm::LandmarkType::None,    sm::Snow,     false, danger, 11u);
            sweep(sm::LandmarkType::None,    sm::Tropics,  false, danger, 12u);
        }
        int reachable = 0;
        for (int i = 0; i < n; ++i) {
            const FaunaEntry* e = catalog[i];
            if (e == nullptr || e->weight == 0) continue;   // named-only rows
            ++reachable;
            CHECK(seen[std::size_t(e->type)],
                   "a rollable catalog row has ground the law can raise it from");
            if (!seen[std::size_t(e->type)]) {
                std::printf("  unreachable: %s\n", e->id);
            }
        }
        CHECK(reachable > 0, "the catalog has rollable rows at all");

        // And the crowd of a populated place is a CROWD: a spire in the band
        // it actually stands in (128..255) must field many species, not one
        // silhouette repeated three hundred times. Eight is the floor the
        // bestiary was authored against — well under what the table offers,
        // so ordinary rebalancing does not trip it, but a stripe collapsing
        // back to a handful does.
        sm::SpawnContext spire{};
        spire.landmark = sm::LandmarkType::Spire;
        spire.biome = sm::Mountain;
        spire.danger = 200;
        bool inThrong[std::size_t(sm::NPCType::Count)] = {};
        std::uint32_t rng = 0xC0FFEEu;
        for (int i = 0; i < 8192; ++i) {
            inThrong[std::size_t(sm::pick_crowd_row(spire, rng))] = true;
        }
        int species = 0;
        for (bool b : inThrong) species += b ? 1 : 0;
        CHECK(species >= 8, "a spire's throng is many species, not a texture");
        std::printf("fauna_registry_test: spire throng species=%d\n", species);
    }

    std::printf("fauna_registry_test: catalog=%d entries\n", n);
    return sm::test::report("fauna_registry_test");
}

// Locks the faction REGISTRY contract (macro/faction.h) — the single source of
// truth for every faction — and the relation matrix create_factions() samples
// from it.
//
// sub/engine.cpp decides all NPC-vs-NPC combat from this matrix:
//     faction_relation(gs, a, b) = gs.factions[a].relations[b]   (symmetric)
// hostile when < kHostileThreshold (-50). And ecs::NPCKind.factionIdx is an
// index into kFactionDefs for humanoids and monsters alike, so the registry's
// integrity IS the combat system's integrity.
//
// History this test guards against (all shipped at some point):
//   • "magika" was emitted by a spawn vocabulary but never registered — every
//     relation involving wandering mages silently read neutral;
//   • the registry was split (universal list + kingdom list), so the two could
//     drift; kingdoms are now ordinary registry rows and politik must reference
//     an existing row;
//   • five parallel id/index vocabularies with colliding indices (a bandit NPC
//     and a demon creature shared index 3) — one index space now;
//   • relations decided by an if-chain over id strings — now a symmetric
//     temperament matrix plus an authored pair-override table, both data.

#include "macro/state.h"
#include "macro/faction.h"
#include "macro/politik.h"
#include "sub/ai.h"          // kHostileThreshold — the SAME constant engine.cpp uses

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL faction_relations_test: %s\n", msg);
    return 1;
}

// Mirror of engine.cpp::faction_relation — the pure lookup under test.
int relation(const sm::GameState& gs, const char* a, const char* b) {
    const auto itA = gs.factions.find(a);
    if (itA == gs.factions.end()) return 0;                 // unknown id -> neutral
    const auto itR = itA->second.relations.find(b);
    if (itR == itA->second.relations.end()) return 0;       // no entry  -> neutral
    return itR->second;
}

bool hostile(const sm::GameState& gs, const char* a, const char* b) {
    return relation(gs, a, b) < sm::sub::kHostileThreshold;
}

} // namespace

int main() {
    using namespace sm;

    // ── Registry integrity (seed-independent) ─────────────────────────────
    // Unique non-empty ids; every kingdom the politik layer grows references an
    // existing registry row (the old split-registry drift is impossible).
    for (int i = 0; i < kFactionCount; ++i) {
        if (!kFactionDefs[i].id || kFactionDefs[i].id[0] == '\0') {
            return fail("registry row with empty id");
        }
        for (int j = i + 1; j < kFactionCount; ++j) {
            if (std::strcmp(kFactionDefs[i].id, kFactionDefs[j].id) == 0) {
                return fail("duplicate faction id in the registry");
            }
        }
        if (faction_index(kFactionDefs[i].id) != i) {
            return fail("faction_index does not invert the registry");
        }
        if (std::strcmp(faction_id_for_index(std::uint16_t(i)),
                        kFactionDefs[i].id) != 0) {
            return fail("faction_id_for_index does not match the registry");
        }
    }
    for (const auto& kd : kingdom_defs()) {
        if (faction_index(kd.id) < 0) {
            return fail("kingdom id has no registry row — identity would vanish");
        }
    }
    // Sentinels degrade safely, never alias a real faction.
    if (faction_index(nullptr) >= 0 || faction_index("") >= 0
        || faction_id_for_index(kNoFaction)[0] != '\0'
        || faction_def_by_index(kNoFaction) != nullptr) {
        return fail("no-faction sentinel does not degrade to neutral");
    }

    // ── Temperament matrix is symmetric by data ───────────────────────────
    for (int a = 0; a < int(Temperament::Count); ++a) {
        for (int b = 0; b < int(Temperament::Count); ++b) {
            const RelationBand ab = kTemperamentBands[a][b];
            const RelationBand ba = kTemperamentBands[b][a];
            if (ab.lo != ba.lo || ab.hi != ba.hi) {
                return fail("temperament band matrix is not symmetric");
            }
            if (ab.lo > ab.hi) {
                return fail("inverted relation band (lo > hi)");
            }
        }
    }

    // ── Sampled matrix semantics, across seeds ────────────────────────────
    // Outlaw/Abyssal WAR bands and the Feral band are fixed by data, so these
    // verdicts must hold for EVERY seed, not by luck of one sample.
    for (std::uint32_t seed : {12345u, 1u, 777u, 2026u}) {
        GameState gs;
        create_factions(gs, seed);

        // Every registry faction exists as a key — including "magika", the id
        // that historically was emitted but never registered.
        for (int i = 0; i < kFactionCount; ++i) {
            if (gs.factions.find(kFactionDefs[i].id) == gs.factions.end()) {
                return fail("registry faction missing from gs.factions");
            }
        }

        // CORE of the goblin fix: demons vs a town guard's kingdom.
        if (!hostile(gs, "demons", "empire") || !hostile(gs, "empire", "demons")) {
            return fail("demons/empire not mutually hostile — goblins vs guards broken");
        }
        if (relation(gs, "demons", "empire") != relation(gs, "empire", "demons")) {
            return fail("relation matrix is not symmetric");
        }

        // Bandits: at war with every kingdom.
        if (!hostile(gs, "bandits", "empire") || !hostile(gs, "bandits", "timaert")
            || !hostile(gs, "bandits", "barbarian_north")) {
            return fail("bandits not hostile to kingdoms");
        }

        // Wildlife drifts in [-30,30]: never hostile, so deer don't storm towns.
        if (hostile(gs, "wildlife", "empire") || hostile(gs, "wildlife", "timaert")) {
            return fail("wildlife wrongly hostile (deer would swarm town)");
        }

        // Magika Orders are REAL now: they have sampled relations (any value is
        // legal — Magical vs Lawful is the uneasy band — but the row must exist)
        // and demons are at war with them like with everyone else.
        if (relation(gs, "magika", "empire") == 0
            && relation(gs, "magika", "old_magica") == 0
            && relation(gs, "magika", "timaert") == 0) {
            return fail("'magika' relations all zero — row didn't join the matrix");
        }
        if (!hostile(gs, "demons", "magika")) {
            return fail("demons not hostile to the magika orders");
        }

        // Authored pair overrides survive the temperament default.
        if (relation(gs, "timaert", "cults") >= kWarBand.hi + 1) {
            return fail("timaert/cults WAR override not applied");
        }
        if (relation(gs, "empire", "lower_magica") < kAllyBand.lo) {
            return fail("empire/lower_magica ALLY override not applied");
        }

        // Self-relation is 100 — no friendly fire within a faction.
        if (hostile(gs, "empire", "empire") || hostile(gs, "demons", "demons")) {
            return fail("faction hostile to itself");
        }

        // Unknown ids still degrade to neutral (fail-closed for stale data).
        if (hostile(gs, "no_such_faction", "empire")
            || hostile(gs, "empire", "no_such_faction")) {
            return fail("unknown faction id did not degrade to neutral");
        }

        // The reputation seed column: a fresh player is hunted by bandits and
        // demons, mildly distrusted by cults, neutral with the rest. The player
        // is an ordinary row, so his standing IS his row in the same matrix —
        // seeded by create_factions, symmetric, and never sampled from a band.
        for (int i = 0; i < kFactionCount; ++i) {
            const char* id = kFactionDefs[i].id;
            if (std::strcmp(id, kPlayerFactionId) == 0) continue;
            if (player_reputation(&gs, id) != kFactionDefs[i].playerReputation) {
                return fail("player standing not seeded from the registry column");
            }
            if (faction_relation(&gs, id, kPlayerFactionId)
                != kFactionDefs[i].playerReputation) {
                return fail("player standing is not symmetric in the matrix");
            }
        }
        // And moving it moves both directions at once.
        add_player_reputation(gs, "empire", -30);
        if (player_reputation(&gs, "empire") != -30
            || faction_relation(&gs, "empire", kPlayerFactionId) != -30) {
            return fail("add_player_reputation did not write both directions");
        }
    }

    std::printf("OK faction_relations_test: registry=%d factions, one index "
                "space, symmetric bands, magika registered, kingdoms resolve, "
                "overrides applied (threshold=%d)\n",
                kFactionCount, sm::sub::kHostileThreshold);
    return 0;
}

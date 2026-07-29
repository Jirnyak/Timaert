// Locks the DATA CONTRACT the universal subworld hostility relation depends on.
//
// sub/engine.cpp::entities_hostile() decides NPC-vs-NPC combat purely from the
// macro faction-relations matrix that create_factions() builds:
//     faction_relation(gs, a, b) = gs.factions[a].relations[b]   (symmetric)
// and treats a pair as hostile when that value < kHostileThreshold (-50).
//
// The goblin-ignores-guards bug (owner report, M&M 6/7/8 reference) is only
// FIXED if that matrix actually rates the relevant faction ids as enemies. This
// test proves the preconditions first-hand, independent of the renderer (which a
// parallel agent is editing): a goblin's faction ("demons") must be hostile to a
// town guard's faction ("empire"); ambient wildlife must NOT be (so deer don't
// swarm a town); the matrix must be symmetric; and the known faction-vocabulary
// gap ("magika" is emitted by npc_faction_id_for but is NOT a kingdom id — the
// real ids are old/northern/lower_magica + lake_duchy) must degrade to a MISS,
// i.e. neutral, never a phantom hostility. If any of these regress, the combat
// fix silently stops working even though it still compiles.

#include "macro/state.h"
#include "sub/ai.h"          // kHostileThreshold — the SAME constant engine.cpp uses

#include <cstdio>
#include <string>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL faction_relations_test: %s\n", msg);
    return 1;
}

// Mirror of engine.cpp::faction_relation — the pure lookup under test. Kept in
// lock-step by asserting the constant (kHostileThreshold) is shared, not copied.
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

    // Build the universal faction set + relation matrix exactly as world boot
    // does. A couple of seeds guard against a lucky single-seed sample: the
    // demons/bandits WAR band and the wildlife bands are seed-independent by
    // construction (resolve_band), so the verdicts below must hold for every
    // seed.
    for (std::uint32_t seed : {12345u, 1u, 777u, 2026u}) {
        GameState gs;
        create_factions(gs, seed);

        // The universal factions must all exist as keys (spawn faction strings
        // map onto these).
        for (const char* id : {"demons", "bandits", "wildlife", "cults",
                               "empire", "timaert"}) {
            if (gs.factions.find(id) == gs.factions.end()) {
                return fail("expected universal/kingdom faction id missing");
            }
        }

        // CORE of the goblin fix: a goblin (FaunaFaction::Demons -> "demons")
        // must be hostile to a town guard (NPCKind faction 0 -> "empire").
        if (!hostile(gs, "demons", "empire")) {
            return fail("demons NOT hostile to empire — goblin would ignore guards");
        }
        // Symmetric: the guard must also see the goblin as an enemy, or only one
        // side fights.
        if (!hostile(gs, "empire", "demons")) {
            return fail("empire NOT hostile to demons — guard would ignore goblin");
        }
        if (relation(gs, "demons", "empire") != relation(gs, "empire", "demons")) {
            return fail("relation matrix is not symmetric");
        }

        // Bandits are civilization's other universal predator — hostile to every
        // kingdom (WAR band in resolve_band).
        if (!hostile(gs, "bandits", "empire") || !hostile(gs, "bandits", "timaert")) {
            return fail("bandits not hostile to kingdoms");
        }

        // Ambient WILDLIFE must NOT be hostile to a town (WILD_PAIR_BAND is
        // [-30,30], never below -50). Otherwise every deer near a city charges
        // the guards — the opposite failure the owner would report next.
        if (hostile(gs, "wildlife", "empire")) {
            return fail("wildlife wrongly hostile to empire (deer would swarm town)");
        }

        // Same faction is allied with itself (relation 100) — two guards, two
        // goblins never fight each other.
        if (hostile(gs, "empire", "empire") || hostile(gs, "demons", "demons")) {
            return fail("faction hostile to itself — friendly fire within a side");
        }

        // The vocabulary gap must be SAFE: "magika" is emitted by
        // npc_faction_id_for(1) but is NOT a real kingdom id, so it resolves to a
        // matrix MISS => neutral, never accidental hostility. (A projected macro
        // mage therefore stays neutral rather than being wrongly attacked; the
        // real fix for magika factions is an owner world-model decision.)
        if (gs.factions.find("magika") != gs.factions.end()) {
            return fail("'magika' unexpectedly a real faction id — revisit the gap");
        }
        if (hostile(gs, "magika", "empire") || hostile(gs, "demons", "magika")) {
            return fail("unknown faction id 'magika' did not degrade to neutral");
        }
    }

    std::printf("OK faction_relations_test: demons/bandits hostile to kingdoms, "
                "wildlife neutral, symmetric, self-allied, magika-gap safe "
                "(threshold=%d)\n", sm::sub::kHostileThreshold);
    return 0;
}

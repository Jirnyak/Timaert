// THE door of the seam: WHOSE RECORD IS THIS BODY?
//
// Owner's form, 2026-09-12 («ЗЕРКАЛО ДЛЯ ВСЕХ», CANON.md): a body standing in
// the subworld OWNS NOTHING. Its sheet, its bars, its bag, what it wears and
// what it knows all belong to the macro record it is a projection of, and every
// reader and every writer goes through here to find them. The player's device
// became the law — he has worked this way since landing 4 (his bars are the
// ordinary Pools on his squad entity) — so the hero husk is not the exception
// that proves the rule, he is the ordinary case of it.
//
// WHY THIS EXISTS AT ALL. Before it, the seam carried COPIES down and folded a
// single number — an hp FRACTION — back up. Two consequences, both shipped:
//   * a lord you stripped, looted and levelled underground climbed out whole,
//     because his belongings were a copy nobody read back (the fold-up knew
//     about hp and nothing else);
//   * the two layers had to agree on conversions (a wound as a fraction, bars
//     built from two different sheets) precisely because there were two
//     memories of one thing. Every such pair drifts — problems.md §43, §44.
// With one memory there is no fold, no conversion, and nothing to forget: the
// sword you pick up is in his bag the instant you pick it up, because his bag
// is the only bag there ever was.
//
// THE ONE RULE, and it has no player branch: a body is a projection of the
// record its `MacroOrigin` names, and a body with no backlink is its own record.
// That second half is not a fallback, it is the other honest kind of birth — a
// citizen in a crowd, a wolf, a bandit rolled from a cell seed is ONE OF MANY
// made visible (sub/spawn.h, the derived form). Nothing above remembers him, so
// there is nothing above to write to; he answers for himself and his death
// settles a STOCK instead (ecs::MacroDebt). Two forms of birth, two honest
// answers, one question.
#pragma once

#include "ecs/components.h"
#include "macro/bonus.h"   // BonusTotals — «что на нём стоит», and its ==

#include <entt/entt.hpp>

namespace sm::sub {

// The entity whose components ARE this body's state. Never null for a valid
// body: a stale backlink (the record was reaped while the body still stood)
// degrades to the body itself rather than to nothing, because a body without
// bars would be an invulnerable ghost — the one failure mode worse than
// losing the write-back.
inline entt::entity record_of(const entt::registry& reg, entt::entity body) {
    if (body == entt::null || !reg.valid(body)) return entt::null;
    if (const auto* origin = reg.try_get<ecs::MacroOrigin>(body)) {
        if (reg.valid(origin->macro)) return origin->macro;
    }
    return body;
}

// THE accessor every typed door below is made of. One template, deliberately:
// the alternative is a hand-written run of near-identical `pools_of` /
// `bag_of` / `worn_of` functions, and a field that falls out of a hand-written
// run is this project's oldest bug shape (the fold-up that dropped a component
// and froze the world's AI — memory: handwritten-foldup-drops-fields). Adding
// a kind of owned state means adding one line, not a fifth twin.
//
// The self-fallback is the same guard as above, one level down: a record that
// does not keep this kind of state at all leaves the body answering for itself.
template <class C>
inline C* state_of(entt::registry& reg, entt::entity body) {
    const entt::entity rec = record_of(reg, body);
    if (rec != entt::null) {
        if (C* owned = reg.try_get<C>(rec)) return owned;
    }
    return reg.try_get<C>(body);
}

template <class C>
inline const C* state_of(const entt::registry& reg, entt::entity body) {
    return state_of<C>(const_cast<entt::registry&>(reg), body);
}

// WHAT STOOD ON THE RECORD when this body's derived numbers were last built.
//
// The cost of the mirror was measured before it was built (owner's note in
// CANON, worst case — all 42 anatomy slots worn): assembling «what stands on
// him» costs 0.00041 ms per body, which is 0.8 % of a frame at 300 bodies and
// was accepted; RE-ROLLING the sheet and the strike off it costs 0.00196 ms,
// five times more, and at a thousand bodies that is 12.5 % of the frame — not
// acceptable, and not necessary, because it almost never changes.
//
// So the assembly runs and the RESULT is compared: `BonusTotals::operator==`
// exists for exactly this question, and the expensive half runs only when the
// answer is no. A lord who levels from a kill mid-fight swings harder on the
// next tick; a lord nobody touched pays one comparison.
struct StandingMirror { BonusTotals totals{}; };

// The three bars (CANON S14). Damage, casting, harvesting and crafting all
// land here — «действия платят в склад», now stated once for every body
// rather than once for the player and once for everyone else.
inline ecs::Pools* pools_of(entt::registry& reg, entt::entity body) {
    return state_of<ecs::Pools>(reg, body);
}
inline const ecs::Pools* pools_of(const entt::registry& reg,
                                  entt::entity body) {
    return state_of<ecs::Pools>(reg, body);
}

} // namespace sm::sub

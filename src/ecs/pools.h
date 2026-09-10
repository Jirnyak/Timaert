// THE POOLS OF A BODY — every body, one block (CANON S14 «три ресурса»;
// owner, 2026-09-09: «это РПГ, у всех должна быть HP SP MP»).
//
// In its own header, apart from the ECS glue, because it is pure DATA: the
// recovery law (macro/player_recovery.cpp) and the stamina bookkeeping
// operate on this block without needing entt or the component roster, and a
// slim test target must be able to compile them without either.
//
// It was called `Health` and it held one bar, because the other two were the
// player's private property: `CombatStats` existed in exactly ONE instance in
// the whole game (PlayerState::combatStats), and `project_combat` — the door
// EVERY body is born through — computed maxMp and maxSp from the sheet and
// threw both away on the spot. A lord had stamina in a different house
// (MacroNpcRuntime) and no mana at all; a body in a scene had neither. Three
// bars of one body lived in three homes, and two kinds of body out of three
// were missing two of them.
//
// One block is the guard the doctrine could not be: a new kind of body cannot
// be born short of a bar, because there is nowhere to leave one out.
//
// INTEGER bars since phase 4г: every combat writer has been whole since the
// dice phase (int amount through the one damage door), so float storage held
// nothing but the memory of fractional wounds that no longer exist.
// Fractional REGEN lives in a carry beside its bar — never in the bar itself.
// The carries sit HERE rather than in a table off to the side because a bar
// and its remainder are one fact: the player's used to live in an App-side
// accumulator that never reached the save, and the lord's did not exist at
// all, so a rest slice worth 0.59 points floored to zero every think and
// «heal an NPC» could not even be expressed. One law
// (player_recovery.h recover_bar) needs one home for its remainder.
//
// STAMINA IS NOT LIKE THE OTHER TWO, and it lives here anyway. `sp` may go
// NEGATIVE — a march can be taken on credit, and the debt is bitten out of hp
// by the exhaustion law (movement_cost.h) — so its carry is SIGNED and settles
// through `settle_sp_carry`, not through `recover_bar`. Every other bar floors
// at zero. That difference is why it kept its own house on MacroNpcRuntime for
// so long; it is not a reason for a body's third bar to live somewhere its
// other two do not. The bar and its ceiling travel together: a ceiling in one
// struct and its value in another is how `maxSp` came to be refreshed by a
// door that could not see the bar it capped.
#pragma once

namespace sm::ecs {

struct Pools {
    int   hp = 0, maxHp = 0;
    int   mp = 0, maxMp = 0;
    int   sp = 0, maxSp = 0;
    float hpCarry = 0.0f;
    float mpCarry = 0.0f;
    float spCarry = 0.0f;   // SIGNED: a march spends through the same remainder
};
static_assert(sizeof(Pools) == 36,
              "Pools grew — it rides the macro snapshot as raw bytes "
              "(save.cpp w.pod), so its layout IS the save format: pay a "
              "kSaveVersion bump. Its neighbours ItemRef/WorldFact/AgentMemory "
              "carry this same guard; this block went without one until the "
              "post-demo audit and grew silently.");

} // namespace sm::ecs

// AUTO-RESOLVE — the macro law of battle (owner, 2026-08-06, macrosim.md):
// two AI squads meeting on the map must produce a winner without a subworld,
// or the macro sim cannot run a war at all. ONE resolver, fed by what the
// rosters already say — the same table rows, the same character sheets, the
// same project_combat numbers the subworld births fight with — plus context
// (terrain, fatigue, who ambushed whom). The player's own battles may use it
// too (the M&B "auto-resolve" button): same function, same inputs, no second
// law of combat. Its agreement with the fought version is PINNED BY TEST
// (tests/auto_battle_test.cpp) as a property — same lineup, both paths, same
// winner and compatible loss magnitude — so a player who fights by hand is
// not playing a different game from the one the world plays around him.
//
// Like sub/movement.h this is a PURE law: it computes who won and who fell and
// touches nothing. The CALLER settles the world — roster rows through the
// macro-stock roster row, the leader through the tracked-death path, the
// survivors of a dead leader through drain_dead_leader_squads, loot and XP
// through their one registries — so the auto-battle and the fought battle
// pay their debts through the very same doors.
//
// Header-only, macro layer, deterministic in (inputs, rng seed): no ECS, no
// engine, testable everywhere.
#pragma once

#include "core/rng.h"
#include "macro/army.h"
#include "macro/character_sheet.h"
#include "macro/npc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm {

// One side of an auto-battle: the squad as the macro layer holds it — the
// leader (the entity itself) plus the roster rows. The aura is COLLECTED BY
// THE CALLER from the leader's sheet (character_sheet.h squad_bonuses), exactly as the body
// births do, so a Leader-perk lord is stronger here and below by the same
// modifiers. Context multipliers are per-side because terrain and fatigue
// are: a squad on its hill fights the value of its hill.
struct AutoBattleSide {
    NPCType       leaderType  = NPCType::Peasant;
    int           leaderLevel = 1;
    std::uint32_t leaderSeed  = 0;
    float         leaderHealthFraction = 1.0f;   // wounds walk in with him
    // A leader STATED directly instead of derived from a row: the PLAYER.
    // His hp ceiling and damage-per-second come from the very numbers his
    // subworld body fights with (sub/engine.cpp spawn_player_entity:
    // combatStats.maxHp, and (base + rawPhysDamage) each kPlayerMeleeCooldown
    // seconds) — the same-game guarantee, extended to the resolver. Both < 0
    // (the default) means "derive from the row" as every NPC leader does.
    float         leaderHpOverride  = -1.0f;
    float         leaderDpsOverride = -1.0f;
    const SoldierSquad* roster = nullptr;   // may be empty/null
    BonusTotals   bonuses{};
    // Context, composed by the caller from what the cell and the squad say:
    // terrain advantage (defender's hill, forest cover — data rows when the
    // encounter system lands) and fatigue (sp / maxSp). 1 = neutral/fresh.
    float         terrain = 1.0f;
    float         fatigue = 1.0f;
};

// Who struck from ambush: the ambusher opens the exchange with a free volley
// before the lines meet — the same edge a real ambush buys in the subworld.
enum class Ambush : std::uint8_t { None = 0, SideA, SideB };

struct AutoBattleOutcome {
    int winner = 0;   // 0 = side A, 1 = side B
    // The fallen, by the same name their deaths are settled under everywhere
    // (SoldierRecord::entityId — the macro-stock roster row's `detail`).
    std::vector<std::uint32_t> casualtiesA, casualtiesB;
    // The leaders' post-battle health as the FRACTION wounds already travel
    // in (sub/spawn.h): 0 = dead, and the caller routes that through the one
    // tracked-death path. A winner limps out; a loser's leader dies ONLY if
    // his whole roster died with him — never by dice (owner ruling).
    float leaderFractionA = 1.0f, leaderFractionB = 1.0f;
};

namespace auto_battle_detail {

// One fighter's worth, from the SAME numbers a subworld body of this row and
// level fights with: hp × damage-per-second of the sheet-projected template.
// The floor() mirrors emplace_body — both layers fight with integers.
// The armour a fighter brings to a macro battle, as ONE number: physical
// blows are what macro armies trade today, so the credit is the mean of the
// three physical columns. When auto-resolve learns attack types, the pairing
// reads the column the OPPONENT's blow names — this helper is the
// approximation, not a second law.
inline int auto_battle_armor(const ArmorProfile& a) {
    return (a.of(DamageType::Slash) + a.of(DamageType::Pierce) +
            a.of(DamageType::Blunt)) / 3;
}

// Armour enters as EFFECTIVE HP through the PERCENT branch of the hybrid
// mitigation law (macro/damage_types.h): a big blow keeps
// kArmorHalving / (kArmorHalving + armor) of itself, so a body of hp H
// absorbs H * (kArmorHalving + armor) / kArmorHalving worth of raw blows —
// the same law, read as a multiplier. The hybrid's THRESHOLD branch (small
// blows fully blocked) is deliberately uncredited here: it depends on the
// opponent's blow size, which a per-fighter power scalar does not know, so
// heavy armour is worth slightly MORE in a fought battle than the
// auto-resolve credits — a bias, not a disagreement of laws. The armour
// source is the door's own for this row: npc_def().armor (macro fighters
// wear no equipment today).
inline float fighter_power(NPCType type, int level, std::uint32_t seed,
                           const BonusTotals* squadBonuses, float healthFraction) {
    CharacterSheet sheet = make_character_sheet(type, level, seed);
    if (squadBonuses) sheet = effective_sheet(sheet, *squadBonuses);
    const NpcTypeDef& def = npc_def(type);
    const CombatTemplate pc = project_combat(sheet, def.combat);
    const float hp  = std::max(1.0f, std::floor(pc.hp)) *
                      std::clamp(healthFraction, 0.0f, 1.0f) *
                      (float(kArmorHalving + auto_battle_armor(def.armor))
                       / float(kArmorHalving));
    const float dps = std::floor(pc.damage) /
                      std::max(0.1f, pc.cooldown);
    return hp * dps;
}

// A member's sheet seed. The subworld derives member seeds from the entry
// window (not stable across entries), so exact sheet identity across the two
// paths does not exist to agree on — what both paths share is the ROW and the
// LEVEL, which fix the sheet's entire point budget; the seed only shuffles
// its allocation. Deterministic from the member's own identity so the same
// battle re-resolved is the same battle.
inline std::uint32_t member_seed(const SoldierRecord& r) {
    return (r.entityId * 2654435761u) ^ (std::uint32_t(r.kind) << 16)
         ^ std::uint32_t(r.level);
}

} // namespace auto_battle_detail

// The squad's strength — THE number the macro AI thinks with. Flee-or-fight
// decisions (Inc 5) and the resolver below read the same function, so a squad
// never runs from a fight the law says it would win: one law of strength.
inline float squad_power(const AutoBattleSide& s) {
    using namespace auto_battle_detail;
    float power;
    if (s.leaderHpOverride >= 0.0f && s.leaderDpsOverride >= 0.0f) {
        power = std::max(1.0f, s.leaderHpOverride)
              * std::clamp(s.leaderHealthFraction, 0.0f, 1.0f)
              * std::max(0.0f, s.leaderDpsOverride);
    } else {
        power = fighter_power(s.leaderType, s.leaderLevel, s.leaderSeed,
                              /*aura*/nullptr, s.leaderHealthFraction);
    }
    if (s.roster) {
        for (const SoldierRecord& r : *s.roster) {
            if (!valid_npc_kind(r.kind)) continue;
            power += fighter_power(NPCType(r.kind),
                                   normalize_soldier_level(r.level),
                                   member_seed(r), &s.bonuses, 1.0f);
        }
    }
    return power * std::max(0.1f, s.terrain) * std::clamp(s.fatigue, 0.1f, 1.0f);
}

// Resolve one battle. Deterministic in (sides, ambush, rng state): the swing
// the rng contributes is bounded (±10% per side), so a clear advantage cannot
// be rolled away — near-parity fights are the only ones chance may tip,
// which is exactly how the fought version behaves.
inline AutoBattleOutcome resolve_auto_battle(const AutoBattleSide& a,
                                             const AutoBattleSide& b,
                                             Ambush ambush, Rng& rng) {
    using namespace auto_battle_detail;
    AutoBattleOutcome out{};

    float pa = squad_power(a);
    float pb = squad_power(b);
    // The ambusher's free volley: the opening exchange happens before the
    // victim's line answers, worth a flat edge to the side that sprang it.
    constexpr float kAmbushEdge = 1.30f;
    if (ambush == Ambush::SideA) pa *= kAmbushEdge;
    if (ambush == Ambush::SideB) pb *= kAmbushEdge;
    // Bounded fortune: ±10% per side.
    pa *= 0.9f + 0.2f * rng.next_f01();
    pb *= 0.9f + 0.2f * rng.next_f01();

    const bool aWins = pa >= pb;
    out.winner = aWins ? 0 : 1;
    const float pw = aWins ? pa : pb;   // winner's effective power
    const float pl = aWins ? pb : pa;

    // Loss fractions, Lanchester-shaped: the winner pays with the square-law
    // remnant (a 2:1 fight costs the victor ~13%, a near-parity fight most of
    // his line), the loser is broken — most fall, the rest are the survivors
    // the deserter pool or the rout will inherit.
    const float ratio = pl / std::max(1.0f, pw);           // (0, 1]
    const float winnerLoss =
        1.0f - std::sqrt(std::max(0.0f, 1.0f - ratio * ratio));
    const float loserLoss = std::clamp(0.55f + 0.45f * ratio, 0.0f, 1.0f);

    // Distribute a side's losses over its actual roster rows. The FRACTION
    // decides how many fell — that is the law above, and it must hold for a
    // band of three as surely as for a thousand — while the rng decides WHO,
    // because the ledger settles deaths by name (the roster row's `detail`).
    // The leader is not a roster row: he takes the battle as wounds (winner)
    // and only a broken side can lose him outright.
    auto distribute = [&rng](const AutoBattleSide& side, float lossFrac,
                             std::vector<std::uint32_t>& casualties) {
        if (!side.roster || side.roster->empty()) return;
        std::vector<std::uint32_t> ids;
        ids.reserve(side.roster->size());
        for (const SoldierRecord& r : *side.roster) {
            if (valid_npc_kind(r.kind)) ids.push_back(r.entityId);
        }
        const int n = int(ids.size());
        const int fallen = std::clamp(
            int(std::lround(lossFrac * float(n))), 0, n);
        // Partial Fisher-Yates: the first `fallen` slots are the dead.
        for (int i = 0; i < fallen; ++i) {
            const int j = i + int(rng.next_u32()
                                  % std::uint32_t(n - i));
            std::swap(ids[std::size_t(i)], ids[std::size_t(j)]);
            casualties.push_back(ids[std::size_t(i)]);
        }
    };
    distribute(a, aWins ? winnerLoss : loserLoss, out.casualtiesA);
    distribute(b, aWins ? loserLoss : winnerLoss, out.casualtiesB);

    // Leaders. The winner walks out wounded in proportion to how hard the
    // fight was. The loser's leader dies ONLY when his whole roster died
    // with him (owner ruling, 2026-08-06): a lord's head is never given to
    // chance, or lords would fall regularly to dice in a war of auto-battles
    // — defeat costs him HP, not his life, while a single man of his still
    // stands. The consequence for a squad of ONE is deliberate: a lone
    // wanderer who loses has nobody left to stand between him and the field.
    // Fractions, because that is how wounds travel between layers.
    const auto roster_size = [](const AutoBattleSide& s) {
        int n = 0;
        if (s.roster) {
            for (const SoldierRecord& r : *s.roster) {
                if (valid_npc_kind(r.kind)) ++n;
            }
        }
        return n;
    };
    const AutoBattleSide& loserSide = aWins ? b : a;
    const auto& loserCasualties = aWins ? out.casualtiesB : out.casualtiesA;
    const bool rosterWipedOut =
        int(loserCasualties.size()) >= roster_size(loserSide);
    const float winnerFraction = std::clamp(1.0f - winnerLoss * 0.8f,
                                            0.05f, 1.0f);
    const float loserFraction = rosterWipedOut
        ? 0.0f
        : std::clamp(0.25f * (1.0f - ratio) + 0.05f, 0.05f, 1.0f);
    const float startA = std::clamp(a.leaderHealthFraction, 0.0f, 1.0f);
    const float startB = std::clamp(b.leaderHealthFraction, 0.0f, 1.0f);
    out.leaderFractionA = startA * (aWins ? winnerFraction : loserFraction);
    out.leaderFractionB = startB * (aWins ? loserFraction : winnerFraction);
    return out;
}

} // namespace sm

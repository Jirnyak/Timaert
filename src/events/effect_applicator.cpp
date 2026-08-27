#include "macro/bonus.h"
#include "events/effect_applicator.h"
#include "macro/currency.h"
#include "macro/spells.h"
#include "events/event_log_util.h"
#include <algorithm>
#include <cstdio>

namespace sm {

namespace {

// A scripted effect is a ROW of the one bonus registry, named by the plot in
// the registry's own authoring key. This was the fifth half-door: six string
// verbs restating, in their own vocabulary, arithmetic that `ItemEffect`
// already restated in a third — three clamp-adds to the same three pools,
// spelled out three times in three files.
//
// The verbs the plot files already speak are kept as ALIASES onto rows rather
// than renamed, because a plot file is content and content does not get
// broken by an engine tidy-up. `restore_hp` and `heal_hp` were always the same
// verb; `drain_sp` is the same row with the other sign, which is the whole
// reason the registry's value is signed.
//
// THERE IS NO `damage_hp`, and its absence is a ruling (owner, 2026-08-27:
// «лучше единая система урона и НЕЗАВИСИМАЯ ОТ ИГРОКА — ИГРОК = НПЦ, значит
// это тоже нарушение»). A verb that subtracts the player's health from a plot
// file was a second damage system that only one body in the world could be
// hurt by: no mitigation, no death protocol, no attacker, and reachable by
// nobody except him. Draining STAMINA is not that — exertion has no door and
// no armour argues with it — which is why `drain_sp` stays and its twin does
// not.
struct EffectVerb {
    const char* verb;
    BonusId     row;
    int         sign;   // -1: the verb SPENDS what its row restores
};

constexpr EffectVerb kEffectVerbs[] = {
    {"heal_hp",    BonusId::HealHp, +1},
    {"restore_hp", BonusId::HealHp, +1},   // always was the same verb
    {"restore_mp", BonusId::HealMp, +1},
    {"restore_sp", BonusId::HealSp, +1},
    {"drain_sp",   BonusId::HealSp, -1},
};

void apply_effect(PlayerState& p, const GameEvent& ev) {
    const std::string& type = ev.s1;
    const int value = ev.ix;
    auto& cs = p.combatStats;

    if (type == "grant_xp") {
        // wis dividend: scripted XP scales by the recipient's expMult too —
        // one law for every grant path (owner ruling 2026-08-05). XP is not a
        // pool and not a sheet address; it has its own door and keeps it.
        award_exp(p.sheet.levelData, value,
                  calculate_derived(p.sheet.attributes, p.sheet.skills).expMult);
        return;
    }

    for (const EffectVerb& v : kEffectVerbs) {
        if (type != v.verb) continue;
        PoolSlice pools{};
        pools.current[int(PoolId::Hp)] = &cs.currentHp;
        pools.maximum[int(PoolId::Hp)] = cs.maxHp;
        pools.current[int(PoolId::Mp)] = &cs.currentMp;
        pools.maximum[int(PoolId::Mp)] = cs.maxMp;
        pools.current[int(PoolId::Sp)] = &cs.currentSp;
        pools.maximum[int(PoolId::Sp)] = cs.maxSp;
        apply_instant(pools, {std::uint8_t(v.row),
                              std::int16_t(v.sign * value)});
        return;
    }
}

} // namespace

void apply_events(std::span<const GameEvent> events, GameState& gs,
                  Inventory* bag,
                  std::vector<GameEvent>* followups) {
    PlayerState& p = gs.player;
    for (auto& ev : events) {
        switch (ev.tag) {
            case EventTag::SpireDepleted:
                // The orb taught its spell: resolve the registry ordinal the
                // engine emitted (ev.b) against kSpellDefs — the applicator
                // may ask the registry itself now (Rule 13). Learn + journal
                // only on FIRST learn; the SpellLearned announcement below is
                // re-applied idempotently by this same switch.
                if (ev.b < std::uint32_t(kSpellCount)) {
                    const SpellDef& def = kSpellDefs[ev.b];
                    if (spellbook_learn(p.spellBook, def.id)) {
                        char msg[96];
                        std::snprintf(msg, sizeof(msg),
                                      "You have learned %s!", def.name);
                        push_event_log(p, {LogType::World, msg,
                                           gs.worldTime.day()});
                        if (followups) {
                            GameEvent learned{EventTag::SpellLearned};
                            learned.s1 = def.id;
                            followups->push_back(std::move(learned));
                        }
                    }
                }
                break;
            case EventTag::QuestComplete:
                if (ev.b != kEventEffectAlreadyApplied) {
                    push_string(p.completedQuestIds, ev.s1);
                }
                break;
            case EventTag::QuestFail:
                if (ev.s2 == "abandoned") {
                    break;
                }
                if (ev.b == kEventEffectAlreadyApplied) {
                    break;
                }
                // TS quest-engine.ts stores failed quests in completedQuestIds
                // as "done (failed)". Keep the native failed ledger too.
                push_string(p.completedQuestIds, ev.s1);
                push_unique_string(p.failedQuestIds, ev.s1);
                break;
            case EventTag::SpellLearned:
                spellbook_learn(p.spellBook, ev.s1);
                break;
            case EventTag::PlayerGoldChange:
                if (ev.b != kEventEffectAlreadyApplied) {
                    if (!bag) break;
                    if (ev.ix >= 0) bag->add("coin_empire", ev.ix);
                    else wallet_spend_up_to(*bag, -ev.ix);
                }
                break;
            case EventTag::ApplyEffect:
                if (ev.b != kEventEffectAlreadyApplied) {
                    apply_effect(p, ev);
                }
                break;
            case EventTag::CodexUnlock:
                // TS dedups codex entries — match.
                if (std::find(p.codexUnlocked.begin(), p.codexUnlocked.end(),
                              ev.s1) == p.codexUnlocked.end()) {
                    p.codexUnlocked.push_back(ev.s1);
                }
                break;
            case EventTag::ReputationChange:
                if (ev.b != kEventEffectAlreadyApplied) {
                    add_player_reputation(gs, ev.s1.c_str(), ev.ix);
                }
                break;
            case EventTag::BattleStart:
                // App runtime routes this into subworld NPC combat; keep a
                // player-facing breadcrumb in the persistent log.
                push_event_log(p, {LogType::Combat,
                    "Encounter: " + ev.s1, 0});
                break;
            default: break;
        }
    }
}

void apply_events(const std::vector<GameEvent>& events, GameState& gs,
                  Inventory* bag,
                  std::vector<GameEvent>* followups) {
    apply_events(std::span<const GameEvent>(events.data(), events.size()), gs,
                 bag,
                 followups);
}

} // namespace sm

#include "events/effect_applicator.h"
#include "macro/currency.h"
#include "events/event_log_util.h"
#include <algorithm>

namespace sm {

namespace {

void apply_effect(PlayerState& p, const GameEvent& ev) {
    const std::string& type = ev.s1;
    const int value = ev.ix;
    auto& cs = p.combatStats;
    // heal_hp == restore_hp (both clamp-add by value, NOT a full restore), and
    // there is no drain_mp verb. grant_xp goes through the ONE grant path
    // (award_exp), so a scripted reward levels the player like any other
    // experience — it used to add to the pool and leave it unspendable.
    if (type == "heal_hp" || type == "restore_hp") {
        cs.currentHp = std::min(cs.currentHp + value, cs.maxHp);
    } else if (type == "damage_hp") {
        cs.currentHp -= value;
    } else if (type == "restore_mp") {
        cs.currentMp = std::min(cs.currentMp + value, cs.maxMp);
    } else if (type == "restore_sp") {
        cs.currentSp = std::min(cs.currentSp + value, cs.maxSp);
    } else if (type == "drain_sp") {
        cs.currentSp = std::max(0, cs.currentSp - value);
    } else if (type == "grant_xp") {
        // wis dividend: scripted XP scales by the recipient's expMult too —
        // one law for every grant path (owner ruling 2026-08-05).
        award_exp(p.sheet.levelData, value,
                  calculate_derived(p.sheet.attributes, p.sheet.skills).expMult);
    }
}

} // namespace

void apply_events(std::span<const GameEvent> events, GameState& gs) {
    PlayerState& p = gs.player;
    for (auto& ev : events) {
        switch (ev.tag) {
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
                    if (ev.ix >= 0) p.inventory.add("coin_empire", ev.ix);
                    else wallet_spend_up_to(p.inventory, -ev.ix);
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

void apply_events(const std::vector<GameEvent>& events, GameState& gs) {
    apply_events(std::span<const GameEvent>(events.data(), events.size()), gs);
}

} // namespace sm

#include "events/effect_applicator.h"
#include <algorithm>

namespace sm {

namespace {

void apply_effect(PlayerState& p, const GameEvent& ev) {
    const std::string& type = ev.s1;
    const int value = ev.ix;
    auto& cs = p.combatStats;
    // TS-faithful (effect-applicator.ts applyEffect_): heal_hp == restore_hp
    // (both clamp-add by value, NOT full restore), no drain_mp, and no
    // level-up inside grant_xp.
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
        p.levelData.exp += value;
    }
}

void push_unique_string(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void push_string(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty()) values.push_back(value);
}

} // namespace

void apply_events(std::span<const GameEvent> events, PlayerState& p) {
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
                    p.gold += ev.ix;
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
                    p.reputation[ev.s1] += ev.ix;
                }
                break;
            case EventTag::BattleStart:
                // App runtime routes this into subworld NPC combat; keep a
                // player-facing breadcrumb in the persistent log.
                p.eventLog.push_back({LogType::Combat,
                    "Encounter: " + ev.s1, 0});
                break;
            default: break;
        }
    }
}

void apply_events(const std::vector<GameEvent>& events, PlayerState& p) {
    apply_events(std::span<const GameEvent>(events.data(), events.size()), p);
}

} // namespace sm

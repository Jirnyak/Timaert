#include "events/effect_applicator.h"
#include <algorithm>

namespace sm {

namespace {

void apply_effect(PlayerState& p, const std::string& type, int value) {
    auto& cs = p.combatStats;
    // TS-faithful (effect-applicator.ts applyEffect_): heal_hp == restore_hp
    // (both clamp-add by value, NOT full restore), no drain_mp, no level-up
    // inside grant_xp — level-up is handled by the PlayerLevelUp event.
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
    if (cs.currentHp < 0) cs.currentHp = 0;
}

} // namespace

void apply_events(const std::vector<GameEvent>& events, PlayerState& p) {
    for (auto& ev : events) {
        switch (ev.tag) {
            case EventTag::PlayerLevelUp:
                try_level_up(p.levelData);
                p.combatStats = calculate_combat_stats(p.attributes, p.skills);
                break;
            case EventTag::QuestCompleted:
                p.completedQuestIds.push_back(int(ev.a));
                break;
            case EventTag::SpellLearned:
                p.spellBookSpellIds.push_back(ev.s1);
                break;
            case EventTag::Trade:
            case EventTag::PlayerGoldChange:
                p.gold += ev.ix;
                if (p.gold < 0) p.gold = 0;
                break;
            case EventTag::ApplyEffect:
                apply_effect(p, ev.s1, ev.ix);
                break;
            case EventTag::CodexUnlock:
                // TS dedups codex entries — match.
                if (std::find(p.codexUnlocked.begin(), p.codexUnlocked.end(),
                              ev.s1) == p.codexUnlocked.end()) {
                    p.codexUnlocked.push_back(ev.s1);
                }
                break;
            case EventTag::ReputationChange:
                p.reputation[ev.s1] += ev.ix;
                break;
            case EventTag::BattleStart:
                // Combat system not yet ported; log as event log entry.
                p.eventLog.push_back({LogType::Combat,
                    "Battle: " + ev.s1, 0});
                break;
            default: break;
        }
    }
}

} // namespace sm

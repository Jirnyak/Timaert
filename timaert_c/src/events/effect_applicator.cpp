#include "events/effect_applicator.h"
#include <algorithm>

namespace sm {

namespace {

void apply_effect(PlayerState& p, const GameEvent& ev) {
    const std::string& type = ev.s1;
    const int value = ev.ix;
    auto& cs = p.combatStats;
    // TS-faithful (effect-applicator.ts applyEffect_): heal_hp == restore_hp
    // (both clamp-add by value, NOT full restore), no drain_mp, no level-up
    // inside grant_xp; level-up is handled by the PlayerLevelUp event.
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

void push_unique_string(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

} // namespace

void apply_events(std::span<const GameEvent> events, PlayerState& p) {
    for (auto& ev : events) {
        switch (ev.tag) {
            case EventTag::PlayerLevelUp:
                if (p.levelData.expToNext > 0) {
                    while (try_level_up(p.levelData)) {}
                    p.combatStats = calculate_combat_stats(p.attributes, p.skills);
                }
                break;
            case EventTag::QuestComplete:
                push_unique_string(p.completedQuestIds, ev.s1);
                break;
            case EventTag::QuestFail:
                push_unique_string(p.failedQuestIds, ev.s1);
                break;
            case EventTag::SpellLearned:
                spellbook_learn(p.spellBook, ev.s1);
                break;
            case EventTag::Trade:
            case EventTag::PlayerGoldChange:
                p.gold += ev.ix;
                if (p.gold < 0) p.gold = 0;
                break;
            case EventTag::ApplyEffect:
                apply_effect(p, ev);
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

bool queue_player_level_up_if_needed(EventBus& bus,
                                     std::span<const GameEvent> appliedEvents,
                                     const LevelData& before,
                                     const LevelData& after) {
    if (before.expToNext <= 0 || after.expToNext <= 0) return false;
    if (before.exp >= before.expToNext || after.exp < after.expToNext) return false;

    bool xpGranted = false;
    for (const auto& ev : appliedEvents) {
        if (ev.tag == EventTag::ApplyEffect
            && ev.s1 == "grant_xp"
            && ev.ix > 0) {
            xpGranted = true;
            break;
        }
    }
    if (!xpGranted) return false;

    LevelData projected = after;
    while (try_level_up(projected)) {}

    GameEvent ev{EventTag::PlayerLevelUp};
    ev.ix = projected.level;
    ev.a = std::uint32_t(projected.exp);
    bus.emit(ev);
    return true;
}

} // namespace sm

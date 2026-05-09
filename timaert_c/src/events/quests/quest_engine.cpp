#include "events/quests/quest_engine.h"
#include "core/torus.h"

namespace sm {

static bool obj_in_radius(float px, float py, float cx, float cy, float r) {
    float dx = px - cx, dy = py - cy;
    return dx*dx + dy*dy <= r * r;
}

static void apply_reward(const Reward& r, PlayerState& p, EventBus& bus) {
    switch (r.kind) {
        case RewardKind::Gold:       p.gold += r.amount; break;
        case RewardKind::Xp:         p.levelData.exp += r.amount; break;
        case RewardKind::Item:       p.inventory.add(r.itemId, r.amount); break;
        case RewardKind::Reputation: p.reputation[r.faction] += r.delta; break;
        case RewardKind::Event:      bus.emit(r.event); break;
    }
}

static bool eval_objective(Objective& o, const std::vector<GameEvent>& events,
                           PlayerState& p, const WorldTime& time) {
    if (o.completed) return true;
    switch (o.kind) {
        case ObjectiveKind::VisitCell:
            if (obj_in_radius(p.x, p.y, float(o.ix), float(o.iy), o.radius)) o.completed = true;
            break;
        case ObjectiveKind::FindLocation:
            // Player.x/y is macroworld; subworld entry handled by caller events.
            for (auto& ev : events)
                if (ev.tag == EventTag::WorldCellChange &&
                    ev.ix == o.cellX && ev.iy == o.cellY) o.completed = true;
            break;
        case ObjectiveKind::DeliverItems:
            if (p.inventory.count(o.itemId) >= o.quantity) {
                // Naive: just flag complete; settlement check is caller-side.
                o.completed = true;
            }
            break;
        case ObjectiveKind::DestroyNpc:
            for (auto& ev : events)
                if (ev.tag == EventTag::NpcDeath && int(ev.a) == o.npcType) o.killed++;
            if (o.killed >= o.count) o.completed = true;
            break;
        case ObjectiveKind::WaitAt:
            if (obj_in_radius(p.x, p.y, float(o.ix), float(o.iy), o.radius)) {
                o.hoursWaited += 1; // Caller pushes per-hour ticks
                if (o.hoursWaited >= o.hoursRequired) o.completed = true;
            }
            break;
        case ObjectiveKind::InteractCell:
            for (auto& ev : events)
                if (ev.tag == EventTag::WorldCellChange &&
                    ev.ix == o.ix && ev.iy == o.iy &&
                    ev.s1 == o.action) o.completed = true;
            break;
    }
    (void)time;
    return o.completed;
}

void QuestEngine::tick(std::vector<Quest>& active, EventBus& bus, PlayerState& p, const WorldTime& time) {
    auto& events = bus.last_tick_events();
    for (auto it = active.begin(); it != active.end(); ) {
        bool allDone = true;
        for (auto& o : it->objectives) if (!eval_objective(o, events, p, time)) allDone = false;
        if (allDone) {
            for (auto& r : it->rewards) apply_reward(r, p, bus);
            GameEvent ev; ev.tag = EventTag::QuestCompleted; ev.s1 = it->id;
            bus.emit(ev);
            p.completedQuestIds.push_back(int(std::hash<std::string>{}(it->id) & 0x7fffffff));
            it = active.erase(it);
        } else if (it->expireDay >= 0 && time.day > it->expireDay) {
            GameEvent ev; ev.tag = EventTag::QuestFailed; ev.s1 = it->id;
            bus.emit(ev);
            it = active.erase(it);
        } else {
            ++it;
        }
    }
}

void QuestEngine::accept(std::vector<Quest>& active, Quest q, EventBus& bus) {
    GameEvent ev; ev.tag = EventTag::QuestAccepted; ev.s1 = q.id;
    bus.emit(ev);
    bus.emit_all(q.onAccept);
    active.push_back(std::move(q));
}

void QuestEngine::abandon(std::vector<Quest>& active, const std::string& id, EventBus& bus) {
    for (auto it = active.begin(); it != active.end(); ++it) {
        if (it->id == id) {
            GameEvent ev; ev.tag = EventTag::QuestAbandoned; ev.s1 = id;
            bus.emit(ev);
            active.erase(it);
            return;
        }
    }
}

} // namespace sm

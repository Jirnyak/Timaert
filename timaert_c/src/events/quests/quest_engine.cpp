#include "events/quests/quest_engine.h"
#include "core/torus.h"
#include <algorithm>
#include <utility>

namespace sm {

namespace {

static bool obj_in_radius(const GameState& gs, float px, float py, float cx, float cy, float r) {
    return torus_dist_sq(px, py, cx, cy, float(gs.mapW), float(gs.mapH)) <= r * r;
}

static bool settlement_position(const GameState& gs, int id, float& x, float& y) {
    for (const auto& s : gs.settlements) {
        if (s.id == id) {
            x = float(s.x);
            y = float(s.y);
            return true;
        }
    }
    for (const auto& v : gs.villages) {
        if (v.id == id) {
            x = float(v.x);
            y = float(v.y);
            return true;
        }
    }
    return false;
}

static void emit_reward(const Reward& r, PlayerState& p, EventBus& bus) {
    switch (r.kind) {
        case RewardKind::Gold: {
            GameEvent ev{EventTag::PlayerGoldChange};
            ev.ix = r.amount;
            bus.emit(ev);
            break;
        }
        case RewardKind::Xp: {
            GameEvent ev{EventTag::ApplyEffect};
            ev.s1 = "grant_xp";
            ev.ix = r.amount;
            bus.emit(ev);
            break;
        }
        case RewardKind::Item:
            p.inventory.add(r.itemId, r.amount);
            break;
        case RewardKind::Reputation: {
            GameEvent ev{EventTag::ReputationChange};
            ev.s1 = r.faction;
            ev.ix = r.delta;
            bus.emit(ev);
            break;
        }
        case RewardKind::Event:
            bus.emit(r.event);
            break;
    }
}

static bool eval_objective(Objective& o, const std::vector<GameEvent>& events,
                           GameState& gs) {
    if (o.completed) return true;
    PlayerState& p = gs.player;
    switch (o.kind) {
        case ObjectiveKind::VisitCell:
            if (obj_in_radius(gs, p.x, p.y, float(o.ix), float(o.iy), o.radius)) o.completed = true;
            break;
        case ObjectiveKind::FindLocation:
            for (auto& ev : events) {
                if (ev.tag == EventTag::PlayerMove &&
                    ev.ix == o.cellX && ev.iy == o.cellY) o.completed = true;
            }
            break;
        case ObjectiveKind::DeliverItems:
            {
                float sx = 0.0f, sy = 0.0f;
                if (settlement_position(gs, o.targetSettlementId, sx, sy)
                    && obj_in_radius(gs, p.x, p.y, sx, sy, 3.0f)
                    && p.inventory.count(o.itemId) >= o.quantity) {
                    o.completed = p.inventory.remove(o.itemId, o.quantity);
                }
            }
            break;
        case ObjectiveKind::DestroyNpc:
            for (auto& ev : events)
                if (ev.tag == EventTag::NpcDeath &&
                    (int(ev.a) == o.npcType || ev.ix == o.npcType)) o.killed++;
            if (o.killed >= o.count) o.completed = true;
            break;
        case ObjectiveKind::WaitAt:
            if (obj_in_radius(gs, p.x, p.y, float(o.ix), float(o.iy), o.radius)) {
                for (auto& ev : events) {
                    if (ev.tag == EventTag::TimeAdvance) {
                        o.hoursWaited += ev.ix > 0 ? ev.ix : 1;
                    }
                }
                if (o.hoursWaited >= o.hoursRequired) {
                    o.completed = true;
                }
            }
            break;
        case ObjectiveKind::InteractCell:
            for (auto& ev : events) {
                if (ev.tag == EventTag::WorldCellChange &&
                    ev.ix == o.ix && ev.iy == o.iy) o.completed = true;
                if (ev.tag == EventTag::LandmarkChangeOwner &&
                    (int(ev.a) == o.ix || ev.ix == o.ix)) o.completed = true;
            }
            break;
    }
    return o.completed;
}

} // namespace

void QuestEngine::tick(std::vector<Quest>& active, EventBus& bus, GameState& gs) {
    auto& events = bus.last_tick_events();
    for (auto it = active.begin(); it != active.end(); ) {
        bool allDone = true;
        bool anyUpdated = false;
        for (auto& o : it->objectives) {
            const bool wasDone = o.completed;
            if (!eval_objective(o, events, gs)) allDone = false;
            if (!wasDone && o.completed) anyUpdated = true;
        }
        if (anyUpdated && !allDone) {
            GameEvent ev{EventTag::QuestUpdate};
            ev.s1 = it->id;
            ev.a = std::uint32_t(quest_id_key(it->id));
            bus.emit(ev);
        }
        if (allDone) {
            for (auto& r : it->rewards) emit_reward(r, gs.player, bus);
            GameEvent ev; ev.tag = EventTag::QuestComplete; ev.s1 = it->id;
            ev.a = std::uint32_t(quest_id_key(it->id));
            bus.emit(ev);
            it = active.erase(it);
        } else if (it->expireDay >= 0 && gs.worldTime.day > it->expireDay) {
            GameEvent ev; ev.tag = EventTag::QuestFail; ev.s1 = it->id;
            ev.a = std::uint32_t(quest_id_key(it->id));
            bus.emit(ev);
            it = active.erase(it);
        } else {
            ++it;
        }
    }
}

void QuestEngine::accept(std::vector<Quest>& active,
                         Quest q,
                         const PlayerState& player,
                         EventBus& bus) {
    if (is_known(active, player, q.id)) return;
    GameEvent ev; ev.tag = EventTag::QuestStart; ev.s1 = q.id;
    ev.a = std::uint32_t(quest_id_key(q.id));
    bus.emit(ev);
    bus.emit_all(q.onAccept);
    active.push_back(std::move(q));
}

void QuestEngine::abandon(std::vector<Quest>& active, const std::string& id, EventBus& bus) {
    for (auto it = active.begin(); it != active.end(); ++it) {
        if (it->id == id) {
            GameEvent ev; ev.tag = EventTag::QuestAbandoned; ev.s1 = id;
            ev.a = std::uint32_t(quest_id_key(id));
            bus.emit(ev);
            active.erase(it);
            return;
        }
    }
}

bool QuestEngine::is_known(const std::vector<Quest>& active,
                           const PlayerState& player,
                           const std::string& id) const {
    for (const auto& q : active) {
        if (q.id == id) return true;
    }
    return std::find(player.completedQuestIds.begin(),
                     player.completedQuestIds.end(),
                     id) != player.completedQuestIds.end()
        || std::find(player.failedQuestIds.begin(),
                     player.failedQuestIds.end(),
                     id) != player.failedQuestIds.end();
}

} // namespace sm

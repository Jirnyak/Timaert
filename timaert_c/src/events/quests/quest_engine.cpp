#include "events/quests/quest_engine.h"
#include "core/torus.h"
#include <algorithm>
#include <cstddef>
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

static void push_unique_string(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

static void push_string(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty()) values.push_back(value);
}

static void emit_reward(const Reward& r, PlayerState& p, EventBus& bus) {
    switch (r.kind) {
        case RewardKind::Gold: {
            p.gold += r.amount;
            GameEvent ev{EventTag::PlayerGoldChange};
            ev.ix = r.amount;
            ev.iy = p.gold;
            ev.b = kEventEffectAlreadyApplied;
            bus.emit(ev);
            break;
        }
        case RewardKind::Xp:
            p.sheet.levelData.exp += r.amount;
            break;
        case RewardKind::Item:
            p.inventory.add(r.itemId, r.amount);
            break;
        case RewardKind::Reputation: {
            p.reputation[r.faction] += r.delta;
            GameEvent ev{EventTag::ReputationChange};
            ev.s1 = r.faction;
            ev.ix = r.delta;
            ev.iy = p.reputation[r.faction];
            ev.b = kEventEffectAlreadyApplied;
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
                        ++o.hoursWaited;
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
    std::vector<Quest> completed;
    completed.reserve(active.size());

    for (std::size_t qi = active.size(); qi > 0u; ) {
        --qi;
        Quest& q = active[qi];
        if (q.expireDay >= 0 && gs.worldTime.day > q.expireDay) {
            push_string(gs.player.completedQuestIds, q.id);
            push_unique_string(gs.player.failedQuestIds, q.id);
            GameEvent ev; ev.tag = EventTag::QuestFail; ev.s1 = q.id;
            ev.s2 = "expired";
            ev.a = std::uint32_t(quest_id_key(q.id));
            ev.b = kEventEffectAlreadyApplied;
            bus.emit(ev);
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(qi));
            continue;
        }

        bool allDone = true;
        bool anyUpdated = false;
        for (auto& o : q.objectives) {
            const bool wasDone = o.completed;
            if (!eval_objective(o, events, gs)) allDone = false;
            if (!wasDone && o.completed) anyUpdated = true;
        }
        if (anyUpdated && !allDone) {
            GameEvent ev{EventTag::QuestUpdate};
            ev.s1 = q.id;
            ev.a = std::uint32_t(quest_id_key(q.id));
            bus.emit(ev);
        }
        if (allDone) {
            completed.push_back(std::move(q));
            active.erase(active.begin() + static_cast<std::ptrdiff_t>(qi));
        }
    }

    for (auto& q : completed) {
        push_string(gs.player.completedQuestIds, q.id);
        for (auto& r : q.rewards) emit_reward(r, gs.player, bus);
        GameEvent ev; ev.tag = EventTag::QuestComplete; ev.s1 = q.id;
        ev.a = std::uint32_t(quest_id_key(q.id));
        ev.b = kEventEffectAlreadyApplied;
        bus.emit(ev);
    }
}

void QuestEngine::accept(std::vector<Quest>& active,
                         Quest q,
                         const PlayerState& player,
                         EventBus& bus) {
    (void)player;
    active.push_back(std::move(q));
    const Quest& accepted = active.back();
    bus.emit_all(accepted.onAccept);
    GameEvent ev; ev.tag = EventTag::QuestStart; ev.s1 = accepted.id;
    ev.s2 = accepted.title;
    ev.a = std::uint32_t(quest_id_key(accepted.id));
    bus.emit(ev);
}

void QuestEngine::abandon(std::vector<Quest>& active, const std::string& id, EventBus& bus) {
    for (auto it = active.begin(); it != active.end(); ++it) {
        if (it->id == id) {
            GameEvent ev; ev.tag = EventTag::QuestFail; ev.s1 = id;
            ev.s2 = "abandoned";
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

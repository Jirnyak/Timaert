#include "events/quests/quest_engine.h"
#include "macro/agent_memory.h"
#include "macro/currency.h"
#include "events/event_log_util.h"
#include "core/torus.h"
#include "macro/markers.h"
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

// Resolve the world cell an objective points at, mirroring exactly the fields
// eval_objective() checks for spatial completion. Returns false for objectives
// with no fixed map cell — a DestroyNpc kill-count, or a delivery whose target
// settlement no longer exists — so those never get a pin.
static bool objective_target_cell(const GameState& gs, const Objective& o,
                                  float& x, float& y) {
    switch (o.kind) {
        case ObjectiveKind::VisitCell:
        case ObjectiveKind::WaitAt:
        case ObjectiveKind::InteractCell:
            x = float(o.ix);    y = float(o.iy);    return true;
        case ObjectiveKind::FindLocation:
            x = float(o.cellX); y = float(o.cellY); return true;
        case ObjectiveKind::DeliverItems:
            return settlement_position(gs, o.targetSettlementId, x, y);
        case ObjectiveKind::DestroyNpc:
            return false;
    }
    return false;
}


// Takes the GameState because a Reputation reward moves the player's row in the
// ONE relation matrix — his standing is not a map of his own any more.
static void emit_reward(const Reward& r, GameState& gs, EventBus& bus,
                        int giverSettlementId) {
    PlayerState& p = gs.player;
    switch (r.kind) {
        case RewardKind::Gold: {
            if (r.amount >= 0) {
                // v1 pays imperial; the giver's own mint when the engine
                // learns factions.
                p.inventory.add("coin_empire", r.amount);
            } else {
                // A penalty takes what the wallet holds; the SHORTFALL is a
                // DEBT FACT — «кто-то должен кому-то столько-то» (owner) —
                // remembered entity-about-entity and summed by the fact
                // arithmetic. When macro relations arrive, this bites.
                const int paid = wallet_spend_up_to(p.inventory, -r.amount);
                const int short_ = -r.amount - paid;
                if (short_ > 0 && giverSettlementId >= 0) {
                    remember(p.memory,
                             make_debt_fact(kDebtToSettlement,
                                            std::uint16_t(giverSettlementId),
                                            short_,
                                            gs.worldTime.day()));
                }
            }
            GameEvent ev{EventTag::PlayerGoldChange};
            ev.ix = r.amount;
            ev.iy = wallet_value(p.inventory);
            ev.b = kEventEffectAlreadyApplied;
            bus.emit(ev);
            break;
        }
        case RewardKind::Xp:
            // Through the one grant path: a contract that pays a level PAYS it.
            // wis dividend applies here like on every grant (owner ruling).
            award_exp(p.sheet.levelData, r.amount,
                      calculate_derived(p.sheet.attributes,
                                        p.sheet.skills).expMult);
            break;
        case RewardKind::Item:
            p.inventory.add(r.itemId, r.amount);
            break;
        case RewardKind::Reputation: {
            add_player_reputation(gs, r.faction.c_str(), r.delta);
            GameEvent ev{EventTag::ReputationChange};
            ev.s1 = r.faction;
            ev.ix = r.delta;
            ev.iy = player_reputation(&gs, r.faction.c_str());
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
            // The KIND of the body that died, and nothing else. NpcDeath carries
            // the dead entity's handle in `a`, its killer in `b` and its type in
            // `ix` (every emitter, without exception). The old condition also
            // accepted `a == npcType`, comparing an ENTITY HANDLE against a type
            // ordinal: handles are small integers too, so the fourth entity in
            // the scene dying counted as a kill of type 4, and a "slay 3 bandits"
            // contract could be completed by three dead rabbits.
            for (auto& ev : events)
                if (ev.tag == EventTag::NpcDeath && ev.ix == o.npcType) o.killed++;
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
            // ONE contract for both tags: the event names the CELL it happened
            // on, in (ix,iy) — the same pair the objective stores. The
            // LandmarkChangeOwner arm used to accept `a == o.ix` OR `ix == o.ix`
            // alone, i.e. a settlement id, or an x with any y, satisfying an
            // objective about a specific cell. It has no emitter anywhere yet, so
            // this is the contract its future emitter must honour rather than a
            // behaviour anyone can be relying on.
            for (auto& ev : events) {
                if ((ev.tag == EventTag::WorldCellChange
                     || ev.tag == EventTag::LandmarkChangeOwner)
                    && ev.ix == o.ix && ev.iy == o.iy) o.completed = true;
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
        if (q.expireDay >= 0 && gs.worldTime.day() > q.expireDay) {
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
        for (auto& r : q.rewards) emit_reward(r, gs, bus, q.giverSettlementId);
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

void rebuild_quest_markers(GameState& gs, const std::vector<Quest>& active) {
    remove_markers_by_prefix(gs.markers, "quest_");
    for (const Quest& q : active) {
        for (std::size_t oi = 0; oi < q.objectives.size(); ++oi) {
            const Objective& o = q.objectives[oi];
            if (o.completed) continue;
            float x = 0.0f, y = 0.0f;
            if (!objective_target_cell(gs, o, x, y)) continue;
            std::string id = "quest_" + q.id + "_" + std::to_string(oi);
            add_marker(gs.markers, std::move(id), MarkerStyle::Quest, x, y, q.title);
        }
    }
}

} // namespace sm

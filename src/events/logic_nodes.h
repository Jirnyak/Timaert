// LogicNode engine — condition vector → effect graph. Mirrors logic-nodes.ts.
#pragma once
#include "core/small_function.h"
#include "events/event_bus.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sm {

struct PlayerState; // fwd
struct NodeContext;

using EventPredicate = SmallFunction<bool(const GameEvent&)>;
using NodeEffect     = SmallFunction<void(NodeContext&)>;
using PureCheck      = SmallFunction<bool(const EventBus&, const PlayerState&)>;

struct ConditionSlot {
    bool isEvent = true;
    EventTag tag = EventTag::Custom;
    EventPredicate predicate;
    PureCheck      check;
};

struct LogicNode {
    std::string id;
    std::string label;
    std::vector<ConditionSlot> conditions;
    std::vector<std::uint8_t>  mask;
    NodeEffect                 effect;
    std::vector<std::string>   next;
    std::vector<std::string>   tags;
};

class LogicNodeEngine;

struct NodeContext {
    EventBus*    bus;
    PlayerState* player;
    LogicNodeEngine* engine;
    void add_node(LogicNode n);
    void remove_node(const std::string& id);
    void activate(const std::string& id);
};

class LogicNodeEngine {
public:
    // Register a node definition. Activation is explicit, matching TS.
    void add(LogicNode n);
    void remove(const std::string& id);
    void activate(const std::string& id);
    void deactivate(const std::string& id);
    void reset();
    std::size_t node_count() const { return nodes_.size(); }
    std::size_t active_count() const { return active_.size(); }
    bool has(const std::string& id) const { return nodes_.find(id) != nodes_.end(); }
    bool is_active(const std::string& id) const;
    bool is_consistent() const;
    // Save-snapshot views (Session 17): node DEFINITIONS are code and are
    // re-registered on every boot; what persists is which of them still
    // EXIST (a consumed one-shot stays consumed) and which are ACTIVE.
    const std::vector<std::string>& active_ids() const { return active_; }
    std::vector<std::string> node_ids() const {
        std::vector<std::string> out;
        out.reserve(nodes_.size());
        for (const auto& [id, n] : nodes_) out.push_back(id);
        return out;
    }

    // Run BEFORE game logic each tick; consumes lastTickEvents.
    void tick(EventBus& bus, PlayerState& player);

private:
    std::unordered_map<std::string, LogicNode> nodes_;
    std::vector<std::string>                   active_;
    std::vector<std::string>                   toRemove_;
    std::vector<std::string>                   toAdd_;
    std::vector<std::string>                   nextSnapshot_;
};

} // namespace sm

// LogicNode engine — condition vector → effect graph. Mirrors logic-nodes.ts.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "events/event_bus.h"

namespace sm {

struct PlayerState; // fwd
struct NodeContext;

using EventPredicate = std::function<bool(const GameEvent&)>;
using NodeEffect     = std::function<void(NodeContext&)>;
using PureCheck      = std::function<bool(const EventBus&, const PlayerState&)>;

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
    void add(LogicNode n);
    void remove(const std::string& id);
    void activate(const std::string& id);
    void deactivate(const std::string& id);

    // Run BEFORE game logic each tick; consumes lastTickEvents.
    void tick(EventBus& bus, PlayerState& player);

private:
    std::unordered_map<std::string, LogicNode> nodes_;
    std::unordered_set<std::string>            active_;
};

} // namespace sm

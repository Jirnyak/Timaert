#include "events/logic_nodes.h"

namespace sm {

void NodeContext::add_node(LogicNode n)              { engine->add(std::move(n)); }
void NodeContext::remove_node(const std::string& id) { engine->remove(id); }
void NodeContext::activate(const std::string& id)    { engine->activate(id); }

void LogicNodeEngine::add(LogicNode n) {
    auto id = n.id;
    nodes_[id] = std::move(n);
    active_.insert(id);
}
void LogicNodeEngine::remove(const std::string& id) {
    nodes_.erase(id);
    active_.erase(id);
}
void LogicNodeEngine::activate(const std::string& id)   { if (nodes_.count(id)) active_.insert(id); }
void LogicNodeEngine::deactivate(const std::string& id) { active_.erase(id); }

void LogicNodeEngine::tick(EventBus& bus, PlayerState& player) {
    if (active_.empty()) return;
    NodeContext ctx{&bus, &player, this};
    std::vector<std::string> toFire;
    toFire.reserve(active_.size());
    auto& last = bus.last_tick_events();

    for (auto& id : active_) {
        auto it = nodes_.find(id);
        if (it == nodes_.end()) continue;
        const auto& node = it->second;
        bool ok = true;
        for (std::size_t i = 0; i < node.conditions.size(); ++i) {
            if (i < node.mask.size() && node.mask[i] == 0) continue;
            const auto& slot = node.conditions[i];
            bool match = false;
            if (slot.isEvent) {
                for (auto& ev : last) {
                    if (ev.tag == slot.tag && (!slot.predicate || slot.predicate(ev))) {
                        match = true; break;
                    }
                }
            } else if (slot.check) {
                match = slot.check(bus, player);
            }
            if (!match) { ok = false; break; }
        }
        if (ok) toFire.push_back(id);
    }

    for (auto& id : toFire) {
        auto it = nodes_.find(id);
        if (it == nodes_.end()) continue;
        if (it->second.effect) it->second.effect(ctx);
        auto next = it->second.next;
        active_.erase(id);
        nodes_.erase(it);
        for (auto& nid : next) activate(nid);
    }
}

} // namespace sm

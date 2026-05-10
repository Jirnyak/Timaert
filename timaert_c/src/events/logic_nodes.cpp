#include "events/logic_nodes.h"

namespace sm {

void NodeContext::add_node(LogicNode n)              { engine->add(std::move(n)); }
void NodeContext::remove_node(const std::string& id) { engine->remove(id); }
void NodeContext::activate(const std::string& id)    { engine->activate(id); }

void LogicNodeEngine::add(LogicNode n) {
    auto id = n.id;
    nodes_[id] = std::move(n);
    active_.insert(id);
    if (pendingFire_.capacity() < active_.size()) {
        pendingFire_.reserve(active_.size());
    }
}
void LogicNodeEngine::remove(const std::string& id) {
    nodes_.erase(id);
    active_.erase(id);
}
void LogicNodeEngine::activate(const std::string& id)   { if (nodes_.count(id)) active_.insert(id); }
void LogicNodeEngine::deactivate(const std::string& id) { active_.erase(id); }
void LogicNodeEngine::reset() {
    nodes_.clear();
    active_.clear();
    pendingFire_.clear();
}

bool LogicNodeEngine::is_consistent() const {
    for (const auto& id : active_) {
        if (nodes_.find(id) == nodes_.end()) return false;
    }
    return active_.size() <= nodes_.size();
}

void LogicNodeEngine::tick(EventBus& bus, PlayerState& player) {
    if (active_.empty()) return;
    NodeContext ctx{&bus, &player, this};
    pendingFire_.clear();
    auto& last = bus.last_tick_events();

    for (const auto& id : active_) {
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
        if (ok) pendingFire_.push_back(&it->first);
    }

    for (const std::string* id : pendingFire_) {
        auto it = nodes_.find(*id);
        if (it == nodes_.end()) continue;
        if (it->second.effect) it->second.effect(ctx);
        bool keepActive = false;
        for (const auto& nid : it->second.next) {
            if (nid == *id) {
                keepActive = true;
                continue;
            }
            activate(nid);
        }
        if (!keepActive) active_.erase(*id);
    }
}

} // namespace sm

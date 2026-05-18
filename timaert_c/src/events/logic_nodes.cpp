#include "events/logic_nodes.h"
#include <algorithm>

namespace sm {

void NodeContext::add_node(LogicNode n)              { engine->add(std::move(n)); }
void NodeContext::remove_node(const std::string& id) { engine->remove(id); }
void NodeContext::activate(const std::string& id)    { engine->activate(id); }

void LogicNodeEngine::add(LogicNode n) {
    auto id = n.id;
    if (nextSnapshot_.capacity() < n.next.size()) {
        nextSnapshot_.reserve(n.next.size());
    }
    nodes_[id] = std::move(n);
}
void LogicNodeEngine::remove(const std::string& id) {
    nodes_.erase(id);
    deactivate(id);
}
void LogicNodeEngine::activate(const std::string& id) {
    if (!is_active(id)) active_.push_back(id);
}
void LogicNodeEngine::deactivate(const std::string& id) {
    active_.erase(std::remove(active_.begin(), active_.end(), id),
                  active_.end());
}
void LogicNodeEngine::reset() {
    nodes_.clear();
    active_.clear();
    toRemove_.clear();
    toAdd_.clear();
    nextSnapshot_.clear();
}

bool LogicNodeEngine::is_active(const std::string& id) const {
    return std::find(active_.begin(), active_.end(), id) != active_.end();
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
    toRemove_.clear();
    toAdd_.clear();
    auto& last = bus.last_tick_events();

    for (std::size_t activeIdx = 0; activeIdx < active_.size(); ) {
        const std::string id = active_[activeIdx];
        auto it = nodes_.find(id);
        if (it == nodes_.end()) {
            toRemove_.push_back(id);
            if (activeIdx < active_.size() && active_[activeIdx] == id) {
                ++activeIdx;
            }
            continue;
        }
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
        if (ok) {
            NodeEffect effect = node.effect;
            nextSnapshot_.assign(node.next.begin(), node.next.end());
            if (effect) effect(ctx);
            toRemove_.push_back(id);
            for (const auto& nid : nextSnapshot_) {
                toAdd_.push_back(nid);
            }
            nextSnapshot_.clear();
        }

        if (activeIdx < active_.size() && active_[activeIdx] == id) {
            ++activeIdx;
        }
    }

    for (const auto& id : toRemove_) {
        deactivate(id);
    }
    for (const auto& id : toAdd_) {
        activate(id);
    }
}

} // namespace sm

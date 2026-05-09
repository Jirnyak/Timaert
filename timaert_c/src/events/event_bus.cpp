#include "events/event_bus.h"
#include <algorithm>

namespace sm {

void EventBus::emit(const GameEvent& ev) {
    tick_.push_back(ev);
    for (auto& s : subs_) if (s.tag == ev.tag) s.h(ev);
}

void EventBus::emit_all(const std::vector<GameEvent>& evs) {
    for (auto& e : evs) emit(e);
}

std::uint32_t EventBus::on(EventTag tag, Handler h) {
    std::uint32_t id = nextSubId_++;
    subs_.push_back({id, tag, std::move(h)});
    return id;
}

void EventBus::unsubscribe(std::uint32_t id) {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
        [&](const Sub& s) { return s.id == id; }), subs_.end());
}

void EventBus::flush(int day, int hour) {
    history_.reserve(history_.size() + tick_.size());
    for (auto& e : tick_) history_.push_back({tickCounter_, day, hour, e});
    last_ = std::move(tick_);
    tick_.clear();
    tickCounter_++;
}

bool EventBus::has_tag(EventTag tag) const {
    for (auto& e : tick_) if (e.tag == tag) return true;
    return false;
}

const GameEvent* EventBus::find(EventTag tag) const {
    for (auto& e : tick_) if (e.tag == tag) return &e;
    return nullptr;
}

std::vector<const GameEvent*> EventBus::find_all(EventTag tag) const {
    std::vector<const GameEvent*> out;
    for (auto& e : tick_) if (e.tag == tag) out.push_back(&e);
    return out;
}

std::vector<WorldHistoryEntry> EventBus::query_history(EventTag tag, std::size_t limit) const {
    std::vector<WorldHistoryEntry> out;
    // Newest-first scan, like TS.
    for (auto it = history_.rbegin(); it != history_.rend() && out.size() < limit; ++it) {
        if (it->event.tag == tag) out.push_back(*it);
    }
    return out;
}

void EventBus::trim_history(std::size_t maxEntries) {
    if (history_.size() > maxEntries) {
        history_.erase(history_.begin(),
                       history_.begin() + (history_.size() - maxEntries));
    }
}

void EventBus::reset() {
    tick_.clear();
    last_.clear();
    history_.clear();
    tickCounter_ = 0;
    subs_.clear();
}

} // namespace sm

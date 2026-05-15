#include "events/event_bus.h"
#include <algorithm>
#include <cstddef>
#include <utility>

namespace sm {

namespace {
constexpr std::size_t kMaxHistoryEntries = 4096;
constexpr std::size_t kInitialTickEvents = 64;
constexpr std::size_t kInitialSubscriptions = 8;
} // namespace

EventBus::EventBus() {
    tick_.reserve(kInitialTickEvents);
    last_.reserve(kInitialTickEvents);
    subs_.reserve(kInitialSubscriptions);
    pendingAdds_.reserve(2);
}

void EventBus::emit(const GameEvent& ev) {
    tick_.push_back(ev);
    ++dispatchDepth_;
    const std::size_t initialCount = subs_.size();
    for (std::size_t i = 0; i < initialCount; ++i) {
        Sub& s = subs_[i];
        if (s.active && s.tag == ev.tag) {
            s.h(ev);
        }
    }
    --dispatchDepth_;
    if (dispatchDepth_ == 0) {
        apply_pending_subscriptions();
    }
}

void EventBus::emit_all(const std::vector<GameEvent>& evs) {
    for (auto& e : evs) emit(e);
}

std::uint32_t EventBus::on(EventTag tag, Handler h) {
    std::uint32_t id = nextSubId_++;
    Sub sub{id, tag, std::move(h), true};
    if (dispatchDepth_ > 0) {
        pendingAdds_.push_back(std::move(sub));
    } else {
        subs_.push_back(std::move(sub));
    }
    return id;
}

void EventBus::unsubscribe(std::uint32_t id) {
    if (dispatchDepth_ > 0) {
        for (auto& s : subs_) {
            if (s.id == id) s.active = false;
        }
        for (auto& s : pendingAdds_) {
            if (s.id == id) s.active = false;
        }
        return;
    }
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
        [&](const Sub& s) { return s.id == id; }), subs_.end());
}

bool EventBus::has_subscribers(EventTag tag) const {
    for (const auto& s : subs_) if (s.active && s.tag == tag) return true;
    for (const auto& s : pendingAdds_) if (s.active && s.tag == tag) return true;
    return false;
}

void EventBus::flush(int day, int hour) {
    if (!tick_.empty()) {
        if (history_.capacity() < kMaxHistoryEntries) {
            history_.reserve(kMaxHistoryEntries);
        }
        const std::size_t incoming = tick_.size();
        if (incoming >= kMaxHistoryEntries) {
            history_.clear();
            const std::size_t start = incoming - kMaxHistoryEntries;
            for (std::size_t i = start; i < incoming; ++i) {
                history_.push_back({tickCounter_, day, hour, tick_[i]});
            }
        } else {
            const std::size_t total = history_.size() + incoming;
            if (total > kMaxHistoryEntries) {
                const std::size_t drop = total - kMaxHistoryEntries;
                history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
            }
            for (auto& e : tick_) history_.push_back({tickCounter_, day, hour, e});
        }
    }
    last_.swap(tick_);
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
    out.reserve(std::min<std::size_t>(tick_.size(), 4u));
    for (auto& e : tick_) if (e.tag == tag) out.push_back(&e);
    return out;
}

std::vector<WorldHistoryEntry> EventBus::query_history(EventTag tag, std::size_t limit) const {
    std::vector<WorldHistoryEntry> out;
    out.reserve(std::min(limit, history_.size()));
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
    nextSubId_ = 1;
    if (dispatchDepth_ > 0) {
        for (auto& s : subs_) s.active = false;
        pendingAdds_.clear();
        resetPending_ = true;
        return;
    }
    subs_.clear();
    pendingAdds_.clear();
    resetPending_ = false;
}

void EventBus::compact_subscriptions() {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
        [](const Sub& s) { return !s.active; }), subs_.end());
}

void EventBus::apply_pending_subscriptions() {
    if (resetPending_) {
        subs_.clear();
        pendingAdds_.clear();
        resetPending_ = false;
        return;
    }

    compact_subscriptions();
    if (pendingAdds_.empty()) return;

    subs_.reserve(subs_.size() + pendingAdds_.size());
    for (auto& s : pendingAdds_) {
        if (s.active) {
            subs_.push_back(std::move(s));
        }
    }
    pendingAdds_.clear();
}

} // namespace sm

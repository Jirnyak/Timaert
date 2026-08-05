#include "events/event_bus.h"
#include <algorithm>
#include <cstddef>
#include <utility>

namespace sm {

namespace {
constexpr std::size_t kMaxHistoryEntries = 4096;
constexpr std::size_t kInitialTickEvents = 64;
constexpr std::size_t kInitialSubscriptions = 8;

std::size_t tag_index(EventTag tag) {
    return static_cast<std::size_t>(tag);
}
} // namespace

EventBus::EventBus() {
    tick_.reserve(kInitialTickEvents);
    last_.reserve(kInitialTickEvents);
}

void EventBus::emit(const GameEvent& ev) {
    tick_.push_back(ev);
    const std::size_t idx = tag_index(ev.tag);
    if (idx >= subsByTag_.size()) return;

    auto& subs = subsByTag_[idx];
    for (std::size_t i = 0; i < subs.size(); ++i) {
        Handler handler = subs[i].h;
        handler(ev);
    }
}

void EventBus::emit_all(const std::vector<GameEvent>& evs) {
    for (auto& e : evs) emit(e);
}

std::uint32_t EventBus::on(EventTag tag, Handler h) {
    std::uint32_t id = nextSubId_++;
    const std::size_t idx = tag_index(tag);
    if (idx >= subsByTag_.size()) return 0;

    auto& subs = subsByTag_[idx];
    if (subs.capacity() == 0u) subs.reserve(kInitialSubscriptions);
    subs.push_back(Sub{id, tag, std::move(h)});
    ++subscriptionCount_;
    return id;
}

void EventBus::unsubscribe(std::uint32_t id) {
    for (auto& subs : subsByTag_) {
        auto it = std::find_if(subs.begin(), subs.end(),
            [&](const Sub& s) { return s.id == id; });
        if (it == subs.end()) continue;

        subs.erase(it);
        if (subscriptionCount_ > 0u) --subscriptionCount_;
        return;
    }
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

std::vector<WorldHistoryEntry> EventBus::query_history(EventTag tag, std::size_t limit) const {
    std::vector<WorldHistoryEntry> out;
    out.reserve(std::min(limit, history_.size()));
    // Newest-first scan, like TS.
    for (auto it = history_.rbegin(); it != history_.rend() && out.size() < limit; ++it) {
        if (it->event.tag == tag) out.push_back(*it);
    }
    return out;
}

void EventBus::reset() {
    tick_.clear();
    last_.clear();
    history_.clear();
    tickCounter_ = 0;
    nextSubId_ = 1;
    for (auto& subs : subsByTag_) subs.clear();
    subscriptionCount_ = 0;
}

} // namespace sm

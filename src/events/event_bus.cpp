#include "events/event_bus.h"
#include <algorithm>
#include <cstddef>
#include <utility>

namespace sm {

namespace {
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

std::uint32_t EventBus::record(const WorldFact& fact) {
    if (!chronicle_) return 0u;
    return chronicle_record(*chronicle_, fact);
}

void EventBus::flush() {
    last_.swap(tick_);
    tick_.clear();
    // The frame's facts are a RANGE, not a buffer: where this frame started
    // becomes where the last one did, and the new frame starts at whatever
    // the chronicle has reached.
    lastFrameFirstSeq_ = frameFirstSeq_;
    frameFirstSeq_ = chronicle_ ? chronicle_->nextSeq : frameFirstSeq_;
    tickCounter_++;
}

void EventBus::reset() {
    tick_.clear();
    last_.clear();
    // The chronicle is the WORLD's and is not the bus's to clear; the
    // bookmarks are the bus's and are.
    frameFirstSeq_ = chronicle_ ? chronicle_->nextSeq : 1u;
    lastFrameFirstSeq_ = frameFirstSeq_;
    tickCounter_ = 0;
    nextSubId_ = 1;
    for (auto& subs : subsByTag_) subs.clear();
    subscriptionCount_ = 0;
}

} // namespace sm

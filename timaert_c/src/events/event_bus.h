// Tick-buffered event bus + history. Mirrors event-bus.ts.
#pragma once

#include "core/small_function.h"
#include "events/event_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm {

class EventBus {
public:
    using Handler = SmallFunction<void(const GameEvent&)>;

    EventBus();

    void emit(const GameEvent& ev);
    void emit_all(const std::vector<GameEvent>& evs);

    // Subscribe to a specific tag. Returns subscription id for unsubscribe.
    std::uint32_t on(EventTag tag, Handler h);
    void unsubscribe(std::uint32_t id);
    bool has_subscribers(EventTag tag) const;

    // Move tick buffer to history; promote to lastTickEvents.
    void flush(int day, int hour);

    // Query helpers (TS-faithful).
    bool has_tag(EventTag tag) const;
    const GameEvent* find(EventTag tag) const;
    std::vector<const GameEvent*> find_all(EventTag tag) const;
    std::vector<WorldHistoryEntry> query_history(EventTag tag, std::size_t limit = 50) const;
    void trim_history(std::size_t maxEntries);
    void reset();
    std::uint32_t tick() const { return tickCounter_; }
    std::size_t subscription_count() const { return subs_.size(); }

    const std::vector<GameEvent>& tick_events() const { return tick_; }
    const std::vector<GameEvent>& last_tick_events() const { return last_; }
    const std::vector<WorldHistoryEntry>& history() const { return history_; }

private:
    struct Sub {
        std::uint32_t id;
        EventTag tag;
        Handler h;
        bool active = true;
    };

    void compact_subscriptions();
    void apply_pending_subscriptions();

    std::vector<GameEvent> tick_;
    std::vector<GameEvent> last_;
    std::vector<WorldHistoryEntry> history_;
    std::uint32_t tickCounter_ = 0;
    std::uint32_t nextSubId_ = 1;
    std::uint32_t dispatchDepth_ = 0;
    bool resetPending_ = false;

    std::vector<Sub> subs_;
    std::vector<Sub> pendingAdds_;
};

} // namespace sm

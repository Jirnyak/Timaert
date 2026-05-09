// Tick-buffered event bus + history. Mirrors event-bus.ts.
#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include "events/event_types.h"

namespace sm {

class EventBus {
public:
    using Handler = std::function<void(const GameEvent&)>;

    void emit(const GameEvent& ev);
    void emit_all(const std::vector<GameEvent>& evs);

    // Subscribe to a specific tag. Returns subscription id (use unsubscribe).
    std::uint32_t on(EventTag tag, Handler h);
    void unsubscribe(std::uint32_t id);

    // Move tick buffer → history; promote to lastTickEvents.
    void flush(int day, int hour);

    // ── Query helpers (TS-faithful). ──
    bool has_tag(EventTag tag) const;
    const GameEvent* find(EventTag tag) const;
    std::vector<const GameEvent*> find_all(EventTag tag) const;
    std::vector<WorldHistoryEntry> query_history(EventTag tag, std::size_t limit = 50) const;
    void trim_history(std::size_t maxEntries);
    void reset();
    std::uint32_t tick() const { return tickCounter_; }

    const std::vector<GameEvent>& tick_events() const { return tick_; }
    const std::vector<GameEvent>& last_tick_events() const { return last_; }
    const std::vector<WorldHistoryEntry>& history() const { return history_; }

private:
    std::vector<GameEvent>           tick_;
    std::vector<GameEvent>           last_;
    std::vector<WorldHistoryEntry>   history_;
    std::uint32_t                    tickCounter_ = 0;
    std::uint32_t                    nextSubId_ = 1;

    struct Sub { std::uint32_t id; EventTag tag; Handler h; };
    std::vector<Sub> subs_;
};

} // namespace sm

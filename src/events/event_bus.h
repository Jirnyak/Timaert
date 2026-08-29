// Tick-buffered event bus. The old 4096-entry history ring is gone: the
// world's past lives in the chronicle and the player's journal (CANON S20.1),
// and a frame's events live in tick_/last_ — a fourth store had no reader
// that those could not serve.
#pragma once

#include "core/small_function.h"
#include "events/event_types.h"
#include "macro/chronicle.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace sm {

class EventBus {
public:
    using Handler = SmallFunction<void(const GameEvent&)>;

    EventBus();

    void emit(const GameEvent& ev);
    void emit_all(const std::vector<GameEvent>& evs);

    // ── THE DOOR INTO THE WORLD'S MEMORY (owner, 2026-08-27) ─────────────
    // «Шина становится ЛЕТОПИСЬЮ целиком»: the bus is the nervous system of
    // the WORLD, not a channel of notifications, so what it carries is a FACT
    // and where facts land is the chronicle (CANON S20.1).
    //
    // Why a second verb beside `emit` and not a mirror inside it: only the
    // EMITTER knows the ordinals. An event carries ECS entity bits; a fact
    // carries save-stable identities (MacroSpawnId, landmark id, faction
    // index), and no table the bus could hold would translate between them.
    // So the caller builds the sentence and the bus files it. `emit` remains
    // for what is genuinely a notification of this frame, and shrinks as the
    // world's happenings move over here.
    //
    // Null chronicle (a headless test, a frame before the world) records
    // nothing and says so by returning 0 — a refusal, not a crash.
    void attach_chronicle(Chronicle* c) { chronicle_ = c; }
    const Chronicle* chronicle() const { return chronicle_; }
    std::uint32_t record(const WorldFact& fact);

    // THIS FRAME'S facts, as a RANGE of sequence numbers rather than a second
    // buffer — which is the whole point of the merge. `frame_first_seq()` is
    // where the frame began; everything from there to the chronicle's next
    // sequence happened during it.
    std::uint32_t frame_first_seq() const { return frameFirstSeq_; }
    std::uint32_t last_frame_first_seq() const { return lastFrameFirstSeq_; }

    // Subscribe to a specific tag. Returns subscription id for unsubscribe.
    // NOTE: production code CONSUMES BY POLLING (tick_events /
    // last_tick_events); the one live subscriber is a smoke harness. The old
    // per-tick query helpers (has_subscribers / has_tag / find / find_all /
    // trim_history) had zero production callers and were deleted 2026-08-05 —
    // a consumer that needs them scans tick_events() itself.
    std::uint32_t on(EventTag tag, Handler h);
    void unsubscribe(std::uint32_t id);

    // Promote the tick buffer to lastTickEvents.
    void flush();

    void reset();
    std::uint32_t tick() const { return tickCounter_; }
    std::size_t subscription_count() const { return subscriptionCount_; }

    const std::vector<GameEvent>& tick_events() const { return tick_; }
    const std::vector<GameEvent>& last_tick_events() const { return last_; }

private:
    struct Sub {
        std::uint32_t id;
        EventTag tag;
        Handler h;
    };

    std::vector<GameEvent> tick_;
    std::vector<GameEvent> last_;

    // Not owned: the chronicle is the WORLD's (GameState), and the bus is the
    // door into it. A bus that owned the world's memory would be a second
    // place the past could live.
    Chronicle* chronicle_ = nullptr;
    std::uint32_t frameFirstSeq_ = 1;
    std::uint32_t lastFrameFirstSeq_ = 1;
    std::uint32_t tickCounter_ = 0;
    std::uint32_t nextSubId_ = 1;

    static constexpr std::size_t kEventTagSlotCount =
        static_cast<std::size_t>(EventTag::LastSerializable) + 1u;
    std::array<std::vector<Sub>, kEventTagSlotCount> subsByTag_{};
    std::size_t subscriptionCount_ = 0;
};

} // namespace sm

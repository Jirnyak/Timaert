// Agent memory (owner's design, W2b): what a squad's leader REMEMBERS.
//
// A caravan sets out remembering its city's market as it stood at departure;
// a peasant may one day remember that his village was raided. The system is
// the owner's brief made structural: entries of DIFFERENT KINDS under one
// roof ("под флажками разное"), flexible and extensible — a new kind of
// memory is a row in the kind enum and a pack/unpack pair, never a new
// component — and BOUNDED, budgeted for the 16384-squad world cap:
//
//     8 slots × 16 B + 8 B header = 136 B per agent
//     16384 agents × 136 B        ≈ 2.2 MiB — the whole world's memory
//
// One entry is a fixed 16-byte envelope: kind + flags + a subject (whose
// market, which village) + the day it was formed (staleness / eviction) + an
// 8-byte payload each kind packs its own way. Remembering the same
// (kind, subject) overwrites — an agent holds ONE current belief about a
// thing, not a history; past the cap the OLDEST memory is evicted, because
// forgetting is what a bounded head does.
//
// POD throughout, no hidden padding: the block rides the macro snapshot
// verbatim (save v28).
#pragma once
#include <cstdint>

#include "macro/commodity.h"
#include "macro/items.h"

namespace sm {

enum class AgentMemoryKind : std::uint8_t {
    None = 0,
    // The subject settlement's market as last seen: per-commodity STOCK
    // CLASS in 4-bit nibbles (payload[i>>1], low nibble first) —
    // 0 none / 1 scarce / 2 stocked / 3 plenty. Fourteen commodities fit in
    // seven of the eight payload bytes.
    MarketSnapshot = 1,
    // A DEBT owed to the subject (owner's ruling: debt is a FACT, not a
    // reputation dent — «кто-то должен кому-то столько-то»). payload[0..3] =
    // amount in universal value (i32, LE); flags = which id space `subject`
    // names (kDebtToSettlement / kDebtToFaction / kDebtToSquad). Remembered
    // entity-about-entity; when macro relations arrive, this is what bites.
    Debt = 2,
};

// FACT ARITHMETIC (owner): same-typed facts COMBINE — one binary fold per
// kind, a table, not branching call sites. A snapshot REPLACES the old
// belief; debts SUM.
enum class MemoryFold : std::uint8_t { Replace = 0, Sum = 1 };

inline MemoryFold fold_for_kind(std::uint8_t kind) {
    return kind == std::uint8_t(AgentMemoryKind::Debt) ? MemoryFold::Sum
                                                       : MemoryFold::Replace;
}

// Debt flags: which id space the creditor lives in.
inline constexpr std::uint8_t kDebtToSettlement = 0;
inline constexpr std::uint8_t kDebtToFaction    = 1;
inline constexpr std::uint8_t kDebtToSquad      = 2;

struct MemoryEntry {
    std::uint8_t  kind = 0;         // AgentMemoryKind
    std::uint8_t  flags = 0;
    std::uint16_t subject = 0;      // kind-specific: settlement id, ordinal…
    std::uint32_t day = 0;          // when formed (staleness, eviction order)
    std::uint8_t  payload[8]{};     // kind-specific packing
};
static_assert(sizeof(MemoryEntry) == 16, "one memory is a 16-byte envelope");

inline constexpr int kAgentMemorySlots = 8;   // po2; the whole-world budget

struct AgentMemory {
    MemoryEntry  slots[kAgentMemorySlots]{};
    std::uint8_t count = 0;
    std::uint8_t pad[7]{};          // explicit: padding is not state
};
static_assert(sizeof(AgentMemory) == kAgentMemorySlots * 16 + 8,
              "the memory block is padding-free and rides the save verbatim");

// The i32 riding payload[0..3] — the Sum fold's number, debt's amount.
inline std::int32_t memory_amount(const MemoryEntry& e) {
    std::int32_t v = 0;
    v |= std::int32_t(e.payload[0]);
    v |= std::int32_t(e.payload[1]) << 8;
    v |= std::int32_t(e.payload[2]) << 16;
    v |= std::int32_t(e.payload[3]) << 24;
    return v;
}
inline void set_memory_amount(MemoryEntry& e, std::int32_t v) {
    e.payload[0] = std::uint8_t(v);
    e.payload[1] = std::uint8_t(v >> 8);
    e.payload[2] = std::uint8_t(v >> 16);
    e.payload[3] = std::uint8_t(v >> 24);
}

// Remember: same (kind, subject) FOLDS by the kind's own law — a snapshot
// replaces the current belief, a debt sums onto the running total (the fact
// arithmetic). A full head evicts the OLDEST entry.
inline void remember(AgentMemory& m, const MemoryEntry& e) {
    // The key is (kind, subject, flags): a debt to settlement 5 and a debt
    // to faction 5 are different facts.
    for (int i = 0; i < int(m.count); ++i) {
        if (m.slots[i].kind == e.kind && m.slots[i].subject == e.subject
            && m.slots[i].flags == e.flags) {
            if (fold_for_kind(e.kind) == MemoryFold::Sum) {
                const std::int32_t total =
                    memory_amount(m.slots[i]) + memory_amount(e);
                m.slots[i].day = e.day;
                set_memory_amount(m.slots[i], total);
            } else {
                m.slots[i] = e;
            }
            return;
        }
    }
    if (int(m.count) < kAgentMemorySlots) {
        m.slots[m.count++] = e;
        return;
    }
    int oldest = 0;
    for (int i = 1; i < kAgentMemorySlots; ++i) {
        if (m.slots[i].day < m.slots[oldest].day) oldest = i;
    }
    m.slots[oldest] = e;
}

// The debt fact, ready to remember: `who` owes `subject` in `space`.
inline MemoryEntry make_debt_fact(std::uint8_t space, std::uint16_t subject,
                                  std::int32_t amount, int day) {
    MemoryEntry e{};
    e.kind = std::uint8_t(AgentMemoryKind::Debt);
    e.flags = space;
    e.subject = subject;
    e.day = std::uint32_t(day < 0 ? 0 : day);
    set_memory_amount(e, amount);
    return e;
}

inline const MemoryEntry* recall(const AgentMemory& m, AgentMemoryKind kind,
                                 std::uint16_t subject,
                                 std::uint8_t flags = 0) {
    for (int i = 0; i < int(m.count); ++i) {
        if (m.slots[i].kind == std::uint8_t(kind)
            && m.slots[i].subject == subject
            && m.slots[i].flags == flags) {
            return &m.slots[i];
        }
    }
    return nullptr;
}

// ── MarketSnapshot packing ───────────────────────────────────────────────

// Stock classes are po2 thresholds — coarse on purpose: a trader's memory of
// a market is "they were drowning in bread", not a ledger.
inline int stock_class(int count) {
    if (count <= 0) return 0;        // none
    if (count < 64) return 1;        // scarce
    if (count < 1024) return 2;      // stocked
    return 3;                        // plenty
}

inline MemoryEntry pack_market_snapshot(const Inventory& store,
                                        std::uint16_t subject, int day) {
    MemoryEntry e{};
    e.kind = std::uint8_t(AgentMemoryKind::MarketSnapshot);
    e.subject = subject;
    e.day = std::uint32_t(day < 0 ? 0 : day);
    static_assert(kCommodityCount <= 16,
                  "seven payload bytes hold at most 16 nibble classes");
    for (int i = 0; i < kCommodityCount; ++i) {
        const int cls = stock_class(store.count(kCommodities[i].id));
        e.payload[i >> 1] |= std::uint8_t((cls & 0xF) << ((i & 1) * 4));
    }
    return e;
}

inline int market_stock_class(const MemoryEntry& e, int commodityIdx) {
    if (commodityIdx < 0 || commodityIdx >= kCommodityCount) return 0;
    return (e.payload[commodityIdx >> 1] >> ((commodityIdx & 1) * 4)) & 0xF;
}

} // namespace sm

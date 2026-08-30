// THE relation matrix — flat, fixed, indexed by faction ordinal.
//
// WHAT IT REPLACED (owner's DOD ruling, 2026-08-27). Relations lived as
// `unordered_map<string, Faction>` whose every row held a second
// `unordered_map<string,int>`: asking "how does A regard B" cost two temporary
// std::strings, two hashes and two strcmps — and it was asked K² times per
// battle tick and once per minimap blip per frame. The registry beside it had
// dense ordinals all along (macro/faction.h faction_index), so the map was
// paying, every frame, to look up something the world already numbered.
//
// It is a plain `std::int8_t rel[64][64]`: 4 KiB, one contiguous block, saved
// byte-for-byte. A relation is -100..100, which is what an int8 is for.
//
// WHY 64, and why that is not a new number: the battle masks are capped at 64
// factions by construction (`kMaxCrowdFactions`, an enemy mask is a u64), so
// 64 is the ceiling the world already had. One ceiling, not two.
//
// The registry fills the first `kFactionCount` slots; the rest are RESERVED
// (owner: «просто зарезервировать слоты под новые фракции»). A faction the
// registry does not know — a kingdom that splinters, the player's own cult, a
// faction arriving inside an old save — CLAIMS one of the free slots and
// behaves like every other row from that moment on. Nothing invents a phantom
// row in a map any more, and nothing is silently dropped.
#pragma once
#include "core/table_guard.h"
#include "macro/faction.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace sm {

// The world's ceiling on distinct factions — the same one the battle masks
// were already built to (sub/movement.h kMaxCrowdFactions). Registry rows take
// the first slots; the tail is claimable at runtime.
inline constexpr int kMaxWorldFactions = 64;
static_assert(kFactionCount <= kMaxWorldFactions,
              "the registry must fit inside the world's faction ceiling");

// A faction slot: the registry ordinal for a known row, or a claimed tail slot
// for one the registry has never heard of. -1 = no slot (unknown and the table
// is full, or an empty id).
using FactionSlot = int;
inline constexpr FactionSlot kNoFactionSlot = -1;

// Relations plus the names of whatever runtime factions claimed tail slots.
// Flat and trivially copyable; `runtimeIds` is the ONE place a string survives,
// because a faction that is not in the registry has nowhere else to keep its
// name — and it is bounded by the same ceiling.
struct RelationMatrix {
    // rel[a][b] = how A regards B, -100..100. Symmetric by construction (every
    // writer goes through set_relation), diagonal 100.
    std::int8_t rel[kMaxWorldFactions][kMaxWorldFactions]{};
    // Slot claimed? Registry slots are claimed at world creation; tail slots
    // when someone first names them.
    bool used[kMaxWorldFactions]{};
    // Names of the tail slots only (a registry slot answers from kFactionDefs).
    // Fixed storage: 24 chars is longer than every id the registry carries.
    static constexpr int kMaxIdLen = 24;
    char runtimeIds[kMaxWorldFactions][kMaxIdLen]{};
};

// The slot of `id` if it already has one, else kNoFactionSlot. Registry ids
// resolve without touching the runtime names at all.
inline FactionSlot faction_slot(const RelationMatrix& m, const char* id) {
    if (!id || id[0] == '\0') return kNoFactionSlot;
    const int reg = faction_index(id);
    if (reg >= 0) return reg;
    for (int i = kFactionCount; i < kMaxWorldFactions; ++i) {
        if (m.used[i] && std::strncmp(m.runtimeIds[i], id,
                                      RelationMatrix::kMaxIdLen) == 0) {
            return i;
        }
    }
    return kNoFactionSlot;
}

// The slot of `id`, CLAIMING a free tail slot if the registry has never heard
// of it. kNoFactionSlot only when the id is empty or the world is full — and a
// full world is a loud condition, not a silent truncation: the caller sees the
// sentinel instead of writing into a phantom row.
inline FactionSlot claim_faction_slot(RelationMatrix& m, const char* id) {
    const FactionSlot found = faction_slot(m, id);
    if (found != kNoFactionSlot) return found;
    if (!id || id[0] == '\0') return kNoFactionSlot;
    for (int i = kFactionCount; i < kMaxWorldFactions; ++i) {
        if (m.used[i]) continue;
        m.used[i] = true;
        std::snprintf(m.runtimeIds[i], RelationMatrix::kMaxIdLen, "%s", id);
        m.rel[i][i] = 100;
        return i;
    }
    return kNoFactionSlot;
}

// The id a slot answers to: the registry's own literal for a registry slot,
// the claimed name for a tail one, "" for an unclaimed slot.
inline const char* faction_id_of_slot(const RelationMatrix& m, FactionSlot s) {
    if (s < 0 || s >= kMaxWorldFactions || !m.used[s]) return "";
    if (s < kFactionCount) return kFactionDefs[s].id;
    return m.runtimeIds[s];
}

// Relation by SLOT — the hot form: two array reads, no strings anywhere.
inline int relation_of(const RelationMatrix& m, FactionSlot a, FactionSlot b) {
    if (a < 0 || b < 0 || a >= kMaxWorldFactions || b >= kMaxWorldFactions) {
        return 0;     // fail-closed: an unplaced faction is neutral
    }
    if (a == b) return 100;
    return int(m.rel[a][b]);
}

// Symmetric write — the only way a relation moves. The map form kept the two
// directions in two places and relied on every writer to remember both.
inline void set_relation(RelationMatrix& m, FactionSlot a, FactionSlot b,
                         int value) {
    if (a < 0 || b < 0 || a >= kMaxWorldFactions || b >= kMaxWorldFactions
        || a == b) {
        return;
    }
    const int v = value < -100 ? -100 : (value > 100 ? 100 : value);
    m.rel[a][b] = std::int8_t(v);
    m.rel[b][a] = std::int8_t(v);
}

// Claim every registry row. Called once when a world is made; the tail stays
// free for whatever the world invents later.
inline void claim_registry_slots(RelationMatrix& m) {
    for (int i = 0; i < kFactionCount; ++i) {
        m.used[i] = true;
        m.rel[i][i] = 100;
    }
}

} // namespace sm

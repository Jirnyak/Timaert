// The ONE six-draw roll that fills ecs::NpcCharacter (per-NPC visual
// identity). Existed as three per-TU copies (macro/npc_spawn.cpp,
// sub/spawn.cpp, sub/engine.cpp) that had to agree on the draw ORDER — the
// thing that must never drift, since every visual derives from it — and
// differed only in the tint base. Seeding stays at the call site; this
// consumes the caller's Rng.
#pragma once
#include <cstdint>
#include "core/rng.h"
#include "ecs/components.h"

namespace sm::ecs {

inline NpcCharacter roll_npc_character(Rng& rng, int tintBase) {
    NpcCharacter ch{};
    ch.visualSeed = rng.next_u32();
    ch.bodyShape = std::uint8_t(rng.next_u32() & std::uint32_t{0x3});
    ch.nameIdx = std::uint8_t(rng.next_u32() & std::uint32_t{0xF});
    ch.tintR = std::uint8_t(tintBase + int(rng.next_u32() % std::uint32_t{96}));
    ch.tintG = std::uint8_t(tintBase + int(rng.next_u32() % std::uint32_t{96}));
    ch.tintB = std::uint8_t(tintBase + int(rng.next_u32() % std::uint32_t{96}));
    return ch;
}

} // namespace sm::ecs

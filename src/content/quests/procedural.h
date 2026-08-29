// Procedural quest generator — example deterministic factory.
// Mirrors quests/procedural-generator.ts (compact form).
#pragma once
#include <cstdint>
#include <vector>
#include "events/quests/quest_types.h"
#include "macro/state.h"

namespace sm {

std::vector<Quest> generate_quests_for_settlement(const Landmark& s,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed);
std::vector<Quest> generate_quests_for_village(const Landmark& v,
                                               const GameState& gs,
                                               std::uint32_t worldSeed);

} // namespace sm

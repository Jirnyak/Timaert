// Procedural quest generator — example deterministic factory.
// Mirrors quests/procedural-generator.ts (compact form).
#pragma once
#include <cstdint>
#include <vector>
#include "events/quests/quest_types.h"
#include "macro/state.h"

namespace sm {

struct MacroStore;   // склад места — колонка ТЕЛА (M-90 шаг 5)
std::vector<Quest> generate_quests_for_settlement(const Landmark& s,
                                                  const MacroStore& st,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed);
std::vector<Quest> generate_quests_for_village(const Landmark& v,
                                                const MacroStore& st,
                                               const GameState& gs,
                                               std::uint32_t worldSeed);

} // namespace sm

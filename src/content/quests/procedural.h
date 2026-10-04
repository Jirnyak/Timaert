// Procedural quest generator — example deterministic factory.
// Mirrors quests/procedural-generator.ts (compact form).
#pragma once
#include <cstdint>
#include <vector>
#include "events/quests/quest_types.h"
#include "macro/state.h"

namespace sm {

// `slot` — ТЕЛО места в гладкой памяти макро-сквадов (ломтик F): ординал,
// адрес, имя, фракция и склад давателя — его колонки.
struct MacroStore;
std::vector<Quest> generate_quests_for_settlement(std::uint16_t slot,
                                                  const MacroStore& st,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed);
std::vector<Quest> generate_quests_for_village(std::uint16_t slot,
                                               const MacroStore& st,
                                               const GameState& gs,
                                               std::uint32_t worldSeed);

} // namespace sm

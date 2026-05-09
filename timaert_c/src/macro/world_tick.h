// World tick — advances time + settlement simulation. Mirrors world-tick.ts.
#pragma once
#include "macro/state.h"

namespace sm {

// Advance world by `dt_seconds` of game time. Returns true on day rollover.
bool tick_world(GameState& gs, float dt_seconds);

// Advance ONLY the world clock (minute/hour/day), skipping every per-day
// simulation step (settlements, villages, economy, player ageing). Used
// while the player is in the subworld so the sun keeps moving but the
// macro economy is not silently fast-forwarded behind the scenes.
bool tick_world_time_only(GameState& gs, float dt_seconds);

} // namespace sm

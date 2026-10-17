// THE macro answer to "what is this cell" (CANON S6, 2026-08-24) — one
// assembler over the layer envelope (macro/macro_world.h).
//
// Every mechanic that wants a cell's facts asks HERE and gets the contribution
// of EVERY system that has one — biome (the one cascade), feature, the
// landmark standing on it with its live fields, trees, fertility, elevation,
// danger zone, land owner, the season's temperature shift. A system that does
// not apply contributes its zero through a null envelope layer — data, not a
// second code path. Before this door the subworld's resolve_context, the
// fauna capacity, the travel pricer and the spawners each assembled their own
// partial copy of this struct, and two of the copies had already drifted.
//
// PERFORMANCE CONTRACT (the door's half of CANON S26): cell_facts is NEVER
// called from a hot loop. A* and the greedy squad step read the baked
// PathCostData; the subworld caches the facts of its nine window cells and
// invalidates on crossing. This function is for bake time, cell entry and
// events — the places that can afford to ask everything at once.
#pragma once
#include "bus/cell_row.h"
#include "macro/macro_world.h"

#include <cstdint>

namespace sm {

// (Формы строк CellFacts/LandmarkFacts ПЕРЕЕХАЛИ в bus/cell_row.h ломтём 3
// эпика фрейма: строка клетки — род 1 локального фрейма, а bus/ не включает
// macro/. Здесь остался СБОРЩИК — он читает конверт слоёв мира.)

// Assemble the facts of one cell. Torus-wrapped; every missing envelope layer
// reads as its zero contribution, and a missing terrain answers open water.
CellFacts cell_facts(const MacroWorld& w, int x, int y);

} // namespace sm

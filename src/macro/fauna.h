// ФАУНА, ПОЛОВИНА КОТОРОЙ НУЖЕН МИР — всё, что нельзя было увезти в
// нейтральный слой.
//
// ЗАКОН ДИКОГО РОЛЛА ПЕРЕЕХАЛ В `tables/fauna.h` ЛОМТЁМ 3 ЭПИКА ПАКЕТОВ v1
// (`FaunaEntry`, `SpawnContext`, `FaunaPick`, `roll_spawns`,
// `spawn_strength`, `danger_match_weight`, `row_power`, `kDangerHalfLife`,
// `fauna_cell_capacity`): это СТРОКИ КАТАЛОГА и чистая арифметика над ними,
// а спрашивает их и субмир, которому `macro/` включать нельзя (§11). Здесь
// остались ровно две вещи, которым МИР нужен по существу:
//   · `fauna_cell_capacity_at` — та же ёмкость, снятая с КЛЕТКИ через
//     конверт слоёв (`MacroWorld`);
//   · виды над каталогом существ — поиск строки по id-строке и по ординалу
//     тела; они держат `<span>`/`<string_view>`, которым в законе ролла
//     места нет.
#pragma once
#include <span>
#include <string_view>
#include "tables/fauna.h"

namespace sm {

// The same capacity resolved from the macro cell's own data, through THE
// layer envelope (macro_world.h MacroWorld): biome via the one biome_at
// cascade, forest class from the tree layer. РОДА МЕСТА ЗДЕСЬ НЕТ (M-371):
// скан сквадов `settlement_at` был вторым носителем рода против байта фичи
// у ролла и умер с род-развилками. Missing layers read as no context:
// nothing to embody, 0.
struct MacroWorld;
int fauna_cell_capacity_at(const MacroWorld& w, int x, int y);

// ── Creature views over THE one body table ───────────────────────────
// The source of truth for every creature (rabbit → dragon) is the same
// `kNpcTypeDefs` row it is for a peasant (tables/npc.h, CANON S16); these are
// convenience views over its creature stripe (природа строки — не человек).
// Ids are one
// contiguous ordinal space — the old `0x100 | catalog index` encoding is DEAD
// (2026-08-20) — so `creature_def_from_kind` is just "the row, if it is a
// beast", serving the death / loot path.
std::span<const FaunaEntry* const> creature_catalog();
const FaunaEntry* creature_def(std::string_view id);        // nullptr if unknown
const FaunaEntry* creature_def_from_kind(std::uint16_t kindType); // nullptr if not a monster
int               creature_index(const FaunaEntry* entry);  // -1 if not in the catalog

} // namespace sm

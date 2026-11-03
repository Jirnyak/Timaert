// Per-mode generators. Each is a self-contained free function. Adding a new
// generator = one function + one entry in dispatch_generate(). Mirrors
// subworld/{forest,grassland,city,village,...}.ts.
#pragma once
#include "sub/map_data.h"
#include "tables/npc.h"   // FaunaBand — полоса спавна модуля (M-371)

namespace sm::sub {

// КАКОЙ МОДУЛЬ ПИШЕТ ЭТУ КЛЕТКУ — одна дверь, и отвечает она УКАЗАТЕЛЕМ НА
// МОДУЛЬ, а не номером в перечислении режимов.
//
// ЗДЕСЬ СТОЯЛ `SubworldMode resolve_mode(...)`, и само существование
// «режимов» было горизонтальной сцепкой модулей (вердикт владельца
// 2026-10-28: «если есть режимы это плохо это значит модули генерацтора
// горищонтально переплетены через какието режимы ЭТО БЕДА И НАСРАЛ»).
// Перечисление сливало ТРИ оси — фичи, биомы и данж — в один словарь, так
// что модуль пашни и модуль горы знали друг о друге номером строки. Теперь
// оси спрашиваются порознь, своими таблицами (`dispatch.cpp`), и модуль
// называет себя сам.
//
// `nullptr` значит РОВНО «эту клетку пишет данж-дверь, не открытый модуль»
// (`dispatch_generate_dungeon@src/sub/dgn/dispatch.cpp`): последняя строка
// резолва безусловна, поэтому иного пустого ответа у двери не бывает.
// `GenInput` живёт в `gens.h` (всё, что модулю дают на руки); указателю на
// функцию полный тип не нужен, поэтому здесь форвард-декларация, а не
// включение — заголовок остаётся минимальным (§12).
struct GenInput;
using GenFn = void (*)(const GenInput&, SubworldMapData&);
GenFn open_air_gen_for(const CellContext& ctx);

// ПОЛОСА СПАВНА БАЙТА ФИЧИ (M-371) — ответ МОДУЛЯ «кто роллится на моей
// клетке»: колонка `fauna` той же строки kFeatureGenRows, что даёт модуль
// геометрии; {0,0,0} = земля отвечает (лес/биом). Вердикт владельца:
// обитатели руины «могут спавниться и по модулую (рандом распред) и +
// контекст полей зоны» — вес по полю зоны даёт сам закон ролла.
FaunaBand feature_fauna_band(std::uint8_t feature);

// Single entry point — dispatches to the right generator. `nbHeights` is
// 9 macro heights, `nbBiome` is 9 macro biomes, `nbFeature` is 9 macro
// features in row-major order for the 3×3 neighbourhood centred on this
// cell. Generators use them to blend heightmap, carve organic roads /
// dirt paths that line up across cell boundaries, etc.
// ЗДЕСЬ СТОЯЛ ПАРАМЕТР `nbPlace` — 9-широкое кольцо `SquadType`, кормившее
// универсальное ровнение рельефа вокруг поселений. Оно умерло вместе с осью
// мест: род соседа ВЫВОДИМ из его же байта фичи
// (`rooted_kind_of_feature@src/tables/feature_def.h`), а байты соседей уже едут
// кольцом `nbFeature` выше — то есть кольцо рода было второй копией той же
// информации, протянутой через весь шов. Ровнение теперь ключуется ФИЧЕЙ
// (`terrain_mod_for@src/sub/base_generator.h`), и у него нет ветки «нет
// данных о соседях», которая прежде объявляла восьмерых соседей пустыми.
// `nbTreeCount` (optional, 9 entries) is the neighbours' macro tree count
// (macro/tree_layer.h) — THE per-cell tree-density target for the scatter.
// Null or a negative entry means "unknown": the count is re-derived from the
// ring cell's biome + the window's forest fraction (tests / bare contexts).
// `nbFertility` (optional, 9 entries) is the neighbours' fertility01 (the
// macro moisture channel) — the field-plot module gates and sizes each plot
// by its OWNING cell's fertility, so a plot straddling a seam is derived
// identically by both sides. Null or a negative entry falls back to the
// centre cell's own fertility01.
void dispatch_generate(const CellContext& ctx,
                       const float nbHeights[9],
                       const Biome nbBiome[9],
                       // One ring wider than the cells built, so every cell of
                       // the 3×3 can count its OWN neighbours (base_generator.h
                       // nbBiome5). Null = a bare fixture with no world round it.
                       const Biome* nbBiome5,
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out,
                       const int* nbTreeCount = nullptr,
                       const float* nbFertility = nullptr);

} // namespace sm::sub

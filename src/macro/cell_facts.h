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
#include "tables/biomes.h"
#include "tables/squad_type.h"
#include "macro/features.h"
#include "macro/macro_world.h"

#include <cstdint>

namespace sm {

// The landmark standing on the cell, with its LIVE fields resolved from
// GameState at the moment of asking (population and ownership drift daily;
// the frame only answers WHO — settlement_at, macro/squad_index.h).
struct LandmarkFacts {
    SquadType type = SquadType::None;   // ось рода (алиас SquadType умирает ломтиком F)
    int  id = -1;          // WORLD-unique landmark ordinal (v54); -1 = none
    // POPULATION, for every kind (§42: this field used to carry a spire's
    // spell TIER instead — an overload that would have handed a tier-3
    // spire a crowd of three demons the day the population door opened).
    int  size = 0;
    // The spire's spell tier (its strength column, asked from the spell
    // registry); 0 for every other kind. Its OWN field, never smuggled.
    int  tier = 0;
    // ЧИСЛО ОРБА — worked-число клетки шпиля (ординал kSpellDefs + 1, 0 =
    // выкачан); 0 у всякого другого рода. ЧИТАТЕЛИ НАЗВАНЫ (DOD п.9):
    // `learn_from_spire_orb@src/sub/engine.cpp` называет им спелл, который
    // орб держал (`ev.b = charge - 1`), а сборщик зон субмира — количество в
    // факте `Explored`. Оба читали его прямым `worked_read` по `*gs_`, то
    // есть лазили в макро-состояние мимо канала; колонка уводит их на пакет
    // (M-233 п.8 + долг M-98).
    int  spell = 0;
    int  factionIdx = -1;  // owning faction (registry index); -1 = none
    bool depleted = false; // шпиль, чей орб забран; у НЕ-шпиля ВСЕГДА false
                           //   (spire_orb@src/macro/spires.h — ОДИН вывод)
};

// НЕЗАДАННЫЙ ПОЛЮС ПРИБИТ КОМПИЛЯТОРОМ, А НЕ ПРОЗОЙ. «Род есть» и «ординал
// есть» обязаны быть РАВНОСИЛЬНЫ: оба поля пишет одна строка одной ветки
// (`cell_facts@src/macro/cell_facts.cpp`, единственный писатель в мире, под
// `lmId != 0`), а род ей отдаёт `settlement_at@src/macro/squad_index.h`,
// которая гейтится `is_settlement_kind` и `None` не возвращает никогда.
// Стена нужна ровно на ВТОРОЙ половине пары — на дефолтах: разъехавшись, они
// сделали бы «ординал без рода» ВЫРАЗИМЫМ, а именно из этой щели и вырос
// снесённый `effective_landmark`, который фабриковал City из одного ординала
// (см. некролог в `src/sub/map_data.h`). Мутация «id = 0» роняет сборку.
static_assert(LandmarkFacts{}.type == SquadType::None
                  && LandmarkFacts{}.id < 0,
              "незаданное место обязано быть незаданным ОБОИМИ полями: род "
              "None и ординал < 0 — иначе «ординал без рода» выразимо, и "
              "фабрикация рода из ординала вернётся");

struct CellFacts {
    int x = 0;             // wrapped world-cell coordinates
    int y = 0;
    Biome        biome = Biome::Water;   // the one cascade (biome_at_cell)
    FeatureType  feature = FT_None;      // road / dirt road / field
    LandmarkFacts landmark{};
    int   treeCount = 0;      // macro tree count; -1 = layer not wired
    float height01 = 0.0f;    // normalized elevation (terrain R)
    float fertility01 = 0.0f; // moisture (terrain G) — the wheat driver
    float temperature01 = 0.0f;   // RAW climate (terrain B): classification
                                  //   never shifts with the season
    float seasonTempOffset = 0.0f; // the season's shift, ITS OWN column —
                                   //   it used to ride smuggled inside a
                                   //   shifted temperature (foliage wants it,
                                   //   classification must not see it)
    std::uint8_t zone = 0;    // the danger byte (macro/zones.h continuum)
    std::uint8_t depositsNear = 0; // live DepositKind bits within the
                                   //   profession reach (kGathererReach)
    // (ownerKingdom died 2026-09-11 with the kingdoms: it was written and
    // never read — ground ownership answers через faction_index_for_cell.)
    int  cropHarvested = 0;   // the wheat scar: what the sickle already took
    bool water = false;       // biome == Water, pre-answered for one-fact
                              //   consumers
};

// Assemble the facts of one cell. Torus-wrapped; every missing envelope layer
// reads as its zero contribution, and a missing terrain answers open water.
CellFacts cell_facts(const MacroWorld& w, int x, int y);

} // namespace sm

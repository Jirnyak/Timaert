// Open-air dispatch — resolve a cell's KIND, hand it to that kind's module,
// and own the two passes that belong to no module: the terrain the modules
// build on, and the reconciliation they all owe the water plane afterwards.
//
// The registry is a TABLE, not a switch. The switch cost four edits per new
// kind (enum, forward declaration, case, and a second case in resolve_mode)
// and silently answered "city" for kinds nobody had written yet; a row costs
// one, and the static_assert below refuses to build if a row goes missing.
#include "sub/gens/dispatch.h"

#include "sub/gens/gens.h"
#include "sub/gens/kit/tiles.h"
#include "sub/height.h"
#include "sub/dgn/dispatch.h"
#include "sub/base_generator.h"
#include "tables/forest.h"

#include <algorithm>
#include <cstdint>

namespace sm::sub {

// One row per SubworldMode: which module writes a cell of this kind.
//
// `gen` may be null for a mode that is not written by an open-air module —
// Dungeon is routed to sub/dgn/ before dispatch ever reaches the table.
struct GenKindRow {
    SubworldMode mode;
    void (*gen)(const GenInput&, SubworldMapData&);
};

constexpr GenKindRow kGenKindRows[] = {
    { SubworldMode::Open,      gen_open     },
    { SubworldMode::City,      gen_city     },
    { SubworldMode::Village,   gen_village  },
    { SubworldMode::Forest,    gen_forest   },
    { SubworldMode::Mountain,  gen_mountain },
    { SubworldMode::Swamp,     gen_swamp    },
    { SubworldMode::Ruin,      gen_ruin     },
    { SubworldMode::Water,     gen_water    },
    // Grassland is open ground that happens to carry no massif — the same
    // module, kept as its own mode because the RESOLVER distinguishes them.
    { SubworldMode::Grassland, gen_open     },
    { SubworldMode::Road,      gen_road     },
    { SubworldMode::Spire,     gen_spire    },
    { SubworldMode::Field,     gen_field    },
    { SubworldMode::Dungeon,   nullptr      },
};
static_assert(rows_in_enum_order(kGenKindRows, &GenKindRow::mode),
              "kGenKindRows row order must mirror SubworldMode");

SubworldMode resolve_mode(const CellContext& ctx) {
    // An interior context outranks everything: the cell shows what stands
    // BEHIND a door, not the land the door stands on (sub/dgn/).
    if (ctx.dungeon.kind != DungeonRef::None) return SubworldMode::Dungeon;
    // ФИЧА ЕСТЬ КЛЮЧ МОДУЛЯ — ОДИН ВОПРОС, ОДИН ЯЗЫК (вердикт владельца
    // 2026-10-28, дословно: «вместо мест он смотрит модули типов фич которые
    // для этого и существуют»; и о порядке сборки: «надо генерацию сделать
    // системно сборкой прям рельеф биомы поля фичи и потом уже вополощать
    // сквады (агностично чисто)»).
    //
    // ЗДЕСЬ СТОЯЛ `switch (ctx.place.kind)` ПО МЁРТВОЙ ОСИ МЕСТ, а ниже —
    // цепочка `if` по фичам: ОДИН вопрос «какой модуль пишет эту клетку»
    // отвечался ДВУМЯ словарями в одной функции. Род места при этом не
    // приносил ни одного бита, которого нет в байте фичи: макро-граница
    // сама делает `place_kind_of_feature(f.feature)`
    // (`cell_facts@src/macro/cell_facts.cpp`), а колонка `kind` строки фичи
    // держит соответствие под круговым `static_assert`
    // (`place_kind_of_feature@src/macro/features.h`). То есть субмир читал
    // ПЕРЕВОД вместо оригинала, и перевод был точным — поэтому перевод оси
    // не меняет ни одного кадра, он снимает мёртвый язык.
    //
    // `default:` ЗАПРЕЩЁН (ЗАКОН СЛОВАРЯ п.4): новая строка слоя фич обязана
    // падать красным у компилятора, а не уходить в землю МОЛЧА.
    switch (FeatureLayer::decode(std::uint8_t(ctx.feature))) {
        case FT_City:     return SubworldMode::City;
        case FT_Village:  return SubworldMode::Village;
        case FT_Ruin:     return SubworldMode::Ruin;
        case FT_Spire:    return SubworldMode::Spire;
        case FT_Road:
        case FT_DirtRoad: return SubworldMode::Road;
        // A bridge is the road continued over a water cell: Road mode carves
        // the line, and gen_road's Biome::Water branch raises the span.
        case FT_Bridge:   return SubworldMode::Road;
        case FT_Field:
        case FT_Pasture:  return SubworldMode::Field;

        // ── КЛЕТКА ПОКАЗЫВАЕТ ЗЕМЛЮ, И У КАЖДОЙ ГРУППЫ СВОЯ ПРИЧИНА ──────
        // СТОЛИЦА ЖДЁТ СВОЕГО МОДУЛЯ. Владелец (2026-10-24): «ФИЧА КАПИТАЛ
        // НАДО! ТАМ БУДЕТ ОСОБЫЙ ГЕНЕРАТОР!» — ключ модуля уже стоит
        // строкой `FT_Capital`, а самого модуля (`SubworldMode::Capital` →
        // `gen_capital`) ещё нет. Направить столицу в `SubworldMode::City`
        // было бы ровно шрам GEN-5: род без своего TU поднимал бы чужой
        // город («the day a castle was placed on the map it would raise a
        // city — walls, keep and four hundred houses»), и подмена осталась
        // бы невидимой. До своего модуля столица показывает ЗЕМЛЮ.
        case FT_Capital:
        // Выемки: своего модуля у шахты и карьера нет — клетка остаётся
        // своей землёй, а выработка живёт числом поля под фичей.
        case FT_ClayPit:
        case FT_IronMine:
        case FT_Quarry:
        case FT_SilverMine:
        case FT_CopperMine:
        case FT_GoldMine:
        // ЛЁН В `Field` НЕ ИДЁТ, И ЭТО СОХРАНЁННОЕ ПОВЕДЕНИЕ, А НЕ ВЫВОД:
        // прежняя цепочка спрашивала `FT_Field` и `FT_Pasture` поимённо, а
        // льняное поле не спрашивала вовсе. Перевод оси этого не менял;
        // вопрос «пашня ли лён» идёт ВЛАДЕЛЬЦУ строкой наряда, а не правкой
        // на ходу (§1 — самовольное «я решил, что так лучше» есть провал).
        case FT_FlaxField:
        case FT_None:
        case FT_Count:    break;
    }
    // Биом под фичей: что люди построили, решает, ЧЕМ клетка является
    // подо ногами; дальше биом заполняет рельеф, а деревья ложатся сверху
    // (лесистый пик — это Mountain с разбросанными деревьями, не Forest).
    if (ctx.biome == Biome::Mountain) return SubworldMode::Mountain;
    if (ctx.biome == Biome::Water)    return SubworldMode::Water;
    if (ctx.biome == Biome::Swamp)    return SubworldMode::Swamp;
    // Forest is a COUNT class, not a feature: massif interiors (tree count ≥
    // half the golden max) generate as Forest; edges/ambience stay open with
    // their trees composed on top by the count-driven scatter.
    if (is_forest_cell(ctx.treeCount)) return SubworldMode::Forest;
    return SubworldMode::Grassland;
}

void dispatch_generate(const CellContext& ctx, const float nbHeights[9],
                       const Biome nbBiome[9], const Biome* nbBiome5,
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out,
                       const int* nbTreeCount,
                       const float* nbFertility) {
    CellContext safeCtx = ctx;
    safeCtx.feature = FeatureLayer::decode(std::uint8_t(ctx.feature));
    // Interior scene: the dungeon module writes the whole cell itself (flat
    // field, sealed geometry) — the open-air terrain pipeline below would
    // only be thrown away.
    if (safeCtx.dungeon.kind != DungeonRef::None) {
        dispatch_generate_dungeon(safeCtx, out);
        return;
    }
    std::uint8_t safeFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        safeFeature[i] = std::uint8_t(FeatureLayer::decode(nbFeature[i]));
    }

    // Per-cell fertility ring for the field-plot module. Missing array or a
    // negative entry = "unknown" — fall back to the centre cell's own value
    // (synthetic/test contexts).
    float safeFertility[9];
    for (int i = 0; i < 9; ++i) {
        safeFertility[i] = (nbFertility && nbFertility[i] >= 0.0f)
            ? nbFertility[i] : ctx.fertility01;
    }

    // Per-cell tree counts (macro TreeLayer). A missing array or a negative
    // entry means "unknown" (synthetic/test contexts without the layer) and
    // falls back to the ring cell's biome ambience — bare contexts have no
    // massif mask, so the forest term is zero by definition.
    int safeTreeCount[9];
    for (int i = 0; i < 9; ++i) {
        if (nbTreeCount && nbTreeCount[i] >= 0) {
            safeTreeCount[i] = std::min(nbTreeCount[i], kMaxTreesPerCell);
        } else {
            safeTreeCount[i] = int(derived_tree_count(nbBiome[i], 0.0f));
        }
    }

    // Universal terrain flattening: one TerrainMod per neighbour, resolved
    // from ITS FEATURE BYTE (roads/settlements calm the terrain they stand
    // on — see base_generator.h).
    //
    // ЗДЕСЬ СТОЯЛО 9-ШИРОКОЕ КОЛЬЦО `SquadType nbPlace`, И ОНО УМЕРЛО ВМЕСТЕ
    // С ОСЬЮ: род соседа выводится из его же байта, а байты соседей каркас
    // уже привёз кольцом `nbFeature`. Кольцо рода было ВТОРОЙ копией той же
    // информации, которую приходилось тянуть через весь шов — и его ветка
    // «нет данных о соседях» ставила восьми соседям `SquadType::None`, то
    // есть врала о мире ровно там, где фикстура его не задала. Теперь
    // такой ветки нет: байт соседа честен по построению.
    TerrainMod nbMods[9]{};
    for (int i = 0; i < 9; ++i) {
        nbMods[i] = terrain_mod_for(FeatureLayer::decode(safeFeature[i]));
    }

    out.heightmap.clear();
    generate_heightmap(out.heightmap, kCellSize, nbHeights, nbBiome, nbBiome5,
                       safeCtx.biome,
                       safeCtx.cx * kCellSize, safeCtx.cy * kCellSize,
                       safeCtx.seaLevel,
                       nbMods, safeCtx.worldCellsX, safeCtx.worldSeed);
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.trav.assign (std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();
    // THE SCENE INHERITS THE WORLD'S PLANE (owner, 2026-09-27: «микромир её
    // наследует»). Not a constant of this layer and not a biome's column — the
    // very number the macro map judges `is_water` by, arriving as context.
    out.waterLevel = safeCtx.seaLevel;

    const GenInput in{safeCtx, nbBiome, safeFeature, safeTreeCount, safeFertility};
    const std::size_t row = std::size_t(resolve_mode(safeCtx));
    if (row < std::size(kGenKindRows) && kGenKindRows[row].gen != nullptr) {
        kGenKindRows[row].gen(in, out);
    } else {
        gen_open(in, out);
    }

    // TS-faithful post-pass: smooth heightmap under road / square tiles so
    // paths look like they were carved into the relief instead of riding
    // its bumps. No-op when the cell has no roads. Mirrors `smoothRoadHeights`
    // applied at the end of `BaseGenerator.generateHeightmap` in TS.
    smooth_road_heights(out.heightmap, out.tiles, kCellSize, kCellSize);
    // …and only now can the road module answer whether a span superseded its
    // own roadway: the pass above was still moving the tiles it judges (gens.h).
    drown_superseded_roadway(out);
    // …and only NOW does a lifted span know what it bridges: the pass above
    // was still cutting the roadway a gateway stands on (sub/height.h).
    seat_lifted_spans(out);
    kit::sync_water_tiles_from_heightmap(out);
}

} // namespace sm::sub

// Open-air dispatch — назвать модуль, пишущий клетку, и отработать два
// прохода, не принадлежащих ни одному модулю: терраин, на котором они
// строят, и примирение с плоскостью воды после них.
//
// РЕЕСТР ЕСТЬ ТАБЛИЦА, А НЕ SWITCH, И ТАБЛИЦ ДВЕ — ПО ЧИСЛУ ОСЕЙ МИРА.
// Прежний switch стоил четырёх правок на новый род и МОЛЧА отвечал «город»
// за роды, которых никто не написал; строка стоит одной, а `static_assert`
// ниже отказывается собираться, если строка пропала. Третьего словаря
// («режимов») между осью и модулем больше нет — почему, сказано у таблиц.
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

// ── МОДУЛЬ НАЗЫВАЕТ СЕБЯ ИЗ СЛОВАРЯ МИРА, А НЕ ИЗ ТРЕТЬЕГО ПЕРЕЧИСЛЕНИЯ ───
//
// ЗДЕСЬ СТОЯЛ `SubworldMode` И ТАБЛИЦА ПО НЕМУ, И ЭТО БЫЛА ГОРИЗОНТАЛЬНАЯ
// СЦЕПКА МОДУЛЕЙ (вердикт владельца 2026-10-28, дословно: «это проблема если
// есть режимы это плохо это значит модули генерацтора горищонтально
// переплетены через какието режимы ЭТО БЕДА И НАСРАЛ»). Перечисление
// СЛИВАЛО ТРИ РАЗНЫЕ ОСИ в один словарь — фичи (City/Village/Ruin/Spire/
// Road/Field), биомы (Forest/Mountain/Swamp/Water/Grassland/Open) и данж, —
// поэтому модуль пашни и модуль горы сидели в ОДНОМ перечислении и знали
// друг о друге хотя бы номером строки. Теперь каждая ось отвечает СВОЕЙ
// таблицей, и ни один модуль не назван через соседа.
//
// ЗАКОН, НА КОТОРОМ ЭТО СТОИТ (владелец, там же): «МЫ ОСОЗНАННО
// ИНКАПСУЛИРУЕМ ВСЕ МОДУЛИ ВНУТРИ СЕБЯ - они не могут горизонатлньо общатсья
// вообще а могут либо писать то угодно внутри себя - остаётся внутни ни хи
// брать боеле фундамнетальные системы»; и о контексте: «контекст не знает что
// его модули берут это решение модуля как он зависит от котнектса». То есть
// §11 «Модули, не паутина» плюс ЗАКОН АГНОСТИЧНОСТИ: вниз к фундаментальному
// — можно, вбок — нет, а контекст своих читателей не знает.
//
// ОДНОРОДНОСТЬ МОДУЛЕЙ — ЗАМЫСЕЛ, А НЕ ДОЛГ (владелец: «это нормалнь очто
// модули единообразнве и там внутри похожее то есть дод инкапсуляцяи
// замысел»), ровно DOD п.5: похожий код в двух модулях есть НОРМА, дефект —
// сцепление. Поэтому строка «у двух байтов один модуль» законна, а слияние
// байтов в один ради DRY — нет.

// ОСЬ ПЕРВАЯ — БАЙТ ФИЧИ. `nullptr` = «этот байт своего модуля ещё не имеет»,
// и клетка честно показывает свою землю (GEN-5: род без своего TU не смеет
// поднимать ЧУЖУЮ стройку).
// `fauna` — ПОЛОСА СПАВНА МОДУЛЯ (M-371, вердикт владельца: обитатели руины
// «могут спавниться и по модулую (рандом распред) и + контекст полей зоны»):
// кто роллится на клетке этого байта; {0,0,0} = земля отвечает. Вес по полю
// зоны даёт сам закон ролла (danger_match_weight), модулю хватает полосы.
struct FeatureGenRow { FeatureType feature; GenFn gen; FaunaBand fauna{}; };
constexpr FeatureGenRow kFeatureGenRows[] = {
    { FT_None,       nullptr      },
    { FT_Road,       gen_road     },
    { FT_DirtRoad,   gen_road     },
    { FT_Field,      gen_field    },
    // Мост — дорога, продолженная над водой: `gen_road` режет линию, а его
    // ветка `Biome::Water` поднимает пролёт.
    { FT_Bridge,     gen_road     },
    // Выемки: шахта и карьер — дыра в земле, своего модуля у них нет.
    { FT_ClayPit,    nullptr      },
    { FT_IronMine,   nullptr      },
    { FT_Quarry,     nullptr      },
    { FT_SilverMine, nullptr      },
    { FT_CopperMine, nullptr      },
    { FT_GoldMine,   nullptr      },
    // ВЫГОН БОЛЬШЕ НЕ РАСТИТ КОЛОСЬЯ (вердикт владельца 2026-10-28: «выгон —
    // ТРАВА, колосья убрать»; и о форме: «пасбище барашей и коней тоже
    // должныч быть от ледьными типами фич со своими ген модулями»). Он СТОЯЛ
    // в модуле ПАШНИ и потому сеял хлеб на пастбище — ровно шрам GEN-5, чужая
    // стройка на своём байте. До своего модуля — голая земля.
    { FT_Pasture,    nullptr      },
    // Лён — своя фича со своим модулем, которого ещё нет; земля до него.
    { FT_FlaxField,  nullptr      },
    // Столица — свой модуль (вердикт владельца 2026-10-24: «ТАМ БУДЕТ
    // ОСОБЫЙ ГЕНЕРАТОР!»; на развилку 2026-11-13 — «Свой модуль — копия
    // городского», ряд M-522): особый генератор пишется в её же файле.
    { FT_Capital,    gen_capital  },
    { FT_City,       gen_city     },
    { FT_Village,    gen_village  },
    // Шпиль — такая же чистая фича без сквада (M-320), и его обитателей тоже
    // объявляет ЕГО модуль: полоса kHabSpire. Строка стояла БЕЗ полосы, и это
    // был молчаливый дефект, замеренный нарядом M-391: бит `kHabSpire` носят
    // 14 строк каталога, и НИ ОДНА полоса в дереве его не производила, то есть
    // башня демонов заселялась ДИЧЬЮ СВОЕЙ ЗЕМЛИ. Замер трёх миров ДО правки:
    // на улице шпиля стояли волки, козлы, зайцы и лисы (21/26/26 голов, 10-12
    // родов), а Лич — единственная строка, у которой ДРУГОЙ земли нет вовсе, —
    // не встал НИ РАЗУ.
    { FT_Spire,      gen_spire,   {kHabSpire, 2, 6} },
    // Руина — чистая фича без сквада (вердикт 2026-11-02: «У РУИНЫ НЕ ДОЛЖНО
    // БЫТЬ СКВАДА … это чистая фича»); её обитателей объявляет ЕЁ модуль:
    // полоса kHabRuin, 2..6 голов за визит — числа прежней колонки рода.
    { FT_Ruin,       gen_ruin,    {kHabRuin, 2, 6} },
};
// ПОЧЕМУ У ШПИЛЯ ТЕ ЖЕ 2..6, ЧТО У РУИНЫ И У КАЖДОЙ НОРЫ ДЕРЕВА
// (`kDungeonKindRows@src/sub/dgn/dispatch.cpp`): счёт в полосе отвечает на
// вопрос «сколько голов поднимает ОДИН визит ОДНОЙ сцены», и он не свойство
// шпиля — свойство визита. Пол 2 держит посещённую нору непустой, потолок 6
// не даёт одному визиту выгрести клетку: природная ёмкость земли (счётные
// строки `kBiomeSpawnCounts`/`kForestSpawnCounts@src/tables/fauna.cpp`) лежит
// в 1..8, и запас ряда Fauna всё равно режет укладку выше неё. Числа — БАЛАНС
// (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ), разный счёт у шпиля был бы высказыванием о
// балансе, которого никто не делал.
static_assert(rows_in_enum_order(kFeatureGenRows, &FeatureGenRow::feature),
              "kFeatureGenRows обязана идти в порядке FeatureType");
static_assert(std::size(kFeatureGenRows) == std::size_t(FT_Count),
              "у КАЖДОГО байта слоя фич обязана быть своя строка модуля");

// ОСЬ ВТОРАЯ — БИОМ, и он спрашивается ТОЛЬКО когда фича промолчала.
// `nullptr` = «у биома своего модуля нет», и тогда решает КЛАСС СЧЁТА
// деревьев, иначе открытая земля.
struct BiomeGenRow { Biome biome; GenFn gen; };
constexpr BiomeGenRow kBiomeGenRows[] = {
    { Tundra,   nullptr      },
    { Taiga,    nullptr      },
    { Snow,     nullptr      },
    { Valley,   nullptr      },
    { Meadow,   nullptr      },
    { Swamp,    gen_swamp    },
    { Desert,   nullptr      },
    { Steppe,   nullptr      },
    { Tropics,  nullptr      },
    { Water,    gen_water    },
    { Mountain, gen_mountain },
};
static_assert(rows_in_enum_order(kBiomeGenRows, &BiomeGenRow::biome),
              "kBiomeGenRows обязана идти в порядке Biome");

FaunaBand feature_fauna_band(std::uint8_t feature) {
    const FeatureType ft = feature_decode(feature);
    return kFeatureGenRows[std::size_t(ft)].fauna;
}

GenFn open_air_gen_for(const CellContext& ctx) {
    // An interior context outranks everything: the cell shows what stands
    // BEHIND a door, not the land the door stands on (sub/dgn/). `nullptr`
    // ОТ ЭТОЙ ДВЕРИ значит РОВНО ОДНО — «клетку пишет данж-дверь, не
    // открытый модуль»; `nullptr` ВНУТРИ таблиц выше значит другое («спроси
    // следующую ось»), и смешаться они не могут, потому что обе таблицы
    // полны, а последняя строка резолва безусловна.
    if (ctx.dungeon.kind != DungeonRef::None) return nullptr;
    // ОСЬ ПЕРВАЯ — ФИЧА: что люди построили, решает, ЧЕМ клетка является
    // подо ногами (вердикт владельца 2026-10-28: «вместо мест он смотрит
    // модули типов фич которые для этого и существуют»).
    const FeatureType feature = feature_decode(std::uint8_t(ctx.feature));
    if (const GenFn f = kFeatureGenRows[std::size_t(feature)].gen) return f;
    // ОСЬ ВТОРАЯ — БИОМ, и он спрашивается ТОЛЬКО когда фича промолчала:
    // биом заполняет рельеф под постройкой. Порядок осей СОХРАНЁН в точности:
    // гора, вода и болото бьют счёт деревьев, поэтому лесистый пик остаётся
    // горой с разбросанными деревьями, а не становится лесом.
    if (const GenFn f = kBiomeGenRows[std::size_t(ctx.biome)].gen) return f;
    // ОСЬ ТРЕТЬЯ — КЛАСС СЧЁТА, а не фича и не биом: внутренности массива
    // (деревьев ≥ половины золотого максимума) пишет лесной модуль, а кромка
    // и ambience остаются открытой землёй с деревьями, разбросанными сверху.
    if (is_forest_cell(ctx.treeCount)) return gen_forest;
    // ОТКРЫТАЯ ЗЕМЛЯ — БЕЗУСЛОВНАЯ ПОСЛЕДНЯЯ СТРОКА, и потому `nullptr` от
    // этой двери не может значить ничего, кроме данжа.
    //
    // ЗДЕСЬ УМЕРЛО РАЗЛИЧИЕ `Grassland`/`Open`: оба режима звали ОДИН И ТОТ
    // ЖЕ `gen_open`, то есть различались ТОЛЬКО номером в мёртвом
    // перечислении. Различие, наблюдаемое лишь через сам словарь, который
    // его и породил, — не различие мира; с «режимами» оно ушло само.
    return gen_open;
}

void dispatch_generate(const CellContext& ctx, const float nbHeights[9],
                       const Biome nbBiome[9], const Biome* nbBiome5,
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out,
                       const int* nbTreeCount,
                       const float* nbFertility) {
    CellContext safeCtx = ctx;
    safeCtx.feature = feature_decode(std::uint8_t(ctx.feature));
    // Interior scene: the dungeon module writes the whole cell itself (flat
    // field, sealed geometry) — the open-air terrain pipeline below would
    // only be thrown away.
    if (safeCtx.dungeon.kind != DungeonRef::None) {
        dispatch_generate_dungeon(safeCtx, out);
        return;
    }
    std::uint8_t safeFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        safeFeature[i] = std::uint8_t(feature_decode(nbFeature[i]));
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
        nbMods[i] = terrain_mod_for(feature_decode(safeFeature[i]));
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
    // Данж отработал выше и сюда не доходит, поэтому дверь обязана назвать
    // модуль; `gen_open` страхует только невозможное.
    const GenFn gen = open_air_gen_for(safeCtx);
    (gen ? gen : gen_open)(in, out);

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

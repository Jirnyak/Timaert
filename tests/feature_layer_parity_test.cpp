#include "check.h"

#include "core/torus.h"
#include "macro/spawners.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{

// ── ФИКСТУРА АВТОРИТ УРОВНЕМ ПОЛЯ, А НЕ БАЙТОМ КАРТЫ ─────────────────────
// Карта хранит СЛОВО (`kFieldWordMax`), и байтовый литерал в ней компилируется
// молча: `140` раньше значило «суша 0.549», а словом значит 0.002 — воду.
// Поэтому двери свидетеля берут АВТОРСКИЙ УРОВЕНЬ 0..1 и переводят его
// единственной дверью записи `field_word_of` — ровно как мир.

// Середина матрицы климата (здесь стоял байт 128).
constexpr std::uint16_t kClimateMid = sm::field_word_of(128.0f / 255.0f);

// Маска A — канал ТЕКСТУРЫ шейдера; вопрос «вода ли» решает плоскость. Но
// писать её согласованно с высотой свидетель обязан, иначе карта несёт два
// правописания одного порога.
constexpr std::uint16_t land_mask(float level01)
{
    return level01 < sm::kDefaultSeaLevel ? std::uint16_t(0)
                                          : std::uint16_t(sm::kFieldWordMax);
}

sm::TerrainData make_terrain(int w, int h, float level01)
{
    sm::TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * std::size_t(h) * 4u, 0);
    td.riverData.assign(std::size_t(w) * std::size_t(h), 0);
    // плоскость моря — у карты
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    const std::uint16_t word = sm::field_word_of(level01);
    const std::uint16_t mask = land_mask(level01);
    for (int i = 0; i < w * h; ++i)
    {
        const std::size_t s = std::size_t(i) * 4u;
        td.rgba[s + 0] = word;
        td.rgba[s + 1] = kClimateMid;
        td.rgba[s + 2] = kClimateMid;
        td.rgba[s + 3] = mask;
    }
    return td;
}

void set_height(sm::TerrainData &td, int x, int y, float level01)
{
    const std::size_t s = (std::size_t(y) * std::size_t(td.width)
                           + std::size_t(x)) * 4u;
    td.rgba[s + 0] = sm::field_word_of(level01);
    td.rgba[s + 3] = land_mask(level01);
}

void set_alpha(sm::TerrainData &td, int x, int y, std::uint16_t alpha)
{
    const std::size_t s = (std::size_t(y) * std::size_t(td.width)
                           + std::size_t(x)) * 4u;
    td.rgba[s + 3] = alpha;
}

std::size_t idx(const sm::TerrainData &td, int x, int y)
{
    return std::size_t(y) * std::size_t(td.width) + std::size_t(x);
}

// Reference reimplementation of build_feature_layer's composing passes.
// Features are MAN-MADE only: mountains are the elevation-classified Mountain
// biome and forests are the tree-count field (macro/tree_layer.h) — neither
// appears here. Passes apply last-writer-wins: dirt roads, then roads.
sm::FeatureLayer build_reference_feature_layer(
    const sm::TerrainData &td,
    const std::vector<std::uint8_t> &roadMask,
    const std::vector<std::uint8_t> *dirtMask)
{
    sm::FeatureLayer fl;
    fl.resize(td.width, td.height);
    if (td.width <= 0 || td.height <= 0 || fl.data.empty())
        return fl;

    const std::size_t total = std::size_t(td.width) * std::size_t(td.height);
    if (td.rgba.size() < total * 4u)
        return fl;

    // ОДИН ПРЕДИКАТ ВОДЫ, КАК В МИРЕ (M-109, вердикт владельца «НИКАКИХ
    // МАСОК»): здесь их было два — «маска ИЛИ высота» и «только маска», — и
    // разницу между ними оправдывала «полоса несогласия берега». Полосы нет:
    // порог один, и он на карте. Мокрая клетка под платным путём есть пролёт
    // (FT_Bridge, всегда камень), сухая — полотно своего класса.
    auto is_water = [&](std::size_t i)
    {
        return td.rgba[i * 4u + 0] < td.seaLevel16;
    };

    if (dirtMask)
    {
        const std::size_t limit = dirtMask->size() < total ? dirtMask->size() : total;
        for (std::size_t i = 0; i < limit; ++i)
        {
            if ((*dirtMask)[i] == 0)
                continue;
            if (is_water(i))
                fl.data[i] = sm::FT_Bridge;
            else
                fl.data[i] = sm::FT_DirtRoad;
        }
    }
    const std::size_t roadLimit = roadMask.size() < total ? roadMask.size() : total;
    for (std::size_t i = 0; i < roadLimit; ++i)
    {
        if (roadMask[i] == 0)
            continue;
        if (is_water(i))
            fl.data[i] = sm::FT_Bridge;
        else
            fl.data[i] = sm::FT_Road;
    }
    return fl;
}

void test_feature_priority_and_water_filter()
{
    sm::TerrainData td = make_terrain(4, 4, 140.0f / 255.0f);
    set_height(td, 2, 2, 0.0f);   // below sea level -> water-filter divergence

    // (0,1): dirt only. (1,0): road only. (1,1): dirt -> road (road wins).
    // (2,2): dirt+road but water -> must stay empty. (3,0): dirt (wrap test).
    std::vector<std::uint8_t> road(std::size_t(td.width) * td.height, 0);
    std::vector<std::uint8_t> dirt(std::size_t(td.width) * td.height, 0);
    dirt[idx(td, 0, 1)] = 255;
    dirt[idx(td, 1, 1)] = 255;
    dirt[idx(td, 2, 2)] = 255;
    dirt[idx(td, 3, 0)] = 255;
    road[idx(td, 1, 0)] = 255;
    road[idx(td, 1, 1)] = 255;
    road[idx(td, 2, 2)] = 255;

    const sm::FeatureLayer fl =
        sm::build_feature_layer(td, road, &dirt);

    CHECK(fl.width == 4 && fl.height == 4,
                 "feature layer dimensions must match terrain");
    CHECK(fl.at(0, 1) == sm::FT_DirtRoad,
                 "dirt-road pass must stamp connector cells");
    CHECK(fl.at(1, 0) == sm::FT_Road,
                 "road pass must stamp main road cells");
    CHECK(fl.at(1, 1) == sm::FT_Road,
                 "road pass must have highest feature priority");
    CHECK(fl.at(2, 2) == sm::FT_Bridge,
                 "a masked biome-water cell is a paid crossing: FT_Bridge");
    CHECK(fl.at(-1, 0) == sm::FT_DirtRoad,
                 "feature lookup must wrap negative x toroidally");
    CHECK(fl.at(5, 1) == sm::FT_Road,
                 "feature lookup must wrap positive x toroidally");
}

void test_empty_and_malformed_inputs_are_safe()
{
    sm::FeatureLayer empty;
    empty.resize(0, 4);
    empty.set(0, 0, sm::FT_Road);

    sm::FeatureLayer shortStorage;
    shortStorage.width = 4;
    shortStorage.height = 4;
    shortStorage.data.assign(1, std::uint8_t(sm::FT_DirtRoad));
    shortStorage.set(3, 3, sm::FT_Road);

    sm::FeatureLayer invalidStorage;
    invalidStorage.resize(2, 2);
    invalidStorage.data[0] = 255u;
    invalidStorage.data[1] = std::uint8_t(sm::FT_DirtRoad);

    sm::FeatureLayer invalidSet;
    invalidSet.resize(1, 1);
    invalidSet.set(0, 0, static_cast<sm::FeatureType>(255u));

    sm::FeatureLayer validStorage;
    validStorage.resize(2, 2);
    validStorage.data[0] = std::uint8_t(sm::FT_Road);
    validStorage.data[1] = std::uint8_t(sm::FT_DirtRoad);

    // FT_Field is a first-class byte: valid, decodes to itself, sanitizer
    // passes it through.
    sm::FeatureLayer fieldStorage;
    fieldStorage.resize(1, 1);
    fieldStorage.data[0] = std::uint8_t(sm::FT_Field);

    std::vector<std::uint8_t> sanitized;

    sm::TerrainData td = make_terrain(2, 2, 200.0f / 255.0f);
    std::vector<std::uint8_t> shortRoad{255};
    std::vector<std::uint8_t> shortDirt{0, 255};
    const sm::FeatureLayer fl =
        sm::build_feature_layer(td, shortRoad, &shortDirt);

    CHECK(empty.width == 0 && empty.height == 0 && empty.data.empty(),
                 "empty feature resize must clear storage");
    CHECK(empty.at(0, 0) == sm::FT_None,
                 "empty feature lookup must be safe");
    // ЗАКОН АДРЕСА (владелец, 2026-09-23): сторона мира — ВСЕГДА степень
    // двойки, поэтому заворот оси есть маска. Здесь стоял предел 3 —
    // ширина, которой у мира не бывает; проверка переписана под новый закон
    // на законную сторону. Инвариант тот же и он сильнее: маска не
    // переполняется НИ НА ОДНОЙ координате, включая INT_MIN, потому что в
    // ней нет ни деления, ни промежуточного сложения.
    CHECK(sm::FeatureLayer::wrap_coord(std::numeric_limits<int>::min(), 4) == 0
                     && sm::FeatureLayer::wrap_coord(std::numeric_limits<int>::max(), 4) == 3
                     && sm::FeatureLayer::wrap_coord(-1, 1024) == 1023,
                 "feature coordinate wrap must handle INT_MIN without overflow");
    CHECK(shortStorage.at(0, 0) == sm::FT_DirtRoad,
                 "short feature storage must allow valid prefix lookup");
    CHECK(shortStorage.at(3, 3) == sm::FT_None,
                 "short feature storage must fail closed outside backing data");
    CHECK(invalidStorage.at(0, 0) == sm::FT_None,
                 "invalid feature bytes must decode to None");
    CHECK(invalidStorage.at(1, 0) == sm::FT_DirtRoad,
                 "valid feature bytes must decode unchanged");
    CHECK(sm::FeatureLayer::is_valid_byte(std::uint8_t(sm::FT_Field))
                     && fieldStorage.at(0, 0) == sm::FT_Field
                     && sm::FeatureLayer::decode(std::uint8_t(sm::FT_Field))
                            == sm::FT_Field,
                 "FT_Field must be a first-class feature byte");
    CHECK(invalidStorage.has_invalid_cell_bytes(),
                 "complete feature storage must report invalid cell bytes");
    CHECK(invalidStorage.copy_sanitized_cells(sanitized)
                     && sanitized.size() == 4u
                     && sanitized[0] == std::uint8_t(sm::FT_None)
                     && sanitized[1] == std::uint8_t(sm::FT_DirtRoad),
                 "feature sanitized copy must normalize invalid bytes only");
    const std::uint8_t *invalidUpload =
        invalidStorage.complete_cells_or_sanitized(sanitized);
    CHECK(invalidUpload == sanitized.data()
                     && sanitized[0] == std::uint8_t(sm::FT_None)
                     && sanitized[1] == std::uint8_t(sm::FT_DirtRoad),
                 "feature upload view must use sanitized scratch for invalid cells");
    const std::uint8_t *validUpload =
        validStorage.complete_cells_or_sanitized(sanitized);
    CHECK(validUpload == validStorage.data.data() && sanitized.empty(),
                 "feature upload view must keep direct storage for valid cells");
    CHECK(!shortStorage.has_invalid_cell_bytes(),
                 "short feature storage must not scan outside valid cell backing");
    CHECK(!shortStorage.copy_sanitized_cells(sanitized) && sanitized.empty(),
                 "short feature storage must not produce a complete sanitized copy");
    CHECK(shortStorage.complete_cells_or_sanitized(sanitized) == nullptr
                     && sanitized.empty(),
                 "short feature storage must not expose an upload view");
    CHECK(invalidSet.data[0] == std::uint8_t(sm::FT_None)
                     && invalidSet.at(0, 0) == sm::FT_None,
                 "feature setter must sanitize invalid enum casts");

    sm::FeatureLayer hugeExtent;
    hugeExtent.width = std::numeric_limits<int>::max();
    hugeExtent.height = 1;
    hugeExtent.data.assign(1u, std::uint8_t(sm::FT_Road));
    hugeExtent.set(std::numeric_limits<int>::max() - 1, 0, sm::FT_DirtRoad);
    // ЗАКОН АДРЕСА (владелец, 2026-09-23) СДЕЛАЛ ЭТОТ ОТКАЗ ПОЛНЫМ. Прежде
    // слой с уродливыми размерами доверялся ЧАСТИЧНО: клетка (0,0) ещё
    // отвечала своим байтом, и только дальняя падала в None. Теперь мир,
    // не бывший квадратом степени двойки, не является миром вовсе, и слой
    // отвечает None ВЕЗДЕ. Ожидание переписано под новый закон, а не
    // подогнано: обещание строки («fail closed») стало исполняться СИЛЬНЕЕ.
    CHECK(hugeExtent.at(0, 0) == sm::FT_None
                     && hugeExtent.at(std::numeric_limits<int>::max() - 1, 0) == sm::FT_None,
                 "malformed huge feature extents must wrap safely and fail closed");
    CHECK(!sm::FeatureLayer::is_valid_byte(255u)
                     && sm::FeatureLayer::is_valid_byte(std::uint8_t(sm::FT_DirtRoad)),
                 "feature byte validation must reject unknown values only");
    // Byte 3 history: it was FT_DirtRoad before the forest renumber, then a
    // guarded-invalid hole, and is now FT_Field — DELIBERATE reuse: the
    // feature grid is regenerated at every boot and never serialized, so a
    // stale pre-renumber 3 has no path into a live layer. The fail-closed
    // frontier moves to the first unassigned byte.
    CHECK(sm::FeatureLayer::decode(3u) == sm::FT_Field,
                 "byte 3 is FT_Field now (grid is never serialized)");
    // Byte 4 is FT_Bridge (2026-08-29): the road carried over a one-cell
    // water crossing, first-class exactly like every other feature. The
    // fail-closed frontier moves to byte 5.
    CHECK(sm::FeatureLayer::is_valid_byte(std::uint8_t(sm::FT_Bridge))
                     && sm::FeatureLayer::decode(std::uint8_t(sm::FT_Bridge))
                            == sm::FT_Bridge,
                 "FT_Bridge must be a first-class feature byte");
    // Bytes 5..10 are the mines — validity comes from the ENUM, not a hand
    // list: the frontier is FT_Count. (Свидетель деревянного моста снят
    // 2026-09-22 вместе с самим байтом: артель больше не строит ничего.)
    CHECK(sm::FeatureLayer::is_valid_byte(std::uint8_t(sm::FT_SilverMine))
                     && sm::FeatureLayer::decode(
                            std::uint8_t(sm::FT_SilverMine))
                            == sm::FT_SilverMine,
                 "a mine is a first-class feature byte");
    CHECK(!sm::FeatureLayer::is_valid_byte(std::uint8_t(sm::FT_Count))
                     && sm::FeatureLayer::decode(std::uint8_t(sm::FT_Count))
                            == sm::FT_None,
                 "first unassigned feature byte must fail closed to None");
    CHECK(fl.at(0, 0) == sm::FT_Road,
                 "short road masks must apply prefix bytes");
    CHECK(fl.at(1, 0) == sm::FT_DirtRoad,
                 "short dirt masks must apply prefix bytes");
}

void test_feature_layer_reference_matrix()
{
    std::uint32_t seed = 0x5eed1234u;
    auto next_u8 = [&]()
    {
        seed = seed * 1664525u + 1013904223u;
        return std::uint8_t(seed >> 24);
    };

    for (int w = 1; w <= 5; ++w)
    {
        for (int h = 1; h <= 4; ++h)
        {
            sm::TerrainData td = make_terrain(w, h, 140.0f / 255.0f);
            const std::size_t total = std::size_t(w) * std::size_t(h);
            for (std::size_t i = 0; i < total; ++i)
            {
                // Случайный БАЙТ ГЕНЕРАТОРА есть случайный УРОВЕНЬ ПОЛЯ: поток
                // LCG не тронут (порядок вызовов и короткое замыкание `||`
                // сохранены дословно), а в канал он едет дверью записи.
                // Порог 42 здесь НАМЕРЕННО не плоскость моря: маска обязана
                // расходиться с высотой, иначе негативный контроль пуст.
                const std::uint8_t heightByte = next_u8();
                td.rgba[i * 4u + 0] =
                    sm::field_word_of(float(heightByte) / 255.0f);
                td.rgba[i * 4u + 3] =
                    heightByte < 42u || (next_u8() & 7u) == 0u
                        ? std::uint16_t(0)
                        : std::uint16_t(sm::kFieldWordMax);
            }

            const std::size_t roadSize = total > 1 ? total - 1 : 0;
            const std::size_t dirtSize = total + 2u;
            std::vector<std::uint8_t> road(roadSize, 0);
            std::vector<std::uint8_t> dirt(dirtSize, 0);
            for (std::size_t i = 0; i < road.size(); ++i)
                road[i] = (next_u8() & 3u) == 0u ? 255 : 0;
            for (std::size_t i = 0; i < dirt.size(); ++i)
                dirt[i] = (next_u8() & 5u) == 0u ? 255 : 0;

            const sm::FeatureLayer actual =
                sm::build_feature_layer(td, road, &dirt);
            const sm::FeatureLayer expected =
                build_reference_feature_layer(td, road, &dirt);

            CHECK(actual.width == expected.width && actual.height == expected.height,
                         "reference matrix dimensions must match");
            CHECK(actual.data == expected.data,
                         "reference matrix must match feature pass contract");
        }
    }
}

void test_feature_water_is_the_plane_not_the_mask()
{
    // ЭТОТ СВИДЕТЕЛЬ ПЕРЕВЁРНУТ ВЕРДИКТОМ ВЛАДЕЛЬЦА (2026-09-25, дословно:
    // «НИКАКИХ МАСОК строго единый порог высоты УРОВЕНЬ моря»). Он звался
    // `test_feature_land_mask_trusts_alpha` и утверждал ровно обратное: что
    // байт маски A есть авторитет воды НЕЗАВИСИМО от высоты. Это охраняло не
    // закон мира, а второй спеллинг одного вопроса (ЗАКОН НУЛЕВОЙ п.5), и
    // ровно этим спеллингом мир расходился с собой на берегу.
    // ЗАКОН АДРЕСА: мир ВСЕГДА квадрат и степень двойки — здесь стояло 3×1.
    sm::TerrainData td = make_terrain(4, 4, 240.0f / 255.0f);
    set_height(td, 2, 0, 40.0f / 255.0f);   // ниже плоскости — вода, и только поэтому
    // НЕГАТИВНЫЙ КОНТРОЛЬ ЗАКОНА: маска лжёт про воду на клетке, чья высота
    // выше плоскости. Мир обязан ответить ЗЕМЛЯ — иначе авторитет вернулся к
    // маске, и вердикт нарушен.
    set_alpha(td, 3, 0, 0u);

    std::vector<std::uint8_t> dirt(std::size_t(td.width) * td.height, 0);
    dirt[idx(td, 0, 0)] = 255;
    dirt[idx(td, 2, 0)] = 255;
    dirt[idx(td, 3, 0)] = 255;
    const std::vector<std::uint8_t> empty(std::size_t(td.width) * td.height, 0);
    const sm::FeatureLayer fl =
        sm::build_feature_layer(td, empty, &dirt);

    CHECK(fl.at(0, 0) == sm::FT_DirtRoad,
                 "dirt pass must stamp features on land cells");
    CHECK(fl.at(2, 0) == sm::FT_Bridge,
                 "a cell BELOW the sea plane under a paid lane stamps FT_Bridge");
    CHECK(fl.at(3, 0) == sm::FT_DirtRoad,
                 "маска A не авторитет: высота выше плоскости — земля, не пролёт");
}

void test_feature_water_filter_uses_map_sea_level()
{
    // ЗАКОН АДРЕСА: квадрат, степень двойки — здесь стояло 3×1.
    // ОДНО ЧИСЛО ДВИЖЕТ МИР (M-109): уровни 0.314 и 0.392 — берег при плоскости
    // 0.30 и дно при 0.40, и различаются два прогона ровно этим числом.
    // Рукописная простановка A=суша отсюда снята: она делала «несогласие
    // маски и высоты», то есть ровно тот второй спеллинг, которого больше нет.
    sm::TerrainData td = make_terrain(4, 4, 120.0f / 255.0f);
    set_height(td, 0, 0, 80.0f / 255.0f);
    set_height(td, 1, 0, 100.0f / 255.0f);
    set_height(td, 2, 0, 120.0f / 255.0f);

    std::vector<std::uint8_t> road(std::size_t(td.width) * td.height, 0);
    std::vector<std::uint8_t> dirt(std::size_t(td.width) * td.height, 0);
    road[idx(td, 0, 0)] = 255;
    dirt[idx(td, 1, 0)] = 255;

    sm::TerrainData lowSeaTd = td;
    lowSeaTd.seaLevel16 = sm::field_word_of(0.30f);
    sm::TerrainData defaultSeaTd = td;
    defaultSeaTd.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    const sm::FeatureLayer lowSea =
        sm::build_feature_layer(lowSeaTd, road, &dirt);
    const sm::FeatureLayer defaultSea =
        sm::build_feature_layer(defaultSeaTd, road, &dirt);

    CHECK(lowSea.at(0, 0) == sm::FT_Road,
                 "feature water filter must use active low sea level for roads");
    CHECK(lowSea.at(1, 0) == sm::FT_DirtRoad,
                 "feature water filter must use active low sea level for dirt roads");
    // Под плоскостью 0.40 те же клетки — вода, а путь по воде ЕСТЬ платный
    // пролёт: камень (FT_Bridge), а не пустота. Прежде здесь ждали FT_None —
    // и это был не закон, а следствие несогласия маски с высотой: широкий
    // предикат воды снимал фичу, узкий отказывался звать её мостом.
    CHECK(defaultSea.at(0, 0) == sm::FT_Bridge,
                 "дорога под плоскостью моря — пролёт, и он камень");
    CHECK(defaultSea.at(1, 0) == sm::FT_Bridge,
                 "грунтовка под плоскостью моря — тот же пролёт, тот же камень");
}

// ── M-112: ЗАПИСЬ ФИЧИ ХОДИТ ОДНОЙ ДВЕРЬЮ, И АДРЕС У НЕЁ ОДИН ─────────────
// Наряд снял девять сырых `data[y*w+x] = FT_…`. Утверждение «второй записи
// нет» адреса в коде не имеет (AGENTS §0 п.2) и потому указывает СЮДА: тут
// стоит контракт, ради которого сырые записи и снесены — один адрес, один
// судья рода байта, и доказательство, что снесённое правописание врало.
void test_feature_write_goes_through_one_door()
{
    constexpr int kSide = 16;   // ЗАКОН АДРЕСА: квадрат, степень двойки
    sm::FeatureLayer fl;
    fl.resize(kSide, kSide);

    // 1. ДВЕ ФОРМЫ — ОДНА ДВЕРЬ. Пара `x,y` и адрес одним числом обязаны
    //    отвечать ОДНИМ байтом на каждой клетке мира; разойдись они — у слоя
    //    два адресных пространства, то есть второй ответ на вопрос «что стоит
    //    на этой клетке» (DOD п.6). Род берётся из ЕНУМА по адресу, а не
    //    списком руками: новая строка реестра фич попадёт под свидетеля сама.
    int samples = 0, mismatches = 0;
    for (int y = 0; y < kSide; ++y)
    {
        for (int x = 0; x < kSide; ++x)
        {
            const std::uint32_t cell = sm::cell_of(x, y, kSide);
            const sm::FeatureType t =
                sm::FeatureType(cell % std::uint32_t(sm::FT_Count));
            fl.set(cell, t);                                  // пишем адресом
            if (fl.at(x, y) != t) ++mismatches;               // читаем парой
            fl.set(x, y, sm::FT_None);                        // пишем парой
            if (fl.at(cell) != sm::FT_None) ++mismatches;     // читаем адресом
            ++samples;
        }
    }
    CHECK(samples == kSide * kSide && mismatches == 0,
          "дверь по адресу и дверь по паре — одна дверь: один байт на клетку");

    // 2. ДВЕРЬ СУДИТ РОД БАЙТА, А СЫРАЯ ЗАПИСЬ НЕ СУДИЛА НИЧЕГО. Это не
    //    украшение: ре-штамп загрузки (`main.cpp`, builtFeatures) несёт байт
    //    ИЗ ФАЙЛА, и до наряда он ложился в поле как есть — незаконный род
    //    доезжал до зон и стоимости пути.
    fl.set(0u, static_cast<sm::FeatureType>(255u));
    CHECK(fl.data[0] == std::uint8_t(sm::FT_None) && fl.at(0u) == sm::FT_None,
          "дверь по адресу обязана сажать незаконный род в FT_None");

    // 3. НЕГАТИВНЫЙ КОНТРОЛЬ: снесённое правописание ОБЯЗАНО врать. Ручной
    //    `y*w+x` по незавёрнутой координате уходит мимо клетки — здесь вообще
    //    за пределы памяти слоя, — и ровно поэтому каждой сырой записи нужны
    //    были два пред-заворота, которые автор мог забыть. Дверь заворачивает
    //    сама, и промахнуться ей нечем.
    const int seamX = kSide + 1;        // тот же столбец мира, что x = 1
    const int seamY = kSide - 1;
    const std::size_t raw =
        std::size_t(seamY) * std::size_t(kSide) + std::size_t(seamX);
    fl.set(seamX, seamY, sm::FT_Bridge);
    const std::uint32_t door = sm::cell_of(seamX, seamY, kSide);
    CHECK(fl.at(door) == sm::FT_Bridge && fl.at(1, kSide - 1) == sm::FT_Bridge
              && std::size_t(door) != raw && raw >= fl.data.size(),
          "ручной y*w+x на шве уезжает с клетки — дверь заворачивает адрес");
}

} // namespace

int main()
{
    test_feature_priority_and_water_filter();
    test_empty_and_malformed_inputs_are_safe();
    test_feature_layer_reference_matrix();
    test_feature_water_is_the_plane_not_the_mask();
    test_feature_water_filter_uses_map_sea_level();
    test_feature_write_goes_through_one_door();
    return sm::test::report("feature_layer_parity_test");
}

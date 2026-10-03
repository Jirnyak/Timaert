// CPU-side macroworld terrain data. Built by CPU synthesis.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#include "core/torus.h"
#include "macro/plates.h"
#include "tables/biomes.h"

namespace sm {

// ── УРОВЕНЬ МОРЯ — ОДНА ПЛОСКОСТЬ НА ВЕСЬ МИР (владелец, 2026-09-27) ──────
// Дословно: «у нас фиксированная плоскость УРОВЕНЬ моря на весь макромир и
// микромир её наследует». Следствия, и они оба отрицательные:
//   • это НЕ поле над клетками — клеточная величина здесь ровно одна, байт
//     высоты в канале R, а порог к ней один на весь тор;
//   • это НЕ колонка биома — `BiomeConfig` субмира держал её одиннадцать раз
//     одинаковой, и одиннадцать копий одного числа колонкой не являются.
// Число 0.40 — дефолт РЕДАКТОРА, а не вывод из инварианта: уровень моря
// авторский, его двигает ползунок экрана кастомного мира. Инвариант, который
// тут действительно есть, другой — порог ОДИН, и живёт он в одном месте.
inline constexpr float kDefaultSeaLevel = 0.40f;

// ── СЛОВО КАРТЫ — unorm16, И ЭТО ВЫВЕДЕНО ЗАМЕРОМ, А НЕ ВКУСОМ ────────────
// Высота хранится СЛОВОМ на канал, а не байтом, потому что с кривой переноса
// (`height_m@src/sub/height.h`) цена одного шага словаря зависит от высоты, и
// байт переставал быть измерительным полом ровно там, где стоят горы.
// Замерено `height_census` на пяти сидах: шаг БАЙТА стоит 5.9 м у воды, 28.0 м
// на горной линии и **162.7 м у p99 суши** — то есть на построенных горах
// вылезала бы терраса в полтораста метров. Шаг СЛОВА стоит там 0.63 м.
// Цена — 4 МиБ → 8 МиБ на карту 1024² (DOD п.2: «десятки и даже сотни
// мегабайт бесплатны»), и ни одного ответа карта при этом не теряет.
inline constexpr float kFieldWordMax = 65535.0f;

// ЧТЕНИЕ: слово карты → нормированное поле. ОДНА дверь на весь проект, и
// существует она затем, чтобы ширина хранения не стояла сорока литералами по
// дереву: до этой правки по `src/` и `tests/` было рассыпано `/ 255.0f`, то
// есть сорок копий словаря, разъехаться с которым он мог молча.
inline constexpr float field01_of(std::uint16_t word) {
    return float(word) * (1.0f / kFieldWordMax);
}

// ЗАПИСЬ: авторский уровень → слово карты, обратная к `field01_of`. `floor`,
// а не округление: клетка ровно на плоскости — суша (вода строго НИЖЕ уровня,
// вердикт владельца 2026-09-25 «есть уровнеь рельефа ниже котрого вода»).
// Единственный переводчик уровня в слово: четыре рукописных
// `uint8_t(seaLevel * 255.0f)` звали эту же величину, усекая её по-своему.
// Звали дверь `sea_level_byte` — по ЕДИНСТВЕННОМУ тогда звонящему; теперь
// через неё авторят уровень и фикстуры свидетелей, и имя от одного звонящего
// стало ложью о её работе.
inline constexpr std::uint16_t field_word_of(float level01) {
    // Усечение неотрицательного И ЕСТЬ floor, поэтому дверь обходится без
    // <cmath> и остаётся constexpr: свидетелям порог нужен под компилятором
    // (ЗАКОН НУЛЕВОЙ п.6), а заголовок не платит за тело (§5 п.13).
    const float c = level01 < 0.0f ? 0.0f : (level01 > 1.0f ? 1.0f : level01);
    return std::uint16_t(int(c * kFieldWordMax));
}

struct LayerParameters {
    // The world's seed IS an integer (CANON S26 «всё дискретно»); it was a
    // float here, so every consumer round-tripped through casts and any seed
    // above 2^24 would have silently lost bits. Where the synthesis feeds it
    // into float noise, the cast happens AT the use site (map_generator.cpp)
    // — every UI-facing seed is < 100000 (see main.cpp), exactly
    // representable, so the generated world is bit-identical.
    std::uint32_t seed = 1u;
    // THE macroworld synthesis defaults — this struct is the source of truth.
    float seaLevel = kDefaultSeaLevel;
    float heightScale = 1.0f;
    float moistureScale = 1.0f;
    float temperatureVariation = 0.30f;
    float continentScale = 0.50f;
    float continentIntensity = 0.40f;
    // НАСКОЛЬКО ПОДНЯЛСЯ МАССИВ НА СХОДЯЩЕМСЯ ШВЕ — вес лерпа между базовым
    // полем и силуэтом гряды (`kRangeFootLevel@src/macro/map_generator.cpp`).
    // СМЫСЛ ЧИСЛА СМЕНИЛСЯ с M-210: прежде это была доля НЕБА над базой
    // (`h += (1−h)·ridged·…`), и у той формы был потолок — выше ≈0.77 поля она
    // не доставала, а крест упирался в единицу и переставал быть крестом.
    // Теперь комбинация выпуклая, из [0,1] не выходит ни при каком весе, и
    // единица здесь значит «гряда поднялась полностью», а не «уткнулась».
    float ridgeIntensity = 1.00f;
    float domainWarp = 0.30f;
    float heightOctaves = 6.0f;
    float moistureOctaves = 4.0f;
};

struct TerrainData {
    int width = 0, height = 0;
    // RGBA: R=height, G=moisture, B=temperature, A=mask (word max=land, 0=water).
    // СЛОВО, А НЕ БАЙТ — вывод у `kFieldWordMax` выше: байт стоил 162.7 м у p99
    // суши, то есть терраса ровно на горах. Четыре канала по 16 бит.
    std::vector<std::uint16_t> rgba;
    // СИД ЭТОЙ КАРТЫ. Кто читает и зачем: `MacroRendererVk::record` — узор
    // карты обязан быть свойством МИРА, а не картинки (вердикт владельца
    // 2026-09-28, CANON S18.2: «и сид и шейдеры от него»). До этого шейдеру
    // ехала константа 1.0 со ссылкой на удалённый GL-рендерер, и позиции крон,
    // места цветов и языки песка совпадали во всех мирах на одинаковых клетках.
    // Форма — та же, что у `seaLevel16`: величина едет С КАРТОЙ, а не вторым
    // параметром через полдерева (прецедент — `DepositLayer::birthSeaLevel`).
    std::uint32_t seed = 0u;
    // R8 river mask generated from the terrain heightmap. 255 = river cell.
    std::vector<std::uint8_t> riverData;
    // Плоскость моря ЭТОЙ карты. Кто читает и зачем: `is_water` ниже, и через
    // неё весь макромир — это единственный ответ на «вода ли клетка»; больше
    // её не читает никто. Почему колонкой карты, а не параметром у каждой
    // двери: порог есть свойство ЗАПЕЧЁННОЙ карты (по нему запекались маска и
    // врез рек), и девять дефолтов `float seaLevel = 0.40f` в `spawners.h`
    // были девятью копиями этого свойства, разъехаться с которым карта могла
    // молча. Прецедент формы — `DepositLayer::birthSeaLevel`.
    // Ставится в РОЖДЕНИИ карты (`generate_terrain`/`generate_river_data`);
    // дефолт — плоскость дефолтного мира, чтобы карта, собранная руками в
    // харнессе, отвечала как мир, а не как «всё суша».
    std::uint16_t seaLevel16 = field_word_of(kDefaultSeaLevel);
    // ── ПОЛЕ БИОМА НАД ТОРОМ (ЗАКОН ПОЛЯ; вердикт владельца 2026-09-28) ──
    // Дословно: «при генерации мира можно функции там ргб и тд, но когда мир
    // уже сгенерился… там должно всё уже быть структурно системно». Это и есть
    // граница двух представлений: РОЖДЕНИЕ считает (функцией, по трём каналам
    // образа), ЖИВОЙ МИР читает (полем, одно число на клетку).
    // Биом — число, привязанное к клетке, значит он ПОЛЕ над миром, а не
    // функция, которую каждый считает за себя: тринадцать мест считали, и
    // четыре из них разъехались (M-110 — свой горный порог у леса, каскад без
    // Mountain у трассера, копия ветвей у зон). Функцию можно переписать на
    // месте; поле имеет ОДНОГО писателя по построению, и второй ответ на
    // «какой биом» перестаёт быть другим вызовом — он становится другим
    // массивом, то есть видимым.
    // Один байт на клетку: 1 МиБ при 1024², 16 МиБ при 4096² — по DOD п.2
    // «десятки и даже сотни мегабайт бесплатны». В сейв не едет: терраин там
    // не хранится вовсе, мир пересобирается из сида.
    // Пишет ровно одна дверь — `bake_biomes`, последним актом рождения, ПОСЛЕ
    // вреза рек (врез срезает высоты, и клетка русла становится водой; поле,
    // выпеченное до него, соврало бы именно на реках).
    std::vector<std::uint8_t> biome;

    // ── КАРКАС ПЛИТ — ГРУБАЯ МАКРОЗАТРАВКА МИРА (M-210) ──────────────────
    // Строки плит плюс поле их ординала над клетками. Рождается вместе с
    // картой и живёт с ней: это тот слой, в который однажды ляжет авторская
    // серая карта («чёрное — море, белое — нагорье»), а сегодня его рисует
    // сид. Полный разбор — в шапке `macro/plates.h`.
    // В сейв не едет, как и всё здесь: мир пересобирается из сида.
    PlateMap plates;

    static bool cell_count_for(int w, int h, std::size_t& out) {
        out = 0;
        if (w <= 0 || h <= 0)
            return false;
        if (std::size_t(w) > std::numeric_limits<std::size_t>::max() / std::size_t(h))
            return false;
        out = std::size_t(w) * std::size_t(h);
        return true;
    }

    std::size_t cell_count() const {
        std::size_t n = 0;
        return cell_count_for(width, height, n) ? n : 0u;
    }

    bool has_rgba_storage() const {
        const std::size_t n = cell_count();
        return n > 0u
            && n <= std::numeric_limits<std::size_t>::max() / 4u
            && rgba.size() >= n * 4u;
    }

    bool has_river_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && riverData.size() >= n;
    }

    // ── ДВЕРИ К КЛЕТКЕ (ЗАКОН АДРЕСА, 2026-09-23) ────────────────────────
    // Здесь стоял сырой `y * width + x` БЕЗ ЗАВОРОТА, и так у всех трёх
    // каналов. Дефекта не случилось: все 19 сегодняшних звонящих заворачивают
    // координату сами, проверено поимённо. Но незаворачивающая дверь — это
    // приглашение: первый же читатель, который решит, что «тор и так тор»,
    // прочитает за границей буфера, и тихо.
    //
    // Fail-closed при незаконной форме мира отдаёт НОЛЬ, а ноль высоты ниже
    // любого уровня моря — то есть незаконный мир целиком вода и на нём
    // ничего не ставится. Это отказ, а не выдуманная суша.
    inline std::uint16_t height_at(int x, int y) const {
        if (!world_shape_ok(width, height)) return 0u;
        return rgba[std::size_t(cell_of(x, y, width)) * 4 + 0];
    }
    inline std::uint16_t moisture_at(int x, int y) const {
        if (!world_shape_ok(width, height)) return 0u;
        return rgba[std::size_t(cell_of(x, y, width)) * 4 + 1];
    }
    inline std::uint16_t temperature_at(int x, int y) const {
        if (!world_shape_ok(width, height)) return 0u;
        return rgba[std::size_t(cell_of(x, y, width)) * 4 + 2];
    }
    // ── ЕДИНСТВЕННЫЙ ОТВЕТ «ВОДА ЛИ КЛЕТКА» ──────────────────────────────
    // Вердикт владельца (2026-09-25, дословно): «НИКАКИХ МАСОК строго единый
    // порог высоты УРОВЕНЬ моря это кстати и для макро и для микро верно».
    // До него вопрос отвечали ВОСЕМЬЮ способами (перепись 2026-09-27): маска
    // A==0 — 10 чтений, маска <полслова, float `h/слово < seaLevel` — 4, рукописный
    // байт мимо двери — 5 в `politik.cpp`, трассер рек через `<=` — 7 (клетка
    // ровно на плоскости была ему водой, а маске сушей), плюс харнесс смоуков
    // и шейдер. Маска при этом не была вторым ЗНАНИЕМ — она была вторым
    // СПЕЛЛИНГОМ: её последним действием переписывает тот же порог
    // (`map_generator.cpp`, врез рек), так что расходиться они могли только
    // округлением, и ровно этим и расходились на берегу.
    // Адрес — ОДНО ЧИСЛО (ЗАКОН АДРЕСА): первичная форма берёт индекс, пара
    // x,y входит через `cell_of` и остаётся для тех, у кого на руках геометрия.
    // Fail-closed в ВОДУ: карты нет — суши нет (ноль вклада, не выдуманная
    // земля и не падение).
    inline bool is_water(std::uint32_t cell) const {
        if (!has_rgba_storage()) return true;
        return rgba[std::size_t(cell) * 4u + 0u] < seaLevel16;
    }
    inline bool is_water(int x, int y) const {
        if (!world_shape_ok(width, height)) return true;
        return is_water(cell_of(x, y, width));
    }
};

// ── THE cell biome classifier (CANON S6, 2026-08-24) ─────────────────────
// One cascade for a WORLD CELL: the sea-level plane decides Water, elevation
// decides Mountain, the climate matrix fills in the rest. `biome_at`
// (biomes.h) stays the pure-math core for callers that do not hold a cell —
// the shader mirror and world-gen scratch buffers.
//
// ПОРОГ, А НЕ МАСКА (вердикт владельца 2026-09-25, снявший прежнюю доктрину
// этой шапки «The mask, not the threshold»): воду называет `is_water`, то есть
// плоскость моря карты. Прежний довод за маску был верен по факту и ложен по
// форме — маска писалась ИЗ того же байта высоты, значит знания не добавляла,
// а второй спеллинг одного вопроса добавляла. Байт A остаётся ЖИВЫМ, но уже
// только как канал ТЕКСТУРЫ для шейдера (`macro.frag`); мир его не спрашивает.
//
// ── ДВА ПРЕДСТАВЛЕНИЯ, ОДИН ЗАКОН, И ГРАНИЦА МЕЖДУ НИМИ — РОЖДЕНИЕ МИРА ──
//
// `biome_classify` — КАСКАД. Считает биом из трёх каналов образа. Звать его
// вправе ТОЛЬКО генерация, и сегодня его зовут ровно двое: трассер рек (ему
// нужен биом ДО вреза русла) и сама выпечка поля. Вызов из живого мира —
// дефект ревью: живой мир читает поле, а не пересчитывает его, иначе у
// вопроса снова два ответа и они снова разъедутся.
//
// Torus-wrapped; fail-closed в ВОДУ (мир без хранилища — мир без суши, по
// которой ходят, на которой растят и охотятся: нулевой вклад, не падение).
inline Biome biome_classify(const TerrainData& td, std::uint32_t cell) {
    if (!td.has_rgba_storage()) return Biome::Water;
    if (td.is_water(cell)) return Biome::Water;
    const std::size_t s = std::size_t(cell) * 4u;
    const float h = field01_of(td.rgba[s + 0u]);
    if (h >= kMountainBiomeLevel) return Biome::Mountain;
    return biome_from_climate(field01_of(td.rgba[s + 2u]),
                              field01_of(td.rgba[s + 1u]));
}

// `biome_at_cell` — ЧТЕНИЕ ПОЛЯ, и это единственный ответ живого мира на
// «какой биом у этой клетки» (CANON S6). Форма адреса та же, что у `is_water`:
// первичен ИНДЕКС (ЗАКОН АДРЕСА), пара x,y остаётся тем, у кого на руках
// геометрия. Fail-closed в ВОДУ — и здесь это не только «нет карты», но и
// «мир не дорождён»: поля нет, значит `bake_biomes` не звали.
inline Biome biome_at_cell(const TerrainData& td, std::uint32_t cell) {
    return std::size_t(cell) < td.biome.size() ? Biome(td.biome[cell])
                                               : Biome::Water;
}
inline Biome biome_at_cell(const TerrainData& td, int x, int y) {
    if (!world_shape_ok(td.width, td.height)) return Biome::Water;
    return biome_at_cell(td, cell_of(x, y, td.width));
}

// ПОСЛЕДНИЙ АКТ РОЖДЕНИЯ: свернуть каскад в поле. Дверь стоит рядом со своим
// полем и своим каскадом — все трое об одном.
// ПОЧЕМУ ТЕЛО В ЗАГОЛОВКЕ ЗАКОННО (§5 п.13 — замерено, не предположено):
// запрещено КАПОЗАВИСИМОЕ тело, то есть такое, чью стоимость фронтенд платит
// НА РАЗБОРЕ — имя с инициализатором над типом размером с кап, которое clang
// обязан попробовать вычислить. Здесь нет ни того, ни другого: длина приходит
// рантаймом из вектора, вычислять на этапе разбора нечего. Судит это не довод,
// а гейт `header_cost_test` (стена 12× медианы пиковой памяти), и он зелёный.
inline void bake_biomes(TerrainData& td) {
    const std::size_t n = td.cell_count();
    if (n == 0u || !td.has_rgba_storage()) {
        td.biome.clear();
        return;
    }
    td.biome.resize(n);
    for (std::size_t c = 0; c < n; ++c)
        td.biome[c] = std::uint8_t(biome_classify(td, std::uint32_t(c)));
}

// Generate the master texture on GPU and read back to CPU. Allocates `texture`.
TerrainData generate_terrain(int w, int h, const LayerParameters& params);

// Second CPU synth pass (also called by generate_terrain): trace least-cost
// rivers hugging climate-biome edges toward the nearest sea, stamp them into
// td.riverData, and carve those cells below sea level so they classify as
// Biome::Water. Exposed for the river generation test suite; call it on a
// TerrainData whose rgba height/moisture/temperature channels are populated.
void generate_river_data(TerrainData& td, const LayerParameters& params);

void destroy_terrain(TerrainData& t);

} // namespace sm

// water_answer_test — ОДНА ПЛОСКОСТЬ МОРЯ НА ВЕСЬ МАКРОМИР (M-109).
//
// Вердикт владельца (2026-09-27, дословно): «у нас фиксированная плоскость
// УРОВЕНЬ моря на весь макромир и микромир её наследует»; и раньше, 2026-09-25:
// «НИКАКИХ МАСОК строго единый порог высоты УРОВЕНЬ моря».
//
// Что охраняет этот свидетель — ЗАКОН, не сегодняшнее поведение:
//   1. одно число мира (`LayerParameters::seaLevel`) целиком определяет, какая
//      клетка вода, и определяет это ОДИНАКОВО для всех потребителей: двери
//      карты, каскада биома, цены пути и запечённого байта маски терраина;
//   2. порог живёт в СЛОВАРЕ СЛОВА высоты (`field_word_of`, наряд B3), и клетка
//      РОВНО на плоскости — суша (вода строго НИЖЕ);
//   3. маска A — не авторитет, а производное: подмена канала маски не меняет
//      ответа мира ни на одной клетке.
//
// Негативные контроли (AGENTS §8 п.6) — каждый обязан ПАДАТЬ, если закон
// нарушен, и они здесь утверждаются как контроли, а не подразумеваются:
//   · плоскость, поднятая на слово выше самой низкой занятой суши, ОБЯЗАНА
//     перекрасить эти клетки в воду — иначе «согласие» проверялось на мире, где
//     проверять нечего. Шаг берётся от САМОЙ КАРТЫ, а не наугад: свидетель
//     рождает своё предусловие сам (§8 п.11);
//   · маска, солгавшая про воду, ОБЯЗАНА быть проигнорирована;
//   · мир, сгенерированный с ДРУГИМ уровнем моря, обязан иметь другую долю
//     суши — иначе число не движет ничего.
#include <cstdio>
#include <vector>

#include "check.h"
#include "tables/biomes.h"
#include "macro/map_generator.h"
#include "macro/pathfinding.h"

using namespace sm;

namespace {

// ЗАКОН АДРЕСА: сторона — степень двойки, мир квадратен. 128² хватает, чтобы
// на сиде была и вода, и суша, и берег.
constexpr int kSide = 128;

LayerParameters params_with_sea(float seaLevel) {
    LayerParameters p{};
    p.seed = 12345u;
    p.seaLevel = seaLevel;
    return p;
}

// Сколько клеток мира считает водой ОДНА дверь.
long water_cells(const TerrainData& td) {
    long n = 0;
    for (std::uint32_t c = 0; c < std::uint32_t(td.cell_count()); ++c)
        if (td.is_water(c)) ++n;
    return n;
}

// ── 1. Плоскость едина: дверь, каскад биома, цена пути и байт маски ───────
void test_one_plane_answers_everyone() {
    TerrainData td = generate_terrain(kSide, kSide, params_with_sea(0.40f));
    CHECK_OR_RETURN(td.has_rgba_storage(), "мир сгенерирован");
    CHECK(td.seaLevel16 == field_word_of(0.40f),
          "карта унесла плоскость моря с собой при рождении");

    const PathCostData cost = build_cost_grid(td, nullptr, nullptr);
    CHECK_OR_RETURN(cost.water.size() == td.cell_count(),
                    "цена пути построена на весь мир");

    long samples = 0, doorWater = 0, disagreeBiome = 0, disagreeCost = 0,
         disagreeMask = 0;
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const std::uint32_t c = cell_of(x, y, kSide);
            const bool water = td.is_water(c);
            ++samples;
            if (water) ++doorWater;
            if ((biome_at_cell(td, x, y) == Biome::Water) != water)
                ++disagreeBiome;
            if ((cost.water[std::size_t(c)] != 0u) != water) ++disagreeCost;
            if ((td.rgba[std::size_t(c) * 4u + 3u] == 0u) != water)
                ++disagreeMask;
        }
    }
    // Счёт обязателен (AGENTS §8 п.3): цикл, который ничего не помёрил,
    // отчитался бы успехом.
    CHECK(samples == long(kSide) * kSide, "перебраны все клетки мира");
    CHECK(doorWater > 0 && doorWater < samples,
          "мир не выродился: в нём есть и вода, и суша");
    CHECK(disagreeBiome == 0, "каскад биома отвечает той же плоскостью");
    CHECK(disagreeCost == 0, "цена пути отвечает той же плоскостью");
    CHECK(disagreeMask == 0,
          "канал маски терраина — производное той же плоскости, не второй ответ");
    std::fprintf(stderr,
                 "[water] мир 128² сид 12345: воды %ld из %ld клеток\n",
                 doorWater, samples);
}

// ── 2. Одно число двигает весь мир ───────────────────────────────────────
void test_one_number_moves_the_world() {
    const TerrainData low = generate_terrain(kSide, kSide, params_with_sea(0.30f));
    const TerrainData mid = generate_terrain(kSide, kSide, params_with_sea(0.40f));
    const TerrainData high = generate_terrain(kSide, kSide, params_with_sea(0.55f));
    CHECK_OR_RETURN(low.has_rgba_storage() && mid.has_rgba_storage()
                        && high.has_rgba_storage(),
                    "три мира сгенерированы");

    const long wLow = water_cells(low);
    const long wMid = water_cells(mid);
    const long wHigh = water_cells(high);
    CHECK(wLow < wMid && wMid < wHigh,
          "выше плоскость — больше воды, и это единственное различие миров");
    std::fprintf(stderr, "[water] воды при 0.30/0.40/0.55: %ld / %ld / %ld\n",
                 wLow, wMid, wHigh);

    // И у высокого мира ту же плоскость видят каскад биома и байт маски —
    // «одно число» значит одно число ДЛЯ ВСЕХ, а не только для двери.
    long disagree = 0;
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            const std::uint32_t c = cell_of(x, y, kSide);
            const bool water = high.is_water(c);
            if ((biome_at_cell(high, x, y) == Biome::Water) != water) ++disagree;
            if ((high.rgba[std::size_t(c) * 4u + 3u] == 0u) != water) ++disagree;
        }
    CHECK(disagree == 0,
          "мир с непривычной плоскостью согласован так же, как дефолтный");
}

// ── 3. Негативные контроли ───────────────────────────────────────────────
void test_negative_controls() {
    TerrainData td = generate_terrain(kSide, kSide, params_with_sea(0.40f));
    CHECK_OR_RETURN(td.has_rgba_storage(), "мир сгенерирован");

    // (а) КОНТРОЛЬ ОСТРОТЫ: подъём плоскости обязан перекрасить хотя бы одну
    // клетку из суши в воду. Без этого утверждения согласие выше могло бы
    // держаться на мире, где берега просто нет.
    //
    // СВИДЕТЕЛЬ РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ САМ (§8 п.11), а не надеется на сид:
    // он находит САМУЮ НИЗКУЮ ЗАНЯТУЮ СУШУ — минимальное слово высоты, которое
    // в карте есть и которое плоскость сегодня сушей считает, — и поднимает
    // плоскость РОВНО НА ОДНО СЛОВО ВЫШЕ него. Тогда клетки с этим словом
    // обязаны стать водой: `is_water` есть строгое `h < plane`, значит
    // `plane = h + 1` топит ровно их, и `after > before` становится ЗАКОНОМ, а
    // не вероятностью. Суша на этом мире непуста (это утверждается выше,
    // `doorWater < samples`), поэтому предмет у контроля есть всегда.
    //
    // ПОЧЕМУ НЕ ПРОСТО `+1` К ПЛОСКОСТИ (так стояло, пока высота была байтом):
    // слово в 257 раз тоньше байта (65535/255 = 257), и на мире 128²
    // ожидаемых клеток на один шаг слова всего 16384/65535 ≈ 0.25, то есть по
    // Пуассону P(≥1) ≈ 1 − e^(−0.25) ≈ 22 %. Контроль «плюс одно слово» стал бы
    // броском монеты: он краснел бы не потому, что закон нарушен, а потому, что
    // миру не повезло, — и следующий агент пошёл бы подгонять мир под везение
    // (ЗАКОН НУЛЕВОЙ п.4). Шаг «до ближайшей занятой суши» от разрешения
    // хранения не зависит вовсе и верен при любой ширине канала.
    const long before = water_cells(td);
    std::uint16_t lowestLand = 0u;
    bool haveLand = false;
    for (std::uint32_t c = 0; c < std::uint32_t(td.cell_count()); ++c) {
        const std::uint16_t h = td.rgba[std::size_t(c) * 4u + 0u];
        if (h < td.seaLevel16) continue;                  // это уже вода
        if (!haveLand || h < lowestLand) { lowestLand = h; haveLand = true; }
    }
    // Дверь обязана СКАЗАТЬ, а не промолчать. Оба отказа невозможны на
    // настоящем мире — и именно поэтому они провал, а не тихий выход: мир,
    // который их выдал, перестал быть миром, на котором закон проверяем.
    CHECK_OR_RETURN(haveLand,
                    "мир не дал ни одной клетки выше плоскости — контролю "
                    "остроты нечего топить");
    CHECK_OR_RETURN(lowestLand < std::uint16_t(kFieldWordMax),
                    "самая низкая суша мира стоит на потолке словаря — "
                    "плоскость поднять некуда");
    const std::uint16_t restorePlane = td.seaLevel16;
    td.seaLevel16 = std::uint16_t(lowestLand + 1u);
    const long after = water_cells(td);
    CHECK(after > before,
          "контроль: плоскость, поднятая на слово выше самой низкой суши, "
          "топит её — одно число двигает ответ мира (иначе прибор слеп)");
    std::fprintf(stderr,
                 "[water] контроль остроты: низшая суша слово %u, плоскость "
                 "%u → %u, воды %ld → %ld\n",
                 unsigned(lowestLand), unsigned(restorePlane),
                 unsigned(lowestLand + 1u), before, after);
    td.seaLevel16 = restorePlane;

    // (б) МАСКА НЕ АВТОРИТЕТ: солгать всеми байтами A — ответ мира не дрогнет.
    // До вердикта 2026-09-25 этот случай перекрасил бы весь мир в воду.
    long flipped = 0;
    for (std::uint32_t c = 0; c < std::uint32_t(td.cell_count()); ++c) {
        const bool water = td.is_water(c);
        td.rgba[std::size_t(c) * 4u + 3u] =
            water ? std::uint16_t(kFieldWordMax) : std::uint16_t(0);
        if (td.is_water(c) != water) ++flipped;
        const bool biomeWater =
            biome_at_cell(td, cell_x(c, kSide), cell_y(c, kSide))
                == Biome::Water;
        if (biomeWater != water) ++flipped;
    }
    CHECK(flipped == 0, "перевёрнутая маска A не меняет ни одного ответа мира");

    // (в) ГРАНИЦА: клетка РОВНО на плоскости — суша; на слово ниже — вода.
    TerrainData edge;
    edge.width = 2;
    edge.height = 2;
    edge.rgba.assign(2u * 2u * 4u, 0u);
    edge.seaLevel16 = field_word_of(0.40f);
    edge.rgba[0 * 4u + 0u] = edge.seaLevel16;                       // ровно
    edge.rgba[1 * 4u + 0u] = std::uint16_t(edge.seaLevel16 - 1u);   // ниже
    CHECK(!edge.is_water(0u), "клетка ровно на плоскости — суша");
    CHECK(edge.is_water(1u), "клетка на слово ниже плоскости — вода");

    // (г) FAIL-CLOSED В ВОДУ: карты нет — суши нет. Отказ «всё суша» пустил бы
    // размещателей искать землю там, где её не бывает.
    const TerrainData absent{};
    CHECK(absent.is_water(0u) && absent.is_water(0, 0),
          "карта без хранилища отвечает водой, а не выдуманной сушей");
}

// ── 4. Перевод float→слово — одна дверь, и она под компилятором ──────────
void test_threshold_lives_in_one_door() {
    // ЛУЧШАЯ ФОРМА СВИДЕТЕЛЯ — нарушение, которое невозможно собрать
    // (ЗАКОН НУЛЕВОЙ п.6): дверь constexpr, значит закон проверяет компилятор.
    // Потолок берётся ИЗ `kFieldWordMax`, а не литералом: 255 стояло здесь до
    // наряда B3, и смена ширины канала переписала бы литерал молча.
    static_assert(field_word_of(0.0f) == 0u, "ноль — дно");
    static_assert(field_word_of(1.0f) == std::uint16_t(kFieldWordMax),
                  "единица — потолок слова");
    static_assert(field_word_of(-1.0f) == 0u, "ниже нуля зажимается");
    static_assert(field_word_of(2.0f) == std::uint16_t(kFieldWordMax),
                  "выше единицы зажимается");
    // Дефолт мира и дефолт карты — ОДНО число, а не два совпавших.
    static_assert(TerrainData{}.seaLevel16 == field_word_of(kDefaultSeaLevel),
                  "карта без генерации несёт плоскость дефолтного мира");
    static_assert(LayerParameters{}.seaLevel == kDefaultSeaLevel,
                  "дефолт генератора читает ту же константу");
    // Число плоскости ВЫВОДИТСЯ из уровня и потолка словаря, а не вписывается
    // руками: вписанное охраняло бы прошлую ширину канала (до B3 здесь стояло
    // прибитое 102 — байт байтовой эпохи).
    CHECK(field_word_of(kDefaultSeaLevel)
              == std::uint16_t(int(kDefaultSeaLevel * kFieldWordMax)),
          "плоскость дефолта в словаре слова — усечение (floor), не округление");
    // И это именно floor, а не совпадение округления: слово плоскости не выше
    // самой плоскости, а следующее за ним — уже выше.
    static_assert(field01_of(field_word_of(kDefaultSeaLevel)) <= kDefaultSeaLevel,
                  "усечение не задирает плоскость");
    static_assert(field01_of(std::uint16_t(field_word_of(kDefaultSeaLevel) + 1u))
                      > kDefaultSeaLevel,
                  "следующее слово уже выше плоскости — усечение, а не сдвиг");
}

} // namespace

int main() {
    test_one_plane_answers_everyone();
    test_one_number_moves_the_world();
    test_negative_controls();
    test_threshold_lives_in_one_door();
    return sm::test::report("water_answer_test");
}

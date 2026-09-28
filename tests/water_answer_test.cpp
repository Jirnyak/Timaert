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
//   2. порог живёт в байтовом словаре высоты (`sea_level_byte`), и клетка
//      РОВНО на плоскости — суша (вода строго НИЖЕ);
//   3. маска A — не авторитет, а производное: подмена байта маски не меняет
//      ответа мира ни на одной клетке.
//
// Негативные контроли (AGENTS §8 п.6) — каждый обязан ПАДАТЬ, если закон
// нарушен, и они здесь утверждаются как контроли, а не подразумеваются:
//   · сдвиг плоскости на один байт ОБЯЗАН перекрасить хотя бы одну клетку —
//     иначе «согласие» проверялось на мире, где проверять нечего;
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
    CHECK(td.seaLevel8 == sea_level_byte(0.40f),
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
          "байт маски терраина — производное той же плоскости, не второй ответ");
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

    // (а) КОНТРОЛЬ ОСТРОТЫ: сдвиг плоскости на ОДИН байт обязан перекрасить
    // хотя бы одну клетку. Без этого утверждения согласие выше могло бы
    // держаться на мире, где берега просто нет.
    const long before = water_cells(td);
    td.seaLevel8 = std::uint8_t(td.seaLevel8 + 1u);
    const long after = water_cells(td);
    CHECK(after > before,
          "контроль: один байт плоскости меняет ответ мира (иначе прибор слеп)");
    td.seaLevel8 = sea_level_byte(0.40f);

    // (б) МАСКА НЕ АВТОРИТЕТ: солгать всеми байтами A — ответ мира не дрогнет.
    // До вердикта 2026-09-25 этот случай перекрасил бы весь мир в воду.
    long flipped = 0;
    for (std::uint32_t c = 0; c < std::uint32_t(td.cell_count()); ++c) {
        const bool water = td.is_water(c);
        td.rgba[std::size_t(c) * 4u + 3u] = water ? 255u : 0u;
        if (td.is_water(c) != water) ++flipped;
        const bool biomeWater =
            biome_at_cell(td, cell_x(c, kSide), cell_y(c, kSide))
                == Biome::Water;
        if (biomeWater != water) ++flipped;
    }
    CHECK(flipped == 0, "перевёрнутая маска A не меняет ни одного ответа мира");

    // (в) ГРАНИЦА: клетка РОВНО на плоскости — суша; на байт ниже — вода.
    TerrainData edge;
    edge.width = 2;
    edge.height = 2;
    edge.rgba.assign(2u * 2u * 4u, 0u);
    edge.seaLevel8 = sea_level_byte(0.40f);
    edge.rgba[0 * 4u + 0u] = edge.seaLevel8;                       // ровно
    edge.rgba[1 * 4u + 0u] = std::uint8_t(edge.seaLevel8 - 1u);    // ниже
    CHECK(!edge.is_water(0u), "клетка ровно на плоскости — суша");
    CHECK(edge.is_water(1u), "клетка на байт ниже плоскости — вода");

    // (г) FAIL-CLOSED В ВОДУ: карты нет — суши нет. Отказ «всё суша» пустил бы
    // размещателей искать землю там, где её не бывает.
    const TerrainData absent{};
    CHECK(absent.is_water(0u) && absent.is_water(0, 0),
          "карта без хранилища отвечает водой, а не выдуманной сушей");
}

// ── 4. Перевод float→байт — одна дверь, и она под компилятором ───────────
void test_threshold_lives_in_one_door() {
    // ЛУЧШАЯ ФОРМА СВИДЕТЕЛЯ — нарушение, которое невозможно собрать
    // (ЗАКОН НУЛЕВОЙ п.6): дверь constexpr, значит закон проверяет компилятор.
    static_assert(sea_level_byte(0.0f) == 0u, "ноль — дно");
    static_assert(sea_level_byte(1.0f) == 255u, "единица — потолок байта");
    static_assert(sea_level_byte(-1.0f) == 0u, "ниже нуля зажимается");
    static_assert(sea_level_byte(2.0f) == 255u, "выше единицы зажимается");
    // Дефолт мира и дефолт карты — ОДНО число, а не два совпавших.
    static_assert(TerrainData{}.seaLevel8 == sea_level_byte(kDefaultSeaLevel),
                  "карта без генерации несёт плоскость дефолтного мира");
    static_assert(LayerParameters{}.seaLevel == kDefaultSeaLevel,
                  "дефолт генератора читает ту же константу");
    CHECK(sea_level_byte(kDefaultSeaLevel) == 102u,
          "0.40 в байтовом словаре высоты — 102 (усечение = floor)");
}

} // namespace

int main() {
    test_one_plane_answers_everyone();
    test_one_number_moves_the_world();
    test_negative_controls();
    test_threshold_lives_in_one_door();
    return sm::test::report("water_answer_test");
}

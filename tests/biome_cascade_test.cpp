// biome_cascade_test — ОДИН КАСКАД «КАКОЙ БИОМ» ДЛЯ КЛЕТКИ МИРА (M-110).
//
// Что охраняет этот свидетель — ЗАКОН, не сегодняшнее поведение:
//   1. на вопрос «какой биом у этой клетки» у мира ОДИН ответ, и зовут его
//      `biome_at_cell` (CANON S6, шапка `map_generator.h`). Кто спрашивает —
//      трассер рек, лес, зоны опасности — спрашивает ЕЁ, а не свою копию
//      каскада (DOD п.6: второй ответ на один вопрос — дефект);
//   2. горная линия — ОДНО число, `kMountainBiomeLevel` (ЗАКОН КОНСТАНТ). До
//      M-110 их было два: мир судил гору по 0.75, а лес по 0.80, и полоса
//      между ними шириной 0.05 высоты была «гора для карты, не гора для
//      леса» — сосны на нарисованном камне;
//   3. берег леса мерится В КЛЕТКАХ, как берег реки двумя строками выше
//      (`kRiverBuffer`), а не в единицах высоты. Число 0.03 было третьим
//      словарём того же запрета и ушло без замены.
//
// Негативные контроли (AGENTS §8 п.6) — каждый обязан ПАДАТЬ при возврате
// второго каскада, и каждый здесь утверждается как контроль, а не
// подразумевается:
//   · плато РОВНО в полосе 0.75…0.80 — прежний лес вставал на нём, нынешний
//     обязан отказать; и та же карта обязана растить лес НИЖЕ полосы, иначе
//     отказ доказывает лишь бесплодную карту;
//   · мир РОВНОГО климата с массивом: у климатического каскада краёв биома на
//     суше нет вовсе, у полного край есть — рим массива. Реки у рима обязаны
//     родиться, а на той же карте БЕЗ массива — нет;
//   · зоны: байт рядом с горной линией обязан дрогнуть на 192 и не дрогнуть на
//     191 — иначе прибор не видит, где стоит порог.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "check.h"
#include "core/torus.h"
#include "tables/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"
#include "macro/spawners.h"
#include "macro/zones.h"

using namespace sm;

namespace {

// ЗАКОН АДРЕСА: сторона — степень двойки, мир квадратен.
constexpr int kSide = 128;

// Горная линия в байтовом словаре высоты: первый байт, который уже гора, и
// предыдущий, который ещё нет. Шапка обещала «выведено из
// `kMountainBiomeLevel`», а стояло ЧИСЛО 192, посчитанное руками под линию
// 0.75; когда линия переехала (2026-10-01, бескламповый синтез), числа разошлись
// с источником и `static_assert` поймал это сборкой — ровно как и должен.
// Теперь вывод настоящий: байт считается ИЗ линии, и свидетель верен при любой.
constexpr float kMountainLineByteF = kMountainBiomeLevel * 255.0f;
constexpr int   kMountainByte =
    int(kMountainLineByteF)
    + (float(int(kMountainLineByteF)) < kMountainLineByteF ? 1 : 0);
static_assert(float(kMountainByte) / 255.0f >= kMountainBiomeLevel,
              "192 обязан быть горой");
static_assert(float(kMountainByte - 1) / 255.0f < kMountainBiomeLevel,
              "191 обязан горой не быть");

// Полоса, в которой два порога расходились: гора для карты (0.75), не гора для
// прежнего леса (0.80). Байт 196 = 0.769 — внутри полосы.
constexpr int kDisputedByte = 196;
static_assert(float(kDisputedByte) / 255.0f >= kMountainBiomeLevel,
              "спорный байт обязан быть горой по единственному порогу");
static_assert(float(kDisputedByte) / 255.0f <= 0.80f,
              "спорный байт обязан лежать НИЖЕ снесённого второго порога 0.80");

LayerParameters world_params() {
    LayerParameters p{};
    p.seed = 12345u;
    return p;
}

// Ровная карта: одна высота, один климат — и климат выбран лесной (умеренный,
// средняя влага → Meadow), чтобы отказ леса нельзя было списать на климат.
TerrainData flat_world(int side, std::uint8_t heightByte) {
    TerrainData td;
    td.width = side;
    td.height = side;
    td.seaLevel8 = sea_level_byte(kDefaultSeaLevel);
    td.rgba.assign(std::size_t(side) * std::size_t(side) * 4u, 0u);
    td.riverData.assign(std::size_t(side) * std::size_t(side), 0u);
    for (std::size_t c = 0; c < std::size_t(side) * std::size_t(side); ++c) {
        td.rgba[c * 4u + 0u] = heightByte;
        td.rgba[c * 4u + 1u] = 128u;   // влага середины матрицы
        td.rgba[c * 4u + 2u] = 128u;   // температура середины матрицы
        td.rgba[c * 4u + 3u] = heightByte < td.seaLevel8 ? 0u : 255u;
    }
    bake_biomes(td);
    return td;
}

// Квадратное плато заданной высоты. ВЫСОТА — ИСТОЧНИК БИОМА, значит всякая её
// правка кончается перепечкой поля: карта, у которой мастер и поле разошлись,
// есть карта с двумя ответами — ровно то, против чего поле и заведено.
void stamp_plateau(TerrainData& td, int x0, int y0, int w, int h,
                   std::uint8_t heightByte) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) {
            const std::size_t c = std::size_t(cell_of(x, y, td.width));
            td.rgba[c * 4u + 0u] = heightByte;
            td.rgba[c * 4u + 3u] = heightByte < td.seaLevel8 ? 0u : 255u;
        }
    bake_biomes(td);
}

bool touches_water(const TerrainData& td, int x, int y) {
    const std::uint32_t c = cell_of(x, y, td.width);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            if (td.is_water(cell_step(c, dx, dy, td.width))) return true;
        }
    return false;
}

// ── 1. Лес спрашивает ТУ ЖЕ дверь, что мир ───────────────────────────────
void test_forest_asks_the_one_cascade() {
    const TerrainData td = generate_terrain(kSide, kSide, world_params());
    CHECK_OR_RETURN(td.has_rgba_storage(), "мир сгенерирован");

    const std::vector<TreePoint> trees = spawn_trees(td, 12345u);
    CHECK_OR_RETURN(!trees.empty(), "на настоящем мире лес есть");

    long onWater = 0, onMountain = 0, onBarrenClimate = 0, onShore = 0;
    for (const TreePoint& t : trees) {
        switch (biome_at_cell(td, t.x, t.y)) {
        case Water:    ++onWater; break;
        case Mountain: ++onMountain; break;
        case Tundra: case Snow: case Desert: ++onBarrenClimate; break;
        default: break;
        }
        if (touches_water(td, t.x, t.y)) ++onShore;
    }
    // Счёт обязателен (§8 п.3): цикл, который ничего не помёрил, отчитался бы
    // успехом.
    CHECK(long(trees.size()) > 0, "перебраны все лесные клетки");
    CHECK(onWater == 0, "лес не стоит на клетке, которую каскад зовёт водой");
    CHECK(onMountain == 0,
          "лес не стоит на клетке, которую каскад зовёт горой — один порог");
    CHECK(onBarrenClimate == 0,
          "лес не стоит на мерзлоте и песке того же каскада");
    CHECK(onShore == 0, "лес не стоит на клетке, касающейся воды");
    // ПЕРЕПИСЬ, а не утверждение: оба числа двигает свод каскадов (лес теряет
    // спорную полосу и берег, реки получают рим массива краем биома), и здесь
    // они печатаются, чтобы сдвиг был виден числом, а не догадкой.
    long riverCells = 0;
    for (std::size_t c = 0; c < td.cell_count(); ++c)
        if (td.riverData[c]) ++riverCells;
    std::fprintf(stderr,
                 "[biome] мир %d² сид 12345: лесных клеток %zu, речных %ld\n",
                 kSide, trees.size(), riverCells);
}

// ── 2. Спорная полоса 0.75…0.80: ПИН ОДНОГО ПОРОГА ───────────────────────
void test_forest_refuses_the_disputed_band() {
    // Ровная лесная равнина, безопасно выше моря и ниже горной линии.
    TerrainData td = flat_world(kSide, 150u);
    // Плато РОВНО в полосе, где два порога расходились.
    constexpr int kPlateau = 32;
    stamp_plateau(td, 48, 48, kPlateau, kPlateau, std::uint8_t(kDisputedByte));

    const std::vector<TreePoint> trees = spawn_trees(td, 12345u);

    long onPlateau = 0, offPlateau = 0;
    for (const TreePoint& t : trees) {
        const bool inside = t.x >= 48 && t.x < 48 + kPlateau
                         && t.y >= 48 && t.y < 48 + kPlateau;
        if (inside) ++onPlateau; else ++offPlateau;
    }
    // КОНТРОЛЬ НЕ-БЕСПЛОДНОСТИ: без него «ноль на плато» доказывал бы лишь
    // карту, на которой лес не растёт нигде.
    CHECK(offPlateau > 0,
          "контроль: равнина той же карты лес растит (иначе отказ ничего не значит)");
    CHECK(onPlateau == 0,
          "плато 0.769 — гора по ЕДИНСТВЕННОМУ порогу, лес на ней не стоит");
    std::fprintf(stderr,
                 "[biome] плато %d (0.769): лес на плато %ld, вне %ld\n",
                 kDisputedByte, onPlateau, offPlateau);
}

// ── 3. Трассер рек видит Mountain — иначе краёв на суше нет ──────────────
void test_river_tracer_sees_the_massif_rim() {
    const LayerParameters params = world_params();

    // Мир РОВНОГО климата: климатическая матрица даёт ОДИН биом на всю сушу,
    // значит единственный край, который может увидеть климатический каскад, —
    // берег моря. Полный каскад видит второй: рим горного массива.
    //
    // Море обязано быть: исток реки ищется среди клеток ДАЛЬШЕ четырёх шагов от
    // воды, а стока без воды не бывает вовсе — трассер на мире без моря выходит
    // первой же дверью. Полоса океана у края даёт сток и не даёт истоков: все
    // её берега ближе четырёх шагов к воде.
    TerrainData flat = flat_world(kSide, 150u);
    constexpr int kOceanW = 16;
    for (int y = 0; y < kSide; ++y)
        stamp_plateau(flat, 0, y, kOceanW, 1, 50u);
    TerrainData massif = flat;
    // Массив стоит вглубь суши — дальше пятнадцати шагов от воды, иначе русло
    // короче минимальной длины и не ставится.
    constexpr int kMassif = 48;
    stamp_plateau(massif, 56, 40, kMassif, kMassif, 230u);

    generate_river_data(flat, params);
    generate_river_data(massif, params);

    long flatRivers = 0, massifRivers = 0, rimRivers = 0;
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            const std::size_t c = std::size_t(cell_of(x, y, kSide));
            if (flat.riverData[c]) ++flatRivers;
            if (massif.riverData[c]) {
                ++massifRivers;
                // «У рима» — не дальше речного края от границы плато
                // (исток берётся при edgeDist <= 2), то есть кольцо в две
                // клетки по обе стороны рубежа.
                const int dx = x < 56 ? 56 - x : (x >= 56 + kMassif ? x - (56 + kMassif - 1) : 0);
                const int dy = y < 40 ? 40 - y : (y >= 40 + kMassif ? y - (40 + kMassif - 1) : 0);
                const int inX = x >= 56 && x < 56 + kMassif
                                    ? std::min(x - 56, 56 + kMassif - 1 - x) : 999;
                const int inY = y >= 40 && y < 40 + kMassif
                                    ? std::min(y - 40, 40 + kMassif - 1 - y) : 999;
                const int toRim = (dx || dy) ? std::max(dx, dy)
                                             : std::min(inX, inY);
                if (toRim <= 2) ++rimRivers;
            }
        }

    // КОНТРОЛЬ: та же карта БЕЗ массива. Ровный климат + нет моря ⇒ краёв нет
    // ⇒ рекам рождаться не от чего. Если и здесь реки есть, прибор мерит не
    // край биома, а что-то другое.
    CHECK(flatRivers == 0,
          "контроль: у мира ровного климата с одним берегом истоку взяться неоткуда");
    CHECK(massifRivers > 0,
          "рим массива ЕСТЬ край биома для трассера — один каскад, с Mountain");
    CHECK(rimRivers > 0, "реки массива рождаются у его рима");
    std::fprintf(stderr,
                 "[biome] трассер: рек без массива %ld, с массивом %ld (у рима %ld)\n",
                 flatRivers, massifRivers, rimRivers);
}

// ── 4. Зоны судят гору тем же порогом ────────────────────────────────────
void test_zones_ask_the_one_cascade() {
    // Зоны без городов, деревень и фич: остаётся шум + горная надбавка, и шум
    // у двух карт одинаков, потому что зависит от координаты и сида, не от
    // высоты. Значит различие непрерывной величины В ОДНОЙ КЛЕТКЕ = надбавка.
    const std::vector<ZoneSeed> none;
    FeatureLayer features;

    auto danger_at = [&](std::uint8_t heightByte) {
        TerrainData td = flat_world(kSide, 150u);
        stamp_plateau(td, 64, 64, 1, 1, heightByte);
        std::vector<float> cont;
        const ZoneLayer zl = generate_zones(kSide, kSide, 777u, none, none,
                                            features, &td, nullptr, &cont);
        const std::size_t c = std::size_t(cell_of(64, 64, kSide));
        return (zl.has_complete_storage() && c < cont.size()) ? cont[c] : 0.0f;
    };

    const float below = danger_at(std::uint8_t(kMountainByte - 1));
    const float at    = danger_at(std::uint8_t(kMountainByte));
    const float far   = danger_at(std::uint8_t(kMountainByte - 2));

    CHECK(at > below,
          "клетка на горной линии несёт горную надбавку зон — тот же порог, что у каскада");
    CHECK(far == below,
          "контроль: ниже линии высота опасности не двигает (прибор мерит порог, не высоту)");
    std::fprintf(stderr,
                 "[biome] зоны у линии: 190=%.4f 191=%.4f 192=%.4f\n",
                 double(far), double(below), double(at));
}

// ── 5. ПОЛЕ ЕСТЬ ОТВЕТ, А КАСКАД — ЕГО ПРОШЛОЕ ───────────────────────────
// Вердикт владельца 2026-09-28: «при генерации мира можно функции там ргб и
// тд, но когда мир уже сгенерился… там должно всё уже быть структурно
// системно». Свидетель охраняет обе половины: поле СОГЛАСНО с каскадом на
// каждой клетке (иначе у мира снова два ответа) и выпечено ПОСЛЕ вреза рек
// (иначе оно врёт ровно на руслах).
void test_field_is_the_answer() {
    const TerrainData td = generate_terrain(kSide, kSide, world_params());
    CHECK_OR_RETURN(td.has_rgba_storage(), "мир сгенерирован");
    CHECK(td.biome.size() == td.cell_count(),
          "поле биома покрывает мир целиком — рождение довело выпечку до конца");

    long samples = 0, disagree = 0, riverCells = 0, riverNotWater = 0;
    for (std::uint32_t c = 0; c < std::uint32_t(td.cell_count()); ++c) {
        ++samples;
        if (biome_at_cell(td, c) != biome_classify(td, c)) ++disagree;
        if (td.riverData[c]) {
            ++riverCells;
            if (biome_at_cell(td, c) != Biome::Water) ++riverNotWater;
        }
    }
    CHECK(samples == long(kSide) * kSide, "перебраны все клетки мира");
    CHECK(disagree == 0, "поле и каскад согласны на каждой клетке");

    // ПОРЯДОК: врез рек опускает русла ниже плоскости ПОСЛЕ того, как трассер
    // отработал. Поле, выпеченное до вреза, назвало бы русло лугом. Счёт
    // русел обязателен — мир без рек доказал бы этим утверждением ничего.
    CHECK(riverCells > 0, "в мире есть реки — иначе порядок проверять не на чем");
    CHECK(riverNotWater == 0,
          "клетка русла читается водой: поле выпечено ПОСЛЕ вреза");

    // КОНТРОЛЬ ОСТРОТЫ (§8 п.6): прибор обязан УМЕТЬ увидеть расхождение.
    // Без этого «disagree == 0» доказывало бы лишь, что сравнение слепо.
    TerrainData poked = td;
    const std::uint32_t victim = cell_of(kSide / 2, kSide / 2, kSide);
    poked.biome[victim] = std::uint8_t(
        biome_at_cell(poked, victim) == Biome::Water ? Biome::Desert
                                                     : Biome::Water);
    CHECK(biome_at_cell(poked, victim) != biome_classify(poked, victim),
          "контроль: подменённый байт поля ОБЯЗАН разойтись с каскадом");

    std::fprintf(stderr,
                 "[biome] поле %zu байт, русел %ld, расхождений %ld\n",
                 td.biome.size(), riverCells, disagree);
}

} // namespace

int main() {
    test_field_is_the_answer();
    test_forest_asks_the_one_cascade();
    test_forest_refuses_the_disputed_band();
    test_river_tracer_sees_the_massif_rim();
    test_zones_ask_the_one_cascade();
    return sm::test::report("biome_cascade_test");
}

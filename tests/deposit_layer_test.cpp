// ОДНО ПРИРОДНОЕ ПОЛЕ ЖИЛ (macro/deposit_layer.h; вердикт владельца
// 2026-10-22 «жилы единые то есть одно природно поле жил»). Охраняемые
// ЗАКОНЫ, не числа:
//   · СРОДСТВО — ВЕС, НЕ ГЕЙТ: в воде жил нет; всякая жила стоит либо на
//     земле выше средней (металл/камень по высоте⁴), либо у воды (глина по
//     влажной низине) — на этой фикстуре вне реки влажность не перелезает
//     порог, так что дизъюнкция ПОЛНА и падает, если сродство сломано;
//   · ГНЁЗДА — полевой закон кластерит: где-то жила держит две смежные
//     клетки (связность, которую генерация шахт сводит в кап залежи);
//   · ДЕТЕРМИНИЗМ — один сид, одна геология; другой сид — другая;
//   · ЁМКОСТЬ — ЧИСТАЯ ФУНКЦИЯ: vein_virgin_at на клетке равен тому, что
//     build записал в грид (одна функция и рождает, и отвечает);
//   · ДВЕРЬ ГЕНЕЗИСА — create_deposit пишет и двигает ревизию.
//
// (Прибор «quantity door / load path» УМЕР с самими дверьми 2026-10-23:
// природное поле геймплеем не мутируется и в сейв не едет; залежь шахты —
// число фичи worked, её транзакции держит field_doors_test.)
#include "check.h"

#include "macro/deposit_layer.h"

#include <algorithm>
#include <cstdint>

namespace {

using namespace sm;

// A little world: sea on the left edge, a river column at x=8 wetting the
// land, a TALL mountain band at y>=48 (the field law concentrates metal
// where the land is HIGH — height⁴), plain grassland elsewhere.
TerrainData make_world() {
    const int w = 64, h = 64;
    TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * h * 4u, 0);
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t s = std::size_t(y * w + x) * 4u;
            float level01 = 140.0f / 255.0f;             // land (0.549)
            if (x < 4) level01 = 40.0f / 255.0f;         // sea (0.157)
            if (y >= 48) level01 = 250.0f / 255.0f;      // peaks (0.98)
            td.rgba[s + 0] = sm::field_word_of(level01);
            td.rgba[s + 1] = sm::field_word_of(200.0f / 255.0f);
            td.rgba[s + 2] = sm::field_word_of(128.0f / 255.0f);
            td.rgba[s + 3] = level01 < sm::kDefaultSeaLevel
                                 ? std::uint16_t(0)
                                 : std::uint16_t(sm::kFieldWordMax);
            if (x == 8 && y < 48) {
                td.rgba[s + 0] = sm::river_bed_word(td.seaLevel16);
                td.rgba[s + 3] = std::uint16_t(0);
            }
        }
    }
    return td;
}

bool river_adjacent(const TerrainData& td, int x, int y) {
    const std::uint32_t at = cell_of(x, y, td.width);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (td.is_water(cell_step(at, dx, dy, td.width))) return true;
    return false;
}

void test_the_field_obeys_the_world() {
    const TerrainData td = make_world();
    const DepositLayer layer = build_deposit_layer(td, 777u);

    CHECK_OR_RETURN(layer.cells.liveCells > 0, "the little world holds veins");
    CHECK(layer.virginUnits > 0, "the fingerprint counts what was born");

    long long landHeightSum = 0, landCells = 0;
    for (int y = 0; y < td.height; ++y)
        for (int x = 0; x < td.width; ++x)
            if (!td.is_water(x, y)) {
                landHeightSum += td.height_at(x, y);
                ++landCells;
            }
    const long long landMean = landHeightSum / std::max(1LL, landCells);

    bool affinityHolds = true;
    bool nested = false;
    layer.cells.for_each_live([&](std::uint32_t idx, std::int32_t remaining) {
        const int x = layer.cells.x_of(idx);
        const int y = layer.cells.y_of(idx);
        // Вода не несёт ничего; всякая жила — высоко ЛИБО у воды. На этой
        // фикстуре вне реки влажность не перелезает порог глины, поэтому
        // дизъюнкция полна: сломай вес — и жила встанет на ровной сухой
        // земле, уронив ровно эту проверку.
        affinityHolds = affinityHolds && !td.is_water(x, y) && remaining > 0
            && (td.height_at(x, y) > landMean || river_adjacent(td, x, y));
        if (!nested) {
            for (int dy = -1; dy <= 1 && !nested; ++dy)
                for (int dx = -1; dx <= 1 && !nested; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (layer.cells.at(x + dx, y + dy) != 0) nested = true;
                }
        }
    });
    CHECK(affinityHolds,
          "affinity is a WEIGHT, never a gate: water holds nothing, a vein "
          "stands high or by the water");
    CHECK(nested, "the field law grows NESTS — adjacent live veins");

    // ЁМКОСТЬ — ЧИСТАЯ ФУНКЦИЯ: та же дверь, что родила мир, отвечает те же
    // числа по клеткам (и нули там, где грид пуст).
    bool pureMatches = true;
    int sampled = 0;
    for (int y = 0; y < td.height; y += 7) {
        for (int x = 0; x < td.width; x += 7) {
            const std::int32_t derived = vein_virgin_at(td, 777u, x, y);
            pureMatches = pureMatches && derived == layer.vein_at(x, y);
            ++sampled;
        }
    }
    CHECK(sampled > 0 && pureMatches,
          "vein_virgin_at IS the built grid — one function births and answers");

    // One seed, one geology; a different seed shuffles the sites.
    const DepositLayer again = build_deposit_layer(td, 777u);
    CHECK(again.cells == layer.cells,
          "the same seed derives the same geology");
    const DepositLayer other = build_deposit_layer(td, 778u);
    CHECK(!(other.cells == layer.cells),
          "a different seed is a different geology");
}

void test_the_genesis_door() {
    DepositLayer layer;
    allocate_deposit_fields(layer, 64, 64);
    const std::uint32_t rev0 = layer.revision;
    create_deposit(layer, 10, 10, 1234);
    CHECK(layer.vein_at(10, 10) == 1234, "genesis writes the cell");
    CHECK(layer.cells.liveCells == 1, "the live count rides the write");
    CHECK(layer.revision > rev0, "the mutation moved the revision");
    create_deposit(layer, 11, 10, 0);
    CHECK(layer.vein_at(11, 10) == 0,
          "genesis of an empty vein is refused — zero is absence");
}

} // namespace

int main() {
    test_the_field_obeys_the_world();
    test_the_genesis_door();
    return sm::test::report("deposit_layer_test");
}

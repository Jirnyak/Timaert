// The ONE growth/diffusion law of the resource fields (owner, R2 track):
// every field is BORN from time and context through the same walker —
// resource_fields_daily_growth — and the nuances are rows, never systems.
//
// Properties pinned (never restated numbers):
//   · TREES — the forest plants the forest: a clear-cut cell inside a
//     living massif regrows to forest class within the owner's ~4-year
//     invariant; an isolated brush cell never seeds a forest; the desert
//     gate starves growth a meadow would get; the cap holds.
//   · FAUNA — beasts breed where beasts are: a hunted cell in a living
//     valley heals at the old 1/epoch rate; a valley emptied WHOLE stays
//     empty (nobody left to breed) — that asymmetry IS the law, and it is
//     what a flat heal-to-baseline could never say (the built-in negative
//     control: a flat law would regrow both).
//   · IRON — born where scarce: a virgin world strikes nothing, a
//     mined-out world strikes within the epoch horizon, the vein lands on
//     a stone host WITHOUT deleting it, and the strike is a pure function
//     of the calendar (same seed+day = same vein).
#include "check.h"

#include "macro/deposit_layer.h"
#include "macro/macro_stock.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kW = 64, kH = 64;

// Dry meadow land everywhere; a mountain band at y>=40 for geology (wide
// enough that the 1/256 iron roll reliably seeds a few veins).
// САМЫЙ ВЫСОКИЙ УРОВЕНЬ, КОТОРЫЙ ЕЩЁ НЕ ГОРА — вывод из `kMountainBiomeLevel`,
// а не литерал. Здесь стояло 180 (0.706): под линией 0.75 это была высокая
// суша, а когда линия переехала на 0.625 (бескламповый синтез, 2026-10-01),
// та же фикстура молча стала ГОРОЙ целиком и унесла с собой то, что тест
// проверяет. Выведенный уровень не может разойтись с линией по построению.
// Величина — УРОВЕНЬ, а не байт: карта перешла на слово (`kFieldWordMax`), и
// байтовый литерал означал бы в ней 1/257 своей прежней высоты, молча.
// Шаг вниз — ровно одно СЛОВО карты: горой `biome_classify` зовёт всё `>=`
// линии, значит ближайший незанятый уровень лежит на `1/kFieldWordMax` ниже.
constexpr float kTallLand01 = sm::kMountainBiomeLevel - 1.0f / sm::kFieldWordMax;
// ПИК — заведомо выше горной линии; высота, с которой геология ещё и считает
// металл (вес ~height⁴). Прежний байт 250 означал этот же уровень.
constexpr float kPeak01 = 250.0f / 255.0f;

TerrainData make_terrain() {
    TerrainData t;
    t.width = kW;
    t.height = kH;
    t.rgba.assign(std::size_t(kW) * kH * 4u, sm::field_word_of(128.0f / 255.0f));
    t.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);  // плоскость — у карты
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const std::size_t s = std::size_t(y * kW + x) * 4u;
            // Peaks, not foothills (v72): the field law of geology weights
            // metal by height⁴, so the fixture band must be honestly TALL
            // for its veins to crest.
            t.rgba[s + 0] = sm::field_word_of(y >= 40 ? kPeak01 : kTallLand01);
                                                   // пики / высокая суша
            t.rgba[s + 3] = std::uint16_t(sm::kFieldWordMax);   // land mask
        }
    }
    // РОЖДЕНИЕ КАРТЫ КОНЧАЕТСЯ ВЫПЕЧКОЙ ПОЛЯ БИОМА (ЗАКОН ПОЛЯ): живой мир
    // читает поле, а не каскад, поэтому карта без выпечки — карта НЕДОРОЖДЁННАЯ,
    // и её биом честно отвечает водой.
    sm::bake_biomes(t);
    return t;
}

// A desert stripe at x < 8 (hot + dry climate channels), meadow elsewhere.
void paint_desert_stripe(TerrainData& t) {
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::size_t s = std::size_t(y * kW + x) * 4u;
            t.rgba[s + 1] = sm::field_word_of(10.0f / 255.0f);    // bone dry
            t.rgba[s + 2] = sm::field_word_of(240.0f / 255.0f);   // scorching
        }
    }
    // КЛИМАТ — ИСТОЧНИК БИОМА, значит правка мастера кончается перепечкой поля
    // (ЗАКОН ПОЛЯ): пустыня, нарисованная в канале и не дошедшая до поля, была
    // бы пустыней лишь для того, кто пересчитывает каскад сам.
    bake_biomes(t);
}

TreeLayer flat_trees(std::uint16_t count) {
    TreeLayer trees;
    trees.width = kW;
    trees.height = kH;
    trees.data.assign(std::size_t(kW) * kH, count);
    return trees;
}

// Run the daily walker over `days` consecutive game days (1-based).
void run_days(MacroWorld& w, int days) {
    for (int day = 1; day <= days; ++day) {
        resource_fields_daily_growth(w, day);
    }
}

void test_forest_plants_the_forest() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    const TerrainData td = make_terrain();

    // A living massif block with one clear-cut cell inside it.
    TreeLayer trees = flat_trees(0);
    for (int y = 4; y <= 8; ++y)
        for (int x = 4; x <= 8; ++x)
            set_tree_count(trees, x, y, kMaxTreesPerCell);
    set_tree_count(trees, 6, 6, 0);
    // An isolated brush cell far from any forest: below the seeding
    // threshold, surrounded by bare ground.
    set_tree_count(trees, 20, 6, 100);
    MacroWorld w{.gs = &gs, .trees = &trees, .terrain = &td};

    // The owner's invariant: a clear-cut inside a living massif returns to
    // forest class in ~4 game years. Allow the loose window [2, 6] years —
    // the property is the timescale, not a pinned constant.
    run_days(w, 2 * 512);
    const bool by2y = is_forest_cell(int(trees.at(6, 6)));
    run_days(w, 4 * 512);   // total 6 years
    CHECK(is_forest_cell(int(trees.at(6, 6))),
          "a clear-cut inside a living massif is forest again within ~6y");
    CHECK(int(trees.at(6, 6)) >= 8192 || !by2y,
          "and it was NOT instant - growth is a timescale, not a snap");
    CHECK(int(trees.at(6, 6)) <= kMaxTreesPerCell
              && int(trees.at(4, 4)) <= kMaxTreesPerCell,
          "the golden cap holds under years of growth");

    CHECK(int(trees.at(20, 6)) == 100,
          "an isolated brush cell below the seeding threshold never grows");
    // The massif SPREADS: its outer neighbours started at 0 and now stand.
    CHECK(int(trees.at(3, 6)) > 0,
          "the forest edge seeds the bare neighbour cell (diffusion)");
    CHECK(gs.resourceScarCells[std::size_t(ResourceFieldId::Trees)].liveCells == 0,
          "years of growth left no scars - the grid is the only state");
}

void test_biome_gate_starves_the_desert() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    TerrainData td = make_terrain();
    paint_desert_stripe(td);

    // Two identical forest blocks: one on the desert stripe, one on meadow.
    TreeLayer trees = flat_trees(0);
    for (int y = 4; y <= 6; ++y) {
        for (int x = 2; x <= 4; ++x)
            set_tree_count(trees, x, y, 12000);         // desert block
        for (int x = 20; x <= 22; ++x)
            set_tree_count(trees, x, y, 12000);         // meadow block
    }
    const int desertBefore = int(trees.at(3, 5));
    const int meadowBefore = int(trees.at(21, 5));
    MacroWorld w{.gs = &gs, .trees = &trees, .terrain = &td};
    run_days(w, 512);

    const int desertGrown = int(trees.at(3, 5)) - desertBefore;
    const int meadowGrown = int(trees.at(21, 5)) - meadowBefore;
    CHECK(meadowGrown > 0, "the meadow block thickens (чащоба in the making)");
    CHECK(desertGrown * 4 < meadowGrown,
          "the desert gate starves growth the meadow gets freely");
}

void test_beasts_breed_where_beasts_are() {
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    const TerrainData td = make_terrain();
    MacroWorld w{.gs = &gs, .terrain = &td};

    const int cap = resource_field_read(w, ResourceFieldId::Fauna, 10, 10);
    CHECK_OR_RETURN(cap >= 2, "the meadow carries beasts to hunt");

    // One hunted cell in a LIVING valley: heals at one head per epoch.
    field_move(w, ResourceFieldId::Fauna, 10, 10, -2);
    // A 5×5 hole shot dead around (20,20): it must repopulate from the
    // OUTSIDE in. The hole's corner (18,18) sees 5 of its 9 neighbours
    // alive — over the half-alive threshold — so it breeds; the centre
    // sees nobody and stays bare. A flat heal-to-baseline law would
    // regrow both at once.
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            field_move(w, ResourceFieldId::Fauna,
                                 20 + dx, 20 + dy, -999);

    run_days(w, 2 * kGrowthEpochDays);

    CHECK(resource_field_read(w, ResourceFieldId::Fauna, 10, 10) == cap,
          "a hunted cell in a living valley healed at one head per epoch");
    CHECK(resource_field_read(w, ResourceFieldId::Fauna, 18, 18) > 0,
          "the hole's corner repopulates first - life walks in from the "
          "living side");
    CHECK(resource_field_read(w, ResourceFieldId::Fauna, 20, 20) == 0,
          "the hole's centre is still bare after two epochs - the wave "
          "has not reached it (a flat law would have)");
}

void test_the_vein_field_is_inert_and_a_mine_does_not_regrow() {
    // ЖИЛЫ ЕДИНЫЕ (вердикты 2026-10-22/23): дикая жила — чистая геология,
    // добыча существует ПО ШАХТАМ, залежь шахты — одноразовый ком кластера.
    // (ЗДЕСЬ СТОЯЛ СВИДЕТЕЛЬ ЗАЖИВЛЕНИЯ ЖИЛЫ — закон 2026-09-22 снят этими
    // вердиктами: природное поле не убывает вовсе, ему нечего заживлять, а
    // восстановление залежи шахты владелец не продиктовал. Свидетель
    // ПЕРЕПИСАН под новый закон, а не подогнан ожиданием под новый ответ.)
    GameState gs{};
    gs.mapW = kW;
    gs.mapH = kH;
    gs.worldSeed = 12345u;
    const TerrainData td = make_terrain();
    DepositLayer deposits = build_deposit_layer(td, gs.worldSeed);
    CHECK_OR_RETURN(deposits.cells.liveCells > 0,
                    "the fixture world holds veins");
    FeatureLayer features;
    features.resize(kW, kH);
    MacroWorld w{.gs = &gs, .terrain = &td,
                 .deposits = &deposits, .features = &features};

    // ДИКАЯ ЖИЛА ИНЕРТНА: выемка без шахты невыразима, календарь её не
    // трогает — поле тождественно рождённому.
    const std::uint32_t firstIdx = deposits.cells.first_live();
    const int vx = deposits.cells.x_of(firstIdx);
    const int vy = deposits.cells.y_of(firstIdx);
    const int wild = int(deposits.cells.at_index(firstIdx));
    CHECK(field_move(w, ResourceFieldId::Vein, vx, vy, -5) == 0,
          "добыча существует по шахтам: дикую жилу выемка не берёт");
    CHECK(int(deposits.cells.at_index(firstIdx)) == wild,
          "и клетка дикой жилы не шелохнулась");
    const DepositLayer born = build_deposit_layer(td, gs.worldSeed);
    run_days(w, 256);
    CHECK(deposits.cells == born.cells,
          "256 дней календаря не сдвинули природное поле ни на юнит");

    // ЗАЛЕЖЬ ШАХТЫ: двигается дверью, вниз до нуля, и НЕ отрастает — ком
    // кластера одноразов («сделаем хоть так потом придумаем лучше»).
    feature_put(features, td, vx, vy, FT_IronMine);
    worked_write(gs, vx, vy, 12);
    CHECK(field_move(w, ResourceFieldId::Vein, vx, vy, -5) == -5,
          "залежь шахты движется дверью и отвечает фактом");
    CHECK(resource_field_read(w, ResourceFieldId::Vein, vx, vy) == 7,
          "остаток залежи — число фичи");
    run_days(w, 256);
    CHECK(resource_field_read(w, ResourceFieldId::Vein, vx, vy) == 7,
          "залежь шахты не отрастает: ряд жилы без закона роста");

    // EXTINCTION is the honest end of the breeding law: a world whose
    // fauna is gone EVERYWHERE has nobody left to breed, anywhere, ever.
    {
        GameState gsx{};
        gsx.mapW = kW;
        gsx.mapH = kH;
        MacroWorld wx{.gs = &gsx, .terrain = &td};
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                field_move(wx, ResourceFieldId::Fauna, x, y, -999);
        run_days(wx, 4 * kGrowthEpochDays);
        int total = 0;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                total += resource_field_read(wx, ResourceFieldId::Fauna, x, y);
        CHECK(total == 0,
              "a world emptied WHOLE stays extinct - nobody is left to "
              "breed (a heal-to-baseline law could never say this)");
    }

    // (Прибор «детерминизм заживления» умер с законом роста жилы; детерминизм
    // САМОГО поля — один сид, одна геология — держит deposit_layer_test.)
}

} // namespace

int main() {
    test_forest_plants_the_forest();
    test_biome_gate_starves_the_desert();
    test_beasts_breed_where_beasts_are();
    test_the_vein_field_is_inert_and_a_mine_does_not_regrow();
    return sm::test::report("resource_growth_test");
}

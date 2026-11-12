// ПОДБОР КЛЕТКИ ПОД СТРУКТУРУ — свидетель двери `pick_site@src/macro/site_pick.h`
// (M-248). Требование владельца 2026-11-10: модуль структуры должно быть
// возможно «системно поставить в любую точку мира» — «просто положить по
// индексу клетки».
//
// ЧТО ДОКАЗЫВАЕТСЯ:
//   1. ДВЕРЬ НЕ СДВИНУЛА МИРЫ СИДОВ. Розыгрыш по всему миру бит в бит равен
//      частному циклу, который стоял в генераторах руин и шпилей: та же
//      клетка И то же состояние потока после. Цикл-эталон выписан здесь один
//      раз как некролог снесённого кода — это свидетель ПЕРЕЕЗДА, а не вторая
//      копия закона: закон «лучшая из N» ниже утверждается ещё и свойством (4).
//   2. «РЯДОМ С X» — ТА ЖЕ АРИФМЕТИКА С ДРУГИМИ ЧИСЛАМИ: каждая названная
//      клетка лежит в радиусе от якоря, в том числе через шов тора.
//   3. Отбор отверг всё — ответ `kNoSite`, и поток ушёл ровно на 2·N.
//   4. РАЗБРОС: названная клетка — дальняя от `apart` среди разыгранных.
//   5. ОБЩИЙ ОТБОР: вода и клетка под фичей с родом места закрыты, дорога и
//      голая суша открыты.
//   6. СКВОЗНОЙ ПУТЬ ТРЕБОВАНИЯ: назвать клетку рядом с городом → положить
//      байт дверью `feature_put` → байт стоит, счётчик правок сдвинулся на
//      единицу, и клетка больше не предлагается.
#include "check.h"
#include "macro/site_pick.h"

#include "core/rng.h"
#include "core/torus.h"
#include "macro/features.h"
#include "macro/map_generator.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

using namespace sm;

namespace {

constexpr int kSide = 64;   // ЗАКОН АДРЕСА: квадрат, степень двойки

// Рельеф: суша везде, кроме полосы воды x ∈ [20, 28) — чтобы отбор воды
// реально срабатывал. Уровень авторится дверью слова поля, как у мира.
TerrainData make_terrain() {
    TerrainData td;
    td.width = kSide;
    td.height = kSide;
    td.rgba.assign(std::size_t(kSide) * kSide * 4u, 0);
    td.seaLevel16 = field_word_of(kDefaultSeaLevel);
    const std::uint16_t land = field_word_of(0.7f);
    const std::uint16_t sea = field_word_of(0.1f);
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const std::size_t s = (std::size_t(y) * kSide + x) * 4u;
            const bool water = x >= 20 && x < 28;
            td.rgba[s + 0] = water ? sea : land;
            td.rgba[s + 3] = water ? std::uint16_t(0)
                                   : std::uint16_t(kFieldWordMax);
        }
    }
    return td;
}

// НЕКРОЛОГ: частный цикл генераторов, как он стоял до двери (руины и шпили
// несли по своей копии). Нужен РОВНО для сверки переезда бит в бит.
template <typename Admit>
std::uint32_t legacy_world_pick(Rng& rng, int side, int tries,
                                const std::vector<std::uint32_t>& placed,
                                Admit&& admit) {
    int bestX = -1, bestY = -1, bestScore = -1;
    for (int t = 0; t < tries; ++t) {
        const int x = int(rng.next_u32() % std::uint32_t(side));
        const int y = int(rng.next_u32() % std::uint32_t(side));
        if (!admit(x, y)) continue;
        int score = side + side;
        for (const std::uint32_t c : placed)
            score = std::min(score, torus_cheb_cells(
                x, y, cell_x(c, side), cell_y(c, side), side));
        if (score > bestScore) { bestScore = score; bestX = x; bestY = y; }
    }
    return bestScore < 0 ? kNoSite : cell_of(bestX, bestY, side);
}

// ── 1. Переезд бит в бит ────────────────────────────────────────────────────
void test_world_pick_matches_the_loop_it_replaced() {
    const TerrainData td = make_terrain();
    const std::unique_ptr<FeatureLayer> fl{new FeatureLayer()};
    fl->resize(kSide, kSide);
    int runs = 0, cellMismatch = 0, streamMismatch = 0, found = 0;
    for (std::uint32_t seed : {1u, 7u, 999u, 12345u, 0xD00DCAFEu}) {
        Rng a(seed), b(seed);
        std::vector<std::uint32_t> placedA, placedB;
        // Двадцать структур подряд одним потоком — как проход генератора:
        // каждая следующая мерит разнос от всех предыдущих.
        for (int n = 0; n < 20; ++n) {
            const auto admitXY = [&](int x, int y) {
                return !td.is_water(cell_of(x, y, td.width)) && ((x + y) % 3) != 0;
            };
            const std::uint32_t want =
                legacy_world_pick(a, kSide, 16, placedA, admitXY);
            const std::uint32_t got = pick_site(
                b, kSide, site_area_world(kSide), 16, placedB,
                [&](std::uint32_t cell) {
                    return admitXY(cell_x(cell, kSide), cell_y(cell, kSide));
                });
            ++runs;
            if (want != got) ++cellMismatch;
            if (a.state != b.state) ++streamMismatch;
            if (want != kNoSite) { placedA.push_back(want); ++found; }
            if (got != kNoSite) placedB.push_back(got);
        }
    }
    CHECK(runs == 100 && found > 50,
          "the sweep ran and actually placed structures");
    CHECK(cellMismatch == 0,
          "the door names the SAME cell the private generator loop named");
    CHECK(streamMismatch == 0,
          "…and leaves the random stream in the SAME state — seeded worlds "
          "did not move");
}

// ── 2. «Рядом с X» ──────────────────────────────────────────────────────────
void test_near_pick_stays_within_radius_across_the_seam() {
    struct Case { int ax, ay, radius; };
    const Case cases[] = {
        {32, 32, 3}, {0, 0, 4}, {kSide - 1, kSide - 1, 5}, {0, 40, 2},
        {10, 0, 0},
    };
    int picks = 0, outside = 0, seamCrossed = 0;
    for (const Case& k : cases) {
        const std::uint32_t anchor = cell_of(k.ax, k.ay, kSide);
        Rng rng(0xA11CEu + std::uint32_t(k.ax * 131 + k.ay));
        for (int n = 0; n < 200; ++n) {
            const std::uint32_t c = pick_site(
                rng, kSide, site_area_near(anchor, k.radius, kSide), 1, {},
                [](std::uint32_t) { return true; });
            if (c == kNoSite) continue;
            ++picks;
            const int d = torus_cheb_cells(cell_x(c, kSide), cell_y(c, kSide),
                                           k.ax, k.ay, kSide);
            if (d > k.radius) ++outside;
            // Клетка по ту сторону шва: координата больше якоря более чем на
            // радиус — значит до неё дошли заворотом.
            if (std::abs(cell_x(c, kSide) - k.ax) > k.radius
                || std::abs(cell_y(c, kSide) - k.ay) > k.radius) {
                ++seamCrossed;
            }
        }
    }
    CHECK(picks == 1000 && outside == 0,
          "every cell named near an anchor lies within its radius");
    CHECK(seamCrossed > 0,
          "…including cells reached across the torus seam — the sample "
          "really crossed it");

    // Радиус ноль — сама клетка якоря.
    Rng r0(5u);
    const std::uint32_t self = cell_of(10, 0, kSide);
    CHECK(pick_site(r0, kSide, site_area_near(self, 0, kSide), 4, {},
                    [](std::uint32_t) { return true; }) == self,
          "radius 0 is the anchor cell itself");

    // Квадрат шире мира есть весь мир ровно один раз: ни одна клетка не
    // разыгрывается дважды чаще соседней из-за заворота.
    const SiteArea whole = site_area_near(cell_of(3, 3, kSide), kSide, kSide);
    const SiteArea half = site_area_near(cell_of(3, 3, kSide), kSide / 2, kSide);
    CHECK(whole.spanX == kSide && whole.spanY == kSide
              && half.spanX == kSide && half.spanY == kSide,
          "an area wider than the world is the world once, not wrapped twice");
    const SiteArea small = site_area_near(cell_of(3, 3, kSide), 5, kSide);
    CHECK(small.spanX == 11 && small.spanY == 11 && small.x0 == -2
              && small.y0 == -2,
          "radius r is the (2r+1)-square centred on the anchor");
}

// ── 3. Нет клетки — нет ответа, поток ушёл на 2·N ───────────────────────────
void test_no_admissible_cell_is_said_not_invented() {
    Rng a(42u), b(42u);
    const std::uint32_t c = pick_site(a, kSide, site_area_world(kSide), 32, {},
                                      [](std::uint32_t) { return false; });
    for (int i = 0; i < 64; ++i) (void)b.next_u32();
    CHECK(c == kNoSite, "an area with no admissible cell answers kNoSite");
    CHECK(a.state == b.state,
          "the stream advanced by exactly two draws per try, whatever the "
          "verdicts were");

    // Незаконный мир и пустая область — отказ БЕЗ розыгрыша.
    Rng z(9u);
    const std::uint32_t before = z.state;
    const auto yes = [](std::uint32_t) { return true; };
    CHECK(pick_site(z, 48, site_area_world(48), 8, {}, yes) == kNoSite
              && pick_site(z, kSide, SiteArea{0, 0, 0, 5}, 8, {}, yes) == kNoSite
              && pick_site(z, kSide, site_area_world(kSide), 0, {}, yes) == kNoSite
              && z.state == before,
          "a non-power-of-two world, an empty area and zero tries refuse "
          "without touching the stream");
}

// ── 4. Разброс: дальняя от поставленных ─────────────────────────────────────
void test_spread_takes_the_farthest_candidate() {
    const std::vector<std::uint32_t> apart = {
        cell_of(8, 8, kSide), cell_of(40, 50, kSide), cell_of(60, 4, kSide)};
    auto min_dist = [&](std::uint32_t cell) {
        int d = kSide + kSide;
        for (const std::uint32_t c : apart)
            d = std::min(d, torus_cheb_cells(cell_x(cell, kSide),
                                             cell_y(cell, kSide),
                                             cell_x(c, kSide),
                                             cell_y(c, kSide), kSide));
        return d;
    };
    int runs = 0, notFarthest = 0, onApart = 0;
    for (std::uint32_t seed = 1; seed <= 50; ++seed) {
        Rng rng(seed), replay(seed);
        const std::uint32_t got = pick_site(
            rng, kSide, site_area_world(kSide), 24, apart,
            [](std::uint32_t) { return true; });
        // Те же 24 клетки тем же потоком — и их лучший разнос.
        int best = -1;
        for (int t = 0; t < 24; ++t) {
            const int x = int(replay.next_u32() % std::uint32_t(kSide));
            const int y = int(replay.next_u32() % std::uint32_t(kSide));
            best = std::max(best, min_dist(cell_of(x, y, kSide)));
        }
        ++runs;
        if (got == kNoSite || min_dist(got) != best) ++notFarthest;
        if (got != kNoSite && min_dist(got) == 0) ++onApart;
    }
    CHECK(runs == 50 && notFarthest == 0,
          "among the drawn candidates the door names one farthest from the "
          "cells it was told to keep apart from");
    CHECK(onApart == 0, "…and never a cell already taken by one of them");
}

// ── 5 + 6. Общий отбор и сквозной путь «назвать клетку → положить байт» ─────
void test_structure_is_put_by_cell_index_near_a_city() {
    const TerrainData td = make_terrain();
    const std::unique_ptr<FeatureLayer> flOwn{new FeatureLayer()};
    FeatureLayer& fl = *flOwn;
    fl.resize(kSide, kSide);

    const std::uint32_t city = cell_of(40, 30, kSide);
    const std::uint32_t road = cell_of(41, 30, kSide);
    const std::uint32_t water = cell_of(22, 30, kSide);
    const std::uint32_t bare = cell_of(45, 33, kSide);
    CHECK_OR_RETURN(feature_put(fl, td, city, FT_City) == FT_City
                        && feature_put(fl, td, road, FT_Road) == FT_Road,
                    "fixture: the city and its road stand");
    CHECK(!site_open_for_structure(fl, td, city),
          "a cell under a feature that has a place kind is taken");
    CHECK(!site_open_for_structure(fl, td, water),
          "water is closed to structures");
    CHECK(site_open_for_structure(fl, td, road)
              && site_open_for_structure(fl, td, bare),
          "a road cell and bare land are open");

    // Двадцать руин рядом с городом, в радиусе 4: каждая встаёт по индексу
    // клетки, держится в радиусе, не садится на город, воду и друг на друга.
    Rng rng(0x51735EEDu);
    std::vector<std::uint32_t> placed;
    int outside = 0, refused = 0;
    for (int n = 0; n < 20; ++n) {
        const std::uint32_t revBefore = fl.revision;
        const std::uint32_t site = pick_site(
            rng, kSide, site_area_near(city, 4, kSide), 32, placed,
            [&](std::uint32_t cell) {
                return site_open_for_structure(fl, td, cell);
            });
        if (site == kNoSite) { ++refused; continue; }
        if (torus_cheb_cells(cell_x(site, kSide), cell_y(site, kSide), 40, 30,
                             kSide) > 4) {
            ++outside;
        }
        const bool stood = feature_put(fl, td, site, FT_Ruin) == FT_Ruin;
        CHECK(stood && fl.at(site) == FT_Ruin
                  && fl.revision == revBefore + 1u,
              "the structure stands on the named cell and the layer's "
              "revision moved by exactly one");
        placed.push_back(site);
    }
    std::vector<std::uint32_t> sorted = placed;
    std::sort(sorted.begin(), sorted.end());
    const bool distinct =
        std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
    CHECK(placed.size() >= 15 && refused + int(placed.size()) == 20,
          "the neighbourhood of the city took the structures it was asked to");
    CHECK(outside == 0 && distinct && fl.at(city) == FT_City,
          "every structure lies within the radius, no two share a cell, and "
          "the city itself was not overwritten");
}

} // namespace

int main() {
    test_world_pick_matches_the_loop_it_replaced();
    test_near_pick_stays_within_radius_across_the_seam();
    test_no_admissible_cell_is_said_not_invented();
    test_spread_takes_the_farthest_candidate();
    test_structure_is_put_by_cell_index_near_a_city();
    return sm::test::report("site_pick_test");
}

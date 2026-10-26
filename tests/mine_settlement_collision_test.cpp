// ШАХТА НЕ ВСТАЁТ НА КЛЕТКУ МЕСТА — СВИДЕТЕЛЬ ЗАКОНА, А НЕ СЕГОДНЯШНЕГО
// ПОВЕДЕНИЯ (наряд M-321, вердикт владельца 2026-10-26).
//
// ОХРАНЯЕМЫЙ ЗАКОН НАЗВАН СЛОВАМИ ВЛАДЕЛЬЦА — ЗАКОН ПОЛЯ п.3: «ЧИСЛО В ПАШНЕ
// ЧИСЛО В ГОРОДЕ ЧИСЛО В ШАХТЕ ЭТО И БУДЕТ ВСЁ ТАКИ ЕДИНОЕ ПЛОСКОЕ ПОЛЕ НАД
// МИРОМ ДЛЯ ЧИСЕЛ В ФИЧАХ», и оттуда же: «Клетка разрабатывается ровно одним
// способом, поэтому второго числа там быть не может». Слой фич — ОДИН БАЙТ НА
// КЛЕТКУ, поле чисел фич — ОДНО число на клетку; значит шахта и поселение на
// одной клетке есть не «спорная клетка», а ДВА ЗАКОНА НАД ОДНИМ ЧИСЛОМ.
//
// ЧЕМ ЭТО БЫЛО КУПЛЕНО, ЧИСЛОМ. `stamp_mines` штампуется ПОСЛЕ поселений и
// ставила шахту на пик кластера жил БЕЗ вопроса о занятости — `feature_put`
// судит только воду. Перепись трёх миров (сиды 12345 / 7 / 2026) нашла
// **20 / 45 / 36** съеденных поселений, и байт там всегда шахтный (ClayPit,
// IronMine, Quarry, CopperMine). Хуже байта было ЧИСЛО: следом шёл
// `worked_write(..., vein_cluster_cap(...))`, а `souls_flock` у поселения
// читает ровно `worked`, поэтому паства деревень 128 превращалась в
// 25 000-32 000, а у города упиралась в потолок ширины клетки 65 535. И
// дальше `numberRow` шахты = `Vein` делал клетку шахтной для
// `vein_apply@src/macro/macro_stock.cpp` — то есть артель, копавшая там
// глину, ВЫЧИТАЛА жителей как руду. Полный разбор — `bugs.md` Л7.
//
// ПОЧЕМУ ФИКСТУРА, А НЕ НАСТОЯЩИЙ МИР. Перепись мира стоит ~20 с на сид —
// это ЗАМЕР, и он уже сделан (его числа выше). Свидетель обязан быть дешёвым
// и ДЕТЕРМИНИРОВАННЫМ, поэтому он САМ РОЖАЕТ СВОЁ ПРЕДУСЛОВИЕ (§8 п.11):
// кластер жил кладётся руками так, что его максимум лежит РОВНО под деревней.
// Надеяться, что генератор выдаст такую клетку, было бы дефектом ревью —
// свидетель краснел бы от невезения мира, а не от нарушения закона.

#include "check.h"

#include "core/torus.h"
#include "macro/deposit_layer.h"
#include "macro/features.h"
#include "macro/place_registry.h"
#include "macro/spawners.h"
#include "macro/state.h"
#include "tables/feature_type.h"

#include <cstdint>

namespace {

constexpr int kSide = 64;            // степень двойки — ЗАКОН АДРЕСА
constexpr int kTownX = 20, kTownY = 20;
constexpr int kPeakX = 21, kPeakY = 20;   // сосед деревни, он же максимум жилы
constexpr std::uint32_t kSeed = 4242u;
constexpr int kVillageFlock = 128;        // та самая здоровая паства замера

// Суша везде: вода отказала бы шахте по своей колонке, и отказ прочёлся бы
// как «закон держится», хотя держала бы его вода (ложный зелёный).
sm::TerrainData make_land(int side) {
    sm::TerrainData td;
    td.width = side;
    td.height = side;
    td.rgba.assign(std::size_t(side) * std::size_t(side) * 4u, 0);
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    const std::uint16_t high = sm::field_word_of(0.75f);
    const std::uint16_t mid = sm::field_word_of(0.5f);
    for (int i = 0; i < side * side; ++i) {
        const std::size_t s = std::size_t(i) * 4u;
        td.rgba[s + 0] = high;
        td.rgba[s + 1] = mid;
        td.rgba[s + 2] = mid;
        td.rgba[s + 3] = std::uint16_t(sm::kFieldWordMax);   // маска суши
    }
    return td;
}

// Мир фикстуры: деревня на своей клетке с паствой числом фичи, и связный
// кластер жил, чей МАКСИМУМ лежит на клетке деревни.
struct Fixture {
    sm::GameState gs;
    sm::TerrainData td;
    sm::FeatureLayer fl;
    sm::DepositLayer deposits;
};

void build(Fixture& f, bool veinPeakUnderTown) {
    f.td = make_land(kSide);
    f.gs.mapW = kSide;
    f.gs.mapH = kSide;
    f.fl.resize(kSide, kSide);
    sm::allocate_deposit_fields(f.deposits, kSide, kSide);

    // Деревня: байт фичи + ПАСТВА числом той же клетки (souls_flock читает
    // worked при bornPopBase == 0 — это и есть носитель, который ломался).
    sm::feature_put(f.fl, f.td, kTownX, kTownY, sm::FT_Village);
    sm::worked_write(f.gs, kTownX, kTownY, kVillageFlock);

    // Кластер из трёх связных клеток: деревня, её сосед и ещё один сосед.
    // Веса подобраны так, что САМАЯ ЖИРНАЯ клетка — под деревней, то есть
    // прежний выбор пика неминуемо целился бы в неё.
    sm::create_deposit(f.deposits, kTownX, kTownY,
                       veinPeakUnderTown ? 9000 : 10);
    sm::create_deposit(f.deposits, kPeakX, kPeakY, 500);
    sm::create_deposit(f.deposits, kPeakX + 1, kPeakY, 100);
}

void test_mine_does_not_eat_the_village() {
    Fixture f;
    build(f, /*veinPeakUnderTown=*/true);
    sm::stamp_mines(f.gs, f.fl, f.td, f.deposits, kSeed);

    // ГЛАВНОЕ: байт деревни цел. Утверждается ОТНОШЕНИЕ («клетка осталась
    // местом»), а не конкретный байт шахты — какой род шахты выпал, решает
    // сеяный вес, и прибивать его литералом значило бы охранять случай.
    const sm::FeatureType onTown = f.fl.at(kTownX, kTownY);
    CHECK(onTown == sm::FT_Village,
          "клетка деревни обязана остаться деревней — шахта не разрабатывает "
          "уже разрабатываемую клетку (ЗАКОН ПОЛЯ п.3)");
    CHECK(sm::place_kind_of_feature(onTown) == sm::SquadType::Village,
          "обратная дверь обязана по-прежнему узнавать в клетке деревню");

    // И ЧИСЛО — вторая половина закона, и она ВАЖНЕЕ байта: именно его
    // перезапись превращала паству 128 в десятки тысяч.
    CHECK(sm::worked_read(f.gs, kTownX, kTownY) == kVillageFlock,
          "паства деревни обязана остаться нетронутой — число фичи у неё "
          "значит души, а не залежь");

    // ШАХТА ВСЁ-ТАКИ ВСТАЛА, И НЕ ТАМ: вердикт владельца именно «пик уезжает
    // на свободную клетку», а не «кластер теряет шахту». Без этой проверки
    // закон был бы зелен и у правки, которая просто перестала ставить шахты.
    const sm::FeatureType onPeak = f.fl.at(kPeakX, kPeakY);
    CHECK(sm::feature_def(onPeak).numberRow == sm::ResourceFieldId::Vein,
          "шахта обязана встать на свободный максимум кластера, а не пропасть");
    CHECK(sm::worked_read(f.gs, kPeakX, kPeakY) > 0,
          "у переехавшей шахты обязана быть своя залежь числом её клетки");
}

// НЕГАТИВНЫЙ КОНТРОЛЬ ПОЛЯРНОСТИ «ОБЯЗАНО МОЛЧАТЬ»: детектор, краснеющий на
// всё, не детектор (§8 п.6). Тот же кластер, но его максимум НЕ под
// деревней — шахта обязана встать ровно туда, куда и раньше, и закон не
// должен мешать нормальному случаю.
void test_free_peak_is_untouched() {
    Fixture f;
    build(f, /*veinPeakUnderTown=*/false);
    sm::stamp_mines(f.gs, f.fl, f.td, f.deposits, kSeed);

    CHECK(sm::feature_def(f.fl.at(kPeakX, kPeakY)).numberRow
              == sm::ResourceFieldId::Vein,
          "свободный максимум кластера обязан получить шахту как прежде");
    CHECK(f.fl.at(kTownX, kTownY) == sm::FT_Village,
          "деревня цела и в этом случае");
    CHECK(sm::worked_read(f.gs, kTownX, kTownY) == kVillageFlock,
          "паства цела и в этом случае");
}

// ТРЕТЬЯ ПРОВЕРКА — ПРО ГРАНИЦУ, А НЕ ПРО СЛУЧАЙ: клетка места закрыта для
// шахты КОЛОНКОЙ, значит закрыта у ЛЮБОГО рода места, а не только у деревни.
// Иначе правка читалась бы как «особый случай деревни» и умерла бы в день,
// когда шахта сядет на руину.
void test_the_closure_is_by_column_not_by_kind() {
    int closed = 0, open = 0;
    for (std::size_t i = 0; i < std::size_t(sm::FT_Count); ++i) {
        const sm::FeatureType ft = sm::FeatureType(i);
        if (sm::place_kind_of_feature(ft) != sm::SquadType::None) ++closed;
        else ++open;
    }
    CHECK(closed == 5,
          "клетку закрывают ровно пять родов мест — столица, город, деревня, "
          "шпиль, руина (строки kPlaces с непустой колонкой feature)");
    CHECK(open > 0 && closed + open == int(sm::FT_Count),
          "перебран ВЕСЬ словарь фич, а не выборка");
}

} // namespace

int main() {
    test_mine_does_not_eat_the_village();
    test_free_peak_is_untouched();
    test_the_closure_is_by_column_not_by_kind();
    return sm::test::report("mine_settlement_collision_test");
}

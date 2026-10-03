// ОДИН ЭМИТЕНТ ОРДИНАЛОВ СУБЪЕКТОВ — МЕСТО И СКВАД НЕ ДЕЛЯТ НОМЕР (M-37,
// вердикт владельца 2026-09-30; ЗАКОН СЛОВАРЯ И ОРДИНАЛА п.6).
//
// Три закона под охраной:
//  1. ВЫДАЧА С ЕДИНИЦЫ: 0 навсегда «никто» — свежий мир рождается с
//     эмитентом 1, и ни один путь рождения не выдаёт нулевой ординал.
//     Летопись писала неизвестного субъекта нулём с v54 — теперь это закон
//     всех ординалов-идентичностей, и коллизия «первый сквад = никто»
//     (spawnId начинался с 0) невозможна по построению.
//  2. ОДНО ПРОСТРАНСТВО: место (add_landmark, id из эмитента) и сквад
//     (spawn_squad → make_npc, единственная дверь рождения) тянут ОДИН счётчик;
//     их номера строго различны и строго возрастают в порядке рождения.
//     До M-37 счётчиков было два, и сквад не мог войти в реестр интересов
//     места без молчаливой коллизии номеров.
//  3. ПОИСК ПО ОРДИНАЛУ: ростер мест append-only + эмитент монотонный ⇒ id
//     в векторе строго возрастают ⇒ landmark_by_id ищет бинарно
//     (state.h landmark_index_by_id). Скан-фолбэк — честность на случай
//     нарушенного инварианта: дверь обязана ответить и на НЕупорядоченном
//     ростере (негативный контроль ниже строит такой руками, в обход
//     эмитента, — ровно тот дефект, который фолбэк прощает, а бинарный
//     поиск в одиночку прозевал бы).
#include "check.h"

#include <utility>

#include "core/rng.h"
#include "macro/npc_spawn.h"
#include "macro/state.h"
#include "macro/store.h"

namespace {

using namespace sm;

void test_issue_starts_at_one() {
    GameState gs{};
    CHECK(gs.nextMacroSpawnOrdinal == 1u,
          "свежий мир: выдача ординалов начинается с 1 — 0 навсегда «никто»");
}

// Сухая равнина целиком — spawn_squad нужна честная суша (образец
// design_characters_test).
TerrainData make_terrain(int w, int h) {
    TerrainData t;
    t.width = w;
    t.height = h;
    t.rgba.assign(std::size_t(w) * std::size_t(h) * 4u, 0);
    for (std::size_t i = 0; i < std::size_t(w) * std::size_t(h); ++i) {
        // УРОВЕНЬ, а не байт: карта хранит слово (`kFieldWordMax`), и байтовый
        // литерал 180 означал бы в ней 0.003 — то есть ВОДУ, молча.
        t.rgba[i * 4u + 0] = sm::field_word_of(180.0f / 255.0f);
                                    // суша выше уровня моря
        t.rgba[i * 4u + 3] = std::uint16_t(sm::kFieldWordMax);   // маска суши
    }
    return t;
}

void test_one_space_for_places_and_squads() {
    GameState gs{};
    gs.mapW = 64; gs.mapH = 64;
    const TerrainData terrain = make_terrain(gs.mapW, gs.mapH);
    auto store = make_macro_store();

    // Место — тем же жестом, что генезис (state.cpp/spires.cpp/ruins.cpp).
    Landmark a{};
    a.type = LandmarkType::Village;
    a.id   = int(gs.nextMacroSpawnOrdinal++);
    add_landmark(gs, std::move(a));

    // Сквад — единственной дверью рождения сквадов.
    SquadSpec spec{};
    spec.leaderType = NPCType::Peasant;
    spec.x = 3;
    spec.y = 4;
    const MacroHandle h = spawn_squad(gs, *store, terrain, spec);
    CHECK(store->valid(h), "фикстура рождает свой сквад сама (§8 п.11)");
    const std::uint32_t squadOrdinal = store->spawnId[h.slot].index;

    // Второе место — после сквада.
    Landmark b{};
    b.type = LandmarkType::City;
    b.id   = int(gs.nextMacroSpawnOrdinal++);
    add_landmark(gs, std::move(b));

    const int placeA = gs.landmarks[0].id;
    const int placeB = gs.landmarks[1].id;
    CHECK(placeA == 1 && squadOrdinal == 2u && placeB == 3,
          "одно пространство: место, сквад, место — 1, 2, 3 из ОДНОГО "
          "эмитента, ни один номер не выдан дважды");
    CHECK(placeA > 0 && squadOrdinal > 0u,
          "нулевой ординал не выдаётся никому (0 = «никто»)");

    // Поиск по ординалу: бинарный по строго возрастающему ростеру.
    CHECK(landmark_by_id(gs, placeA) == &gs.landmarks[0]
              && landmark_by_id(gs, placeB) == &gs.landmarks[1],
          "landmark_by_id находит оба места при номерах вперемешку со "
          "сквадами (плотности landmarks[id-1] больше нет)");
    CHECK(landmark_by_id(gs, 0) == nullptr
              && landmark_by_id(gs, -1) == nullptr
              && landmark_by_id(gs, int(squadOrdinal)) == nullptr,
          "«никто», отрицательный и СКВАДНЫЙ номер местом не отвечают");
}

void test_scan_fallback_survives_unsorted_roster() {
    // Негативный контроль честности: ростер, собранный МИМО эмитента с
    // нарушенным порядком (7 раньше 5), — бинарный поиск такой инвариант
    // прозевал бы, дверь обязана ответить фолбэком.
    GameState gs{};
    Landmark hi{};
    hi.type = LandmarkType::Ruin;
    hi.id = 7;
    add_landmark(gs, std::move(hi));
    Landmark lo{};
    lo.type = LandmarkType::Ruin;
    lo.id = 5;
    add_landmark(gs, std::move(lo));
    CHECK(landmark_by_id(gs, 5) == &gs.landmarks[1]
              && landmark_by_id(gs, 7) == &gs.landmarks[0],
          "скан-фолбэк отвечает и на неупорядоченном ростере — корректность "
          "не висит на инварианте, которого не держит static_assert");
}

}  // namespace

int main() {
    test_issue_starts_at_one();
    test_one_space_for_places_and_squads();
    test_scan_fallback_survives_unsorted_roster();
    return sm::test::report("ordinal_space_test");
}

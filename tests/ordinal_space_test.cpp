// ОДИН ЭМИТЕНТ ОРДИНАЛОВ СУБЪЕКТОВ — МЕСТО И СКВАД НЕ ДЕЛЯТ НОМЕР (M-37,
// вердикт владельца 2026-09-30; ЗАКОН СЛОВАРЯ И ОРДИНАЛА п.6).
//
// Три закона под охраной:
//  1. ВЫДАЧА С ЕДИНИЦЫ: 0 навсегда «никто» — свежий мир рождается с
//     эмитентом 1, и ни один путь рождения не выдаёт нулевой ординал.
//     Летопись писала неизвестного субъекта нулём с v54 — теперь это закон
//     всех ординалов-идентичностей, и коллизия «первый сквад = никто»
//     (spawnId начинался с 0) невозможна по построению.
//  2. ОДНО ПРОСТРАНСТВО: место (birth_place, ординал из эмитента ВНУТРИ
//     двери) и сквад (spawn_squad → make_npc, единственная дверь рождения)
//     тянут ОДИН счётчик; их номера строго различны и строго возрастают в
//     порядке рождения. До M-37 счётчиков было два, и сквад не мог войти в
//     реестр интересов места без молчаливой коллизии номеров.
//  3. ПОИСК ПО ОРДИНАЛУ: эмитент монотонный ⇒ ординально отсортированный
//     порядок закона (SquadWalkEntry, выводится из store каждый драйв)
//     строго возрастает ⇒ горячая дверь резолва ищет бинарно. Скан-фолбэк —
//     честность на случай нарушенного инварианта: дверь обязана ответить и
//     на НЕупорядоченном порядке (негативный контроль ниже строит такой
//     руками — ровно тот дефект, который фолбэк прощает, а бинарный поиск в
//     одиночку прозевал бы).
//     ЛОМТИК F: прежним носителем этого закона был append-only вектор мест
//     (`landmark_index_by_id`); он умер вместе со строкой места, и закон
//     перешёл на ТУ ЖЕ механику над порядком тел — её шапка
//     (`macro_handle_by_spawn_id@src/macro/squad.h`) называет умершую дверь
//     своим предшественником дословно.
#include "check.h"

#include <utility>
#include <vector>

#include "core/rng.h"
#include "macro/npc_spawn.h"
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/squad.h"         // резолв по ординалу: место и сквад
#include "macro/squad_walk.h"    // SquadWalkEntry — порядок закона
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

    // Место — тем же жестом, что генезис (state.cpp/spires.cpp/ruins.cpp):
    // ординал эмитится ВНУТРИ двери, звонящий его не выбирает (ломтик F).
    const MacroHandle a =
        birth_place(gs, *store, SquadType::Village, 1, 2);
    CHECK(store->valid(a), "фикстура рождает своё место сама (§8 п.11)");

    // Сквад — единственной дверью рождения сквадов.
    SquadSpec spec{};
    spec.leaderType = NPCType::Peasant;
    spec.x = 3;
    spec.y = 4;
    const MacroHandle h = spawn_squad(gs, *store, terrain, spec);
    CHECK(store->valid(h), "фикстура рождает свой сквад сама (§8 п.11)");
    const std::uint32_t squadOrdinal = store->spawnId[h.slot].index;

    // Второе место — после сквада.
    const MacroHandle b = birth_place(gs, *store, SquadType::City, 5, 6);
    CHECK(store->valid(b), "второе место родилось тем же жестом");

    const int placeA = int(store->spawnId[a.slot].index);
    const int placeB = int(store->spawnId[b.slot].index);
    CHECK(placeA == 1 && squadOrdinal == 2u && placeB == 3,
          "одно пространство: место, сквад, место — 1, 2, 3 из ОДНОГО "
          "эмитента, ни один номер не выдан дважды");
    CHECK(placeA > 0 && squadOrdinal > 0u,
          "нулевой ординал не выдаётся никому (0 = «никто»)");

    // Поиск по ординалу: резолв субъекта плюс ГЕЙТ ОСИ РОДА — после смерти
    // строки «это место?» отвечает только колонка тела.
    CHECK(place_handle_by_ordinal(*store, std::uint32_t(placeA)).slot == a.slot
              && place_handle_by_ordinal(*store,
                                         std::uint32_t(placeB)).slot == b.slot,
          "place_handle_by_ordinal находит оба места при номерах вперемешку "
          "со сквадами (плотности landmarks[id-1] больше нет)");
    CHECK(!store->valid(place_handle_by_ordinal(*store, 0u))
              && !store->valid(place_handle_by_ordinal(*store, 0xFFFFFFFFu))
              && !store->valid(place_handle_by_ordinal(*store, squadOrdinal)),
          "«никто», небывалый и СКВАДНЫЙ номер местом не отвечают");
}

void test_scan_fallback_survives_unsorted_order() {
    // Негативный контроль честности: ПОРЯДОК ЗАКОНА, собранный мимо
    // `collect_squads_by_ordinal` с нарушенной сортировкой (второй ординал
    // раньше первого), — бинарный поиск такой инвариант прозевал бы, дверь
    // обязана ответить фолбэком. Носитель закона сменился ломтиком F
    // (вектор мест → порядок тел), сам закон тот же: корректность не висит
    // на инварианте, которого не держит static_assert.
    GameState gs{};
    gs.mapW = 64; gs.mapH = 64;
    auto store = make_macro_store();
    const MacroHandle lo =
        birth_place(gs, *store, SquadType::Ruin, 1, 1);
    const MacroHandle hi =
        birth_place(gs, *store, SquadType::Ruin, 2, 2);
    CHECK_OR_RETURN(store->valid(lo) && store->valid(hi),
                    "свидетель родил оба своих места сам (§8 п.11)");
    const std::uint32_t loOrd = store->spawnId[lo.slot].index;
    const std::uint32_t hiOrd = store->spawnId[hi.slot].index;
    CHECK_OR_RETURN(hiOrd > loOrd,
                    "эмитент монотонен — порядку есть что нарушать");

    // Порядок НАОБОРОТ: ровно тот дефект, который фолбэк прощает.
    const std::vector<SquadWalkEntry> broken = {
        SquadWalkEntry{hiOrd, hi.slot}, SquadWalkEntry{loOrd, lo.slot}};
    CHECK(macro_handle_by_spawn_id(*store, broken, loOrd).slot == lo.slot
              && macro_handle_by_spawn_id(*store, broken, hiOrd).slot
                     == hi.slot,
          "скан-фолбэк отвечает и на неупорядоченном порядке закона");
    // И НЕГАТИВНЫЙ КОНТРОЛЬ САМОГО ФОЛБЭКА: попадание в индекс
    // ВЕРИФИЦИРУЕТСЯ колонкой spawnId, поэтому порядок, врущий про слот,
    // соврать двери не может.
    const std::vector<SquadWalkEntry> lying = {
        SquadWalkEntry{loOrd, hi.slot}, SquadWalkEntry{hiOrd, lo.slot}};
    CHECK(macro_handle_by_spawn_id(*store, lying, loOrd).slot == lo.slot,
          "устаревший порядок не может подсунуть чужой слот: попадание "
          "проверяется колонкой");
}

}  // namespace

int main() {
    test_issue_starts_at_one();
    test_one_space_for_places_and_squads();
    test_scan_fallback_survives_unsorted_order();
    return sm::test::report("ordinal_space_test");
}

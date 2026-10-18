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
//  3. ПОИСК ПО ОРДИНАЛУ НЕ УМЕЕТ СОВРАТЬ. Эмитент монотонный ⇒ справочник
//     идентичности стора (`ordKey`/`ordSlot`, M-271) строго возрастает ⇒
//     резолв ищет бинарно. Честность держит не сортировка, а ПРОВЕРКА
//     найденного: слот жив И его колонка `spawnId` совпала с ключом. Поэтому
//     испорченный справочник умеет только ПРОМОЛЧАТЬ, и негативный контроль
//     ниже портит его руками.
//     НОСИТЕЛЬ ЭТОГО ЗАКОНА МЕНЯЛСЯ ДВАЖДЫ, А ЗАКОН НЕТ: append-only вектор
//     мест (`landmark_index_by_id`, умер со строкой места) → порядок закона,
//     который звонящий приносил С СОБОЙ отдельным параметром (умер с M-271,
//     потому что был ВТОРОЙ дверью одного вопроса) → справочник у эмитента.
//     Прежняя редакция этого свидетеля охраняла «скан-фолбэк прощает
//     неупорядоченный порядок»; фолбэка больше нет — скан снесён, — и то же
//     требование предъявлено справочнику: соврать нечем.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (M-271, 2026-10-19; обе полярности, §8 п.6 —
// три свидетеля в прогоне: этот, `macro_store_test`, `macro_snapshot_test`):
//  · рождение НЕ заводит строку справочника — КРАСНЫЙ втройне: 3 FAIL здесь,
//    4 в сторе, 2 в снимке;
//  · резолв НЕ проверяет найденное — КРАСНЫЙ, 3 FAIL здесь. И ЭТО ВТОРОЙ
//    СНИМОК: с первой редакцией свидетеля мутация прошла ЗЕЛЁНОЙ во всех
//    трёх, потому что проверки спрашивали `store->valid(h)` ПОВЕРХ возврата
//    двери, а `valid()` перепроверяет ровно то же условие сама. Дыра была в
//    свидетеле, и нашла её только записанная мутация: теперь судится САМ
//    возврат — не найдено значит ПУСТОЙ хэндл, а не «невалидный»;
//  · переименование пишет колонку, справочник НЕ правит — КРАСНЫЙ, 1 FAIL
//    здесь и 2 в сторе (ровно тот молчаливый разъезд, ради которого сырое
//    присваивание `spawnId` и запрещено правилом R8 `arch_guard_test`);
//  · компакция НИЧЕГО не отбрасывает — КРАСНЫЙ, 1 FAIL здесь; он же
//    доказывает, что ветка переполнения прогоном ДОСТИГАЕТСЯ, а не описана;
//  · бинарный поиск заменён ЛИНЕЙНЫМ по тому же справочнику (ответы те же,
//    цена другая) — МОЛЧИТ во всех трёх. Детектор судит ОТВЕТЫ, а не
//    реализацию: краснеющий на всё детектором не является.
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
    auto st = sm::make_macro_store();
    CHECK(st->nextSpawnId == 1u,
          "свежий стор: выдача ординалов начинается с 1 — 0 навсегда «никто»");
    const MacroHandle h = store_birth(*st);
    CHECK(h.id == 1u, "первое рождение получает ординал 1 от двери (M-220)");
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

void test_directory_cannot_lie() {
    // Негативный контроль честности: справочник идентичности, ИСПОРЧЕННЫЙ
    // руками (строка указывает на чужой слот), не смеет подсунуть этот слот
    // звонящему — попадание проверяется колонкой `spawnId`. Корректность не
    // висит на инварианте, которого не держит static_assert.
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
                    "эмитент монотонен — справочнику есть что нарушать");
    CHECK(macro_handle_by_spawn_id(*store, loOrd).slot == lo.slot
              && macro_handle_by_spawn_id(*store, hiOrd).slot == hi.slot,
          "целый справочник отвечает обоими слотами");

    // ЛЖИВАЯ СТРОКА: ключи на месте, слоты поменяны местами.
    for (std::uint32_t i = 0; i < store->ordCount; ++i) {
        if (store->ordKey[i] == loOrd) store->ordSlot[i] = hi.slot;
        else if (store->ordKey[i] == hiOrd) store->ordSlot[i] = lo.slot;
    }
    // СУДИТСЯ САМ ВОЗВРАТ ДВЕРИ, А НЕ `valid()` ПОВЕРХ НЕГО, и это не
    // придирка: `valid()` перепроверяет ровно то же условие, поэтому
    // свидетель, спрашивающий её, слеп к СНОСУ проверки внутри двери —
    // мутация «резолв не проверяет найденное» прошла зелёной именно так
    // (таблица негативного контроля, 2026-10-19). Контракт двери сильнее:
    // не найдено — ПУСТОЙ хэндл.
    const MacroHandle gotLo = macro_handle_by_spawn_id(*store, loOrd);
    const MacroHandle gotHi = macro_handle_by_spawn_id(*store, hiOrd);
    CHECK(gotLo.slot == kMacroNoSlot && gotLo.id == 0u
              && gotHi.slot == kMacroNoSlot && gotHi.id == 0u,
          "справочник, врущий про слот, двери не врёт: ответ ПУСТОЙ хэндл, "
          "а не ЧУЖОЙ сквад");

    // И ТА ЖЕ ПРОВЕРКА СНИМАЕТ УСТАРЕВШУЮ СТРОКУ: слот умер и переродился
    // под новым именем — старый ключ обязан отвечать «никого».
    store_reset(*store);
    MacroHandle a = birth_place(gs, *store, SquadType::Ruin, 3, 3);
    CHECK_OR_RETURN(store->valid(a), "свидетель родил своё место сам");
    const std::uint32_t aOrd = a.id;
    store_death(*store, a);
    const MacroHandle b = birth_place(gs, *store, SquadType::Ruin, 3, 3);
    CHECK(store->valid(b) && b.slot == a.slot && b.id != aOrd,
          "freelist вернул тот же слот новому жильцу с новым именем");
    const MacroHandle ghost = macro_handle_by_spawn_id(*store, aOrd);
    CHECK(ghost.slot == kMacroNoSlot && ghost.id == 0u,
          "имя покойника не отвечает слотом, который занял другой");
}

void test_directory_equals_an_honest_scan() {
    // ГЛАВНЫЙ ЗАКОН M-271: дверь-справочник отвечает РОВНО то же, что
    // честный линейный скан всех слотов, — на мире, прожившем рождения,
    // смерти, переименования и ПЕРЕПОЛНЕНИЕ справочника (компакцию).
    // Скан здесь не вторая реализация в продакшене (DOD п.6), а эталон
    // свидетеля: та форма, которую M-271 и снёс, оставлена ровно затем,
    // чтобы новую было с чем сверить.
    auto store = make_macro_store();
    MacroStore& s = *store;
    const auto honest_scan = [&](std::uint32_t id) -> std::uint16_t {
        for (std::uint32_t slot = 0; slot < kUnifiedCap; ++slot)
            if (s.alive[slot] != 0 && s.spawnId[slot].index == id)
                return std::uint16_t(slot);
        return kMacroNoSlot;
    };

    Rng rng(0xD17Eu);
    std::vector<std::uint32_t> issued;
    std::vector<MacroHandle> live;
    // Мир, который РОЖДАЕТ БОЛЬШЕ КАПА за прогон: справочник обязан
    // переполниться и компактнуться, иначе эта ветка не проверена ни разу.
    for (int step = 0; step < int(kUnifiedCap) * 2; ++step) {
        const bool kill = !live.empty() && (rng.next_u32() % 3u) == 0u;
        if (kill) {
            const std::size_t k = rng.next_u32() % std::uint32_t(live.size());
            store_death(s, live[k]);
            live[k] = live.back();
            live.pop_back();
            continue;
        }
        MacroHandle h = store_birth(s);
        if (!s.valid(h)) continue;   // кап полон — следующий шаг убьёт
        // Каждое десятое рождение принимает ЧУЖОЕ имя (путь загрузки и
        // фикстур): вставка в середину справочника, а не дозапись.
        if ((rng.next_u32() % 10u) == 0u)
            store_rename_ordinal(s, h, s.nextSpawnId + 1000u);
        issued.push_back(h.id);
        live.push_back(h);
    }
    CHECK_OR_RETURN(!issued.empty() && !live.empty(),
                    "прогон родил мир, которому есть что спрашивать");
    CHECK(s.ordCount <= std::uint32_t(kUnifiedCap),
          "справочник не перерос кап: компакция на рождении его разобрала");

    std::size_t probes = 0, found = 0, mismatches = 0;
    const auto probe = [&](std::uint32_t id) {
        ++probes;
        const std::uint16_t want = honest_scan(id);
        const MacroHandle got = macro_handle_by_spawn_id(s, id);
        // Сырой возврат, не `s.valid(got)`: см. довод выше.
        const bool wantEmpty = want == kMacroNoSlot;
        if (wantEmpty ? (got.slot != kMacroNoSlot || got.id != 0u)
                      : (got.slot != want || got.id != id))
            ++mismatches;
        if (want != kMacroNoSlot) ++found;
    };
    for (std::uint32_t id : issued) probe(id);      // жившие: живые и мёртвые
    probe(0);                                       // «никто»
    probe(0xFFFFFFFFu);                             // небывалый
    for (int i = 0; i < 64; ++i) probe(rng.next_u32() % 100000u);

    CHECK(probes > 0 && found > 0 && mismatches == 0,
          "справочник и честный скан согласны на КАЖДОМ опрошенном "
          "ординале — включая покойников, «никого» и небывалых");
}

}  // namespace

int main() {
    test_issue_starts_at_one();
    test_one_space_for_places_and_squads();
    test_directory_cannot_lie();
    test_directory_equals_an_honest_scan();
    return sm::test::report("ordinal_space_test");
}

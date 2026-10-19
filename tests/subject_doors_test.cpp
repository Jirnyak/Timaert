// ДВЕРИ СУБЪЕКТА МАКРОКАРТЫ: ГЛАГОЛЫ И КОНТЕЙНЕР — ОДНА ФОРМА АДРЕСА.
//
// Здесь стоял `map_subject_test`, и он утверждал про `map_subject.h` — файл,
// УМЕРШИЙ ЦЕЛИКОМ 2026-10-19 (M-281). Свидетель переписан, а не подогнан:
// §8 п.7 отдаёт презумпцию вины тесту при одобренном улучшении, но §8 п.5
// требует, чтобы он утверждал ЗАКОН, а не пересказывал новую форму. Поэтому
// ниже проверяются ровно те законы, носителем которых он был, — и ни один из
// них не остался без свидетеля при сносе (§5 п.6).
//
// ЧТО СНЕСЕНО И ПОЧЕМУ ОНО БЫЛО ВТОРЫМ ОТВЕТОМ. `MapSubject` нёс ХРАНИМЫЙ
// enum рода {None, Squad, Landmark} плюс ДВА адреса, и три его двери
// ветвились по этому роду. Перепись 2026-10-19 назвала цену: у всех трёх
// дверей было НОЛЬ прод-звонящих — они жили только в этом тесте, — а живые
// читатели тех же двух вопросов читали колонки НАПРЯМУЮ в трёх местах. То
// есть хранимый род держал не систему, а её чучело.
//
// И ДВУХ АДРЕСОВ ТОЖЕ НЕ БЫЛО. `MacroHandle` есть {слот, ординал}, а «ординал
// места» был ровно его половиной `id` с выброшенным слотом, который тут же
// восстанавливали резолвом. Отсюда главный закон этого файла: субъект
// адресуется ОДИНАКОВО, стоит он или идёт.
//
// ДЫРА, КОТОРУЮ СНОС ЗАКРЫЛ ПО ПОСТРОЕНИЮ: `subject_of_squad(h)` принимал
// ЛЮБОЙ хэндл, включая хэндл неподвижного сквада, и выдавал ему ХОДОЧЬИ
// глаголы — ни один `static_assert` этого не запрещал. Теперь спросить
// «как у сквада» нельзя: у вопроса один ответ, и его даёт строка рода
// (test_no_second_address_form ниже утверждает именно это).
#include "check.h"
#include "macro/labour.h"   // settle_souls / souls_flock — двери душ

#include "ecs/components.h"   // MacroSpawnId — ординал слота store
#include "macro/landmark_registry.h"
#include "macro/place_birth.h"   // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/squad.h"         // actions_of + place_handle_by_ordinal
#include "macro/state.h"
#include "macro/store.h"
#include "tables/map_actions.h"

#include <cstdio>

namespace {

// Место есть неподвижный сквад (ломтик F): склад его — колонка ТЕЛА, род —
// колонка оси, ординал ВЫДАЁТ ЭМИТЕНТ. Фикстура запоминает, кем родились её
// трое, и спрашивает двери ровно этими ординалами.
struct Fixture {
    sm::GameState gs;
    int cityId = 0;
    int vilId = 0;
    int spireId = 0;
};

Fixture make_world(sm::MacroStore& st) {
    Fixture fx{};
    sm::GameState& gs = fx.gs;
    gs.mapW = 64;
    gs.mapH = 64;
    // Эмитент поднят ВЫШЕ рукописных ординалов сквадов этой фикстуры (5, 6):
    // выдача с единицы столкнула бы место и сквад в одном ординале, и дверь
    // отвечала бы «тем же объектом» по совпадению. С M-220 эмитент — у стора.
    st.nextSpawnId = 7u;
    const sm::MacroHandle city = sm::birth_place(
        gs, st, sm::SquadType::City, 10, 10, -1, "Testholm");
    fx.cityId = int(st.spawnId[city.slot].index);
    sm::creatures_push(st.inventory[city.slot].inv,
        sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 2, 11u));
    // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число фичи,
    // головы в инвентарь ТЕЛА — тем же законом, что генезис.
    sm::settle_souls(gs, st, city.slot, 300);
    // A VILLAGE and a SPIRE on the same one id space (v54): the doors must
    // answer for them exactly as they do for the city — kind-blind.
    const sm::MacroHandle village = sm::birth_place(
        gs, st, sm::SquadType::Village, 20, 20, -1, "Hamlet");
    fx.vilId = int(st.spawnId[village.slot].index);
    sm::settle_souls(gs, st, village.slot, 40);
    const sm::MacroHandle spire =
        sm::birth_place(gs, st, sm::SquadType::Spire, 40, 40);
    fx.spireId = int(st.spawnId[spire.slot].index);
    return fx;
}

// ПОДВИЖНЫЙ сквад — тот же `store_birth`, что у всякого сквада, плюс колонка
// оси. Фикстура пинит рукописный ординал: хэндл пересобирается ПОСЛЕ записи
// колонки, иначе он нёс бы выданный дверью ординал и протух бы на месте.
sm::MacroHandle make_squad(sm::MacroStore& st, std::uint32_t ordinal,
                           sm::SquadType type = sm::SquadType::None) {
    sm::MacroHandle h = sm::store_birth(st);
    sm::store_rename_ordinal(st, h, ordinal);
    st.runtime[h.slot].squadType = std::uint8_t(type);
    sm::creatures_push(st.inventory[h.slot].inv,
        sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 2, 21u));
    return sm::handle_at(st, h.slot);
}

// ЗАКОН ПЕРВЫЙ — ОДИН КОНТЕЙНЕР, И ЭТО ТОТ ЖЕ ОБЪЕКТ ПАМЯТИ (M-71).
// Утверждается АДРЕСНОЕ равенство, поэтому «один склад» есть факт памяти, а
// не соглашение о копиях. Дверь — выжившая (`body_state`), а не снесённая
// обёртка над ней: закон переехал к носителю, который его исполняет.
void test_one_container_is_the_very_object() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);
    const MacroHandle squad = make_squad(st, 5, SquadType::Artel);

    CHECK(&body_state<ecs::NpcInventory>(st, squad)->inv
              == &st.inventory[squad.slot].inv,
          "у ходока склад ЕСТЬ его колонка инвентаря — тот самый объект");

    const MacroHandle city = place_handle_by_ordinal(st, std::uint32_t(fx.cityId));
    CHECK(&body_state<ecs::NpcInventory>(st, city)->inv
              == &st.inventory[city.slot].inv,
          "у стоящего склад ЕСТЬ колонка инвентаря ЕГО ЖЕ ТЕЛА, не второй стор");

    // PLAY-2's law: дверь КИНД-СЛЕПА. Деревня и шпиль отвечают той же дверью,
    // что город, — фильтра по `SquadType` нет ни одного.
    for (const int id : {fx.vilId, fx.spireId}) {
        const MacroHandle h = place_handle_by_ordinal(st, std::uint32_t(id));
        CHECK(body_state<ecs::NpcInventory>(st, h) != nullptr,
              "деревня и шпиль открывают дверь как всякое стоящее");
    }

    // Запись через дверь видна по прямому пути: за дверью ОДИН объект.
    Inventory* store = body_state<ecs::NpcInventory>(st, city)
        ? &body_state<ecs::NpcInventory>(st, city)->inv : nullptr;
    CHECK_OR_RETURN(store != nullptr, "склад города открывается");
    store->add("food", 3);
    CHECK(st.inventory[city.slot].inv.count("food") == 3,
          "еда, положенная дверью, лежит в колонке того же слота");
}

// ЗАКОН ВТОРОЙ — ГЛАГОЛЫ ОБЪЯВЛЯЕТ СТРОКА, А ОТВЕЧАЕТ ОДНА ДВЕРЬ (CANON S22).
// Вердикты владельца 2026-09-11 пинятся как ЛИТЕРАЛЬНЫЕ маски (сами числа
// замысла, домашний стиль), поэтому съехавшая колонка спотыкается здесь, а
// не в живой игре.
void test_verbs_are_declared_by_the_row() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);

    const MacroHandle city = place_handle_by_ordinal(st, std::uint32_t(fx.cityId));
    const MacroHandle vil  = place_handle_by_ordinal(st, std::uint32_t(fx.vilId));
    const MacroHandle sp   = place_handle_by_ordinal(st, std::uint32_t(fx.spireId));

    CHECK(actions_of(st, city)
              == (kMapActTrade | kMapActHire | kMapActQuests | kMapActEnter),
          "город: прилавок, найм, доска контрактов и вход");
    CHECK(actions_of(st, vil)
              == (kMapActTrade | kMapActHire | kMapActQuests | kMapActEnter),
          "деревня — тот же вердикт: прилавок + найм + доска (и вход)");
    CHECK(actions_of(st, sp) == kMapActEnter,
          "шпиль своих глаголов не объявляет: минимум входа");

    // ВОЙНА — ОТДЕЛЬНЫЙ ТРЕК: через это меню место не говорит и не бьётся.
    // Тот же закон стоит `static_assert`-ом над таблицей
    // (`landmark_registry.h`); здесь он проверяется НА ВЫХОДЕ ДВЕРИ, то есть
    // вместе с примесью Enter, которой таблица не делает.
    CHECK((actions_of(st, city) & (kMapActTalk | kMapActAttack)) == 0
              && (actions_of(st, sp) & (kMapActTalk | kMapActAttack)) == 0,
          "ни одно стоящее не предлагает разговор или удар этим меню");

    // ХОДОК ГОВОРИТ, ТОРГУЕТ И БЬЁТСЯ — И ЭТО ТЕПЕРЬ СТРОКА, А НЕ ЛИТЕРАЛ В
    // КОДЕ ДВЕРИ. Проверяются ОБА подвижных случая: названный тип и
    // бестиповый сквад («точка течи границы миров» — шапка оси).
    const MacroHandle artel = make_squad(st, 5, SquadType::Artel);
    const MacroHandle bare  = make_squad(st, 6, SquadType::None);
    CHECK(actions_of(st, artel) == (kMapActTalk | kMapActTrade | kMapActAttack),
          "артель: разговор, торг, удар — колонкой своей строки");
    CHECK(actions_of(st, bare) == (kMapActTalk | kMapActTrade | kMapActAttack),
          "бестиповый сквад — ходок, и глаголы у него ходочьи");

    // ВОЙТИ МОЖНО ТОЛЬКО В ТО, ЧТО СТОИТ. Enter выводится из `walkable`
    // ОДНИМ местом (дверью), и у подвижной строки он ложен — значит бита не
    // будет никаким путём. До M-281 строка `None` несла `walkable = true`, и
    // это было незаметно ровно потому, что ответ за неё давал литерал.
    CHECK((actions_of(st, artel) & kMapActEnter) == 0
              && (actions_of(st, bare) & kMapActEnter) == 0,
          "в человека не входят: Enter у подвижного рода невыразим");
}

// ЗАКОН ТРЕТИЙ — ВТОРОЙ ФОРМЫ АДРЕСА НЕТ, И ПОТОМУ НЕТ ДЫРЫ.
// Прежде субъект можно было назвать «как сквада» (хэндл) или «как место»
// (ординал), и первое принимало хэндл МЕСТА, выдавая ему ходочьи глаголы.
// Теперь форма одна: ординал — половина хэндла, и обе двери отвечают о
// ОДНОМ слоте одно и то же, кем бы субъект ни был.
void test_no_second_address_form() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);

    const MacroHandle city = place_handle_by_ordinal(st, std::uint32_t(fx.cityId));
    CHECK(int(city.id) == fx.cityId,
          "«ординал места» ЕСТЬ половина id его хэндла, а не второй адрес");
    CHECK(city == handle_at(st, city.slot),
          "резолв по ординалу и обход по слоту дают ОДИН хэндл");

    // Хэндл стоящего, отданный двери глаголов, отвечает глаголами СТОЯЩЕГО.
    // Это и есть закрытая дыра: ходочью маску ему выдать больше нечем.
    CHECK((actions_of(st, city) & (kMapActTalk | kMapActAttack)) == 0,
          "хэндл города не умеет притвориться ходоком — спрашивать некого");
}

// Fail closed: нерождённый, протухший или мёртвый субъект не предлагает
// НИЧЕГО и не открывает ничьего склада — дисциплина всякой макро-двери
// (нулевой вклад S6).
void test_the_doors_fail_closed() {
    using namespace sm;
    auto worldStore_ = sm::make_macro_store();
    MacroStore& st = *worldStore_;
    Fixture fx = make_world(st);

    CHECK(actions_of(st, MacroHandle{}) == 0
              && body_state<ecs::NpcInventory>(st, MacroHandle{}) == nullptr,
          "нерождённый хэндл не называет никого");

    CHECK(place_handle_by_ordinal(st, 9999u) == MacroHandle{},
          "неизвестный ординал резолвится в пустой хэндл, а не в чужой слот");

    // Ординал ЖИВОГО ходока, поданный в дверь МЕСТА, — честное «никого»:
    // пространство ординалов одно (M-37), и различает их ось рода.
    const MacroHandle artel = make_squad(st, 5, SquadType::Artel);
    CHECK(place_handle_by_ordinal(st, artel.id) == MacroHandle{},
          "ординал ходока не отдаёт его за место: ось рода гейтит резолв");

    // Поколение мертвит пережившую смерть ссылку: тот же слот, но старый
    // хэндл — «никто».
    const MacroHandle doomed = make_squad(st, 6, SquadType::Artel);
    sm::store_death(st, doomed);
    CHECK(actions_of(st, doomed) == 0
              && body_state<ecs::NpcInventory>(st, doomed) == nullptr,
          "хэндл, переживший смерть своего слота, не называет никого");

    (void)fx;
}

} // namespace

int main() {
    test_one_container_is_the_very_object();
    test_verbs_are_declared_by_the_row();
    test_no_second_address_form();
    test_the_doors_fail_closed();
    return sm::test::report("subject_doors_test");
}

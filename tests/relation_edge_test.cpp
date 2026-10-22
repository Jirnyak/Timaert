// РЕБРО СВЯЗИ (M-245): одна ось, две пары — Parent/Child и Suzerain/Vassal,
// одна дверь `set_relation@src/macro/store.h` на оба конца, долг дани НА
// РЕБРЕ Suzerain вассала (`tithe_of`). Законы, которые стережёт этот файл:
//   · оба конца ставятся ОДНИМ вызовом, зеркальная роль — вторым значением
//     оси (роль несимметрична по природе, род 6 фрейма);
//   · носитель роли у субъекта ровно один: смена сюзерена снимает прежнего
//     с ОБОИХ концов, и с ребром умирает долг (прощение сменой феода);
//   · self-связь законна ТОЛЬКО у Parent (дом места — оно само), одной
//     записью без зеркала; self-феод — отказ;
//   · СМЕРТЬ ЧИСТИТ РЁБРА: контрагенты умершего теряют свой конец в
//     `store_death` — иначе дом копит Child-рёбра умерших артелей до
//     потолка реестра (128) молча.
// Негативные контроли: односторонняя запись мимо двери ВИДНА детектору
// пары; конец, не снятый смертью, ВИДЕН (§8 п.6).
#include "check.h"

#include "macro/squad.h"
#include "macro/store.h"

#include <cstdint>
#include <memory>

namespace {

using namespace sm;

MacroStore& store() {
    static std::unique_ptr<MacroStore> st = make_macro_store();
    return *st;
}

// Рождение тела с прибитым ординалом — идиома фикстур (woodcutter и др.).
MacroHandle birth(MacroStore& st, std::uint32_t ordinal) {
    MacroHandle h = store_birth(st);
    store_rename_ordinal(st, h, ordinal);
    return handle_at(st, h.slot);
}

// Детектор ПАРЫ: у a стоит роль на b, у b — зеркало на a. Перевычисления
// продакшена здесь нет: дверь пару ПОДДЕРЖИВАЕТ, детектор её СУДИТ.
bool pair_holds(const MacroStore& st, MacroHandle a, MacroHandle b,
                Stance role, Stance mirror) {
    const Interest* fwd = interest_find(st.interests[a.slot], int(b.id));
    const Interest* back = interest_find(st.interests[b.slot], int(a.id));
    return fwd && back && fwd->stance == std::uint8_t(role)
        && back->stance == std::uint8_t(mirror);
}

void test_both_ends_one_door() {
    MacroStore& st = store();
    store_reset(st);
    const MacroHandle city = birth(st, 10u);
    const MacroHandle vil  = birth(st, 20u);
    const MacroHandle artel = birth(st, 30u);

    set_relation(st, int(vil.id), int(city.id), Stance::Suzerain);
    CHECK(pair_holds(st, vil, city, Stance::Suzerain, Stance::Vassal),
          "феод: оба конца одним вызовом");
    CHECK(suzerain_of(st, vil.slot) == int(city.id),
          "дверь чтения видит сюзерена");

    set_relation(st, int(artel.id), int(vil.id), Stance::Parent);
    CHECK(pair_holds(st, artel, vil, Stance::Parent, Stance::Child),
          "родство: оба конца одним вызовом");
    CHECK(parent_of(st, artel.slot) == int(vil.id),
          "дом = родитель (ребро Parent)");
}

void test_role_is_single_and_debt_dies_with_the_edge() {
    MacroStore& st = store();
    store_reset(st);
    const MacroHandle a = birth(st, 11u);
    const MacroHandle s1 = birth(st, 21u);
    const MacroHandle s2 = birth(st, 31u);

    set_relation(st, int(a.id), int(s1.id), Stance::Suzerain);
    Interest* edge = tithe_of(st, a.slot);
    CHECK_OR_RETURN(edge != nullptr, "ребро дани живёт у вассала");
    edge->owedValue = 500;
    CHECK(owes_tithe(st, a.slot), "долг на ребре виден");

    // Смена феода: прежний сюзерен теряет ОБА конца, долг прощён.
    set_relation(st, int(a.id), int(s2.id), Stance::Suzerain);
    CHECK(interest_find(st.interests[s1.slot], int(a.id)) == nullptr,
          "прежний сюзерен потерял свой конец");
    CHECK(suzerain_of(st, a.slot) == int(s2.id), "новый сюзерен встал");
    CHECK(!owes_tithe(st, a.slot),
          "долг умер вместе с ребром — прощение сменой феода");

    // «Стал ничьим»: otherId = 0 снимает роль целиком.
    set_relation(st, int(a.id), 0, Stance::Suzerain);
    CHECK(suzerain_of(st, a.slot) == 0, "ничей — роли нет");
    CHECK(interest_find(st.interests[s2.slot], int(a.id)) == nullptr,
          "и зеркальный конец снят");
}

void test_self_edges() {
    MacroStore& st = store();
    store_reset(st);
    const MacroHandle place = birth(st, 12u);

    // Self-феод — отказ: вассал сам себе не платит.
    set_relation(st, int(place.id), int(place.id), Stance::Suzerain);
    CHECK(suzerain_of(st, place.slot) == 0, "self-феод отвергнут");

    // Self-Parent — закон места: одна запись, без зеркала.
    set_relation(st, int(place.id), int(place.id), Stance::Parent);
    CHECK(parent_of(st, place.slot) == int(place.id),
          "дом места — оно само (self-ребро Parent)");
    CHECK(interests_count(st.interests[place.slot]) == 1,
          "self-ребро — ОДНА запись, зеркало не съело второй слот");
}

void test_death_cleans_counterpart_ends() {
    MacroStore& st = store();
    store_reset(st);
    const MacroHandle vil = birth(st, 13u);
    const MacroHandle artel = birth(st, 23u);
    set_relation(st, int(artel.id), int(vil.id), Stance::Parent);
    CHECK_OR_RETURN(
        interest_find(st.interests[vil.slot], int(artel.id)) != nullptr,
        "до смерти Child-конец у дома стоит (контроль предусловия)");

    store_death(st, artel);
    CHECK(interest_find(st.interests[vil.slot], int(artel.id)) == nullptr,
          "смерть сквада сняла его конец у контрагента (store_death)");
}

void test_detector_sees_a_broken_pair() {
    MacroStore& st = store();
    store_reset(st);
    const MacroHandle a = birth(st, 14u);
    const MacroHandle b = birth(st, 24u);
    // Односторонняя запись МИМО двери — ровно тот дефект, который пара
    // делает невыразимым; детектор обязан его видеть (§8 п.6).
    interest_set(st.interests[a.slot], int(b.id), Stance::Suzerain, 0, 0);
    CHECK(!pair_holds(st, a, b, Stance::Suzerain, Stance::Vassal),
          "детектор ВИДИТ односторонее ребро (негативный контроль)");
}

} // namespace

int main() {
    test_both_ends_one_door();
    test_role_is_single_and_debt_dies_with_the_edge();
    test_self_edges();
    test_death_cleans_counterpart_ends();
    test_detector_sees_a_broken_pair();
    return sm::test::report("relation_edge_test");
}

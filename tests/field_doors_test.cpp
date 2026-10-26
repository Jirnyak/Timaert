// Двери пульта полей (ломоть 1 M-72, ЗАКОН ТРЁХ ДВЕРЕЙ: drain возвращает
// ФАКТ). Что здесь охраняется — ЗАКОНЫ, не числа:
//   · field_move отвечает фактом: взять больше, чем есть, невыразимо, и
//     звонящий читает, СКОЛЬКО мир реально отдал;
//   · field_harvest — транзакция кредит→дебет ОДНИМ коммитом: поле платит
//     ровно за то, что контейнер принял; отказ контейнера (спина, слот)
//     оставляет поле нетронутым;
//   · двойная выемка двух сквадов одной клетки НЕ дюпает — прежние 57 строк
//     между read и apply у ai_gatherer дюпали её по построению;
//   · выход решает ФИЧА клетки (лён поверх пашни — вердикт 2026-10-22 «что
//     там окажется решает фича шахты», тот же закон), иначе умолчание ряда;
//   · feature_put снимает фичу значением FT_None — «выработанную шахту снять
//     нечем» было дырой наряда.
//
// Негативные контроли — ТАБЛИЦЕЙ МУТАЦИЙ (§8 п.6) в теле коммита ломтя.
#include "check.h"

#include <memory>

#include "macro/anketa.h"
#include "macro/features.h"
#include "macro/labour.h"
#include "macro/macro_stock.h"
#include "macro/map_generator.h"
#include "macro/place_birth.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/store.h"
#include "macro/tree_layer.h"
#include "macro/world_row.h"
#include "tables/items.h"
#include "tables/npc.h"

namespace {

using namespace sm;

constexpr int kMap = 64;

// Мир свидетеля: терраин с честной фертильностью (G-канал — её читает
// baseline пашни), слой деревьев, слой фич. Свидетель РОЖАЕТ свои
// предусловия сам (§8 п.11) — ни одна клетка не надеется на генератор.
struct World {
    std::unique_ptr<MacroStore> store;
    GameState gs;
    TerrainData td;
    TreeLayer trees;
    FeatureLayer features;
    MacroHandle village{};
    MacroHandle artel{};

    MacroWorld mw() {
        return MacroWorld{.gs = &gs,
                          .trees = &trees,
                          .store = store.get(),
                          .terrain = &td,
                          .features = &features};
    }
};

World make_world() {
    World w{make_macro_store(), GameState{}, TerrainData{}, TreeLayer{},
            FeatureLayer{}, {}, {}};
    w.gs.mapW = kMap;
    w.gs.mapH = kMap;
    w.td.width = kMap;
    w.td.height = kMap;
    w.td.rgba.assign(std::size_t(kMap) * kMap * 4u, 0);
    w.td.seaLevel16 = field_word_of(kDefaultSeaLevel);
    for (std::size_t c = 0; c < std::size_t(kMap) * kMap; ++c) {
        const std::size_t s = c * 4u;
        w.td.rgba[s + 0] = field_word_of(140.0f / 255.0f);   // суша
        w.td.rgba[s + 1] = field_word_of(200.0f / 255.0f);   // фертильность
        w.td.rgba[s + 2] = field_word_of(128.0f / 255.0f);
        w.td.rgba[s + 3] = std::uint16_t(kFieldWordMax);
    }
    w.trees.width = kMap;
    w.trees.height = kMap;
    w.trees.data.assign(std::size_t(kMap) * kMap, 0);
    w.features.resize(kMap, kMap);
    allocate_world_fields(w.gs, kMap, kMap);

    w.village = birth_place(w.gs, *w.store, SquadType::Village, 10, 10, -1,
                            "Doorholm");
    settle_souls(w.gs, *w.store, w.village.slot, 40);

    // Артель с анкетой: спина лидера — честная грузоподъёмность анкеты,
    // та же дверь, какой рождает make_npc.
    MacroStore& st = *w.store;
    MacroHandle h = store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.sub[h.slot] = {};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(SquadType::Artel);
    st.runtime[h.slot] = rt;
    st.sheet[h.slot] = make_character_sheet(NPCType::Peasant, 3,
                                            avatar_sheet_seed(7u));
    ecs::Pools pools{};
    refresh_body_from_sheet(pools, &st.runtime[h.slot], st.sheet[h.slot],
                            NPCType::Peasant);
    st.pools[h.slot] = pools;
    w.artel = h;
    return w;
}

// ── field_move: факт, а не эхо намерения ─────────────────────────────────
void test_move_returns_fact() {
    World w = make_world();
    MacroWorld mw = w.mw();
    set_tree_count(w.trees, 5, 5, 3);

    CHECK(field_move(mw, ResourceFieldId::Trees, 5, 5, -10) == -3,
          "drain: хотели 10, стояло 3 — факт −3, не эхо намерения");
    CHECK(resource_field_read(mw, ResourceFieldId::Trees, 5, 5) == 0,
          "после выемки клетка пуста");
    CHECK(field_move(mw, ResourceFieldId::Trees, 5, 5, -1) == 0,
          "из пустой клетки взять нечего — факт 0, дюп невыразим");
    CHECK(field_move(mw, ResourceFieldId::Trees, 5, 5, +4) == +4,
          "возврат: четыре встали — факт +4");

    // Шрамовый ряд: пашня. Ёмкость клетки — baseline(фертильность) > 0.
    const int cap = resource_field_read(mw, ResourceFieldId::Wheat, 6, 6);
    CHECK_OR_RETURN(cap > 8, "фикстура несёт честную ёмкость пашни");
    CHECK(field_move(mw, ResourceFieldId::Wheat, 6, 6, -5) == -5,
          "шрамовый ряд: взято ровно 5");
    CHECK(field_move(mw, ResourceFieldId::Wheat, 6, 6, +9999) == +5,
          "заживление клампится ёмкостью: вернулось ровно снятое");
}

// ── field_harvest: транзакция честна и атомарна ──────────────────────────
void test_harvest_honest_and_no_dupe() {
    World w = make_world();
    MacroWorld mw = w.mw();
    set_tree_count(w.trees, 12, 10, 3);
    Inventory& bag = w.store->inventory[w.artel.slot].inv;

    const int got = field_harvest(mw, ResourceFieldId::Trees, 12, 10,
                                  /*want=*/5, w.artel.slot);
    CHECK(got == 3, "хотели 5, стояло 3 — транзакция вернула факт 3");
    CHECK(bag.count("wood") == 3, "кредит: ровно факт лёг в контейнер");
    CHECK(resource_field_read(mw, ResourceFieldId::Trees, 12, 10) == 0,
          "дебет: поле отдало ровно факт");

    // ДВОЙНАЯ ВЫЕМКА: второй сквад приходит на ту же клетку В ТОТ ЖЕ тик.
    // Прежний путь (read → 57 строк → apply) отдал бы обоим по 3.
    const int second = field_harvest(mw, ResourceFieldId::Trees, 12, 10,
                                     /*want=*/5, w.village.slot);
    CHECK(second == 0, "второму скваду той же клетки взять нечего — не дюпает");
    CHECK(bag.count("wood") == 3, "первый унёс своё, больше ниоткуда не взялось");
}

// ── отказ контейнера оставляет поле нетронутым ───────────────────────────
void test_refusal_leaves_field() {
    World w = make_world();
    MacroWorld mw = w.mw();
    set_tree_count(w.trees, 20, 20, 50);

    // СПИНА ПОЛНА: грузим артель её же добычей до отказа (want велик — дверь
    // сама клампит по спине), потом спрашиваем ещё раз.
    int lastGot = -1;
    for (int i = 0; i < 64 && lastGot != 0; ++i) {
        lastGot = field_harvest(mw, ResourceFieldId::Trees, 20, 20,
                                /*want=*/1 << 20, w.artel.slot);
        if (lastGot > 0) set_tree_count(w.trees, 20, 20, 50);   // долить
    }
    CHECK_OR_RETURN(lastGot == 0, "спина артели конечна — отказ достигнут");
    const int standing = resource_field_read(mw, ResourceFieldId::Trees, 20, 20);
    CHECK(field_harvest(mw, ResourceFieldId::Trees, 20, 20, 1 << 20,
                        w.artel.slot) == 0,
          "полная спина: транзакция отвечает 0");
    CHECK(resource_field_read(mw, ResourceFieldId::Trees, 20, 20) == standing,
          "и поле НЕ тронуто — добыча не испаряется в полный контейнер");

    // СЛОТ СУЩЕСТВА: пастбище со стадом, а область существ контейнера забита
    // стопками разных уровней — push_stack отказывает, стадо цело.
    CHECK_OR_RETURN(feature_put(w.features, w.td, 30, 30, FT_Pasture)
                        == FT_Pasture,
                    "загон встал");
    worked_write(w.gs, 30, 30, 7);   // стадо — число фичи (M-258)
    Inventory& bag = w.store->inventory[w.artel.slot].inv;
    // Именная душа не сливается ни с кем (entityId — свой слот каждой),
    // поэтому область существ забивается честно, слот за слотом.
    for (std::uint32_t id = 1; id <= 2048; ++id) {
        SoldierRecord s{};
        s.entityId = id;
        s.kind = std::uint16_t(NPCType::Horse);
        s.level = 1;
        if (!creatures_push(bag, s)) break;
    }
    CHECK_OR_RETURN(!creatures_push_stack(bag, NPCType::Horse, 1, 1),
                    "область существ забита — предусловие отказа создано");
    CHECK(field_harvest(mw, ResourceFieldId::Livestock, 30, 30, 2,
                        w.artel.slot) == 0,
          "нет слота под душу: транзакция отвечает 0");
    CHECK(resource_field_read(mw, ResourceFieldId::Livestock, 30, 30) == 7,
          "и стадо не тронуто");

    // СЛОТА НЕТ ПРИ СВОБОДНОЙ СПИНЕ — ровно тот случай, где «дебет до
    // кредита» терял бы мир: души подняли спину (haulG — массы они не несут),
    // но под НОВЫЙ предмет нет ни стака, ни пустого слота. Предусловие
    // «спина свободна» утверждается вслух — иначе отказ пришёл бы по спине
    // (take = 0 до кредита), и дыра дебета осталась бы невидимой.
    CHECK_OR_RETURN(feature_put(w.features, w.td, 22, 20, FT_Field)
                        == FT_Field,
                    "парцелла под жатву встала");
    const std::int64_t freeG =
        carry_capacity_g(w.store->runtime[w.artel.slot], bag)
        - inventory_mass_g(bag);
    CHECK_OR_RETURN(freeG > 64000, "спина свободна — отказ будет СЛОТОМ");
    const int wheatBefore = resource_field_read(mw, ResourceFieldId::Wheat,
                                                22, 20);
    CHECK_OR_RETURN(wheatBefore > 2, "пашня несёт честную ёмкость");
    CHECK(field_harvest(mw, ResourceFieldId::Wheat, 22, 20, 2,
                        w.artel.slot) == 0,
          "нет слота под предмет при свободной спине: транзакция отвечает 0");
    CHECK(resource_field_read(mw, ResourceFieldId::Wheat, 22, 20)
              == wheatBefore,
          "и пашня не тронута — дебет без кредита невыразим");
}

// ── выход решает фича клетки, иначе умолчание ряда ───────────────────────
void test_feature_decides_yield() {
    World w = make_world();
    MacroWorld mw = w.mw();
    Inventory& bag = w.store->inventory[w.artel.slot].inv;

    // Пашня: фича без оверрайда — умолчание ряда Wheat («food»).
    CHECK_OR_RETURN(feature_put(w.features, w.td, 8, 8, FT_Field) == FT_Field,
                    "парцелла встала");
    CHECK_OR_RETURN(field_harvest(mw, ResourceFieldId::Wheat, 8, 8, 2,
                                  w.artel.slot) == 2,
                    "жатва пашни состоялась");
    CHECK(bag.count("food") == 2, "пашня даёт умолчание ряда — пищу");

    // Лён: ТА ЖЕ фертильность, тот же ряд Wheat — выход переопределяет фича.
    CHECK_OR_RETURN(feature_put(w.features, w.td, 9, 8, FT_FlaxField)
                        == FT_FlaxField,
                    "льняная парцелла встала");
    CHECK_OR_RETURN(field_harvest(mw, ResourceFieldId::Wheat, 9, 8, 2,
                                  w.artel.slot) == 2,
                    "жатва льна состоялась");
    CHECK(bag.count("fibre") == 2,
          "лён поверх пашни: выход решила ФИЧА, не колонка ряда");
    CHECK(bag.count("food") == 2, "и пища от этого не прибавилась");

    // Пастбище: существо — умолчание ряда Livestock, душой в контейнер.
    CHECK_OR_RETURN(feature_put(w.features, w.td, 11, 8, FT_Pasture)
                        == FT_Pasture,
                    "загон встал");
    worked_write(w.gs, 11, 8, 5);
    const int before = creature_count(bag);
    CHECK_OR_RETURN(field_harvest(mw, ResourceFieldId::Livestock, 11, 8, 1,
                                  w.artel.slot) == 1,
                    "поимка состоялась");
    CHECK(creature_count(bag) == before + 1,
          "конь встал ДУШОЙ в контейнер, не предметом в сумку");
    CHECK(resource_field_read(mw, ResourceFieldId::Livestock, 11, 8) == 4,
          "стадо отдало ровно факт");
}

// ── feature_put: FT_None — значение, и оно СНИМАЕТ ───────────────────────
void test_feature_put_none_removes() {
    World w = make_world();
    CHECK_OR_RETURN(feature_put(w.features, w.td, 40, 40, FT_Field)
                        == FT_Field,
                    "фича встала");
    CHECK(feature_put(w.features, w.td, 40, 40, FT_None) == FT_None,
          "снятие отвечает фактом FT_None");
    CHECK(w.features.at(40, 40) == FT_None,
          "клетка чиста — выработанную фичу ЕСТЬ чем снять");
}

} // namespace

int main() {
    test_move_returns_fact();
    test_harvest_honest_and_no_dupe();
    test_refusal_leaves_field();
    test_feature_decides_yield();
    test_feature_put_none_removes();
    return sm::test::report("field_doors_test");
}

// Свидетель переписи форм мира (ряд M-441; CANON S10 «ИНВАРИАНТ МИРА»).
// Что здесь охраняется — ЗАКОНЫ, не числа:
//   · ДОБЫЧА ЕСТЬ ПЕРЕХОД ФОРМЫ. Транзакция «поле → контейнер» ничего не
//     создаёт и ничего не теряет: залежь убывает ровно на то, на сколько
//     прирастает локальное, и сумма форм по своей оси стоит на месте. Судится
//     на каждом роде выхода: предмет ряда, предмет фичи поверх ряда (лён,
//     металл шахты) и существо (стадо → души);
//   · ОСИ НЕ СМЕШИВАЮТСЯ. Выемка стада не трогает стоимость, выемка леса не
//     трогает души;
//   · ДУША СЧИТАЕТСЯ РОВНО РАЗ. Поселённые люди лежат головами в контейнере
//     места, и число поля фич под ним — второй носитель ТОЙ ЖЕ величины: в
//     сумму душ оно не входит;
//   · ПРИБОР ВИДИТ РОЖДЕНИЕ ИЗ НИЧЕГО. Прибавка поля без источника сдвигает
//     сумму ровно на цену прибавленного — без этого первый закон проходил бы
//     и у слепой переписи.
//
// Негативные контроли — таблицей мутаций (§8 п.6) в ряду M-441.
#include "check.h"
#include "world_forms_census.h"

#include <memory>

#include "bus/anketa.h"
#include "bus/store.h"
#include "bus/world_row.h"
#include "macro/birth_squad.h"
#include "macro/features.h"
#include "macro/labour.h"
#include "macro/macro_stock.h"
#include "macro/map_generator.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "tables/items.h"
#include "tables/npc.h"

namespace {

using namespace sm;

constexpr int kMap = 64;
constexpr int kSettled = 40;

// Мир свидетеля рождает свои предусловия сам (§8 п.11): суша с честной
// фертильностью, слой деревьев, слой фич, деревня с поселёнными душами и
// артель с анкетой — её спина и есть грузоподъёмность транзакции.
struct World {
    std::unique_ptr<MacroStore> store;
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    TerrainData td;
    MacroHandle village{};
    MacroHandle artel{};

    MacroWorld mw() {
        return MacroWorld{.gs = &gs, .store = store.get(), .terrain = &td};
    }
};

World make_world() {
    World w;
    w.store = make_macro_store();
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
    tree_layer_reset(w.gs.treeLayer, kMap, kMap);
    w.gs.features.resize(kMap, kMap);
    allocate_world_fields(w.gs, kMap, kMap);

    w.village = birth_squad(w.gs, nullptr, *w.store, SquadType::Village, 10,
                            10, -1, "Formholm");
    settle_souls(w.gs, *w.store, w.village.slot, kSettled);

    MacroStore& st = *w.store;
    const MacroHandle h = store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
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

long long item_value(const char* id) {
    const ItemDef* def = item_def_at(item_index(id));
    return def ? (long long)def->value : 0;
}

// Одна выемка предметного ряда, судимая как переход формы. `yieldId` — что
// обязано лечь в руки: цена единицы берётся у СТРОКИ ПРЕДМЕТА, а не у
// переписи, поэтому перепись, оценившая залежь чужой строкой, краснит.
void judge_item_harvest(World& w, ResourceFieldId row, int x, int y,
                        const char* yieldId, const char* what) {
    MacroWorld mw = w.mw();
    const long long unit = item_value(yieldId);
    CHECK_OR_RETURN(unit > 0, "строка выхода несёт цену — иначе закон пуст");
    const WorldForms before = census_world_forms(mw);
    const int got = field_harvest(mw, row, x, y, /*want=*/3, w.artel.slot);
    CHECK_OR_RETURN(got > 0, what);
    const WorldForms after = census_world_forms(mw);

    const std::size_t r = std::size_t(row);
    CHECK(before.depositValue[r] - after.depositValue[r] == got * unit,
          "залежь убыла ровно на цену взятого");
    CHECK(after.localValue - before.localValue == got * unit,
          "локальное приросло ровно на цену взятого");
    CHECK(after.value_carried() == before.value_carried(),
          "добыча есть переход формы: сумма стоимости стоит на месте");
    CHECK(after.souls_carried() == before.souls_carried(),
          "оси не смешиваются: выемка предмета не трогает души");
}

// ── предметные ряды: умолчание ряда и оверрайд фичи ──────────────────────
void test_item_harvest_is_form_transition() {
    World w = make_world();

    set_tree_count(w.gs.treeLayer, 12, 10, 9);
    judge_item_harvest(w, ResourceFieldId::Trees, 12, 10, "wood",
                       "рубка состоялась");

    CHECK_OR_RETURN(feature_put(w.gs.features, w.td, 8, 8, FT_Field)
                        == FT_Field,
                    "парцелла встала");
    judge_item_harvest(w, ResourceFieldId::Wheat, 8, 8, "food",
                       "жатва пашни состоялась");

    CHECK_OR_RETURN(feature_put(w.gs.features, w.td, 9, 8, FT_FlaxField)
                        == FT_FlaxField,
                    "льняная парцелла встала");
    judge_item_harvest(w, ResourceFieldId::Wheat, 9, 8, "fibre",
                       "жатва льна состоялась");

    CHECK_OR_RETURN(feature_put(w.gs.features, w.td, 14, 8, FT_IronMine)
                        == FT_IronMine,
                    "шахта встала");
    feature_number_write(w.gs, 14, 8, 9);   // залежь — число фичи шахты
    judge_item_harvest(w, ResourceFieldId::Vein, 14, 8, "iron",
                       "выемка жилы состоялась");
}

// ── ряд-существо: стадо уходит в души, стоимость не тронута ──────────────
void test_creature_harvest_is_form_transition() {
    World w = make_world();
    MacroWorld mw = w.mw();
    CHECK_OR_RETURN(feature_put(w.gs.features, w.td, 11, 8, FT_Pasture)
                        == FT_Pasture,
                    "загон встал");
    feature_number_write(w.gs, 11, 8, 5);   // стадо — число фичи

    const WorldForms before = census_world_forms(mw);
    const std::size_t r = std::size_t(ResourceFieldId::Livestock);
    CHECK_OR_RETURN(before.depositHeads[r] == 5,
                    "стадо видно переписи головами залежи");
    CHECK(before.depositValue[r] == 0,
          "и стоимостью оно не оценено: выход ряда — существо");
    const int got = field_harvest(mw, ResourceFieldId::Livestock, 11, 8, 2,
                                  w.artel.slot);
    CHECK_OR_RETURN(got == 2, "поимка состоялась");
    const WorldForms after = census_world_forms(mw);

    CHECK(before.depositHeads[r] - after.depositHeads[r] == got,
          "залежь душ убыла ровно на пойманных");
    CHECK(after.localBeasts - before.localBeasts == got,
          "и они встали головами в контейнер");
    CHECK(after.souls_carried() == before.souls_carried(),
          "поимка есть переход формы: сумма душ стоит на месте");
    CHECK(after.value_carried() == before.value_carried(),
          "оси не смешиваются: выемка стада не трогает стоимость");
}

// ── душа считается ровно раз ─────────────────────────────────────────────
void test_soul_counted_once() {
    World w = make_world();
    MacroWorld mw = w.mw();
    const WorldForms f = census_world_forms(mw);

    CHECK(f.localFolk == kSettled, "поселённые лежат головами в контейнере");
    CHECK(f.flockNumber == kSettled && f.folkRooted == kSettled,
          "и второй носитель — число поля фич под местом — называет их же");
    CHECK(f.localAvatars == 1,
          "аватар есть у ходячего сквада и нет у места");
    CHECK(f.souls_local() == kSettled + 1,
          "в сумму душа входит раз: число поля фич не слагаемое");
    CHECK(f.souls_deposits() == 0, "залежей душ в этом мире нет");
}

// ── прибор видит рождение из ничего ──────────────────────────────────────
void test_census_sees_creation() {
    World w = make_world();
    MacroWorld mw = w.mw();
    const long long wood = item_value("wood");
    CHECK_OR_RETURN(wood > 0, "строка дерева несёт цену");
    const WorldForms before = census_world_forms(mw);
    CHECK_OR_RETURN(field_move(mw, ResourceFieldId::Trees, 30, 30, +7) == 7,
                    "семь деревьев встали без источника");
    const WorldForms grown = census_world_forms(mw);
    CHECK(grown.value_carried() - before.value_carried() == 7 * wood,
          "прибавка поля без источника сдвигает сумму на свою цену");

    Inventory& bag = w.store->inventory[w.artel.slot].inv;
    CHECK_OR_RETURN(bag.add_of(item_index("wood"), 4),
                    "четыре полена легли в контейнер без источника");
    const WorldForms gifted = census_world_forms(mw);
    CHECK(gifted.localValue - grown.localValue == 4 * wood,
          "и подарок контейнеру виден локальной формой");
}

} // namespace

int main() {
    test_item_harvest_is_form_transition();
    test_creature_harvest_is_form_transition();
    test_soul_counted_once();
    test_census_sees_creation();
    return sm::test::report("world_forms_census_test");
}

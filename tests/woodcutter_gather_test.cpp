// The first honest work-loop (W2b): a village woodcutter CHOPS the world and
// HAULS it home. Pinned:
//   · the chop leaves through the registry's Trees carrier row — the layer
//     count really falls and the revision moves (the grid rides the save
//     whole, v36, so a save remembers the stump);
//   · the haul rides in the woodcutter's OWN bag and lands in his HOME
//     village's universal inventory (the home-link fix: a village man works
//     for the village, not for the nearest city);
//   · CONSERVATION — wood gained by the store == wood lost by the layer,
//     and the bag is empty after delivery: nothing minted, nothing dropped.
#include "check.h"
#include <memory>
#include "macro/labour.h"   // settle_souls — двери душ
#include "macro/upkeep_window.h"   // upkeep_bill — счёт по таблице

#include "macro/npc_ai.h"
#include "macro/rooted_iter.h"  // for_each_rooted — перепись мест
#include "macro/birth_squad.h"   // birth_squad — место родится ТЕЛОМ
#include "bus/world_row.h"
#include "macro/chronicle.h"
#include "macro/econ_day.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/resource_field.h"
#include "macro/squad.h"
#include "macro/deposit_layer.h"
#include "macro/tree_layer.h"
#include "bus/store.h"

#include <limits>
#include <cmath>
#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 32;


// Суша под дверью фичи: стена ломтя 1 закрыла сырой set — фикстура ставит
// фичу той же дверью, что мир (feature_put; воду та спрашивает у карты).
sm::TerrainData all_land() {
    sm::TerrainData td;
    td.width = kMap;
    td.height = kMap;
    td.rgba.assign(std::size_t(kMap) * std::size_t(kMap) * 4u, 0);
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    for (std::size_t c = 0; c < std::size_t(kMap) * std::size_t(kMap); ++c)
        td.rgba[c * 4u + 0] = sm::field_word_of(140.0f / 255.0f);
    return td;
}
// Count the chronicle's facts of one kind around a cell — the witcher's own
// question, asked by the tests that pin what the work-loops write (and, by
// the negative controls, what they must NOT write).
struct FactTally {
    int n = 0;
    WorldFact last{};
};
FactTally tally_facts(const Chronicle& c, FactKind kind, int x, int y) {
    struct Ctx { FactKind kind; FactTally out; } ctx{kind, {}};
    // Индекс летописи выводной (M-423): свежий, собранный из лога на лету.
    const std::unique_ptr<ChronicleLinks> links = make_chronicle_links();
    chronicle_near(c, *links, x, y, /*radiusCells*/1, /*sinceDay*/0,
                   [](void* u, const WorldFact& f) {
                       Ctx& t = *static_cast<Ctx*>(u);
                       if (f.kind != std::uint16_t(t.kind)) return;
                       ++t.out.n;
                       if (t.out.n == 1) t.out.last = f;
                   }, &ctx);
    return ctx.out;
}

sm::MacroHandle make_woodcutter(sm::MacroStore& st, float x, float y,
                                int homeVillageId) {
    sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(int(x), int(y), kMap)};
    st.runtime[h.slot].sub = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.state = std::uint8_t(NPCState::Idle);
    rt.recoverySteps = 0;
    ecs::Pools pools{};
    const CharacterSheet sheet = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(11u));
    refresh_body_from_sheet(pools, &rt, sheet, NPCType::Peasant);
    pools.sp = pools.maxSp;
    // Лесоруб — ТИП сквада со своим модулем (M-488, вердикт владельца
    // 2026-11-13): что добывать, называет тип, а не выбор в поле.
    rt.squadType = std::uint8_t(SquadType::Woodcutters);
    st.runtime[h.slot] = rt;
    sm::store_rename_ordinal(st, h, 11u);
    h = sm::handle_at(st, h.slot);
    sm::set_relation(st, int(h.id), homeVillageId, sm::Stance::Parent);
    // Уровень — АНКЕТА (M-246а): та же дверь, какой рождает make_npc.
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;
    return h;
}

void test_the_chop_is_real_and_the_haul_comes_home() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);

    // A little forest cell four cells east of the village — small enough to
    // be felled to BARE within the run, so the chronicle negative control
    // below is a real condition and not a vacuous one.
    TreeLayer& layer = gs.treeLayer;
    tree_layer_reset(layer, kMap, kMap);
    layer.data[10 * kMap + 14] = 16;
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;

    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store: склад
    // — его колонка, ординал выдаёт эмитент ВНУТРИ двери рождения.
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    const int vilId = int(st.spawnId[vil.slot].index);
    // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в число фичи
    // фичи, головы в инвентарь ТЕЛА — тем же законом, что генезис.
    sm::settle_souls(gs, st, vil.slot, 40);
    const sm::MacroHandle wc = make_woodcutter(st, 10.0f, 10.0f, vilId);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);

    // Live a while: idle -> travel -> WORK (the chop) -> return (the haul).
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st,
                      .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const int layerLost = 16 - int(layer.at(cell_of(14, 10, layer.width)));
    const int storeGained =
        st.inventory[vil.slot].inv.count("wood");
    const int inBag =
        (*sm::body_state<ecs::NpcInventory>(st, wc)).inv.count("wood");
    // ── ЧЕТВЁРТОЕ МЕСТО, ГДЕ ТЕПЕРЬ БЫВАЕТ ДЕРЕВО: ОНО САМО СТАЛО ВЕЩЬЮ ──
    //
    // Ломоть 5а M-72 дал деревне МОДУЛЬ, и она производит из того, что ей
    // привезли: её анкета знает Masonry 30, а `furniture` = {wood 2} и
    // `carving` = {wood 1} — рецепты ровно под эту полку. То есть у дерева
    // появился ЗАКОННЫЙ СТОК, которого у ведомости ниже не было, и прежняя
    // сумма «склад + спина» перестала быть полной: она мерила ОСТАТОК
    // сырья там, где теперь стоит ПРОИЗВОДИТЕЛЬ.
    //
    // ЗАМЕР, НАЗВАВШИЙ ПРИЧИНУ: срублено 16, на полке осталось дерева 2,
    // мебели 4 (по 2 дерева) и резьбы 6 (по 1) — 2 + 8 + 6 = РОВНО 16.
    // Материя не потерялась ни на единицу, то есть закон цел, а неполна
    // была ВЕДОМОСТЬ. Поэтому чинится она, а не мир (ЗАКОН НУЛЕВОЙ п.3:
    // причина найдена, и лечение — в свидетеле).
    //
    // СКОЛЬКО ДЕРЕВА ЛЕЖИТ В ГОТОВЫХ ВЕЩАХ — спрашивается у ЕДИНОЙ ТАБЛИЦЫ
    // МАТЕРИИ (`item_parts@src/tables/items.h`), той же, по которой считает
    // производство. Это не копия продакшен-логики (§8 п.5): копией было бы
    // перевычислить РАНЖИРОВАНИЕ рук, а здесь складывается состав — то есть
    // ведомость читает авторскую строку, как и велит §8 п.4.
    //
    // ШАГ ОДИН НАРОЧНО, И ЭТО СКАЗАНО ВСЛУХ: единственные стоки дерева в
    // этой фикстуре — односоставные вещи. Углубится цепочка (дерево → часть
    // → изделие) — сумма ниже РАЗОЙДЁТСЯ И ПОКРАСНЕЕТ, и это правильное
    // поведение ведомости: она потребует себя продлить, а не промолчит.
    const int woodIdx = sm::item_index("wood");
    int woodInGoods = 0;
    {
        const auto cat = sm::item_catalog();
        const sm::Inventory& shelf = st.inventory[vil.slot].inv;
        for (int i = 0; i < int(cat.size()); ++i) {
            const int have = shelf.count_of(i);
            if (have <= 0) continue;
            for (const sm::ItemPart& part : sm::item_parts(i))
                if (int(part.def) == woodIdx)
                    woodInGoods += have * int(part.count);
        }
    }

    CHECK(layerLost > 0, "the chop really fell trees in the layer");
    // (Здесь стояло «the haul reached the village store» по СЫРОМУ дереву на
    // полке. С ряда M-486 деревня вправе пустить в дело всё дерево до
    // последнего — «сырьё осталось» было обстоятельством прежнего
    // ранжирования, а не законом. Что привезённое дошло, держат две
    // проверки ниже: дерево стало вещью, и материя сошлась.)
    CHECK(woodInGoods > 0,
          "деревня ПУСТИЛА привезённое в дело — иначе слагаемое ниже было бы "
          "нулевым и ведомость снова мерила бы только сырьё");
    CHECK(layerLost == storeGained + inBag + woodInGoods,
          "CONSERVATION: layer loss == сырьё на складе + сырьё в спине + "
          "дерево, УЖЕ СТАВШЕЕ ВЕЩЬЮ — ничего не чеканится и не исчезает");
    CHECK(layer.revision > 0,
          "the chop moved the grid revision - the map sprite and the save "
          "(which carries the grid whole) both see the stump");
    int rootedCount = 0;
    sm::for_each_rooted(st,
                       [&](std::uint16_t) { ++rootedCount; });
    CHECK(rootedCount == 1,
          "the village man hauls for the VILLAGE (no city even exists here)");

    // NEGATIVE CONTROL for the vein writer: the forest cell was felled to
    // bare ground, and the chronicle stays SILENT — the forest regrows by its
    // own law (resource_fields_daily_growth), so an emptied cell is weather,
    // not the irreversible loss FactKind::Drained records.
    CHECK(layer.at(cell_of(14, 10, layer.width)) == 0, "the control condition fired: bare cell");
    CHECK(tally_facts(gs.chronicle, FactKind::Drained, 14, 10).n == 0,
          "a felled forest writes NO Drained fact - a forest is not a vein");
}

// ОДИН АКТ КРЮ — ПО ОБЪЕКТУ НА РАБОЧЕГО ЮНИТА (владелец, дословно: «каждый
// работник рубит по дереву»; цена акта делится на рабочих юнитов — CANON S25,
// 2026-11-13). Сколько просить у земли, решает модуль артели; этот свидетель
// держит его решение: первый же акт крю из восьми юнитов снимает с клетки
// восемь деревьев, а не одно.
void test_a_crew_takes_one_object_per_unit() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);

    constexpr int kTrees = 64;
    TreeLayer& layer = gs.treeLayer;
    tree_layer_reset(layer, kMap, kMap);
    layer.data[10 * kMap + 14] = kTrees;
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    const int vilId = int(st.spawnId[vil.slot].index);
    sm::settle_souls(gs, st, vil.slot, 40);
    const sm::MacroHandle wc = make_woodcutter(st, 10.0f, 10.0f, vilId);
    constexpr int kCrewPeople = 7;
    sm::Inventory& crewBag =
        (*sm::body_state<ecs::NpcInventory>(st, wc)).inv;
    CHECK(sm::raise_flock_into_container(crewBag, kCrewPeople) == kCrewPeople,
          "семь человек встали в инвентарь артели");
    const int units = sm::work_units(crewBag);
    CHECK(units == 1 + kCrewPeople && units < kTrees,
          "предусловие: рабочих юнитов восемь (аватар и семь человек), и "
          "деревьев на клетке больше, чем юнитов");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    const std::uint32_t site = cell_of(14, 10, layer.width);
    int firstTake = 0;
    for (int i = 0; i < 400 && firstTake == 0; ++i) {
        const int before = int(layer.at(site));
        MacroWorld mw{.gs = &gs, .store = &st, .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
        firstTake = before - int(layer.at(site));
    }
    CHECK(firstTake == units,
          "ПЕРВЫЙ АКТ КРЮ СНЯЛ ПО ДЕРЕВУ НА РАБОЧЕГО ЮНИТА — восемь, не одно");
    std::printf("  [акт крю] юнитов %d, первый акт снял %d деревьев из %d\n",
                units, firstTake, kTrees);
}

void test_the_farmer_works_the_field() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    // A field two cells east — where stamp_field_features would put one.
    FeatureLayer& features = gs.features;
    features.resize(kMap, kMap);
    feature_put(features, all_land(), cell_of(12, 10, features.width), FT_Field);

    // The terrain master: fertile land everywhere. Without it the reap is
    // fail-closed (Field Inc F4), so the honest test wires it.
    TerrainData terrain;
    terrain.width = kMap;
    terrain.height = kMap;
    // УРОВНИ, а не байты: карта хранит слово (`kFieldWordMax`), и байтовый
    // литерал 160 означал бы фертильность 0.002 — то есть пустошь, молча.
    terrain.rgba.assign(std::size_t(kMap) * kMap * 4u,
                        sm::field_word_of(128.0f / 255.0f));
    for (std::size_t i = 1; i < terrain.rgba.size(); i += 4) {
        terrain.rgba[i] = sm::field_word_of(160.0f / 255.0f);   // G = fertility
    }

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;

    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    sm::MacroHandle h = sm::store_birth(st);
    sm::MacroHandle e = h;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.runtime[h.slot].sub = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime prt{};
    prt.state = std::uint8_t(NPCState::Idle);
    prt.recoverySteps = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt, make_character_sheet(NPCType::Peasant, 2, avatar_sheet_seed(12u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.squadType = std::uint8_t(SquadType::Ploughmen);
    st.runtime[h.slot] = prt;
    sm::store_rename_ordinal(st, h, 12u);
    h = sm::handle_at(st, h.slot);
    sm::set_relation(st, int(h.id), int(st.spawnId[vil.slot].index),
                     sm::Stance::Parent);
    e = h;   // алиас перецелен ПОСЛЕ пина ординала (M-220)
    // Уровень — АНКЕТА (M-246а): та же дверь, какой рождает make_npc.
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 2, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 20;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st, .terrain = &terrain};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    const int foodUnits = st.inventory[vil.slot].inv.count("food");
    const int inBag = (*sm::body_state<ecs::NpcInventory>(st, e)).inv.count("food");
    CHECK(foodUnits > 0, "the farmer's haul reached the village store");
    // THE BATCH LAW THIS USED TO PIN IS GONE (owner, 2026-09-16). It read
    // `foodUnits % kGatherPerCycle == 0` — "the haul arrives in whole cycle
    // yields" — and it was true only while a take was a declared batch of
    // eight. A take is now ONE object per hand, at exactly the price the
    // player pays for one, and the TRIP emerges from the backs and the bar
    // instead of from a constant. Nothing is pinned in its place because
    // nothing quantised survives: the honest statement left is conservation,
    // below, and it is the one that was load-bearing all along.
    //
    // CONSERVATION (Field Inc F4): every unit that reached anybody left the
    // world — the field's scar is exactly as deep as store PLUS bag. The bag
    // half is new and it is not bookkeeping: the farmer can now be caught
    // mid-trip with the haul on his back, where the old single-take cycle always
    // ended at the door.
    const sm::ResourceGrid& wheatScars = gs.wheatScars;
    CHECK(wheatScars.at(cell_of(12, 10, wheatScars.width)) == foodUnits + inBag,
          "food gained by store AND bag == stands the field lost");
    CHECK(wheatScars.liveCells == 1,
          "the farmer scars only the field he works");
}

void test_farmer_without_terrain_conjures_nothing() {
    // The fail-closed half of the same law (mirrors no-layer-no-chop): an
    // unwired terrain means no ledger to settle against, so NOTHING is
    // gathered — food from thin air died with Field Inc F4.
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    FeatureLayer& features = gs.features;
    features.resize(kMap, kMap);
    feature_put(features, all_land(), cell_of(12, 10, features.width), FT_Field);

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;

    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.runtime[h.slot].sub = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime prt{};
    prt.state = std::uint8_t(NPCState::Idle);
    prt.recoverySteps = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &prt, make_character_sheet(NPCType::Peasant, 2, avatar_sheet_seed(12u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    prt.squadType = std::uint8_t(SquadType::Ploughmen);
    st.runtime[h.slot] = prt;
    sm::store_rename_ordinal(st, h, 12u);
    h = sm::handle_at(st, h.slot);
    sm::set_relation(st, int(h.id), int(st.spawnId[vil.slot].index),
                     sm::Stance::Parent);
    // Уровень — АНКЕТА (M-246а): та же дверь, какой рождает make_npc.
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 2, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 20;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 60u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(st.inventory[vil.slot].inv.count("food") == 0,
          "no terrain wired: nothing to reap against, nothing conjured");
    CHECK(gs.wheatScars.liveCells == 0,
          "no terrain wired: no scar appears either");
}

// ── «ДОМА» — КЛЕТКА ДОМА: У КРЫЛЬЦА НЕ СДАЮТ (M-461) ─────────────────────
// Сдача груза случается в клетке дома — тем же равенством индексов, каким
// ноги судят прибытие («ничего не телепортируется», S5). Пока модуль
// повторял порог ног «ближе двух клеток», прогулка, кончившаяся в соседней с
// домом клетке, выливала сумку на склад через клетку. Обе полярности: у
// крыльца груз остаётся в спине, в клетке дома — уходит на склад (иначе
// первая проверка была бы верна и при сломанной сдаче).
void test_the_haul_is_not_delivered_from_the_porch() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    tree_layer_reset(gs.treeLayer, kMap, kMap);   // леса нет — работы нет

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    const int vilId = int(st.spawnId[vil.slot].index);
    sm::settle_souls(gs, st, vil.slot, 40);
    const sm::MacroHandle wc = make_woodcutter(st, 13.0f, 10.0f, vilId);
    auto bag_wood = [&] {
        return (*sm::body_state<ecs::NpcInventory>(st, wc)).inv.count("wood");
    };
    (*sm::body_state<ecs::NpcInventory>(st, wc)).inv.add("wood", 5);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    auto think = [&] {
        MacroWorld mw{.gs = &gs, .store = &st};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    };
    // Прогулка к клетке у крыльца — та же запись, какой её ставит ветка
    // блуждания артели: адрес и «стать Idle по прибытии».
    auto walk_to = [&](int x, int y, NPCState leg) {
        ecs::goal_set(st.goal[wc.slot], ecs::cell_index(x, y, kMap), 0, 0,
                      std::uint8_t(NPCState::Idle));
        st.runtime[wc.slot].state = std::uint8_t(leg);
        for (int i = 0; i < 40 && st.goal[wc.slot].underway != 0; ++i) think();
    };

    walk_to(11, 10, NPCState::Wandering);
    CHECK(st.cell[wc.slot].idx == ecs::cell_index(11, 10, kMap)
              && st.goal[wc.slot].underway == 0,
          "фикстура: артель ПРИБЫЛА в клетку рядом с домом");
    think();   // модуль читает «я пришёл»
    CHECK(bag_wood() == 5,
          "У КРЫЛЬЦА НЕ СДАЮТ: в соседней с домом клетке груз остался в спине");

    walk_to(10, 10, NPCState::Returning);
    CHECK(st.cell[wc.slot].idx == ecs::cell_index(10, 10, kMap),
          "фикстура: артель прибыла В клетку дома");
    think();
    CHECK(bag_wood() == 0,
          "В КЛЕТКЕ ДОМА — СДАЛ: тот же груз ушёл из спины на склад");
}

void test_no_layer_no_chop() {
    // Without a live tree layer the behaviour must not invent wood — the
    // old walk-to-forest-and-back pantomime, unchanged.
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    const std::vector<TreePoint> trees{{14, 10}};
    TreeGrid grid;
    build_tree_grid(grid, trees, kMap, kMap);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    make_woodcutter(st, 10.0f, 10.0f,
                    int(st.spawnId[vil.slot].index));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st, .treeGrid = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(st.inventory[vil.slot].inv.count("wood") == 0,
          "no layer => no honest wood, and none minted from nothing");
}

} // namespace

// THE SAME MINE, THROUGH THE OTHER DRIVER. There are two macro-AI drivers and
// the game picks between them by WHERE THE PLAYER IS: the budgeted one runs
// while he is in a subworld (main.cpp), the full one while he is on the map.
// The budgeted one was assembling its own copy of the tick context and had
// silently lost one pointer of the seventeen — the deposit layer — so every
// miner, quarryman and clay-digger in the world stopped digging the moment the
// player walked through a door, and resumed when he came out. The world must
// live the same with him and without him (CANON.md S2), so the two drivers
// have to agree about the same mine.
void test_the_mine_runs_while_the_player_is_away() {
    using namespace sm;
    constexpr int kMap = 32;
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    // ЖИЛЫ ЕДИНЫЕ (2026-10-22): рабочее место шахтёра — ШАХТА (фича с
    // numberRow == Vein + залежь числом фичи), не голая геология. Свидетель
    // рожает своё предусловие сам: фича + число фичи, как их ставит генерация.
    FeatureLayer& features = gs.features;
    features.resize(kMap, kMap);
    feature_put(features, all_land(), cell_of(14, 10, features.width), FT_IronMine);
    feature_number_write(gs, cell_of(14, 10, gs.mapW), 20);

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;

    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.runtime[h.slot].sub = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.state = std::uint8_t(NPCState::Idle);
    rt.recoverySteps = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt, make_character_sheet(NPCType::Peasant, 3, avatar_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    rt.squadType = std::uint8_t(SquadType::Miners);
    st.runtime[h.slot] = rt;
    sm::store_rename_ordinal(st, h, 13u);
    h = sm::handle_at(st, h.slot);
    sm::set_relation(st, int(h.id), int(st.spawnId[vil.slot].index),
                     sm::Stance::Parent);
    // Уровень — АНКЕТА (M-246а): та же дверь, какой рождает make_npc.
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st};
        tick_macro_npc_ai(mw, art, kAiTicks);
    }

    // Залежь шахты — число фичи: выработка видна там же, где её оставила
    // транзакция. Что добыто — решила ФИЧА (железная шахта → железо).
    const int veinLeft = feature_number_read(gs, cell_of(14, 10, gs.mapW));
    CHECK(20 - veinLeft > 0,
          "the mine is worked while the player is underground, exactly as it "
          "is worked while he is on the map");
}

// ── ПАМЯТЬ АГЕНТА — СВИДЕТЕЛЬ УМЕР ВМЕСТЕ С ВМЕСТИЛИЩЕМ (2026-10-11) ─────
// Здесь стоял `test_agent_memory_is_bounded_and_current`: он проверял законы
// САМОГО ВМЕСТИЛИЩА — отзыв по ключу (род, субъект, пространство), свёртку
// одного ключа своим законом (долг СУММИРУЕТСЯ), кап слотов и забывание
// старейшего за капом. Проверял честно и на живом роде.
//
// Снят как ОСИРОТЕВШИЙ (AGENTS §5 п.6), а не как плохой: вся `AgentMemory`
// уничтожена вердиктом владельца по переписи сирот — `recall` имел НОЛЬ
// прод-вызовов, то есть НИКТО НИКОГДА НЕ ВСПОМИНАЛ, а писала память одна
// недоплата штрафа квеста. 2.125 МиБ по капу, 99 % мёртвых байт
// макро-состояния. Законы вместилища верны и сегодня — проверять их не на
// чем: вместилища нет. Когда отношения макромира дойдут до долга, память
// заведут заново ТАМ, где её читают (M-206), и свидетель вернётся вместе с
// ней. Из прежнего жильца уцелела одна функция — `stock_class` переехала к
// своему единственному читателю (`src/content/quests/procedural.cpp`).

// ЗДЕСЬ СТОЯЛИ ДВА СВИДЕТЕЛЯ РЕЙСА К РЫНКУ — «крю деревни продаёт в ближайшем
// городе» и «корован без рынка ждёт дома». Оба переехали 2026-11-12 (M-478)
// в `tests/rooted_module_day_test.cpp` и там же сменили предмет: рейс больше
// не «рейс сбыта деревни», который грузит себя сам и покупает то, чего не
// хватает дому, а корован города по замыслу владельца — город рождает и
// грузит, рейс растит стоимость своего трюма и возвращает наторгованное
// домой. Фикстура с рейсом, собранным РУКОЙ и приписанным деревне, описывала
// бы путь, которого в игре нет.

// The miner: шахтёры — свой ТИП со своим модулем (M-488: профессия есть тип
// сквада, а не строка общей таблицы целей), тот же цикл «к месту — акт —
// домой», что у лесорубов выше, написанный в своём модуле целиком.
// Pinned: the ore leaves through the Iron carrier row, hauls home into the
// village store, CONSERVES, the drained vein stays a VISIBLE cell at 0 and
// a dry world gives the miner nothing further; no deposit layer wired = no
// ore conjured (the shared fail-closed rule).
void test_the_miner_works_the_vein() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    // ЖИЛЫ ЕДИНЫЕ (2026-10-22): рабочее место — ШАХТА (фича + залежь числом
    // фичи); что добыто, решает СТРОКА фичи (железная шахта → iron).
    FeatureLayer& features = gs.features;
    features.resize(kMap, kMap);
    feature_put(features, all_land(), cell_of(14, 10, features.width), FT_IronMine);
    feature_number_write(gs, cell_of(14, 10, gs.mapW), 20);

    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;

    // Место есть неподвижный сквад, и ломтиком F оно ЕСТЬ слот store.
    const sm::MacroHandle vil =
        sm::birth_squad(gs, nullptr, st, SquadType::Village, 10, 10);
    const int vilId = int(st.spawnId[vil.slot].index);
    sm::MacroHandle h = sm::store_birth(st);
    sm::MacroHandle e = h;
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st.runtime[h.slot].sub = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.state = std::uint8_t(NPCState::Idle);
    rt.recoverySteps = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt, make_character_sheet(NPCType::Peasant, 3, avatar_sheet_seed(13u)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    rt.squadType = std::uint8_t(SquadType::Miners);
    st.runtime[h.slot] = rt;
    sm::store_rename_ordinal(st, h, 13u);
    h = sm::handle_at(st, h.slot);
    sm::set_relation(st, int(h.id), vilId, sm::Stance::Parent);
    e = h;   // алиас перецелен ПОСЛЕ пина ординала (M-220)
    // Уровень — АНКЕТА (M-246а): та же дверь, какой рождает make_npc.
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;

    MacroNpcAiRuntime art{};
    reset_macro_npc_ai_runtime(art, 70u);
    for (int i = 0; i < 400; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st};
        tick_macro_npc_ai(mw, art, kAiTicks);
    }

    const int veinLeft = feature_number_read(gs, cell_of(14, 10, gs.mapW));
    const int veinLost = 20 - veinLeft;
    const int storeGained = st.inventory[vil.slot].inv.count("iron");
    const int inBag = (*sm::body_state<ecs::NpcInventory>(st, e)).inv.count("iron");
    // ЖЕЛЕЗО, УЖЕ СТАВШЕЕ ВЕЩЬЮ: что и сколько крафтить, решает модуль деревни
    // (ряд M-486), и привезённое железо он вправе пустить в дело. Ведомость
    // материи обязана видеть его и там — та же единая таблица состава, что у
    // дерева выше (`item_parts@src/tables/items.h`), тот же один шаг.
    const int ironIdx = sm::item_index("iron");
    int ironInGoods = 0;
    {
        const auto cat = sm::item_catalog();
        const sm::Inventory& shelf = st.inventory[vil.slot].inv;
        for (int i = 0; i < int(cat.size()); ++i) {
            const int have = shelf.count_of(i);
            if (have <= 0) continue;
            for (const sm::ItemPart& part : sm::item_parts(i))
                if (int(part.def) == ironIdx)
                    ironInGoods += have * int(part.count);
        }
    }

    CHECK(veinLost > 0, "the dig really drained the vein");
    CHECK(storeGained + ironInGoods > 0,
          "the haul reached the village store — сырьём или уже вещью");
    CHECK(veinLost == storeGained + inBag + ironInGoods,
          "CONSERVATION: vein loss == сырьё на складе + сырьё в спине + "
          "железо, уже ставшее вещью");
    // The world ran dry: 20 units at kGatherPerWorkerDay per trip is gone
    // within the 400 thinks — and by the ANNIHILATION law (owner,
    // 2026-08-28) the worked-out vein is a vein that no longer exists: the
    // cell leaves the map, the counter keeps the scarcity baseline, and the
    // chronicle (below) is the only record of what stood here.
    CHECK(veinLeft == 0,
          "the worked-out mine reads 0 — its deposit is the feature's number");
    CHECK(storeGained + inBag + ironInGoods == 20,
          "everything the vein ever held is accounted for");

    // The worked-out vein is a FACT of the world (S20.1): filed ONCE — the
    // transition is the story, thirty daily hauls are weather — by the home
    // village, at the vein's cell, naming WHAT ran dry by its registry row.
    const FactTally drained = tally_facts(gs.chronicle, FactKind::Drained,
                                          14, 10);
    CHECK(drained.n == 1,
          "one dead vein = ONE Drained fact, not one per haul");
    CHECK(drained.last.subject == std::uint32_t(vilId)
              && drained.last.subjectKind
                     == std::uint8_t(FactSubject::Rooted),
          "the fact names the village whose man worked the vein out");
    CHECK(drained.last.x == 14 && drained.last.y == 10,
          "the fact stands on the vein's own cell");
    CHECK(drained.last.amount == int(ResourceFieldId::Vein) + 1,
          "the fact says WHAT ran dry: the resource registry row, +1");

    // No deposit layer wired = no ore conjured (the shared fail-closed rule).
    std::unique_ptr<GameState> gs2Own = make_game_state();
    GameState& gs2 = *gs2Own;
    gs2.mapW = kMap;
    gs2.mapH = kMap;
    auto w2Store_ = sm::make_macro_store();
    sm::MacroStore& st2 = *w2Store_;
    // У ВТОРОГО МИРА — СВОЁ ТЕЛО МЕСТА: место ЕСТЬ слот своего store
    // (ломтик F), и одно тело на два мира невыразимо. Деревня рождается
    // здесь заново, своим эмитентом.
    const sm::MacroHandle vil2 =
        sm::birth_squad(gs2, nullptr, st2, SquadType::Village, 10, 10);
    sm::MacroHandle h2 = sm::store_birth(st2);
    st2.cell[h2.slot] = ecs::MacroCell{ecs::cell_index(10, 10, kMap)};
    st2.runtime[h2.slot].sub = {};   // доля клетки: центр (M-244a)
    st2.kind[h2.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                     std::uint16_t(faction_index("timaert"))};
    st2.runtime[h2.slot] = rt;
    sm::store_rename_ordinal(st2, h2, 14u);
    h2 = sm::handle_at(st2, h2.slot);
    // Уровень — АНКЕТА (M-246а): та же дверь, какой рождает make_npc.
    st2.sheet[h2.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st2.spawnId[h2.slot].index));
    {
        ecs::Pools p2{};
        p2.hp = p2.maxHp = 30;
        p2.sp = p2.maxSp = 100;
        st2.pools[h2.slot] = p2;
    }
    MacroNpcAiRuntime art2{};
    reset_macro_npc_ai_runtime(art2, 71u);
    for (int i = 0; i < 200; ++i) {
        MacroWorld mw2{.gs = &gs2, .store = &st2};
        tick_macro_npc_ai(mw2, art2, kAiTicks);
    }
    CHECK(st2.inventory[vil2.slot].inv.count("iron") == 0,
          "no deposit layer => no honest ore, and none minted from nothing");
}

// ЗДЕСЬ СТОЯЛ `test_the_catch_lands_as_creatures` — «артель ловит стадо с
// клетки загона, улов встаёт ДУШАМИ в её контейнер». Снят ВМЕСТЕ С
// НОСИТЕЛЕМ (§8 п.6, M-488): добычи существ артелью больше нет — модули типов
// добывают только предметы, ловля стада ждёт ряда M-491 (оценка головы-
// существа). Живая половина того свидетеля — «обоз = спина анкеты плюс
// граммы объектов, конь несёт свои 120 кг» — держится ниже, в
// `test_a_hauless_head_does_not_grow_the_train`, на конях, положенных в
// контейнер прибором.

// ── АВТОРСКИЙ НОЛЬ СПИНЫ ЗНАЧИТ НОЛЬ (M-297) ──────────────────────────────
// Закон: обоз = спина АНКЕТЫ + Σ килограммов ОБЪЕКТОВ, и безрукая строка не
// несёт НИЧЕГО. Колонка `haulMult` объявляет это прямо — «ни кролика, ни
// волка, ни ястреба, ни дракона в этом мире не запрягают», — а умолчание
// колонки есть 1.0 (человек), поэтому ветка «если ноль, считай единицей»
// могла перехватить ТОЛЬКО авторский ноль, то есть отменяла вердикт данными.
// До этого свидетеля закон носителя не имел вовсе: ни один тест не спрашивал,
// что обоз НЕ ВЫРОС.
//
// СВИДЕТЕЛЬ РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ САМ (§8 п.11): головы кладёт он, а не
// генератор, — волка в контейнере мир сегодня нигде не раздаёт, и надежда на
// везение сида сделала бы тест красным от невезения, а не от нарушения.
void test_a_hauless_head_does_not_grow_the_train() {
    auto store = sm::make_macro_store();
    sm::MacroStore& st = *store;
    const sm::MacroHandle h = make_woodcutter(st, 10.0f, 10.0f, 0);

    const std::int64_t bare = sm::carry_capacity_g(st, h);
    CHECK(bare > 0,
          "ПРЕДУСЛОВИЕ: у сквада есть своя спина от анкеты (иначе нулём "
          "ниже можно было бы доказать что угодно)");

    // ── БЕЗРУКИЕ: волк и дракон. Ноль есть ЗНАЧЕНИЕ, а не отсутствие ──────
    CHECK(creatures_push_stack(st.inventory[h.slot].inv, NPCType::Wolf,
                               npc_def(NPCType::Wolf).baseLevel, 3),
          "ПРЕДУСЛОВИЕ: три волка встали в контейнер");
    CHECK(creatures_push_stack(st.inventory[h.slot].inv, NPCType::Dragon,
                               npc_def(NPCType::Dragon).baseLevel, 1),
          "ПРЕДУСЛОВИЕ: дракон встал в контейнер");
    CHECK(sm::carry_capacity_g(st, h) == bare,
          "ВОЛК И ДРАКОН ОБОЗ НЕ РАСТЯТ: авторский ноль колонки значит НОЛЬ "
          "килограммов, а не одну человеческую спину");

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ: детектор обязан ВИДЕТЬ рост ─────────────────
    // Без него «равно» выше доказывало бы лишь то, что дверь не зовётся.
    CHECK(creatures_push_stack(st.inventory[h.slot].inv, NPCType::Horse,
                               npc_def(NPCType::Horse).baseLevel, 2),
          "ПРЕДУСЛОВИЕ: два коня встали в тот же контейнер");
    const std::int64_t withHorses = sm::carry_capacity_g(st, h);
    const std::int64_t horseG = std::int64_t(std::lround(
        double(npc_def(NPCType::Horse).haulMult) * double(sm::kHaulBackG)));
    CHECK(withHorses == bare + 2 * horseG,
          "КОНЬ РАСТИТ, И РОВНО НА СВОИ ГРАММЫ: 2 × haulMult × "
          "kHaulBackG, и ни волк, ни дракон рядом не прибавили ничего");
    // И НЕЗАВИСИМЫЙ ЯКОРЬ НА САМО ЧИСЛО (§8 п.6): конь в этом
    // мире вьючный на 120 кг — это сказано выводом самой
    // колонки (`kHaulBackG@src/tables/npc.h`: «вьючный конь ~120»).
    // Тождество выше слепо к ФОРМУЛЕ (обе стороны читают одну
    // и ту же колонку), а это утверждение о МИРЕ и поймает
    // смену кванта, которую тождество пережило бы зелёным.
    CHECK(horseG == 120'000,
          "спина коня есть 120 000 г = 120 кг — независимый "
          "факт мира, а не пересчёт продакшен-формулы");
    CHECK(withHorses > bare,
          "НЕГАТИВНЫЙ КОНТРОЛЬ: прибор отличает рост от его отсутствия");

    // Вклад объекта АБСОЛЮТЕН: он не зависит от анкеты хозяина (ЗАКОН ДВУХ
    // ТАБЛИЦ — у объекта анкеты нет, умножаться ему не на что). Прежняя
    // доля этот же конь оценила бы в `анкета × 8`, то есть в разных
    // контейнерах по-разному.
    const std::int64_t richer = bare * 2;
    st.runtime[h.slot].carryPerSoulG = std::int32_t(richer);
    CHECK(sm::carry_capacity_g(st, h) == richer + 2 * horseG,
          "КИЛОГРАММЫ КОНЯ ОТ БОГАТСТВА ХОЗЯИНА НЕ ЗАВИСЯТ: удвоенная "
          "анкета сдвинула ТОЛЬКО своё слагаемое");

    // ── ШИРИНА `int64` — НЕ ОСТОРОЖНОСТЬ, А РАСЧЁТ, И ВОТ ЕЁ СВИДЕТЕЛЬ ────
    // Обоз складывается в `int64` потому, что у голов нет структурного
    // потолка: `ItemRef::count` — `int32`, слотов 1024, и спина коня 120 000 г.
    // Порог заворота `int32` — 2 147 483 647 / 120 000 ≈ **17 896 голов**, то
    // есть табун, который контейнер держит ОДНИМ стаком. Замеренный город
    // давал 11 774 тела — 66 % диапазона, — значит это не умозрительный край.
    //
    // БЕЗ ЭТОЙ ПРОВЕРКИ ВЫБОР ТИПА БЫЛ БЕЗ НОСИТЕЛЯ: записанная мутация
    // «сузить возврат двери до `int32`» прошла ЗЕЛЁНОЙ по всем 51 проверке
    // этого файла, потому что ни один свидетель не строил табун такого
    // размера (§8 п.6 — зелёная мутация есть дыра свидетеля, а не прочность).
    {
        auto big = sm::make_macro_store();
        sm::MacroStore& bst = *big;
        const sm::MacroHandle bh = make_woodcutter(bst, 20.0f, 20.0f, 0);
        const int herd = 20'000;   // выше порога 17 896
        CHECK(creatures_push_stack(bst.inventory[bh.slot].inv, NPCType::Horse,
                                   npc_def(NPCType::Horse).baseLevel, herd),
              "ПРЕДУСЛОВИЕ: табун из 20 000 коней встал ОДНИМ стаком");
        const std::int64_t huge = sm::carry_capacity_g(bst, bh);
        CHECK(huge > std::int64_t(std::numeric_limits<std::int32_t>::max()),
              "ОБОЗ ПЕРЕВАЛИВАЕТ ЗА `int32` И ОТВЕЧАЕТ ЧЕСТНО: сужение здесь "
              "завернуло бы сумму в ОТРИЦАТЕЛЬНУЮ, то есть табун сделал бы "
              "спину меньше, чем у пустого сквада");
        CHECK(huge == sm::carry_capacity_g(bst, bh)
                  && huge >= std::int64_t(herd) * horseG,
              "и она не меньше суммы спин самого табуна — слагаемое анкеты "
              "только добавляет");
    }

    // ── СВОЯ СПИНА СКВАДА — ЭТО АНКЕТА, И КОЛОНКА СТРОКИ В НЕЁ НЕ ВХОДИТ ──
    // Строка берётся НЕ произвольная: `Dragon` — живой продакшен-случай, три
    // авторских сквада `dragon1/2/3@src/macro/characters.h` рождаются ею, и
    // колонка у неё авторский НОЛЬ. Прежняя дверь множила анкету на колонку,
    // то есть без фоллбэка дала бы дракону спину 0 — вечный перегруз и
    // SP-долг на первом же трофее. Теперь колонки в этом числе нет вовсе.
    {
        const sm::CharacterSheet ds = make_character_sheet(
            NPCType::Dragon, 10, avatar_sheet_seed(5u));
        ecs::Pools dp{};
        ecs::MacroNpcRuntime drt{};
        refresh_body_from_sheet(dp, &drt, ds, NPCType::Dragon);
        CHECK(drt.carryPerSoulG
                  == get_carry_capacity(ds.attributes, ds.skills),
              "СПИНА ДРАКОНА-СКВАДА = ЕГО АНКЕТА: авторский ноль колонки — "
              "про упряжь объекта, а не про то, что тело не несёт ничего");
        CHECK(drt.carryPerSoulG > 0,
              "и она ПОЛОЖИТЕЛЬНА: нулевой обоз означал бы перегруз на "
              "первом килограмме трофея");
    }
}

int main() {
    test_the_chop_is_real_and_the_haul_comes_home();
    test_a_hauless_head_does_not_grow_the_train();
    // Was DEFINED and never CALLED — found the day -Wunused-function came
    // on (С3): a silently never-running test, the exact family check.h's
    // zero-checks rule hunts, hidden one scope deeper.
    test_no_layer_no_chop();
    test_the_haul_is_not_delivered_from_the_porch();
    test_a_crew_takes_one_object_per_unit();
    test_the_farmer_works_the_field();
    test_farmer_without_terrain_conjures_nothing();
    test_the_miner_works_the_vein();
    test_the_mine_runs_while_the_player_is_away();
    return sm::test::report("woodcutter_gather_test");
}

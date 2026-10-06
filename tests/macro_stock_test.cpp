// The ledger between the layers (macro/macro_stock.{h,cpp}).
//
// The rule it serves: the subworld is a CONTEXT of the macro world, so anything
// borrowed down there — a citizen out of a town's population, a tree out of a
// cell's forest — is paid back UP. Before this system each quantity grew its
// own hand-made write-back, trees had one and population had none, and killing
// a town's people left the map still counting them as alive.
//
// So what is asserted here is not "the numbers came out right" but the
// PROPERTIES that make the system a system:
//   * the table is total — every stock has a row, or borrowing it is silent;
//   * borrowing and returning are the same row, so they cannot drift apart;
//   * a stock is bounded — a place can be emptied but never owe people;
//   * a debt names its subject, so one town's dead never bill its neighbour;
//   * a malformed receipt changes nothing (fail closed).
#include "check.h"
#include <memory>
#include "macro/labour.h"   // settle_souls / souls_flock — двери душ

#include "macro/deposit_layer.h"
#include "macro/world_row.h"
#include "macro/macro_stock.h"
#include "macro/landmark_iter.h"  // for_each_place — места по слотам
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "macro/store.h"

#include <entt/entt.hpp>
#include <cstdio>

namespace {

// Мир фикстуры — СТРОКИ ПЛЮС ТЕЛА (M-90 шаг 5): склад места (а в нём —
// домашние головы, которыми сток Population и платит) живёт колонкой
// MacroStore, поэтому store приезжает вместе с GameState.
struct World {
    std::unique_ptr<sm::MacroStore> store;
    sm::GameState gs;
    // ОРДИНАЛ НАЗЫВАЕТ ЭМИТЕНТ, А НЕ ФИКСТУРА (ломтик F): рукописный
    // `Landmark::id` умер со строкой места, поэтому мир держит у себя
    // ординалы своих четырёх — расписка (MacroStockKey) приходит с ними.
    int cityId = 0;
    int otherId = 0;
    int twinId = 0;
    int hamletId = 0;
};

World make_world() {
    World wld{sm::make_macro_store(), sm::GameState{}};
    sm::GameState& gs = wld.gs;
    sm::MacroStore& st = *wld.store;
    gs.mapW = 64;
    gs.mapH = 64;
    {
        const sm::MacroHandle h = sm::birth_place(
            gs, st, sm::SquadType::City, 10, 10, -1, "Testholm");
        wld.cityId = int(st.spawnId[h.slot].index);
        // Души — ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число
        // фичи, головы в инвентарь. Сток `population` читает и пишет ровно
        // эту пару.
        sm::settle_souls(gs, st, h.slot, 300);
    }
    {
        const sm::MacroHandle h = sm::birth_place(
            gs, st, sm::SquadType::City, 30, 30, -1, "Neighbour");
        wld.otherId = int(st.spawnId[h.slot].index);
        sm::settle_souls(gs, st, h.slot, 300);
    }
    // ONE landmark id space (v54): every place draws on the one issuer, so a
    // fixture with two places wearing one number would no longer be a world
    // this game can generate. The collision fixture (village 7 beside city 7)
    // guarded the register-bit crutch; the invariant now is that the id ALONE
    // bills the right place.
    {
        const sm::MacroHandle h = sm::birth_place(
            gs, st, sm::SquadType::Village, 40, 40, -1, "Twinvale");
        wld.twinId = int(st.spawnId[h.slot].index);
        sm::settle_souls(gs, st, h.slot, 80);
    }
    {
        const sm::MacroHandle h = sm::birth_place(
            gs, st, sm::SquadType::Village, 20, 20, -1, "Hamlet");
        wld.hamletId = int(st.spawnId[h.slot].index);
        sm::settle_souls(gs, st, h.slot, 40);
    }
    return wld;
}

// Read each kind on its own so an assertion can say WHICH kind of place paid —
// the ids are unique (v54), but the bill must still land on the right row.
// Род и ординал — КОЛОНКИ ТЕЛА (ломтик F), поэтому оба вопроса задаются
// одному обходу мест.
int population_of_kind(const World& wld, sm::SquadType kind, int id) {
    int found = -1;
    sm::for_each_place(*wld.store, [&](std::uint16_t slot) {
        if (sm::SquadType(wld.store->runtime[slot].squadType) != kind) return;
        if (int(wld.store->spawnId[slot].index) != id) return;
        found = sm::souls_flock(wld.gs, *wld.store, slot);
    });
    return found;
}
int city_population_of(const World& wld, int id) {
    return population_of_kind(wld, sm::SquadType::City, id);
}
int village_population_of(const World& wld, int id) {
    return population_of_kind(wld, sm::SquadType::Village, id);
}
int population_of(const World& wld, int id) {
    const int c = city_population_of(wld, id);
    return c >= 0 ? c : village_population_of(wld, id);
}

// The table must answer for EVERY stock the enum declares. A row that goes
// missing does not crash — it silently stops paying the world back, which is
// the exact failure this system exists to end.
void test_the_table_is_total() {
    using namespace sm;
    int rows = 0, unnamed = 0;
    for (std::uint8_t i = 0; i < std::uint8_t(MacroStock::Count); ++i) {
        ++rows;
        const char* id = macro_stock_id(MacroStock(i));
        if (id == nullptr || id[0] == '\0' || id[0] == '?') ++unnamed;
    }
    CHECK(rows == int(MacroStock::Count) && rows > 0 && unnamed == 0,
          "every declared stock has a named row: the table is the system");
}

// The heart of it: `read` and `write` are two ends of ONE row, so what the
// world lends it can take back, exactly.
void test_borrow_and_return_are_symmetric() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    sm::TreeLayer trees;
    trees.width = gs.mapW;
    trees.height = gs.mapH;
    trees.data.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH), 500);
    MacroWorld w{.gs = &gs, .trees = &trees, .store = wld.store.get()};

    const MacroStockKey town{wld.cityId, 10, 10};
    const MacroStockKey cell{-1, 3, 4};

    const int pop0  = macro_stock_read(w, MacroStock::Population, town);
    const int tree0 = macro_stock_read(w, MacroStock::TreeCount, cell);
    CHECK(pop0 == 300 && tree0 == 500,
          "read reports what the macro world actually holds");

    macro_stock_apply(w, MacroStock::Population, town, -12);
    macro_stock_apply(w, MacroStock::TreeCount,  cell, -30);
    CHECK(macro_stock_read(w, MacroStock::Population, town) == pop0 - 12
              && macro_stock_read(w, MacroStock::TreeCount, cell) == tree0 - 30,
          "spending a stock lowers exactly what was spent");

    macro_stock_apply(w, MacroStock::Population, town, +12);
    macro_stock_apply(w, MacroStock::TreeCount,  cell, +30);
    CHECK(macro_stock_read(w, MacroStock::Population, town) == pop0
              && macro_stock_read(w, MacroStock::TreeCount, cell) == tree0,
          "returning what was borrowed restores the world exactly");
}

// A place can be emptied. It can never owe people, and a cell can never hold
// more forest than a cell is allowed to hold.
void test_stocks_are_bounded() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    sm::TreeLayer trees;
    trees.width = gs.mapW;
    trees.height = gs.mapH;
    trees.data.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH), 10);
    MacroWorld w{.gs = &gs, .trees = &trees, .store = wld.store.get()};

    macro_stock_apply(w, MacroStock::Population,
                      MacroStockKey{wld.cityId, 10, 10}, -100000);
    CHECK(macro_stock_read(w, MacroStock::Population,
                           MacroStockKey{wld.cityId, 10, 10}) == 0,
          "a town can be emptied to zero and never below it");

    macro_stock_apply(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1}, -100000);
    CHECK(macro_stock_read(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1}) == 0,
          "a cell's forest bottoms out at bare ground, not at a negative count");

    macro_stock_apply(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1},
                      kMaxTreesPerCell * 4);
    CHECK(macro_stock_read(w, MacroStock::TreeCount, MacroStockKey{-1, 1, 1})
              == kMaxTreesPerCell,
          "a cell cannot be planted past the densest forest a cell can be");
}

// A receipt names its subject. One town's dead must never be billed to the
// town next door — the failure mode of every "nearest settlement" shortcut.
void test_debts_bill_their_own_subject() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    MacroWorld w{.gs = &gs, .store = wld.store.get()};

    entt::registry reg;
    const auto citizen = reg.create();
    stamp_macro_debt(reg, citizen, MacroStock::Population,
                     MacroStockKey{wld.cityId, 10, 10}, 1);
    const auto* debt = reg.try_get<ecs::MacroDebt>(citizen);
    CHECK_OR_RETURN(debt != nullptr, "stamping leaves a receipt on the body");

    settle_macro_debt(w, *debt, -1);
    CHECK(population_of(wld, wld.cityId) == 299,
          "the dead citizen's own town shrinks by one");
    CHECK(population_of(wld, wld.otherId) == 300,
          "the town next door is untouched");
    CHECK(population_of(wld, wld.hamletId) == 40, "and so is the village");

    // A village is a named place with people too, on the SAME id space (v54):
    // the id alone names it, no register bit rides the receipt.
    const auto villager = reg.create();
    stamp_macro_debt(reg, villager, MacroStock::Population,
                     MacroStockKey{wld.hamletId, 20, 20}, 3);
    settle_macro_debt(w, *reg.try_get<ecs::MacroDebt>(villager), -1);
    CHECK(population_of(wld, wld.hamletId) == 37,
          "a village pays from its own people, by the amount the receipt says");

    // THE NEGATIVE CONTROL for the one-space law: killing two villagers of
    // Twinvale must leave every CITY exactly where the earlier checks
    // left them — the id alone finds the village, never a city.
    const auto twinVillager = reg.create();
    stamp_macro_debt(reg, twinVillager, MacroStock::Population,
                     MacroStockKey{wld.twinId, 40, 40}, 2);
    settle_macro_debt(w, *reg.try_get<ecs::MacroDebt>(twinVillager), -1);
    CHECK(village_population_of(wld, wld.twinId) == 78,
          "a village pays its own dead through the one id space");
    CHECK(city_population_of(wld, wld.cityId) == 299
              && city_population_of(wld, wld.otherId) == 300,
          "and no city is billed for them");

    // Signed both ways (owner's ruling): the same row settles creation.
    settle_macro_debt(w, *reg.try_get<ecs::MacroDebt>(villager), +1);
    CHECK(population_of(wld, wld.hamletId) == 40,
          "handing the borrowed thing back credits the same place");
}

// A receipt that names nothing must change nothing: the system fails closed,
// so a spawner that half-stamps cannot quietly drain a random town.
void test_malformed_receipts_do_nothing() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    MacroWorld w{.gs = &gs, .store = wld.store.get()};
    const int before = population_of(wld, wld.cityId);

    ecs::MacroDebt zeroAmount{std::uint8_t(MacroStock::Population),
                              wld.cityId, 10, 10, 0};
    ecs::MacroDebt unknownStock{std::uint8_t(MacroStock::Count),
                                wld.cityId, 10, 10, 5};
    ecs::MacroDebt noSubject{std::uint8_t(MacroStock::Population), 0, 10, 10, 5};
    ecs::MacroDebt strangerId{std::uint8_t(MacroStock::Population), 9999, 0, 0, 5};

    settle_macro_debt(w, zeroAmount,   -1);
    settle_macro_debt(w, unknownStock, -1);
    settle_macro_debt(w, noSubject,    -1);
    settle_macro_debt(w, strangerId,   -1);
    CHECK(population_of(wld, wld.cityId) == before,
          "a receipt for nothing, for an unknown stock or for nobody moves no stock");

    // And a world with no tree layer at all must not pretend it wrote one.
    MacroWorld headless{.gs = &gs, .store = wld.store.get()};
    macro_stock_apply(headless, MacroStock::TreeCount, MacroStockKey{-1, 0, 0}, -5);
    CHECK(macro_stock_read(headless, MacroStock::TreeCount, MacroStockKey{-1, 0, 0}) == 0,
          "a missing tree layer reads zero and swallows writes instead of crashing");
}

// ── The creature row: a squad's members are a stock like anyone else ─────────

// One squad the way the overworld shapes them: the entity IS the leader, the
// ordinal is its save-stable name, the creatures holds everyone else.
sm::MacroHandle make_squad(sm::ecs::World& w, std::uint32_t ordinal,
                           std::initializer_list<std::uint32_t> memberIds) {
    sm::MacroStore& st = sm::store_of(w);
    const sm::MacroHandle h = sm::store_birth(st);
    st.spawnId[h.slot] = sm::ecs::MacroSpawnId{ordinal};
    auto& bag = st.inventory[h.slot];
    for (std::uint32_t id : memberIds) {
        sm::creatures_push(bag.inv, sm::make_soldier(
            std::uint8_t(sm::NPCType::Guard), 2, id));
    }
    return h;
}

// A member's death removes the very soldier who fell — named by the receipt,
// never "one of them" — and only from its own squad.
void test_the_creature_row_pays_by_name() {
    using namespace sm;
    ecs::World world;
    auto worldStore_ = sm::make_macro_store();
    sm::store_attach(world, worldStore_.get());
    make_squad(world, 5, {11u, 22u, 0x80000021u});   // high-bit id: a garrison-
    const auto other = make_squad(world, 6, {77u});  // born soldier's shape
    MacroWorld w{.world = &world, .store = &sm::store_of(world)};

    const MacroStockKey member11{5, 0, 0, 11};
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 3,
          "read reports how many members the squad actually holds");

    macro_stock_apply(w, MacroStock::Creatures, member11, -1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 2,
          "a death removes exactly one member");
    macro_stock_apply(w, MacroStock::Creatures, member11, -1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 2,
          "the same man cannot fall twice: a spent receipt moves nothing");

    // Ids are bit patterns: the high bit (garrison id space) must round-trip
    // through the signed detail field intact.
    macro_stock_apply(w, MacroStock::Creatures,
                      MacroStockKey{5, 0, 0, std::int32_t(0x80000021u)}, -1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 1,
          "a high-bit entityId names its member exactly");

    CHECK(macro_stock_read(w, MacroStock::Creatures, MacroStockKey{6, 0, 0}) == 1,
          "the squad next door never pays for this one's dead");

    // Fail closed, like every malformed receipt in this table.
    macro_stock_apply(w, MacroStock::Creatures, MacroStockKey{5, 0, 0, -1}, -1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 1,
          "a nameless death removes nobody: the receipt names its member");
    macro_stock_apply(w, MacroStock::Creatures, MacroStockKey{5, 0, 0, 22}, +1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 1,
          "a bare positive delta conjures nobody: recruitment brings real rows");
    macro_stock_apply(w, MacroStock::Creatures, MacroStockKey{999, 0, 0, 22}, -1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 1
              && macro_stock_read(w, MacroStock::Creatures,
                                  MacroStockKey{6, 0, 0}) == 1,
          "a receipt against an unknown squad moves no creatures anywhere");

    // The full settle path — the same door a subworld death actually uses.
    entt::registry& reg = world.reg;
    const auto body = reg.create();
    stamp_macro_debt(reg, body, MacroStock::Creatures,
                     MacroStockKey{5, 0, 0, 22}, 1);
    const auto* debt = reg.try_get<ecs::MacroDebt>(body);
    CHECK_OR_RETURN(debt != nullptr && debt->detail == 22,
                    "the receipt carries the member's name to the grave");
    settle_macro_debt(w, *debt, -1);
    CHECK(macro_stock_read(w, MacroStock::Creatures, member11) == 0,
          "settling the receipt removes the named member");
    (void)other;
}

// ПАВШИЙ СКВАД ГИБНЕТ ЦЕЛИКОМ, И СУДЬБА У ВСЕХ ОДНА (M-228, вердикт
// владельца 2026-10-06: «тупо уничтожение… не важно кто умер и умер всё
// никаких»; закон назван ещё 2026-09-21 — CANON S9 п.6 «при бое убитый сквад
// не должен идти в дезертиры он погибает»).
//
// Здесь стоял свидетель ОБРАТНОГО закона — «уцелевшие падают в пул
// дезертиров». У него умер НОСИТЕЛЬ (AGENTS §5 п.6), и подгонять его было
// нельзя: он охранял второй источник пула, которого по канону не существует.
//
// Пинится ровно то, что РАЗЛИЧАЕТ новый закон от старого, и обе половины:
//   · гибнут ВСЕ головы, включая зверя — ветки по роду убитого нет ни одной;
//   · ведомость склада душ считает ПАСТВУ, поэтому зверь в её число не
//     входит; это не ветка судьбы, а единица учёта (econ_day.h SoulsKilled).
// «Пул не вырос» — негативный контроль сноса: верни слив, и он краснеет.
void test_the_fallen_squads_creatures_die() {
    using namespace sm;
    ecs::World world;
    auto worldStore_ = sm::make_macro_store();
    sm::store_attach(world, worldStore_.get());
    const auto fallen = make_squad(world, 10, {1u, 2u});
    // ЗВЕРЬ В ТОМ ЖЕ КОНТЕЙНЕРЕ: свидетель рождает своё предусловие сам
    // (AGENTS §8 п.11) — без него «одна судьба на всех» нечем проверить.
    sm::creatures_push_stack(sm::store_of(world).inventory[fallen.slot].inv,
                             NPCType::Horse, 1, 1);
    make_squad(world, 11, {3u});
    sm::macro_mark_dead(sm::store_of(world), fallen);

    // ДВЕРЬ ТРЕБУЕТ МИР (v122), и это не удобство, а закон: погибшая душа
    // ПОКИНУЛА паству своего дома, и число фичи обязано упасть
    // (leave_home_flock). GameState на КУЧЕ: он ~0.84 МиБ (грабля ломтика C).
    auto gsp = std::make_unique<sm::GameState>();
    sm::GameState& gs = *gsp;
    Inventory& pool = gs.deserterPool;

    struct Tally { int souls = 0; int reports = 0; } tally;
    const EconFactSink sink = [](void* u, const EconFact& f) {
        if (f.kind != EconFact::Kind::SoulsKilled) return;
        auto* t = static_cast<Tally*>(u);
        t->souls += f.amount;
        ++t->reports;
    };

    CHECK(kill_fallen_squad_creatures(sm::store_of(world), gs, sink, &tally)
              == 3,
          "гибнут ВСЕ головы павшего — два человека И конь, без ветки по роду");
    CHECK(creature_count(pool) == 0,
          "НЕГАТИВНЫЙ КОНТРОЛЬ (M-228): павшие НЕ дезертируют — пул не вырос "
          "ни на одну голову");
    CHECK(tally.souls == 2 && tally.reports == 1,
          "ведомость склада душ назвала смерть ОДНИМ фактом и в ДУШАХ: конь "
          "паствой не был");
    MacroWorld w{.world = &world, .store = &sm::store_of(world)};
    CHECK(macro_stock_read(w, MacroStock::Creatures, MacroStockKey{10, 0, 0}) == 0,
          "контейнер павшего пуст: платить второй раз не из чего");
    CHECK(macro_stock_read(w, MacroStock::Creatures, MacroStockKey{11, 0, 0}) == 1,
          "a live leader keeps his men");

    CHECK(kill_fallen_squad_creatures(sm::store_of(world), gs, sink, &tally)
                  == 0
              && tally.reports == 1,
          "второй проход не убивает никого и НЕ докладывает: мёртвых дважды "
          "не считают");
}

// ЗАПИСЬ ПАВШЕГО ЖИВЁТ РОВНО СТОЛЬКО, СКОЛЬКО АРЕНДА ОКНА (M-226, AGENTS
// ЗАКОН ШВА: девять клеток ведёт субмир один, и жать их записи наверху
// значило бы завести второго писателя).
//
// ПОЧЕМУ ЭТО ВАЖНО ЧИСЛОМ, А НЕ ПО ЗАМЫСЛУ: со смертью существ (M-228) гейт
// `creatures_empty` перестал держать слот, и запись павшего стала жить ОДИН
// макро-тик — подобрать с трупа было нечего. Гейт аренды и есть срок жизни
// трупа, а конец аренды (подъём ИЛИ ре-центр рамки, оба равноправны) — тот
// момент, когда неподобранное сворачивается в казну.
//
// Свидетель судит ОБЕ полярности одного числа и рождает своё предусловие
// сам (AGENTS §8 п.11): аренда сдаётся дверью мира.
void test_the_lease_holds_the_dead_record_against_the_sweep() {
    using namespace sm;
    ecs::World world;
    auto worldStore_ = sm::make_macro_store();
    sm::store_attach(world, worldStore_.get());
    auto gsp = std::make_unique<sm::GameState>();
    sm::GameState& gs = *gsp;
    gs.mapW = gs.mapH = 64;

    // Два павших с ПУСТЫМ контейнером существ — то есть ровно те, кого свип
    // жнёт: один внутри арендованной рамки, один за ней.
    const auto inside  = make_squad(world, 40, {});
    const auto outside = make_squad(world, 41, {});
    sm::MacroStore& st = sm::store_of(world);
    st.cell[inside.slot]  = ecs::MacroCell{cell_of(10, 10, gs.mapW)};
    st.cell[outside.slot] = ecs::MacroCell{cell_of(20, 20, gs.mapW)};
    sm::macro_mark_dead(st, inside);
    sm::macro_mark_dead(st, outside);

    // Рамка стоит на соседе клетки «внутри» — значит сама клетка в аренде
    // через `cell_step`, а не потому, что совпала с центром.
    sm::lease_window_at(gs, cell_of(11, 10, gs.mapW));
    CHECK(cell_is_leased(gs, st.cell[inside.slot].idx)
              && !cell_is_leased(gs, st.cell[outside.slot].idx),
          "фикстура: одна запись в арендованной рамке, другая за ней");

    CHECK(destroy_dead_macro_squads(st, gs) == 1,
          "свип взял РОВНО одну запись: арендованную он не трогает");
    CHECK(st.valid(inside) && !st.valid(outside),
          "труп в окне дожил до подбора, труп за рамкой сжат как всегда");

    // ── ВТОРАЯ ПОЛЯРНОСТЬ: КОНЕЦ АРЕНДЫ И ЕСТЬ МОМЕНТ ЖАТВЫ ─────────────
    sm::release_window(gs);
    CHECK(destroy_dead_macro_squads(st, gs) == 1 && !st.valid(inside),
          "аренда снята — та же запись сжата тем же свипом, без второго "
          "закона жизни записи и без сезонного чистильщика");
}

// The Trees row is a CARRIER row (resource_field.h): its live state is the
// dense grid the map renders, not a sparse scar map. The discipline that
// makes that safe for the 21 direct grid readers: every registry write lands
// in the grid AND moves its revision (the u_treeMap refresh driver), and the
// scar slot for Trees stays EMPTY — a scar there would be a second, silently
// diverging copy of the forest.
void test_trees_are_a_carrier_row() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    sm::TreeLayer trees;
    trees.width = gs.mapW;
    trees.height = gs.mapH;
    trees.data.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH), 500);
    // Real terrain (dry land, meadow climate), so the growth walk below
    // actually RUNS (it is fail-closed without terrain, and a walk that
    // never ran proves nothing).
    sm::TerrainData td;
    td.width = gs.mapW;
    td.height = gs.mapH;
    // УРОВНИ, а не байты: карта хранит слово (`kFieldWordMax`), и байтовый
    // литерал 180 означал бы в ней 0.003 — то есть ВОДУ, молча. Уровни те же,
    // что несли прежние байты: 128→0.502 климат, 180→0.706 высота.
    td.rgba.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH) * 4u,
                   sm::field_word_of(128.0f / 255.0f));
    for (std::size_t i = 0; i < td.rgba.size(); i += 4) {
        td.rgba[i + 0] = sm::field_word_of(180.0f / 255.0f);   // height: land
        td.rgba[i + 3] = std::uint16_t(sm::kFieldWordMax);     // mask: land
    }
    // РОЖДЕНИЕ КАРТЫ КОНЧАЕТСЯ ВЫПЕЧКОЙ ПОЛЯ БИОМА (ЗАКОН ПОЛЯ): живой мир
    // читает поле, а не каскад, поэтому карта без выпечки — карта
    // НЕДОРОЖДЁННАЯ, и её биом честно отвечает водой.
    sm::bake_biomes(td);
    MacroWorld w{.gs = &gs, .trees = &trees, .store = wld.store.get(),
                 .terrain = &td};

    const std::uint32_t rev0 = trees.revision;
    resource_field_apply(w, ResourceFieldId::Trees, 5, 6, -123);
    CHECK(int(trees.at(5, 6)) == 377,
          "a registry write lands in the very grid the renderer draws");
    CHECK(resource_field_read(w, ResourceFieldId::Trees, 5, 6) == 377,
          "the registry reads the same grid back");
    CHECK(trees.revision == rev0 + 1,
          "a registry write moves the grid revision (u_treeMap refresh)");
    CHECK(gs.resourceScarCells[std::size_t(ResourceFieldId::Trees)].liveCells == 0,
          "the Trees scar slot stays EMPTY - the grid is the only state");
    CHECK(resource_field_scar(gs, ResourceFieldId::Trees, 6u * 64u + 5u) == 0,
          "a carrier row reports no scar");

    // Inc C: the growth law is live. The felled cell sits among live
    // forest (neighbours at 500), so its due slice day births trees back —
    // through the SAME carrier door (grid + revision), never a scar.
    const int before = int(trees.at(5, 6));
    const std::uint32_t cellIdx = 6u * 64u + 5u;
    const int dueDay =
        int(cellIdx % std::uint32_t(kGrowthEpochDays)) + kGrowthEpochDays;
    resource_fields_daily_growth(w, dueDay);
    CHECK(int(trees.at(5, 6)) > before,
          "a due visit among live forest births trees into the grid");
    CHECK(gs.resourceScarCells[std::size_t(ResourceFieldId::Trees)].liveCells == 0,
          "growth writes the carrier, never a scar");
}

// The deposit rows (Clay/Iron/Stone) are carrier rows too: one registry
// door, per-kind maps underneath. What must hold: a write lands in ITS kind
// and no other, an absent vein refuses the write (mining invents no
// geology), and the scar slots stay empty.
void test_deposits_are_carrier_rows() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    sm::DepositLayer deposits;
    allocate_deposit_fields(deposits, gs.mapW, gs.mapH);
    deposits.grid(DepositKind::Stone).write(9, 9, 1000);
    deposits.grid(DepositKind::Iron).write(9, 9, 64);   // a vein IN the quarry
    MacroWorld w{.gs = &gs, .store = wld.store.get(), .deposits = &deposits};

    CHECK(resource_field_read(w, ResourceFieldId::Stone, 9, 9) == 1000
              && resource_field_read(w, ResourceFieldId::Iron, 9, 9) == 64,
          "each kind's row reads its own map of the shared cell");

    resource_field_apply(w, ResourceFieldId::Iron, 9, 9, -60);
    CHECK(resource_field_read(w, ResourceFieldId::Iron, 9, 9) == 4
              && resource_field_read(w, ResourceFieldId::Stone, 9, 9) == 1000,
          "mining one kind never bleeds into the other kind's map");

    resource_field_apply(w, ResourceFieldId::Clay, 9, 9, +500);
    CHECK(resource_field_read(w, ResourceFieldId::Clay, 9, 9) == 0
              && deposits.grid(DepositKind::Clay).liveCells == 0,
          "a kind the cell does not hold refuses the write: mining invents "
          "no geology");

    resource_field_apply(w, ResourceFieldId::Iron, 9, 9, -100);
    CHECK(resource_field_read(w, ResourceFieldId::Iron, 9, 9) == 0
              && deposits.grid(DepositKind::Iron).at(9, 9) == 0,
          "an over-drained vein is ANNIHILATED - no dead cell lingers (v55)");

    for (std::size_t f : {std::size_t(ResourceFieldId::Clay),
                          std::size_t(ResourceFieldId::Iron),
                          std::size_t(ResourceFieldId::Stone)}) {
        CHECK(!gs.resourceScarCells[f].live(),
              "a deposit row pays for NO scar field - its carrier cells are "
              "the only state (the registry's own rule)");
    }
}

} // namespace

int main() {
    test_the_table_is_total();
    test_borrow_and_return_are_symmetric();
    test_stocks_are_bounded();
    test_debts_bill_their_own_subject();
    test_malformed_receipts_do_nothing();
    test_the_creature_row_pays_by_name();
    test_the_fallen_squads_creatures_die();
    test_the_lease_holds_the_dead_record_against_the_sweep();
    test_trees_are_a_carrier_row();
    test_deposits_are_carrier_rows();
    return sm::test::report("macro_stock_test");
}

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
#include "macro/currency.h"      // barter_swap — сделка идёт ТЕМ ЖЕ законом
#include "macro/macro_stock.h"
#include "scene_objects_fixture.h"  // арена: квитанция — колонка слота (ломоть 4)
#include "sub/spawn.h"            // stamp_macro_debt — дверь штампа (ломоть 4)
#include "macro/landmark_iter.h"  // for_each_place — места по слотам
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "macro/store.h"

#include <cstdio>

namespace {

// РАСПИСКА ПРИМЕНЯЕТСЯ ТАК ЖЕ, КАК ЕЁ ПРИМЕНЯЕТ ДЕЛЬТА (писатель строки —
// push_stock_op@src/sub/engine.cpp, те же гейты и тот же ключ): дверь
// settle_macro_debt умерла 2026-10-23 (0 прод-звонящих, ломоть 1 M-72), а
// ЗАКОН расписки — один ключ, негодная строка не меняет ничего — жив у
// дельты, и ровно его этот хелпер держит под свидетелями ниже.
inline void settle_like_the_delta(sm::MacroWorld& w,
                                  const sm::ecs::MacroDebt& d, int sign) {
    if (d.stock >= std::uint8_t(sm::MacroStock::Count) || d.amount == 0)
        return;
    sm::macro_stock_apply(w, sm::MacroStock(d.stock),
                          sm::MacroStockKey{d.subject, d.cellX, d.cellY,
                                            d.detail, d.detailKind,
                                            d.detailLevel},
                          sign * int(d.amount));
}

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

    // Арена — носитель квитанции (ломоть 4: колонка debt слота), и она НА
    // ТЕСТ, а не на реестр (ломоть E): 38.3 МиБ умирают с этим кадром.
    auto arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena_;
    const int sCitizen = sm::test::give_body(objs);   // тело = слот (ломоть D)
    sub::stamp_macro_debt(objs, sCitizen,
                          MacroStock::Population,
                          MacroStockKey{wld.cityId, 10, 10}, 1);
    const auto* debt = sub::slot_debt(objs, sCitizen);
    CHECK_OR_RETURN(debt != nullptr, "stamping leaves a receipt on the body");

    settle_like_the_delta(w, *debt, -1);
    CHECK(population_of(wld, wld.cityId) == 299,
          "the dead citizen's own town shrinks by one");
    CHECK(population_of(wld, wld.otherId) == 300,
          "the town next door is untouched");
    CHECK(population_of(wld, wld.hamletId) == 40, "and so is the village");

    // A village is a named place with people too, on the SAME id space (v54):
    // the id alone names it, no register bit rides the receipt.
    const int sVillager = sm::test::give_body(objs);
    sub::stamp_macro_debt(objs, sVillager,
                          MacroStock::Population,
                          MacroStockKey{wld.hamletId, 20, 20}, 3);
    settle_like_the_delta(w, *sub::slot_debt(objs, sVillager), -1);
    CHECK(population_of(wld, wld.hamletId) == 37,
          "a village pays from its own people, by the amount the receipt says");

    // THE NEGATIVE CONTROL for the one-space law: killing two villagers of
    // Twinvale must leave every CITY exactly where the earlier checks
    // left them — the id alone finds the village, never a city.
    const int sTwinVillager = sm::test::give_body(objs);
    sub::stamp_macro_debt(objs, sTwinVillager,
                          MacroStock::Population,
                          MacroStockKey{wld.twinId, 40, 40}, 2);
    settle_like_the_delta(w, *sub::slot_debt(objs, sTwinVillager), -1);
    CHECK(village_population_of(wld, wld.twinId) == 78,
          "a village pays its own dead through the one id space");
    CHECK(city_population_of(wld, wld.cityId) == 299
              && city_population_of(wld, wld.otherId) == 300,
          "and no city is billed for them");

    // Signed both ways (owner's ruling): the same row settles creation.
    settle_like_the_delta(w, *sub::slot_debt(objs, sVillager), +1);
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

    settle_like_the_delta(w, zeroAmount,   -1);
    settle_like_the_delta(w, unknownStock, -1);
    settle_like_the_delta(w, noSubject,    -1);
    settle_like_the_delta(w, strangerId,   -1);
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
sm::MacroHandle make_squad(sm::MacroStore& st, std::uint32_t ordinal,
                           std::initializer_list<std::uint32_t> memberIds) {
    sm::MacroHandle h = sm::store_birth(st);
    sm::store_rename_ordinal(st, h, ordinal);
    h = sm::handle_at(st, h.slot);
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
    auto worldStore_ = sm::make_macro_store();
    make_squad(*worldStore_, 5, {11u, 22u, 0x80000021u});   // high-bit id: a garrison-
    const auto other = make_squad(*worldStore_, 6, {77u});  // born soldier's shape
    MacroWorld w{.store = worldStore_.get()};

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
    auto arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena_;
    const int sBody = sm::test::give_body(objs);   // тело = слот (ломоть D)
    sub::stamp_macro_debt(objs, sBody,
                          MacroStock::Creatures,
                          MacroStockKey{5, 0, 0, 22}, 1);
    const auto* debt = sub::slot_debt(objs, sBody);
    CHECK_OR_RETURN(debt != nullptr && debt->detail == 22,
                    "the receipt carries the member's name to the grave");
    settle_like_the_delta(w, *debt, -1);
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
    auto worldStore_ = sm::make_macro_store();
    const auto fallen = make_squad(*worldStore_, 10, {1u, 2u});
    // ЗВЕРЬ В ТОМ ЖЕ КОНТЕЙНЕРЕ: свидетель рождает своё предусловие сам
    // (AGENTS §8 п.11) — без него «одна судьба на всех» нечем проверить.
    sm::creatures_push_stack(worldStore_->inventory[fallen.slot].inv,
                             NPCType::Horse, 1, 1);
    make_squad(*worldStore_, 11, {3u});
    sm::macro_mark_dead(*worldStore_, fallen);

    // ДВЕРЬ ТРЕБУЕТ МИР (v122), и это не удобство, а закон: погибшая душа
    // ПОКИНУЛА паству своего дома, и число фичи обязано упасть
    // (leave_home_flock). GameState на КУЧЕ: он ~0.84 МиБ (грабля ломтика C).
    auto gsp = std::make_unique<sm::GameState>();
    sm::GameState& gs = *gsp;

    struct Tally { int souls = 0; int reports = 0; } tally;
    const EconFactSink sink = [](void* u, const EconFact& f) {
        if (f.kind != EconFact::Kind::SoulsKilled) return;
        auto* t = static_cast<Tally*>(u);
        t->souls += f.amount;
        ++t->reports;
    };

    CHECK(kill_fallen_squad_creatures(*worldStore_, gs, sink, &tally)
              == 3,
          "гибнут ВСЕ головы павшего — два человека И конь, без ветки по роду");
    // (НЕГАТИВНЫЙ КОНТРОЛЬ M-228 «павшие НЕ дезертируют — пул не вырос ни на
    // одну голову» потерял НОСИТЕЛЯ: пул дезертиров уничтожен 2026-10-11.
    // `CHECK(true, …)` на его место не ставим — это проверка, которая не
    // может упасть никогда (§8 п.6). Закон держит утверждение ниже: контейнер
    // павшего ПУСТ, то есть головы никуда не переехали.)
    CHECK(tally.souls == 2 && tally.reports == 1,
          "ведомость склада душ назвала смерть ОДНИМ фактом и в ДУШАХ: конь "
          "паствой не был");
    MacroWorld w{.store = worldStore_.get()};
    CHECK(macro_stock_read(w, MacroStock::Creatures, MacroStockKey{10, 0, 0}) == 0,
          "контейнер павшего пуст: платить второй раз не из чего");
    CHECK(macro_stock_read(w, MacroStock::Creatures, MacroStockKey{11, 0, 0}) == 1,
          "a live leader keeps his men");

    CHECK(kill_fallen_squad_creatures(*worldStore_, gs, sink, &tally)
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
    auto worldStore_ = sm::make_macro_store();
    auto gsp = std::make_unique<sm::GameState>();
    sm::GameState& gs = *gsp;
    gs.mapW = gs.mapH = 64;

    // Два павших с ПУСТЫМ контейнером существ — то есть ровно те, кого свип
    // жнёт: один внутри арендованной рамки, один за ней.
    const auto inside  = make_squad(*worldStore_, 40, {});
    const auto outside = make_squad(*worldStore_, 41, {});
    sm::MacroStore& st = *worldStore_;
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
    field_move(w, ResourceFieldId::Trees, 5, 6, -123);
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

// РЯД ЖИЛЫ — НОСИТЕЛЬ С ДВУМЯ ЧУЖИМИ КОНТЕЙНЕРАМИ (жилы единые, 2026-10-22):
// дикая жила — производный грид (инертен для записи: добыча существует по
// шахтам), залежь шахты — число фичи worked под numberRow == Vein. Что
// держится здесь: чтение различает оба носителя колонкой строки фичи, запись
// берёт только залежь, шрамового поля ряд не заводит.
// (Прежняя посылка прибора — «роды различимы в одной клетке» — снята
// вердиктом: рода у жилы нет, прибор переписан, не подогнан.)
void test_vein_is_a_two_carrier_row() {
    using namespace sm;
    World wld = make_world();
    sm::GameState& gs = wld.gs;
    sm::DepositLayer deposits;
    allocate_deposit_fields(deposits, gs.mapW, gs.mapH);
    create_deposit(deposits, 9, 9, 1000);   // дикая жила
    FeatureLayer features;
    features.resize(gs.mapW, gs.mapH);
    MacroWorld w{.gs = &gs, .store = wld.store.get(),
                 .deposits = &deposits, .features = &features};

    CHECK(resource_field_read(w, ResourceFieldId::Vein, 9, 9) == 1000,
          "без шахты ряд читает дикую жилу — геологию видно");
    CHECK(field_move(w, ResourceFieldId::Vein, 9, 9, -60) == 0
              && deposits.vein_at(9, 9) == 1000,
          "дикая жила инертна: добыча существует по шахтам");

    {   // Шахта — дверью рода фичи (стена ломтя 1): суша под дверью.
        TerrainData td;
        td.width = gs.mapW;
        td.height = gs.mapH;
        td.rgba.assign(std::size_t(gs.mapW) * std::size_t(gs.mapH) * 4u, 0);
        td.seaLevel16 = field_word_of(kDefaultSeaLevel);
        for (std::size_t c = 0;
             c < std::size_t(gs.mapW) * std::size_t(gs.mapH); ++c)
            td.rgba[c * 4u + 0] = field_word_of(140.0f / 255.0f);
        feature_put(features, td, 9, 9, FT_Quarry);
    }
    worked_write(gs, 9, 9, 500);   // залежь, свёрнутая генерацией
    CHECK(resource_field_read(w, ResourceFieldId::Vein, 9, 9) == 500,
          "под шахтой ряд читает ЗАЛЕЖЬ — число фичи, не геологию");
    CHECK(field_move(w, ResourceFieldId::Vein, 9, 9, -60) == -60
              && worked_read(gs, 9, 9) == 440,
          "выемка движет залежь и отвечает фактом");
    CHECK(deposits.vein_at(9, 9) == 1000,
          "и не трогает природное поле — метал не убывает");
    CHECK(field_move(w, ResourceFieldId::Vein, 9, 9, -1000) == -440
              && worked_read(gs, 9, 9) == 0,
          "взять больше, чем лежит, невыразимо: факт — остаток");

    CHECK(!gs.resourceScarCells[std::size_t(ResourceFieldId::Vein)].live(),
          "a vein row pays for NO scar field - its carriers are "
          "the only state (the registry's own rule)");
}

// ── СДЕЛКА С СУЩЕСТВОМ ПРОХОДИТ ДВЕРЬ ПАСТВЫ, И В ОБЕ СТОРОНЫ (M-292,
// ЗАКОН КОНТЕЙНЕРА п.3) ──────────────────────────────────────────────────
//
// ЧТО ОХРАНЯЕТСЯ. У «душа покинула место» ДВА носителя: голова в контейнере
// и число фичи (паства). Писать их порознь запрещено — именно это однажды
// чинили у найма, и прилавок повторил бы дефект буквально: место продолжало
// бы кормить, растить и облагать данью проданного человека.
//
// ПОЧЕМУ ЭТОТ СВИДЕТЕЛЬ ВООБЩЕ НУЖЕН, ХОТЯ ПРАВКА ВЫГЛЯДИТ ИНТЕРФЕЙСНОЙ:
// ctest НЕ МОЖЕТ увидеть мёртвый UI-путь (§8 п.6, пятая слепота), и кадр
// тоже — сдвиг числа паствы на кадре не виден. Но дверь паствы живёт в
// СИМУЛЯЦИИ, а не в представлении, и вот её и судим: прилавок зовёт ровно
// `flock_left_home`, ту же дверь, что вкладка Recruit.
//
// ИЗМЕРЕНИЕ, А НЕ НАМЕРЕНИЕ: сделка вправе перевезти МЕНЬШЕ запрошенного
// (кап контейнера), поэтому паства двигается на «было минус стало», и этим
// же одним выражением покрывается ОБРАТНАЯ сделка — голова, отданная месту.
void test_a_creature_deal_moves_the_flock_both_ways() {
    using namespace sm;
    auto gsp = std::make_unique<sm::GameState>();
    sm::GameState& gs = *gsp;
    gs.mapW = gs.mapH = 64;
    auto storePtr = sm::make_macro_store();
    sm::MacroStore& st = *storePtr;

    // Свидетель рождает своё предусловие сам (§8 п.11): место и его паства —
    // дверями мира, а не присвоением колонки.
    const MacroHandle town = sm::birth_place(gs, st, SquadType::City, 12, 9);
    const int kSouls = 40;
    CHECK(sm::settle_souls(gs, st, town.slot, kSouls) == kSouls,
          "паства встала дверью мира целиком");
    Inventory& shelf = st.inventory[town.slot].inv;
    CHECK(souls_flock(gs, st, town.slot) == kSouls
              && souls_home(st, town.slot) == kSouls,
          "оба носителя души согласны ДО сделки: дома столько же, сколько в "
          "пастве");

    // ── ИГРОК ПОКУПАЕТ ГОЛОВЫ ──────────────────────────────────────────
    Inventory playerBag;
    const int headSlot = shelf.creature_first();
    CHECK(headSlot < kMaxInventorySlots, "головы лежат областью сверху");
    const std::uint16_t headRow = shelf.slots[std::size_t(headSlot)].def;
    CHECK(value_of(shelf.slots[std::size_t(headSlot)]) > 0,
          "и прилавок видит их ценой — на нуле этой цены стоял весь дефект");

    const int kBought = 7;
    int folkBefore = count_human_souls(shelf);
    CHECK(barter_swap(playerBag, shelf, {},
                      {BarterLine{headSlot, kBought, headRow}}),
          "сделка настала ТЕМ ЖЕ barter_swap, второго прилавка нет");
    CHECK(flock_left_home(gs, st, town.slot, folkBefore) == kBought,
          "дверь паствы назвала ИЗМЕРЕННЫЙ факт, а не намерение");
    CHECK(souls_flock(gs, st, town.slot) == kSouls - kBought,
          "паства места упала РОВНО на число ушедших голов: иначе город "
          "кормит и облагает данью проданного");
    CHECK(souls_home(st, town.slot) == kSouls - kBought,
          "и оба носителя снова согласны");
    CHECK(creature_count(playerBag) == kBought,
          "а головы честно у покупателя");

    // ── ИГРОК ОТДАЁТ ИХ ОБРАТНО ────────────────────────────────────────
    // Прилавок двусторонен по построению, и без этой половины «в поле» =
    // паства - дома ушло бы в МИНУС, то есть мир соврал бы о том, где его
    // люди. Ветки на это нет: то же измерение, другой знак.
    const int backSlot = playerBag.creature_first();
    const int kGivenBack = 4;
    folkBefore = count_human_souls(shelf);
    CHECK(barter_swap(playerBag, shelf,
                      {BarterLine{backSlot, kGivenBack, headRow}}, {}),
          "обратная сделка проходит тем же законом");
    CHECK(flock_left_home(gs, st, town.slot, folkBefore) == -kGivenBack,
          "дельта ЗНАКОВАЯ: пришло — значит минус ушло");
    CHECK(souls_flock(gs, st, town.slot) == kSouls - kBought + kGivenBack,
          "паства выросла ровно на пришедшие головы");
    CHECK(souls_flock(gs, st, town.slot) == souls_home(st, town.slot),
          "и «в поле» осталось нулём, а не отрицательным числом");
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
    test_vein_is_a_two_carrier_row();
    test_a_creature_deal_moves_the_flock_both_ways();
    return sm::test::report("macro_stock_test");
}

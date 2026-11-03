// СВИДЕТЕЛЬ КОНСТРУКТОРА ЗАДАЧ (ломоть 6б M-72) — ЗАВЕДЁН ВМЕСТЕ С СИСТЕМОЙ.
//
// ЧТО ЗДЕСЬ УТВЕРЖДАЕТСЯ — ЗАКОНЫ ФОРМЫ, НЕ ПЕРЕСКАЗ КОДА:
//   1) ДОСКА ДЕТЕРМИНИРОВАНА: та же доска в тот же день у того же места —
//      офферы не состояние мира, а вывод из (сид, день, место).
//   2) ОФФЕР НАЗЫВАЕТ НАСТОЯЩИЙ КОНТЕКСТ: нехватка — той же дверью мира
//      (season_demand_for), добыча — враждебный сосед, цель визита — фича
//      из слоя. Оракул — та дверь, какой живёт мир, не копия формулы.
//   3) ВЗЯТИЕ = ЦЕЛЬ ИГРОКУ (та же колонка рода 2, что у любого сквада) +
//      ФАКТ QuestTaken (первый писатель рода за историю словаря — M-270).
//   4) СУД ПО ХОДУ, ПАМЯТЬЮ МИРА: fetch — инвентарь, kill/visit — летопись
//      с дня взятия. Прогресс-состояния не существует.
//   5) СДАЧА: товар УХОДИТ в инвентарь места дверью deal; QuestDone платит
//      славу сам (дверь летописи); опыт — squad_award_exp, и он равен
//      ЧИСТОЙ СТОИМОСТИ доставленного (колонка value той же таблицы, какой
//      платит весь мир) — вердикт «НИКОГДА МОНЕТЫ».
//   6) СРОК — контекст строки глагола, 0 = бесконечный; просроченный
//      контракт не судится и не сдаётся.
//   7) ПИСАТЕЛЬ ВИЗИТА пишет Explored ровно в клетке цели-фичи и дедупит
//      ПАМЯТЬЮ МИРА (прецедент SubZone), не своим флагом.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (§8 п.6, обе полярности; прогнано ВЖИВУЮ,
// контроль — чистое дерево: OK, 52 проверки):
//   · суд fetch: `>= amount` → `>= amount + 1` (off-by-one инвентаря)
//                                → КРАСНО, 11 из 52 (суд, сдача, перенос,
//                                  слава, опыт, гашение цели)
//   · task_take не пишет QuestTaken (record_deed обойдён, цель остаётся)
//                                → КРАСНО, 17 из 52 (контракт невидим миру)
//   · сдача не переносит товар (`want = 0`: deal везёт пустоту)
//                                → КРАСНО, 8 из 52 (закон сохранения прихода)
//   · писатель визита пишет на ЛЮБОЙ клетке (гейт адреса цели снят)
//                                → КРАСНО, 1 из 52 — ровно детектор
//                                  «в чужой клетке факта не появилось»
//   · балансное число: радиус скана добычи 18 → 16
//                                → МОЛЧИТ, OK 52 (добыча в 2 клетках;
//                                  детектор, краснеющий на баланс, — не
//                                  детектор)
//
// ЧТО ПЕРВЫЙ ПРОГОН НАШЁЛ В МОДУЛЕ — ЖИВОЙ ДЕФЕКТ, НЕ ПОКРЫТИЕ: скан
// контракта сравнивал ДНИ, и повторное взятие в день сдачи читалось «уже
// сдан» (day == day). Порядок внутри дня несёт seq — починено сравнением
// seq, и мутация-регресс обратно в дни краснит части 5-7.
#include "check.h"

#include "core/time.h"            // kTicksPerDay — день двигается тиком
#include "macro/birth_squad.h"    // место родится ТЕЛОМ
#include "macro/chronicle.h"
#include "macro/econ_day.h"       // commodity_item_index — ординал каталога
#include "macro/economy.h"        // season_demand_for — оракул нужды
#include "macro/factions.h"
#include "macro/features.h"
#include "macro/labour.h"         // settle_souls — двери душ
#include "macro/map_generator.h"  // TerrainData — мастер земли фикстуры
#include "macro/npc_ai.h"
#include "macro/squad.h"
#include "macro/squad_index.h"
#include "bus/store.h"
#include "macro/tasks.h"
#include "tables/commodity.h"
#include "tables/faction.h"
#include "tables/items.h"
#include "tables/npc.h"
#include "tables/seasons.h"       // kDaysPerSeason — горизонт счёта нужд

#include <cstdint>
#include <cstdio>
#include <memory>

namespace {

using namespace sm;

constexpr int kMap = 32;

// Земля без воды — фичам есть куда встать (идиома woodcutter_gather_test).
TerrainData all_land() {
    TerrainData td;
    td.width = kMap;
    td.height = kMap;
    td.rgba.assign(std::size_t(kMap) * std::size_t(kMap) * 4u, 0);
    td.seaLevel16 = field_word_of(kDefaultSeaLevel);
    for (std::size_t c = 0; c < std::size_t(kMap) * std::size_t(kMap); ++c)
        td.rgba[c * 4u + 0] = field_word_of(140.0f / 255.0f);
    return td;
}

// Сколько фактов рода K летопись помнит ровно в клетке (x, y).
int facts_at(const GameState& gs, int x, int y, FactKind kind) {
    struct Ctx {
        FactKind kind;
        std::int16_t x, y;
        int n = 0;
    } ctx{kind, std::int16_t(x), std::int16_t(y), 0};
    chronicle_near(gs.chronicle, x, y, 1, 0,
                   [](void* user, const WorldFact& f) {
                       auto& c = *static_cast<Ctx*>(user);
                       if (f.kind == std::uint16_t(c.kind) && f.x == c.x
                           && f.y == c.y) {
                           ++c.n;
                       }
                   },
                   &ctx);
    return ctx.n;
}

MacroHandle make_walker(MacroStore& st, int x, int y, const char* factionId,
                        std::uint32_t ordinal) {
    MacroHandle h = store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, kMap)};
    st.runtime[h.slot].sub = {};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index(factionId))};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(SquadType::None);
    rt.state = std::uint8_t(NPCState::Idle);
    st.runtime[h.slot] = rt;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &st.runtime[h.slot],
        make_character_sheet(NPCType::Peasant, 3, avatar_sheet_seed(ordinal)),
        NPCType::Peasant);
    st.pools[h.slot] = pools;
    store_rename_ordinal(st, h, ordinal);
    h = handle_at(st, h.slot);
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st.spawnId[h.slot].index));
    return h;
}

struct Rig {
    GameState gs{};
    std::unique_ptr<MacroStore> store = make_macro_store();
    SquadIndex idx{};
    FeatureLayer features{};
    MacroHandle place{};
    MacroHandle player{};
    MacroHandle prey{};
    int placeId = 0;
    std::uint32_t preyId = 0;
};

void build_rig(Rig& r) {
    r.gs.mapW = kMap;
    r.gs.mapH = kMap;
    r.gs.worldSeed = 777u;
    chronicle_init(r.gs.chronicle, kMap, kMap);
    create_factions(r.gs, 777u);
    r.features.width = kMap;
    r.features.height = kMap;
    r.features.data.assign(std::size_t(kMap) * std::size_t(kMap), 0u);

    MacroStore& st = *r.store;
    // Фракция города названа явно: дефолт −1 значит «без народа», а вражда
    // спрашивается у фракции МЕСТА — безнародный город ни с кем не воюет.
    r.place = birth_squad(r.gs, st, SquadType::City, 10, 10,
                          std::int16_t(faction_index("timaert")));
    r.placeId = int(st.spawnId[r.place.slot].index);
    settle_souls(r.gs, st, r.place.slot, 50);
    // Дом голоден СЧЁТОМ (CANON S10): непогашенный сезонный счёт еды — ровно
    // то, из чего мир читает нужду; из него же доска обязана родить «принеси».
    st.upkeep[r.place.slot].needDebt[commodity_index("food")] =
        std::int32_t(50 * kDaysPerSeason);

    // Руина в округе — цель «посети» из СЛОЯ ФИЧ, дверью мира (feature_put).
    feature_put(r.features, all_land(), 14, 10, FT_Ruin);

    // Игрок ничем не особенен — обычный сквад (ЗАКОН АНКЕТЫ п.6).
    r.player = make_walker(st, 10, 10, "timaert", 101u);
    // Добыча — сосед, враждебный фракции места: вражда ставится ДВЕРЬЮ
    // (set_relation пишет обе стороны — симметрия есть закон).
    r.prey = make_walker(st, 12, 10, "bandits", 102u);
    r.preyId = st.spawnId[r.prey.slot].index;
    set_relation(r.gs.factions, FactionSlot(faction_index("timaert")),
                 FactionSlot(faction_index("bandits")), -100);
    CHECK(factions_hostile(r.gs.factions,
                           FactionSlot(faction_index("timaert")),
                           FactionSlot(faction_index("bandits"))),
          "фикстура: вражда поставлена дверью мира, а не записью в память");

    build_squad_index(r.idx, st, kMap, kMap);
}

bool same_offer(const TaskOffer& a, const TaskOffer& b) {
    return a.verb == b.verb && a.object == b.object && a.target == b.target
        && a.cell == b.cell && a.amount == b.amount && a.xp == b.xp;
}

}  // namespace

int main() {
    Rig r;
    build_rig(r);
    MacroStore& st = *r.store;
    GameState& gs = r.gs;
    const int foodItem = commodity_item_index(commodity_index("food"));

    // ── 1) ДОСКА ДЕТЕРМИНИРОВАНА И НЕ ПУСТА ─────────────────────────────
    TaskOffer a[4];
    TaskOffer b[4];
    const int na = task_offers_for(st, gs, r.idx, r.features, r.place, a, 4);
    const int nb = task_offers_for(st, gs, r.idx, r.features, r.place, b, 4);
    CHECK(na > 0, "голодный город с врагом и руиной под боком даёт работу");
    CHECK(na == nb, "та же доска в тот же день: счёт офферов совпал");
    bool identical = na == nb;
    for (int i = 0; i < na && i < nb; ++i) {
        identical = identical && same_offer(a[i], b[i]);
    }
    CHECK(identical, "та же доска в тот же день: офферы байт в байт");

    // ── 2) ОФФЕРЫ НАЗЫВАЮТ НАСТОЯЩИЙ КОНТЕКСТ ───────────────────────────
    bool sawKill = false;
    bool sawVisit = false;
    for (int i = 0; i < na; ++i) {
        const TaskOffer& o = a[i];
        CHECK(o.verb < std::size(kTaskVerbs), "глагол оффера — строка оси");
        CHECK(o.amount > 0, "оффер считает ненулевое N");
        const TaskVerbDef& v = kTaskVerbs[o.verb];
        if (v.object == TaskObjectSpace::Need) {
            const Inventory& inv = st.inventory[r.place.slot].inv;
            const int lack =
                season_demand_for(int(o.object), st.upkeep[r.place.slot].needDebt,
                                  souls_home(st, r.place.slot),
                                  st.sheet[r.place.slot].skills, &inv)
                - inv.count_of(int(o.object));
            CHECK(lack > 0, "«принеси» называет НАСТОЯЩУЮ нехватку — оракул "
                            "season_demand_for, та же дверь, какой живёт мир");
        }
        if (v.object == TaskObjectSpace::Hostile) {
            sawKill = true;
            CHECK(o.target == r.preyId,
                  "«убей» назвал враждебного соседа, а не кого попало");
        }
        if (v.object == TaskObjectSpace::Feature) {
            sawVisit = true;
            CHECK(o.object == std::uint16_t(FT_Ruin),
                  "«посети» назвал род фичи из слоя");
            CHECK(o.cell == cell_of(14, 10, kMap),
                  "«посети» закэшировал адрес найденной фичи (кэш предиката)");
        }
    }
    CHECK(sawKill, "враждебный сосед в радиусе — глагол «убей» на доске");
    CHECK(sawVisit, "руина в радиусе — глагол «посети» на доске");

    // ── 3) ВЗЯТИЕ: ЦЕЛЬ + ФАКТ ──────────────────────────────────────────
    TaskOffer fetch{};
    fetch.verb = 0;
    fetch.object = std::uint16_t(foodItem);
    fetch.target = std::uint32_t(r.placeId);
    fetch.cell = st.cell[r.place.slot].idx;
    fetch.amount = 5;
    CHECK(task_take(st, gs, r.player, r.place, fetch),
          "взятие проходит: цель игроку, QuestTaken миру");
    {
        const ecs::SquadGoal& g = st.goal[r.player.slot];
        CHECK(g.cell == st.cell[r.place.slot].idx,
              "цель игрока — та же колонка рода 2: адрес сдачи");
        CHECK(ecs::goal_kind(g.what) == ecs::GoalKind::Rooted
                  && ecs::goal_ordinal(g.what) == std::uint32_t(r.placeId),
              "цель игрока названа родом Rooted и ординалом места");
        CHECK(g.amount == 5, "amount цели — колонка, не флаг");
    }
    TaskActive act = task_active_at(st, gs, r.player, r.place);
    CHECK(act.valid && !act.expired, "контракт активен: QuestTaken найден");
    CHECK(act.verb == 0 && act.object == std::uint16_t(foodItem)
              && act.amount == 5,
          "контракт распакован из amount факта (глагол·объект·счёт)");
    CHECK(act.takenDay == gs.worldTime.day(), "день взятия — день факта");

    // ── 4) СУД ПО ХОДУ: fetch ────────────────────────────────────────────
    CHECK(!task_judged_done(st, gs, r.player, act),
          "пустая сумка — «не выполнено»");
    CHECK(!task_turn_in(st, gs, r.player, r.place),
          "сдача без товара отказывает и мира не трогает");
    st.inventory[r.player.slot].inv.add("food", 5);
    CHECK(task_judged_done(st, gs, r.player, act),
          "товар в сумке — суд говорит «да» (инвентарь, не счётчик)");

    const int placeFoodBefore =
        st.inventory[r.place.slot].inv.count_of(foodItem);
    const std::int32_t debtBefore =
        st.upkeep[r.place.slot].needDebt[commodity_index("food")];
    const std::uint32_t renownBefore = st.runtime[r.player.slot].renown;
    const std::uint32_t expBefore = st.sheet[r.player.slot].levelData.exp;
    const int lvlBefore = sm::level_of(st.sheet[r.player.slot].levelData);
    CHECK(task_turn_in(st, gs, r.player, r.place), "сдача проходит");
    {
        // ЗАКОН СОХРАНЕНИЯ ПРИХОДА: дверь deal гасит долг нужд приходом
        // (econ_pay_debt, CANON S10) — голодный город СЪЕДАЕТ привезённое в
        // счёт. Полка + погашенный долг = лот, и ни единицей меньше.
        const int shelfGain = st.inventory[r.place.slot].inv.count_of(foodItem)
                              - placeFoodBefore;
        const std::int32_t debtPaid =
            debtBefore
            - st.upkeep[r.place.slot].needDebt[commodity_index("food")];
        CHECK(shelfGain + int(debtPaid) == 5,
              "товар УШЁЛ месту: полка + погашенный долг = лот (дверь deal)");
    }
    CHECK(st.inventory[r.player.slot].inv.count_of(foodItem) == 0,
          "сумка игрока опустела ровно на лот");
    CHECK(st.runtime[r.player.slot].renown > renownBefore,
          "слава заплачена дверью летописи (QuestDone)");
    {
        const ItemDef* d = item_def_at(foodItem);
        const int wantXp = (d ? d->value : 0) * 5;
        // Опыт КУМУЛЯТИВЕН (M-383), поэтому прирост есть разность самого
        // носителя, а не «сколько осталось в полосе»: веха, перешагнутая этим
        // грантом, больше ничего из него не вычитает.
        const int gained =
            int(st.sheet[r.player.slot].levelData.exp - expBefore);
        const bool levelled =
            sm::level_of(st.sheet[r.player.slot].levelData) > lvlBefore;
        CHECK(levelled || gained == wantXp,
              "опыт = ЧИСТАЯ СТОИМОСТЬ доставленного (колонка value той же "
              "таблицы; множитель анкеты равен 100% у крестьянина)");
        CHECK(gained > 0 || levelled, "опыт анкете начислен");
    }
    CHECK(st.goal[r.player.slot].cell == ecs::kNoCell,
          "цель погашена сдачей — задача есть одна цель");
    act = task_active_at(st, gs, r.player, r.place);
    CHECK(!act.valid, "QuestDone новее QuestTaken — активного контракта нет");

    // ── 5) СРОК: просроченный контракт не судится и не сдаётся ──────────
    st.inventory[r.player.slot].inv.add("food", 5);
    CHECK(task_take(st, gs, r.player, r.place, fetch), "второй контракт взят");
    gs.worldTime.tick +=
        kTicksPerDay * std::uint64_t(kTaskVerbs[0].deadlineDays + 1);
    act = task_active_at(st, gs, r.player, r.place);
    CHECK(act.valid && act.expired, "срок строки вышел — контракт просрочен");
    CHECK(!task_judged_done(st, gs, r.player, act),
          "просроченный не судится выполненным даже с товаром в сумке");
    CHECK(!task_turn_in(st, gs, r.player, r.place),
          "сдача просроченного отказывает");

    // ── 6) УБЕЙ: суд летописью ──────────────────────────────────────────
    TaskOffer kill{};
    kill.verb = 1;
    kill.target = r.preyId;
    kill.cell = st.cell[r.prey.slot].idx;
    kill.amount = 1;
    CHECK(task_take(st, gs, r.player, r.place, kill), "контракт на добычу взят");
    act = task_active_at(st, gs, r.player, r.place);
    CHECK(act.valid && act.verb == 1, "новейший QuestTaken вытеснил старый");
    CHECK(!task_judged_done(st, gs, r.player, act),
          "летопись не помнит убийства — «не выполнено»");
    {
        // Мир записывает убийство ТОЙ ЖЕ дверью, какой его пишет автобой и
        // обратный поток субмира (raise_macro_fact): свидетель рожает своё
        // предусловие сам (§8 п.11).
        WorldFact wf{};
        wf.day = gs.worldTime.day();
        wf.kind = std::uint16_t(FactKind::Killed);
        wf.objectKind = std::uint8_t(FactSubject::Squad);
        wf.object = r.preyId;
        wf.x = std::int16_t(12);
        wf.y = std::int16_t(10);
        wf.amount = 1;
        record_deed(st, gs, wf, r.player);
    }
    CHECK(task_judged_done(st, gs, r.player, act),
          "летопись помнит — суд говорит «да» без своего счётчика");
    CHECK(task_turn_in(st, gs, r.player, r.place), "сдача добычи проходит");

    // ── 7) ПОСЕТИ: писатель визита + дедуп памятью мира ─────────────────
    TaskOffer visit{};
    visit.verb = 2;
    visit.object = std::uint16_t(FT_Ruin);
    visit.cell = cell_of(14, 10, kMap);
    visit.amount = 1;
    CHECK(task_take(st, gs, r.player, r.place, visit), "контракт визита взят");
    act = task_active_at(st, gs, r.player, r.place);
    CHECK(act.valid && act.verb == 2, "контракт визита активен");
    task_note_player_arrival(st, gs, r.player, 20, 20);
    CHECK(!task_judged_done(st, gs, r.player, act),
          "чужая клетка визита НЕ пишет — гейт по адресу цели");
    CHECK(facts_at(gs, 20, 20, FactKind::Explored) == 0,
          "и в самой чужой клетке факта не появилось");
    task_note_player_arrival(st, gs, r.player, 14, 10);
    CHECK(task_judged_done(st, gs, r.player, act),
          "пришёл в клетку цели — мир запомнил (Explored), суд говорит «да»");
    CHECK(facts_at(gs, 14, 10, FactKind::Explored) == 1,
          "визит записан ровно одним фактом");
    task_note_player_arrival(st, gs, r.player, 14, 10);
    CHECK(facts_at(gs, 14, 10, FactKind::Explored) == 1,
          "повторный приход дедупится ПАМЯТЬЮ МИРА, не своим флагом");
    CHECK(task_turn_in(st, gs, r.player, r.place), "сдача визита проходит");
    act = task_active_at(st, gs, r.player, r.place);
    CHECK(!act.valid, "после сдачи визита активного контракта нет");

    return sm::test::report("task_constructor_test");
}

// balance_run — the world plays itself, headless (owner track 2026-08-30;
// CANON S10 «Баланс экономики выводится ДУБЛЬ-ПРОГОНОМ», work_vector §1
// «Методика баланса»). NOT a ctest: it is the measuring instrument of the
// economy track — it raises the SAME world the game boots (macro/world_gen.h,
// one baker, CANON S26), runs it at the live loop's own cadence (256 AI
// sweeps + the daily tick per game day — the 8192 frame steps of the live
// loop are a rendering artifact and are skipped), and writes per-day TSV:
//
//   <out>/world_<seed>.tsv     one row per day: totals + per-commodity flows
//   <out>/landmarks_<seed>.tsv one row per landmark per day: pop/wellbeing/stocks
//
// Usage: balance_run [seedsCsv] [days] [outDir]
//        defaults:    12345      256    balance_out
// Exit 1 = a world LAW broke (printed as [law] ... FAIL); 0 otherwise.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/time.h"
#include "ecs/components.h"
#include "tables/commodity.h"
#include "macro/currency.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "tables/faction.h"
#include "macro/anketa.h"
#include "macro/macro_world.h"
#include "macro/map_generator.h"
#include "macro/nav_field.h"
#include "macro/npc_ai.h"
#include "macro/world_row.h"
#include "macro/pathfinding.h"
#include "macro/settlement_score.h"   // kSettlementReach — рука места
#include "macro/spawners.h"
#include "macro/state.h"
#include "macro/labour.h"
#include "macro/landmark_iter.h"   // for_each_place — обход мест по слотам
#include "macro/squad.h"           // place_handle_by_ordinal — резолв места
#include "macro/store.h"
#include "macro/tree_layer.h"
#include "macro/world_gen.h"
#include "macro/world_tick.h"
#include "macro/zones.h"

namespace {

// One game day of econ facts, folded flat. World-aggregate on purpose: the
// per-landmark cut comes from sampling the place bodies at day end;
// facts carry the FLOWS the stocks alone cannot show.
struct DayAccum {
    long long gathered[sm::kCommodityCount] = {};
    long long produced[sm::kCommodityCount] = {};
    long long consumed[sm::kCommodityCount] = {};
    int starvedPops = 0;
    long long mintedCoins = 0;
    // ── ВЕДОМОСТЬ СКЛАДА ДУШ (econ_day.h, CANON S9) ──────────────────────
    // Уровни (кто где стоит) прибор снимает сам вечерним свипом; ПОТОКИ к
    // вечеру не видны — место, выплатившее сто душ в артель, и место,
    // потерявшее сто душ, выглядят одинаково. Эти четыре величины и есть
    // разница между «мир умирает» и «мир переливается».
    long long soulsBorn = 0;
    // (`crewsUnfed`/`soulsUnfed` УМЕРЛИ 2026-10-06, M-232: исхода «ушёл за
    // харч» в мире больше нет — некормленные умирают и попадают в
    // `starvedPops` выше, той же строкой, которой говорит голод места.)
    int crewsUnpaid = 0;         // контейнеров провалило окно по ПЛАТЕ
    long long soulsUnpaid = 0;
    // ЕДИНСТВЕННАЯ БОЕВАЯ УБЫЛЬ (M-228): души, погибшие с павшим сквадом.
    // До 2026-10-06 они не умирали, а переезжали в пул дезертиров, и
    // ведомость об этом молчала — закон сохранения душ мира не сходился ни
    // на одном сиде, потому что строки просто не было.
    long long soulsKilled = 0;

    void reset() { *this = DayAccum{}; }
};

void econ_fact_sink(void* user, const sm::EconFact& f) {
    auto* a = static_cast<DayAccum*>(user);
    const bool hasCommodity =
        f.commodity >= 0 && f.commodity < sm::kCommodityCount;
    switch (f.kind) {
        case sm::EconFact::Kind::Gathered:
            if (hasCommodity) a->gathered[f.commodity] += f.amount;
            break;
        case sm::EconFact::Kind::Produced:
            if (hasCommodity) a->produced[f.commodity] += f.amount;
            break;
        case sm::EconFact::Kind::Consumed:
            if (hasCommodity) a->consumed[f.commodity] += f.amount;
            break;
        case sm::EconFact::Kind::Starved: a->starvedPops += f.amount; break;
        // amount = coins struck (commodity carries the INPUT silver row, so
        // this is its own counter, not a produced[] line).
        case sm::EconFact::Kind::Minted: a->mintedCoins += f.amount; break;
        // Slot hygiene, not a flow of goods: what the melt RETURNS is not
        // reported as Produced on purpose (entropy is a sink, not a source).
        case sm::EconFact::Kind::Scrapped: break;
        // Ведомость склада душ. Один факт дезертирства = одна артель,
        // поэтому здесь считаются ОБЕ величины: артели фактами, души суммой.
        case sm::EconFact::Kind::SoulsBorn:
            a->soulsBorn += f.amount;
            break;
        case sm::EconFact::Kind::SoulsDesertedUnpaid:
            a->crewsUnpaid += 1;
            a->soulsUnpaid += f.amount;
            break;
        case sm::EconFact::Kind::SoulsKilled:
            a->soulsKilled += f.amount;
            break;
    }
}

// Every coin row of the faction registry's mint columns, counted by catalog
// ordinal × NOMINAL (its value column): with three nominals per realm
// (verdict №1, 2026-09-17) the money supply is the VALUE of the coin goods —
// a gold piece is a hundred coppers of supply, not one.
long long coins_in(const sm::Inventory& inv, const std::vector<int>& coinIdx) {
    long long total = 0;
    for (int idx : coinIdx) {
        const sm::ItemDef* def = sm::item_def_at(idx);
        total += (long long)inv.count_of(idx) * (def ? def->value : 1);
    }
    return total;
}

// ── ПЕРЕПИСЬ ЖИВЫХ СЛОТОВ МАКРО-СТОРА ────────────────────────────────────
//
// ПОЧЕМУ ПОСТОЯННЫЙ ЖИЛЕЦ, А НЕ ЗОНД ПОД ОДИН ВОПРОС. До 2026-10-18 число
// населения мира жило ТОЛЬКО в прозе доков, и их было ДВА несводимых: «12.8k
// слотов = 78 % капа» (пик за 512 дней, родился 2026-09-25 выводом капа 2^15)
// против «13.7–14.4 тыс. на день 48, три мира» (шапка `squad_index.h`,
// 2026-10-03), причём меньшее стояло при БОЛЬШЕМ горизонте. Развести их было
// нечем: оба зонда снесли после ответа (M-72, ЗАМЕР-ОСНОВАНИЕ п.IV), и живого
// прибора на число живых слотов не осталось ни одного. Снесённый зонд
// доказывает только то, что его автор видел число один раз. Разбор спора —
// в SKELETON §I.10 («ЧЕМ ЭТО ЗАКРЫВАЕТ СТАРЫЙ СПОР»): оба старых числа сняты
// ДО слияния мест в стор, то есть мест не считали вовсе, — потому колонка
// мест здесь стоит отдельно и рядом.
//
// ЧЕТЫРЕ ВЕЛИЧИНЫ, И ОНИ ОТВЕЧАЮТ НА РАЗНЫЕ ВОПРОСЫ — ровно та развилка, из
// которой спор и вырос:
//   alive     — сколько слотов ЗАНЯТО, счётчиком самого стора (`aliveCount`);
//   aliveScan — то же число НЕЗАВИСИМЫМ сканом колонки `alive[]`. Это не
//               вторая система (DOD п.6), а СВИДЕТЕЛЬ бухгалтерии: закон
//               прогона ниже стоит на их равенстве, и совравший счётчик
//               становится виден в тот же день;
//   dead      — занятые слоты с поднятым байтом `dead`. Ни думка, ни каркас
//               клеток их не видят (`collect_squads_by_ordinal` фильтрует
//               `st.dead[slot] == 0`), значит МНОЖЕСТВО ДУМКИ = alive − dead,
//               и «занято» с «ходит в думку» путать нельзя;
//   places    — из множества думки те, у кого архетип Place: их дневная думка
//               кончается ранним возвратом гейта `squad_archetype(…) ==
//               SquadArchetype::Place` в `npc_ai.cpp`. Счёт идёт ДВЕРЬЮ
//               `for_each_place@src/macro/landmark_iter.h` — второго написания
//               закона «что есть место» прибор не заводит.
struct SlotCensus {
    long long alive = 0;
    long long aliveScan = 0;
    long long dead = 0;
    long long places = 0;
};

SlotCensus slot_census(const sm::MacroStore& st) {
    SlotCensus c;
    c.alive = (long long)st.aliveCount;
    for (std::size_t s = 0; s < sm::kUnifiedCap; ++s) {
        if (st.alive[s] == 0) continue;
        ++c.aliveScan;
        if (st.dead[s] != 0) ++c.dead;
    }
    sm::for_each_place(st, [&](std::uint16_t) { ++c.places; });
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    const char* seedsCsv = argc > 1 ? argv[1] : "12345";
    const int days = argc > 2 ? std::atoi(argv[2]) : 256;
    const std::string outDir = argc > 3 ? argv[3] : "balance_out";
    if (days <= 0) {
        std::fprintf(stderr, "balance_run: days must be positive\n");
        return 1;
    }

    std::vector<std::uint32_t> seeds;
    for (const char* p = seedsCsv; *p;) {
        char* end = nullptr;
        const unsigned long v = std::strtoul(p, &end, 10);
        if (end == p) break;
        seeds.push_back(std::uint32_t(v));
        p = *end == ',' ? end + 1 : end;
    }
    if (seeds.empty()) {
        std::fprintf(stderr, "balance_run: no seeds in '%s'\n", seedsCsv);
        return 1;
    }

#ifdef _WIN32
    std::system(("mkdir \"" + outDir + "\" 2>NUL").c_str());
#else
    std::system(("mkdir -p '" + outDir + "'").c_str());
#endif

    // Coin catalog ordinals, resolved once (strings are authoring keys).
    // The coin rows are the faction registry's own mint columns (the
    // kCurrencyDefs list died 2026-09-18 with verdict №1) — dedup because
    // culture groups fold onto their realm's family.
    std::vector<int> coinIdx;
    for (const auto& f : sm::kFactionDefs) {
        for (const char* c : f.mint) {
            if (!c || !c[0]) continue;
            const int idx = sm::item_index(c);
            if (idx >= 0
                && std::find(coinIdx.begin(), coinIdx.end(), idx)
                       == coinIdx.end()) {
                coinIdx.push_back(idx);
            }
        }
    }

    bool lawsHold = true;

    for (const std::uint32_t seed : seeds) {
        const auto t0 = std::chrono::steady_clock::now();

        // ── Raise THE world (same baker as the game boot) ────────────────
        sm::GameState gs;
        sm::TerrainData terrain{};
        std::vector<sm::TreePoint> trees;
        sm::TreeLayer treeLayer;
        sm::DepositLayer deposits;
        sm::FeatureLayer features;
        sm::ZoneLayer zones;
        sm::TreeGrid treeGrid;
        sm::PathCostData pathCost;
        auto macroStore = sm::make_macro_store();

        // Каркас клеток по сквадам живёт в рантайме АИ (носитель мира,
        // ломтик F); рантайм рождается ДО генезиса — генезис строит каркас.
        sm::MacroNpcAiRuntime ai;
        sm::reset_macro_npc_ai_runtime(ai, seed);

        sm::WorldGenParams gp{};
        gp.seed = seed;
        sm::WorldGenOut go{};
        go.gs = &gs;
        go.terrain = &terrain;
        go.trees = &trees;
        go.treeLayer = &treeLayer;
        go.deposits = &deposits;
        go.features = &features;
        go.zones = &zones;
        go.treeGrid = &treeGrid;
        go.squadIndex = &ai.squadIndex;
        go.pathCost = &pathCost;
        go.store = macroStore.get();
        sm::generate_macro_world(go, gp);

        // Muster law: nobody is BORN at sea. Сквады живут в store (M-106 —
        // entt-вью по макро-населению пуст по построению и молчал бы вечно);
        // адрес сквада — колонка cell, тот же row-major индекс, что у поля
        // воды.
        {
            int atSea = 0;
            for (std::size_t s = 0; s < sm::kUnifiedCap; ++s) {
                if (!macroStore->alive[s]) continue;
                const std::uint32_t idx = macroStore->cell[s].idx;
                if (!pathCost.water.empty()
                    && idx < pathCost.water.size() && pathCost.water[idx])
                    ++atSea;
            }
            std::fprintf(stderr, "[muster] spawned at sea: %d\n", atSea);
        }

        DayAccum accum;
        sm::NavWorld nav;        // запечённая навигация (CANON S7), derived
        sm::MacroWorld mw{};
        mw.gs = &gs;
        mw.nav = &nav;
        mw.trees = &treeLayer;
        mw.store = macroStore.get();
        mw.terrain = &terrain;
        mw.deposits = &deposits;
        mw.features = &features;
        mw.zones = &zones;
        mw.pathCost = &pathCost;
        mw.treeGrid = &treeGrid;
        mw.squads = &ai.squadIndex;
        mw.econFacts = &econ_fact_sink;
        mw.econFactsUser = &accum;

        // ── TSV writers ──────────────────────────────────────────────────
        char path[512];
        std::snprintf(path, sizeof path, "%s/world_%u.tsv", outDir.c_str(),
                      seed);
        FILE* fw = std::fopen(path, "w");
        std::snprintf(path, sizeof path, "%s/landmarks_%u.tsv",
                      outDir.c_str(), seed);
        FILE* fl = std::fopen(path, "w");
        if (!fw || !fl) {
            std::fprintf(stderr, "balance_run: cannot open outputs in %s\n",
                         outDir.c_str());
            return 1;
        }
        // ЗОНД РАСПРЕДЕЛЕНИЯ ПОРУЧЕНИЙ (2026-09-19): куда рулетка дела
        // артелей — счёт живых крестьянских крю по глаголу поручения, плюс
        // пул дезертиров (кровь закона 1/8). Ответ на «почему зерно не
        // едет»: мало рейсов или пустые сделки.
        // (Колонки `coinLootPool` и `deserters` УНИЧТОЖЕНЫ 2026-10-11 вместе
        // со своими величинами: казна пула и пул дезертиров были половинами
        // контура — писатель живой, читателей ноль. Прибор не печатает то,
        // чего в мире нет; колонки читать ПО ИМЕНИ, §5 п.7.)
        std::fprintf(fw, "day\tpop\tcoinLandmarks\tcoinSquads"
                         "\tstarvedPops\tminted\ttrades\ttradedValue\tfoodHolds"
                         "\tcrewsGather\tcrewsSell\tcrewsOther"
                         // ЛОШАДЬ-ЮНИТ (2026-09-19): свидетель контура —
                         // табуны в гарнизонах и спины в отрядах, миром.
                         "\thorsesGarr\thorsesSquads\tpastures"
                         // ПАРЦЕЛЛЫ — свидетель ЗАСЕЯННОЙ ЗЕМЛИ (M-96).
                         // Матушка-земля одна: поле фертильности не знает,
                         // кто на нём стоит, а фича сверху говорит, что с
                         // клетки снимают. Поэтому парцелла — ОДНО число:
                         // хлеб и лён качают одно поле (kGathererDefs
                         // Worksite::HomeField / HomeFlaxField). Читает
                         // разбор еды: «еда просела — пахать стало негде»
                         // отличается от «еда просела — некому пахать»
                         // ТОЛЬКО этой колонкой.
                         "\tparcels"
                         // ── БАЛАНС ДУШ МИРА (CANON S9, владелец
                         // 2026-09-21) ──────────────────────────────────
                         // Население — РЕСУРС места, которым оно платит за
                         // артель; значит «мир умирает» и «мир переливает
                         // осёдлых в вооружённых» — разные события, и
                         // различает их только полный счёт по контейнерам.
                         // Контейнеров ЧЕТЫРЕ, и это поправка 2026-10-06:
                         // ПАСТВА мест (popCity/Vil/Lair/Else — число
                         // worked, то есть дома И в поле), сквады БЕЗ дома
                         // (банды) и пул дезертиров. soulsWorld — их сумма,
                         // и падать она может ТОЛЬКО от смерти.
                         //
                         // ЗДЕСЬ БЫЛО ПЯТЬ, И ТРИ ИЗ НИХ СЧИТАЛИ ОДНИХ И ТЕХ
                         // ЖЕ ЛЮДЕЙ. `soulsHomed` идёт по ВСЕМ живым слотам
                         // и берёт тех, чьё ребро Parent разрешается в
                         // место; у места он разрешается в НЕГО САМОГО
                         // (place_birth.h «дом места — оно само»), поэтому
                         // места попадали в `soulsHomed`, а `soulsGarr`
                         // считал их же второй раз, а `popTotal` — третий,
                         // пастве которой они и принадлежат. Сумма
                         // ТРОИЛА осёдлых: день 32 сида 12345 печатал
                         // soulsWorld 1 402 207 при пастве 478 427.
                         // Величина, обещавшая «падать только от смерти»,
                         // не могла этого обещания держать — она не была
                         // счётом людей вовсе. Колонки-слагаемые оставлены
                         // (каждая отвечает на свой вопрос), но в СУММУ
                         // теперь входит каждая душа РОВНО раз.
                         "\tpopCity\tpopVil\tpopLair\tpopElse"
                         "\tsoulsHomed\tsoulsFree\tsoulsGarr"
                         "\tsoulsWorld"
                         // Потоки дня (ведомость склада душ, econ_day.h).
                         "\tsoulsBorn"
                         "\tcrewsUnpaid\tsoulsUnpaid\tsoulsKilled");
        for (int c = 0; c < sm::kCommodityCount; ++c) {
            const char* id = sm::kCommodities[c].id;
            std::fprintf(fw, "\t%s_stock\t%s_gathered\t%s_produced"
                             "\t%s_consumed\t%s_city\t%s_vil",
                         id, id, id, id, id, id);
        }
        // ── ПЕРЕПИСЬ СЛОТОВ И ЦЕНА ДУМКИ — В САМОМ КОНЦЕ СТРОКИ ──────────
        // Приписаны ХВОСТОМ нарочно: ни один индекс прежних колонок не
        // сдвинулся, поэтому базы, снятые до 2026-10-18, сравнимы с новыми
        // — и всё равно читать их положено ПО ИМЕНИ заголовка, никогда по
        // номеру (§5 п.7).
        //
        // `aiMsDay` — ВРЕМЯ ПРИБОРА, А НЕ ВЕЛИЧИНА МИРА, и это сказано здесь
        // вслух, чтобы следующий читатель не принял его за второе: стенные
        // миллисекунды есть свойство ЖЕЛЕЗА и загрузки машины, их не читает
        // ни одно число симуляции, калибровать по ним запрещено
        // («КАЛИБРОВАТЬ ТОЛЬКО В ТИКАХ ИГРЫ»), и закона на них в этом
        // приборе нет ни одного. Что в них стоит: сумма 256 вызовов
        // `tick_macro_npc_ai` за игровой день — ровно тот проход, который
        // переписывает эпик АИ (M-72), без дневного тика мира рядом.
        //
        // И ЧТО ИМЕННО МЕРЯЕТСЯ СЕГОДНЯ, СКАЗАНО ВСЛУХ, ИНАЧЕ ЧИСЛО СОВРЁТ
        // ПОСЛЕ ЭПИКА: за этой дверью сейчас стоит не только думка, а ВСЯ
        // дневная экономика — `rotate_worker_squads` с аукционом внутри. По
        // M-72 аукцион УНИЧТОЖАЕТСЯ, а «экономики как самостоятельной системы
        // не будет» (владелец): её порождают ИИ-модули сквадов, их анкеты и
        // поля. Значит и число слотов (население рождает та же ротация), и
        // цена прохода после эпика меняются ПО ЗАМЫСЛУ — это не регресс.
        // Прибор потому и постоянный жилец: дверь остаётся той же, и дельту
        // эпика видно ЭТИМИ ЖЕ колонками, а не новым зондом.
        std::fprintf(fw, "\tslotsAlive\tslotsDead\tslotsPlace\taiMsDay");
        std::fprintf(fw, "\n");
        // ТРИ ПОТОКА МАТЕРИИ, ПО МЕСТАМ (CANON S10, 2026-09-20): гипотеза о
        // расщеплении горизонта — про МЕДИАНЫ по местам (еда топит пол
        // кривой, а рукотворные строки голодают у потолка), и мировой
        // агрегат медианы показать не может. По строке на поток: food =
        // ЕДА, cloth = БЛАГА, iron = РЕСУРС.
        //
        // ДВЕ КОЛОНКИ ДОЛГА ВМЕСТО ОДНОГО ЛИТЕРАЛЬНОГО НУЛЯ (починка прибора
        // 2026-09-21). Здесь печаталось `unmet` константой 0, а `bread` и
        // `grain` были ОДНИМ И ТЕМ ЖЕ ординалом — наследие сноса хлеба,
        // пережившее его на день. Прибор, который печатает одну величину
        // дважды под разными именами и назначенный ноль под третьим, не
        // меряет, а успокаивает: все числа сессий 4-5 про «хлеб в городах»
        // и «зерно» сняты с этих колонок. Долг — та самая величина, которой
        // МЕРЯЕТСЯ нужда с 2026-09-19 (CANON S10, потребление = долг):
        // голодный счёт хранится в житель-днях (popPerUnitDay == 1), и
        // непогашенный остаток на границе И ЕСТЬ то, чем место умрёт.
        // ЛЕС ПОД МЕСТОМ И ЛЕС ПОД РУКОЙ (M-111, шаг 1). Вето «клетка внутри
        // лесного массива» сменилось ВЕСОМ, и вопрос «куда мир теперь ставит
        // места» ДВУСТОРОННИЙ: «деревни у леса появились» читает лес В РУКЕ,
        // «деревни В ЧАЩЕ — редкость, а не норма» читает лес ПОД СОБОЙ. Одной
        // колонкой второй вопрос не задать — чаща всегда и «у леса» тоже, так
        // что `treesNear` без `trees` их не различает. Обе — в единицах
        // TreeLayer (0..kMaxTreesPerCell), порог класса — is_forest_cell.
        std::fprintf(fl, "day\tid\ttype\tpop\twellbeing\tstarved"
                         "\tdebtFood\tdebtComfort\tfood\tcloth\tiron\tcoin"
                         "\ttrees\ttreesNear\n");

        // Голодная строка — ДВЕРЬ (econ_day.h hunger_item_*), не литерал:
        // ровно та причина, по которой дверь и заведена — переименуй строку
        // лестницы, и литерал в приборе замолчит, не сломавшись.
        const int foodIdx = sm::hunger_item_index();
        const int clothIdx = sm::item_index("cloth");
        const int ironIdx = sm::item_index("iron");
        // needDebt индексируется ТОВАРНЫМ ординалом (state.h) — берём его у
        // той же таблицы лестницы, которую читает econ_debt_boundary, а не
        // переписываем её здесь второй копией (закон тестов §5).
        const int hungerOrd = sm::hunger_commodity_ordinal();
        int comfortOrd[sm::kCommodityCount] = {};
        int comfortOrdCount = 0;
        for (int c = 0; c < sm::kCommodityCount; ++c) {
            if (sm::commodity_is_comfort(c)) comfortOrd[comfortOrdCount++] = c;
        }
        std::uint32_t ringCursor = gs.chronicle.nextSeq;

        // ── The days: the live loop's cadence without its frames ─────────
        // One game day = kTicksPerDay/kAiTicks = 256 iterations of
        // [advance 32 ticks → one AI sweep], daily tick at the rollover —
        // exactly the order the app's frame runs them (main.cpp).
        constexpr int kSweepsPerDay = int(sm::kTicksPerDay / sm::kAiTicks);
        // Закон бухгалтерии слотов — судится КАЖДЫЙ день, а не на конце
        // прогона: счётчик, совравший на дне 33 и сошедшийся к дню 512,
        // прошёл бы итоговую проверку молча.
        bool slotLawHolds = true;
        int slotLawFirstBadDay = -1;
        SlotCensus lastCensus;
        double lastAiMs = 0.0;
        for (int d = 0; d < days; ++d) {
            accum.reset();
            double aiSecs = 0.0;
            for (int s = 0; s < kSweepsPerDay; ++s) {
                sm::tick_world(gs, *macroStore, gs.worldTickRt, sm::kAiTicks,
                               /*max_daily_ticks=*/32, &mw);
                // Время прибора, не мира (см. шапку колонки `aiMsDay`):
                // замер стоит ВОКРУГ двери думки и внутрь её не лезет —
                // `npc_ai.cpp` эпик АИ переписывает целиком, и правка в нём
                // была бы работой на снос (§5 п.14).
                const auto a0 = std::chrono::steady_clock::now();
                sm::tick_macro_npc_ai(mw, ai, sm::kAiTicks);
                aiSecs += std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - a0).count();
            }

            // Day-end sampling: stocks from the landmarks, flows from facts.
            long long popTotal = 0, coinLm = 0;
            // Склады душ мест по родам. Логово выделено отдельной колонкой
            // не для красоты: по вердикту владельца «банды это население
            // ландмарка логово бандитов» — значит колонка обязана быть
            // ненулевой, а сегодня она ноль, и это измеряемая недостройка.
            long long popCity = 0, popVil = 0, popLair = 0, popElse = 0;
            long long stock[sm::kCommodityCount] = {};
            long long stockCity[sm::kCommodityCount] = {};
            long long stockVil[sm::kCommodityCount] = {};
            // Место ЕСТЬ слот store (ломтик F): род, адрес, ординал и склад
            // — его колонки, и замер идёт тем же обходом, что живой мир.
            sm::for_each_place(*macroStore, [&](std::uint16_t slot) {
                const sm::SquadType kind =
                    sm::SquadType(macroStore->runtime[slot].squadType);
                const int lmx = sm::ecs::cell_x(macroStore->cell[slot], gs.mapW);
                const int lmy = sm::ecs::cell_y(macroStore->cell[slot], gs.mapW);
                const sm::Inventory& bag = macroStore->inventory[slot].inv;
                // ПАСТВА — дверью мира (labour.h souls_flock): у поселения
                // worked-число фичи, у данжа головы его толпы (v122).
                const long long flock = sm::souls_flock(gs, *macroStore, slot);
                popTotal += flock;
                switch (kind) {
                    case sm::SquadType::City: popCity += flock;
                        break;
                    case sm::SquadType::Village: popVil += flock;
                        break;
                    case sm::SquadType::Lair: popLair += flock;
                        break;
                    default: popElse += flock; break;
                }
                coinLm += coins_in(bag, coinIdx);
                for (int c = 0; c < sm::kCommodityCount; ++c) {
                    const long long n =
                        bag.count_of(sm::commodity_item_index(c));
                    stock[c] += n;
                    if (kind == sm::SquadType::City) stockCity[c] += n;
                    else if (kind == sm::SquadType::Village)
                        stockVil[c] += n;
                }
                const bool settled = kind == sm::SquadType::City
                                  || kind == sm::SquadType::Village;
                if (settled) {
                    long long debtComfort = 0;
                    for (int k = 0; k < comfortOrdCount; ++k)
                        debtComfort += macroStore->upkeep[slot]
                                           .needDebt[comfortOrd[k]];
                    // Лес под местом и лес в его руке — бокс ±kSettlementReach
                    // шагами ИНДЕКСА (ЗАКОН АДРЕСА), той же рукой, которой
                    // мерит сам скор.
                    const std::uint32_t lmCell =
                        sm::cell_of(lmx, lmy, gs.mapW);
                    const int treesHere = int(treeLayer.at(lmx, lmy));
                    int treesNear = 0;
                    for (int dy = -sm::kSettlementReach;
                         dy <= sm::kSettlementReach; ++dy)
                        for (int dx = -sm::kSettlementReach;
                             dx <= sm::kSettlementReach; ++dx) {
                            const std::uint32_t n =
                                sm::cell_step(lmCell, dx, dy, gs.mapW);
                            treesNear = std::max(
                                treesNear,
                                int(treeLayer.at(sm::cell_x(n, gs.mapW),
                                                 sm::cell_y(n, gs.mapW))));
                        }
                    std::fprintf(fl,
                                 "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d"
                                 "\t%d\t%lld\t%d\t%d\n",
                                 gs.worldTime.day(),
                                 int(macroStore->spawnId[slot].index),
                                 int(kind), int(flock),
                                 int(macroStore->wellbeing[slot]
                                         .seasonWellbeing),
                                 int(macroStore->wellbeing[slot]
                                         .starvedYesterday),
                                 hungerOrd >= 0
                                     ? macroStore->upkeep[slot]
                                           .needDebt[hungerOrd]
                                     : 0,
                                 debtComfort,
                                 bag.count_of(foodIdx),
                                 bag.count_of(clothIdx),
                                 bag.count_of(ironIdx),
                                 coins_in(bag, coinIdx),
                                 treesHere, treesNear);
                }
            });
            long long coinSquads = 0, foodHolds = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kUnifiedCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& bag = macroStore->inventory[slot];
                coinSquads += coins_in(bag.inv, coinIdx);
                foodHolds += bag.inv.count_of(foodIdx);
            }
            long long horsesGarr = 0, soulsGarr = 0;
            sm::for_each_place(*macroStore, [&](std::uint16_t slot) {
                const sm::Inventory& bag = macroStore->inventory[slot].inv;
                horsesGarr +=
                    sm::creature_count_of(bag, sm::NPCType::Horse);
                // Гарнизон — область существ ЕДИНОГО контейнера места
                // (M-71). Считаются ЛЮДИ: табун у места свой столбец, и
                // душой населения лошадь не была никогда.
                soulsGarr += sm::count_human_souls(bag);
            });
            long long horsesSquads = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kUnifiedCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                horsesSquads += sm::creature_count_of(
                    macroStore->inventory[slot].inv, sm::NPCType::Horse);
            }
            // ДУШИ В СКВАДАХ, разделённые ПО АДРЕСУ ДОМА. Лидер — такая же
            // душа, как любая в контейнере (CANON S4: «одиночка = лидер с
            // пустым контейнером»), поэтому он +1, а не особый случай.
            // Бездомный сквад — это банда: выплаченная кем-то душа, которую
            // не ждёт ни один склад. По вердикту владельца такого состояния
            // быть не должно вовсе («банды это население ландмарка логово
            // бандитов»), и эта колонка меряет, сколько мира сейчас живёт
            // мимо закона.
            long long soulsHomed = 0, soulsFree = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kUnifiedCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& kind = macroStore->kind[slot];
                long long souls = sm::is_folk_kind(kind.type) ? 1 : 0;
                souls += sm::count_human_souls(
                    macroStore->inventory[slot].inv);
                if (souls <= 0) continue;
                if (macroStore->valid(sm::place_handle_by_ordinal(
                        *macroStore,
                        std::uint32_t(sm::parent_of(*macroStore, slot)))))
                    soulsHomed += souls;
                else
                    soulsFree += souls;
            }
            long long pastures = 0, parcels = 0;
            if (!features.data.empty()) {
                for (const std::uint8_t f : features.data) {
                    if (f == sm::FT_Pasture) ++pastures;
                    else if (f == sm::FT_Field || f == sm::FT_FlaxField)
                        ++parcels;
                }
            }
            int crewsGather = 0, crewsSell = 0, crewsOther = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kUnifiedCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& kind = macroStore->kind[slot];
                const auto& crt = macroStore->runtime[slot];
                if (kind.type != std::uint16_t(sm::NPCType::Peasant))
                    continue;
                if (crt.squadType == std::uint8_t(sm::SquadType::Artel))
                    ++crewsGather;
                else if (crt.squadType
                         == std::uint8_t(sm::SquadType::Caravan))
                    ++crewsSell;
                else
                    ++crewsOther;
            }
            // The day's DEALS, read off the chronicle ring by sequence — the
            // same shop window the witcher asks (S20.1): every Traded fact
            // since yesterday's cursor.
            long long trades = 0, tradedValue = 0;
            for (const sm::WorldFact& f : gs.chronicle.ring) {
                if (f.seq < ringCursor || f.seq == 0) continue;
                if (f.kind != std::uint16_t(sm::FactKind::Traded)) continue;
                trades += 1;
                tradedValue += f.amount;
            }
            ringCursor = gs.chronicle.nextSeq;

            std::fprintf(fw,
                         "%d\t%lld\t%lld\t%lld\t%d\t%lld\t%lld"
                         "\t%lld\t%lld\t%d\t%d\t%d",
                         gs.worldTime.day(), popTotal, coinLm, coinSquads,
                         accum.starvedPops,
                         accum.mintedCoins, trades, tradedValue, foodHolds,
                         crewsGather, crewsSell, crewsOther);
            std::fprintf(fw, "\t%lld\t%lld\t%lld\t%lld",
                         horsesGarr, horsesSquads, pastures, parcels);
            // Баланс душ мира. soulsWorld печатается суммой, а не считается
            // читателем TSV, ровно по той же причине, по какой прибор вообще
            // существует: величина, которую каждый читатель складывает сам,
            // рано или поздно складывается по-разному.
            std::fprintf(fw, "\t%lld\t%lld\t%lld\t%lld"
                             "\t%lld\t%lld\t%lld\t%lld"
                             "\t%lld\t%d\t%lld\t%lld",
                         popCity, popVil, popLair, popElse,
                         soulsHomed, soulsFree, soulsGarr,
                         // Паства мест + бездомные: ни одной души дважды.
                         // Слагаемое «пул» ушло вместе с самим пулом
                         // (2026-10-11), и сумма от этого стала ЧЕСТНЕЕ: душа
                         // ушедшего больше нигде не числится, потому что её
                         // нигде и нет. `soulsHomed` и `soulsGarr` в сумму НЕ
                         // входят — они подмножества паствы и живут рядом
                         // как ответы на «кто стоит дома» и «кто за домом
                         // числится».
                         popTotal + soulsFree,
                         accum.soulsBorn,
                         accum.crewsUnpaid, accum.soulsUnpaid,
                         accum.soulsKilled);
            for (int c = 0; c < sm::kCommodityCount; ++c) {
                std::fprintf(fw, "\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld",
                             stock[c], accum.gathered[c], accum.produced[c],
                             accum.consumed[c], stockCity[c], stockVil[c]);
            }
            const SlotCensus sc = slot_census(*macroStore);
            std::fprintf(fw, "\t%lld\t%lld\t%lld\t%.3f",
                         sc.alive, sc.dead, sc.places, aiSecs * 1000.0);
            // ЗАКОН СЛОТОВ, И ОН ЖЕ ДЕТЕКТОР ПРИБОРА. Три утверждения, и ни
            // одно не есть пересказанное число (§8 п.4): (1) счётчик стора
            // сходится с независимым сканом своей же колонки — бухгалтерия
            // рождения и смерти не врёт; (2) занятых слотов не ноль;
            // (3) мест не ноль и они ПОДМНОЖЕСТВО множества думки
            // (alive − dead), потому что дверь мест фильтрует мёртвых тем же
            // предикатом. Ненулевое число мест утверждается ОСОЗНАННО как
            // закон мира, а не как везение сида (§8 п.11): генерация обязана
            // поставить столицу каждой фракции и города вокруг неё (ЗАКОН
            // ГЕНЕРАЦИИ п.6), поэтому мир без мест есть сломанный мир, а не
            // неудачный сид.
            const bool slotLawDay =
                sc.alive == sc.aliveScan && sc.alive > 0 && sc.places > 0
                && sc.places <= sc.alive - sc.dead;
            if (!slotLawDay && slotLawHolds) {
                slotLawHolds = false;
                slotLawFirstBadDay = gs.worldTime.day();
                std::fprintf(stderr,
                             "[slots] ДЕНЬ %d: alive=%lld scan=%lld dead=%lld "
                             "places=%lld\n",
                             slotLawFirstBadDay, sc.alive, sc.aliveScan,
                             sc.dead, sc.places);
            }
            lastCensus = sc;
            lastAiMs = aiSecs * 1000.0;
            std::fprintf(fw, "\n");
        }

        {
            // Тот же закон на конец прогона — по store, не по пустому
            // entt-вью (см. рождение выше).
            int atSea = 0;
            for (std::size_t s = 0; s < sm::kUnifiedCap; ++s) {
                if (!macroStore->alive[s]) continue;
                const std::uint32_t idx = macroStore->cell[s].idx;
                if (!pathCost.water.empty()
                    && idx < pathCost.water.size() && pathCost.water[idx])
                    ++atSea;
            }
            std::fprintf(stderr, "[muster] AT SEA at run end: %d\n", atSea);
        }
        // Closing muster: the trade fleet is this track's working part, and
        // its health must be readable without a debugger.
        {
            int caravans = 0, vendors = 0, vIdle = 0, vAway = 0;
            long long vendorLoad = 0;
            for (std::uint16_t slot = 0;
                 slot < std::uint16_t(sm::kUnifiedCap); ++slot) {
                if (macroStore->alive[slot] == 0) continue;
                const auto& crt = macroStore->runtime[slot];
                const auto& bag = macroStore->inventory[slot];
                // Рейс сбыта — поручение крестьян (аукцион, CANON S10):
                // «вендор» смотра = артель с errand=Sell, тип умер.
                if (crt.squadType == std::uint8_t(sm::SquadType::Caravan)) {
                    ++vendors;
                    if (crt.state == 0) ++vIdle; else ++vAway;
                    // КОЛОНКА ОСТАЁТСЯ В КИЛОГРАММАХ, И ЭТО РЕШЕНИЕ ПРИБОРА,
                    // А НЕ НЕДОДЕЛКА: сменить ей единицу значило бы сравнить
                    // граммы с килограммами прошлой базы и назвать это
                    // сдвигом мира (§5 п.7 — прибор читается по ИМЕНИ
                    // колонки). Обрезание то же, что было: `(long long)` от
                    // килограммов во флоате и целое деление на 1000 дают один
                    // и тот же пол.
                    vendorLoad += (long long)(sm::inventory_mass_g(bag.inv)
                                              / 1000);
                    continue;
                }
                // ЗОНД КАРАВАНОВ СНЯТ 2026-09-21: род NPCType::Merchant
                // снесён — караван был сквадом, притворившимся видом.

            }
            std::fprintf(stderr,
                         "[balance] caravans=%d vendors=%d (idle=%d away=%d "
                         "load=%lldkg)\n",
                         caravans, vendors, vIdle, vAway, vendorLoad);
        }

        // ── Laws (the invariants of work_vector §1, v0 set) ──────────────
        // Assert relations, not retuned literals: the world must still be
        // inhabited after the run — a dead world is a broken economy, not a
        // tuning choice. Finer laws (conservation to the unit, price
        // corridors) land with the mechanics that make them checkable.
        long long popEnd = 0;
        int alive = 0;
        sm::for_each_place(*macroStore, [&](std::uint16_t slot) {
            const sm::SquadType kind =
                sm::SquadType(macroStore->runtime[slot].squadType);
            if (kind != sm::SquadType::City
                && kind != sm::SquadType::Village) return;
            const int flock = sm::souls_flock(gs, *macroStore, slot);
            popEnd += flock;
            alive += flock > 0 ? 1 : 0;
        });
        const bool populated = popEnd > 0 && alive > 0;
        lawsHold = lawsHold && populated && slotLawHolds;

        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now()
                                          - t0).count();
        std::fprintf(stderr,
                     "[balance] seed=%u days=%d pop=%lld aliveSettlements=%d "
                     "secs=%.1f\n",
                     seed, days, popEnd, alive, secs);
        std::fprintf(stderr, "[law] world still populated: %s\n",
                     populated ? "OK" : "FAIL");
        // Перепись последнего дня — чтобы ответ читался без TSV; `aiMsDay`
        // здесь стоит рядом с числом слотов не ради удобства, а потому что
        // поодиночке они оба бессмысленны: цена думки есть цена ЭТОГО
        // населения. Стенные миллисекунды — время прибора (см. колонку).
        std::fprintf(stderr,
                     "[slots] seed=%u day=%d alive=%lld dead=%lld places=%lld "
                     "walked=%lld aiMsDay=%.3f\n",
                     seed, gs.worldTime.day(), lastCensus.alive,
                     lastCensus.dead, lastCensus.places,
                     lastCensus.alive - lastCensus.dead, lastAiMs);
        if (slotLawHolds) {
            std::fprintf(stderr,
                         "[law] store slot bookkeeping (aliveCount == scan, "
                         "places subset of walked): OK\n");
        } else {
            std::fprintf(stderr,
                         "[law] store slot bookkeeping (aliveCount == scan, "
                         "places subset of walked): FAIL (первый день "
                         "расхождения %d)\n",
                         slotLawFirstBadDay);
        }
        std::fclose(fw);
        std::fclose(fl);
    }

    return lawsHold ? 0 : 1;
}

// The world's clock, and the daily simulation it pays for.
//
// Drives:
//   • Ticks elapsed → minute/hour/day rollover (core/time.h owns the ladder).
//   • On day rollover: settlement + village daily simulation
//     (economy, garrison, history), trade-route settlement
//     and dispatch, player upkeep + ageing.
//
// This file no longer knows how long a day is in real seconds, or how much
// slower the clock runs underground. It is handed whole ticks and moves the
// world by exactly that many.

#include "macro/world_tick.h"
#include "macro/econ_day.h"
#include "macro/labour.h"   // souls_home / souls_flock — две двери душ места
#include "macro/landmark_iter.h"  // for_each_place — обход мест по слотам
#include "macro/squad.h"        // record_landmark_fact — летопись места
#include "macro/currency.h"
#include "macro/fauna.h"
#include "macro/macro_stock.h"
#include "tables/npc.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/resource_field.h"   // kGrowthEpochDays — the regrow epoch
#include "tables/seasons.h"          // season_boundary — единое окно мира (S19.2)
#include "macro/anketa.h"
#include "macro/spires.h"   // spire_orb — тир орба ОДНИМ выводом (M-233 п.8)
#include "macro/zones.h"            // the ruin's danger byte (same)
#include "macro/scent_field.h"
#include "macro/threat_field.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

// (Сторож пары EconSite↔колонка реестра умер вместе с EconSite,
// 2026-09-18: что место УМЕЕТ, теперь говорит его АНКЕТА — колонка `sheet`
// его тела, заполненная рождением из стола по роду, — а рецепт называет
// ремесло и ранг.)

// (`rand01_` умер 2026-09-22 вместе с жребием состава контейнера: его
// единственным читателем был `garrison_recruit_`, бросавший монетку
// «страж или крестьянин» на КАЖДУЮ набранную душу. Состав перестал быть
// случайным — и поток вместе с ним.)

} // namespace

// ДЕНЬ СКВАДА — ТО, ЧТО ИДЁТ КАЖДЫЕ СУТКИ, И НИЧЕГО БОЛЬШЕ.
//
// ГРАНИЦА СЕЗОНА УШЛА ОТСЮДА ЦЕЛИКОМ, И ЭТО СНОС ДВОЙНОГО СУДА (M-233).
// Здесь стояла ВТОРАЯ дверь содержания — `econ_debt_boundary`, — и в день
// границы поселение судилось ДВАЖДЫ: сперва тут по старому счёту, потом
// `upkeep_season_window` по счёту, только что этой же дверью перезаписанному.
// Замер назвал цену поимённо: на дне 33 первая дверь убила НОЛЬ (Σ
// `starvedYesterday` по всем 2190 и 2213 поселениям), вторая — 51 322 и
// 51 544, потому что принимала СВЕЖИЙ сезонный счёт за недоимку и казнила
// ту его долю, которую город не мог выплатить из кармана в первый же день
// сезона — до того, как сезон хоть что-то произвёл.
//
// Четыре умения мёртвой двери НЕ ПОТЕРЯНЫ, а переехали в выжившую
// (`upkeep_season_window@src/macro/upkeep_window.h`): комфортные строки
// счёта, благополучие, прощение хвоста меньше одной рто-доли и закрытие
// ВСЕГО счёта у мёртвого. Переезд их РАСШИРИЛ: они жили за гейтом рода и
// артели с корованами не касались вовсе.
//
// Благополучие теперь ЧИТАЕТСЯ здесь, а пишется там — один писатель на
// величину, как и было, только дверь другая.
void settle_landmark_day(GameState& gs, MacroStore& st, std::uint16_t slot,
                         int day, EconFactSink sink, void* user) {
    (void)day;   // сутки больше не ветвятся: границу судит одна дверь мира
    // Плечо сквада — колонки его ТЕЛА (ломтик F): склад, счёт нужд и
    // благополучие живут в store, строки места больше не существует.
    Inventory& store = st.inventory[slot].inv;
    Wellbeing& wb = st.wellbeing[slot];
    // ДНЕВНОЕ ГАШЕНИЕ — страховочный такт той же двери (двери прихода гасят
    // долг сразу; этот такт кроет пути мимо них): вчерашний привоз и
    // сегодняшняя выпечка (econ_produce_day идёт ПЕРЕД этим днём) платят
    // по счёту не позже суток.
    econ_pay_debt(store, st.upkeep[slot].needDebt, sink, user);
    // Daily slot hygiene (CANON «Крафт/Скрап») — hygiene, not a balance.
    econ_store_hygiene(store, sink, user);

    wb.popGrowthCarry += population_delta_per_day(
        souls_flock(gs, st, slot), float(wb.seasonWellbeing) / 255.0f);
    const int whole = int(wb.popGrowthCarry);
    if (whole > 0) {
        wb.popGrowthCarry -= float(whole);
        // No ceiling (CANON S25): supply is the only cap — a place that
        // outgrows its fields pays less of its bill, its wellbeing falls and
        // the growth stops. And NO FLOOR either (owner, 2026-08-29): the old
        // minPop = 10/5 minted people from air and made every settlement
        // immortal (canon-audit B6). Убыль делает ТОЛЬКО смерть на границе;
        // ноль поглощающий по самому закону (рост от нуля = 0).
        //
        // Родившаяся душа — ГОЛОВА в инвентаре дома (у данжа — голова его
        // толпы); worked-паства поселения растёт на ФАКТ вставших: отказ
        // контейнера не рождает счётных призраков.
        const int born = settle_souls(gs, st, slot, whole);
        // ВЕДОМОСТЬ СКЛАДА ДУШ (econ_day.h SoulsBorn): единственный приход
        // на склад душ во всём мире. Убыль у склада своя — голод здесь же
        // (Starved выше), дезертирство в окне артели, бой. Доклад идёт
        // только о приходе: закон сохранения собирается из прихода, убыли
        // и уровней, а не из трёх копий одной величины.
        if (born > 0 && sink) {
            EconFact f{};
            f.kind = EconFact::Kind::SoulsBorn;
            f.amount = born;
            sink(user, f);
        }
    }
}

namespace {

// ДАНЬ — ОДИН ПРОХОД ПУЛА ФЕОДАЛЬНЫХ РЁБЕР (v121; вердикт владельца: «дань
// и феодальный граф — система фракций»). «Только место С СЮЗЕРЕНОМ должно»
// стало построением: ребро и есть феод, у вершины цепи ребра нет.
//
// БАЗА НАЧИСЛЕНИЯ — СРЕДНЕЕ СКЛАДА ВАССАЛА ЗА СЕЗОН (владелец 2026-09-02:
// «лучше среднего склада за месяц, а то пустой склад случайно — и ничего
// не платит, или наоборот»): ПАМЯТЬ МИРА с горизонтом сезона на РЕБРЕ,
// одна дверь на всех (macro/memory.h, CANON S19.2), кормится ежедневно —
// день уплаты не лотерея «уехал ли вендор этим утром». Память
// ПРЕДМАСШТАБИРОВАНА (v104: значение × горизонт — иначе склад ≤31 держал
// среднее ровно ноль вечно и лестница комфорта не облагалась никогда) и
// ОДНА — стоимость склада целиком (v108, владелец: «всё в инвентаре это
// товар»). Начисление падает на границе сезона одним штампом МИРА:
// 1/8 от того, чем вассал располагал весь сезон (владелец 2026-09-21:
// дань — налог на ИМУЩЕСТВО, а не на приход); память читается своей
// дверью — `>> 3` по сырому полю дал бы ставку в 32 раза больше закона.
void tithe_daily_(GameState& gs, MacroStore& st, int day) {
    FactionState& f = gs.factions;
    const int season = day / kDaysPerSeason;
    const bool assess = season_boundary(day)
                     && f.fiefSeasonAssessed != season;
    if (assess) f.fiefSeasonAssessed = season;
    for (int i = 0; i < f.fiefTotal; ++i) {
        TitheEdge& e = f.fief[i];
        const MacroHandle v = place_handle_by_ordinal(st,
                                                      std::uint32_t(e.vassal));
        if (!st.valid(v)) continue;   // вассал умер — ребро снимет смена феода
        memory_track(e.avgValue,
                     inventory_value(st.inventory[v.slot].inv));
        if (assess) e.owedValue += memory_value(e.avgValue) >> 3;
    }
}

// The pure econ steps are landmark-blind (they see one Inventory); this relay
// stamps the landmark id onto every fact on its way to the listener — the
// daily tick is the ONE caller that knows whose store it handed in.
struct EconFactRelay {
    EconFactSink sink = nullptr;
    void* user = nullptr;
    int landmarkId = -1;
};

void relay_econ_fact_(void* user, const EconFact& fact) {
    const auto* r = static_cast<const EconFactRelay*>(user);
    EconFact stamped = fact;
    stamped.landmarkId = r->landmarkId;
    r->sink(r->user, stamped);
}

// ── ДЕНЬ СКВАДА — ОДИН ПРОХОД, НИ ОДНОГО ГЕЙТА ПО РОДУ (M-233) ───────────
//
// Здесь стояли ДВА БЛИЗНЕЦА — `tick_settlements_` и `tick_villages_`, — и
// различались они РОВНО ОДНОЙ строкой: город передавал право монеты своей
// фракцией, деревня не передавала ничего. Всё остальное было копией: тот же
// крафт, тот же счёт, та же летопись. Два ответа на один вопрос мира
// («что сквад делает за сутки») — DOD п.6, и оба за гейтом рода.
//
// ПРАВО МОНЕТЫ ОКАЗАЛОСЬ НЕ ПРАВОМ РОДА, А КОЛОНКОЙ АНКЕТЫ, И ЭТО СНЯЛО
// ЕДИНСТВЕННОЕ РАЗЛИЧИЕ. Минтовый рецепт гейтится `recipe_known(hands, …)`
// — скилами СВОЕГО листа, — а аргумент фракции лишь называет СЕМЬЮ монет
// (`faction_mint_rows(-1)` возвращает `{-1,-1,-1}`, и кандидат отпадает
// fail-closed). Значит фракция передаётся ВСЕМ без изменения поведения, а
// старая асимметрия была дефектом: деревня, чей лист умеет монетный двор,
// не могла бить монету своей фракции — не потому, что не вправе, а потому
// что её ветка аргумента не передавала. Комментарий на месте сам обещал
// «право станет колонкой, когда место начнёт отличаться от своего рода» —
// колонкой оно уже было, и ею же осталось.
//
// ГЕЙТЫ `!= City` И `!= Village` УМЕРЛИ, И МИР ОТ ЭТОГО ПРИБЫЛ: шпиль,
// руина, логово, святилище, шахта и башня не получали дневной экономики
// НИКОГДА — проход до них просто не доходил, и ветка данжа внутри общего
// хвоста была мёртвым кодом в продакшене. Теперь живут все.
//
// ЧЕГО ЗДЕСЬ ЕЩЁ НЕТ, И ЭТО НАЗВАНО ВСЛУХ: проход идёт по `for_each_place`,
// то есть по НЕПОДВИЖНЫМ. Расширить его на артели и корованы нельзя до
// того, как `souls_flock@src/macro/labour.h` научится отвечать за ХОДОКА:
// у подвижного рода `bornPopBase == 0`, и дверь возвращает ему worked-число
// КЛЕТКИ, НА КОТОРОЙ ОН СТОИТ, — чужую паству, по которой рост тут же
// дописал бы ему душ в чужой дом. Это вопрос «что такое паства у того, у
// кого нет своей клетки», и он идёт владельцу, а не выдумывается здесь.
void tick_squads_day_(GameState& gs, MacroStore& st, int day,
                      EconFactSink sink, void* user) {
    for_each_place(st, [&](std::uint16_t slot) {
        const int id = int(st.spawnId[slot].index);
        Inventory& store = st.inventory[slot].inv;
        EconFactRelay relay{sink, user, id};
        const EconFactSink rs = sink ? &relay_econ_fact_ : nullptr;
        void* ru = sink ? static_cast<void*>(&relay) : nullptr;
        // Сквад КРАФТИТ прежде, чем ест: сегодняшний стол первым, с того же
        // единственного инвентаря, который набивают корованы и с которого
        // торгует рынок. Руки ранжируются по стоимости выхода на день
        // (econ_day.h), рецепт берётся из АНКЕТЫ — её скилы и есть «что
        // этот сквад умеет».
        // Ноль душ держит ноль верстаков: один `max(1, …)` печатал
        // призрачного работника пустому городу — то же рождение из воздуха,
        // что делал пол населения.
        const int heads = souls_home(st, slot);
        econ_produce_day(store, st.upkeep[slot].needDebt,
                         st.sheet[slot].skills,
                         heads > 0
                             ? std::max(1, heads / kCreaturesPerCityWorker)
                             : 0,
                         heads, rs, ru,
                         faction_or_freefolk(
                             std::int16_t(st.kind[slot].factionIdx)));

        // Сутки: гашение счёта, гигиена склада, рост по благополучию.
        // Границу сезона судит ОДНА дверь мира (upkeep_season_window), и
        // летопись голода пишется там же, где суд.
        // (Дань ушла из пер-местного дня: её ведёт один проход пула
        // феодальных рёбер tithe_daily_ — v121, род 6.)
        settle_landmark_day(gs, st, slot, day, rs, ru);
    });
}

} // namespace

namespace {

// ── Daily player tick (age only) ──────────────────────────────
// The player's UPKEEP left this function on 2026-09-17 («общее содержание —
// игрок == нпц», CANON S14/S19.2): his squad pays board and wage through THE
// one season window every squad pays through (npc_ai squad_season_window) —
// no CHA haggling (upkeep is maintenance, not a deal), the wage burns into
// the loot pool like everyone's. Ageing needs no body.
void tick_player_daily_(PlayerState& p) {
    p.ageDays += 1;
}

} // namespace

// The kind's own context score, resolved the way its GENESIS pass resolved
// it (spires.cpp reads the spell's tier, ruins.cpp the site's danger byte)
// — the cell_facts precedent: the score's source is inherently per-kind
// context, and it is recomputed rather than stored so the save never
// carries a second copy of what the world already knows.
static int landmark_context_score(const MacroWorld& w, int x, int y) {
    // Тир орба — ОДИН вывод на весь мир (spire_orb@src/macro/spires.h,
    // M-233 п.8): здесь стояло ВТОРОЕ его написание, побайтово то же, что в
    // сборщике фактов клетки. Выкачанный шпиль забыл свой спелл, поэтому
    // падает на байт зоны ниже — гейт расстановки и так заставил зону
    // следовать тиру. Род сквада этой функции больше не нужен ВОВСЕ: смысл
    // числу задаёт фича клетки, а она здесь и спрашивается.
    if (const int tier = spire_orb(w, x, y).tier; tier > 0) return tier;
    return w.zones ? int(w.zones->at(x, y)) : 0;
}

void regrow_dungeon_populations(const MacroWorld& w, MacroStore& st, int day) {
    if (!w.gs) return;
    const GameState& gs = *w.gs;
    for_each_place(st, [&](std::uint16_t slot) {
        const SquadType kind = SquadType(st.runtime[slot].squadType);
        const LandmarkDef& def = landmark_def(kind);
        if (def.bornPopBase == 0) return;   // settlements keep their own law
        if (souls_flock(gs, st, slot) <= 0) return;  // wiped clean stays dead
        const int id = int(st.spawnId[slot].index);
        if ((id % kGrowthEpochDays) != (day % kGrowthEpochDays)) return;
        const int x = ecs::cell_x(st.cell[slot], gs.mapW);
        const int y = ecs::cell_y(st.cell[slot], gs.mapW);
        const int mean = int(def.bornPopBase)
                       + int(def.bornPopPerScore)
                             * landmark_context_score(w, x, y);
        if (souls_flock(gs, st, slot) < mean) {
            // Отросшая душа данжа — ГОЛОВА его толпы (переворот v122,
            // вердикт 3): слабейшая строка полосы crowdHabitat, тем же
            // выводом, что и генезис (шпиль → Imp, руина → CaveBat).
            const NPCType crowd = weakest_crowd_kind(kind);
            if (crowd != NPCType::Count) {
                creatures_push_stack(st.inventory[slot].inv, crowd,
                                     npc_def(crowd).baseLevel, 1);
            }
        }
    });
}

void reset_world_tick_runtime(WorldTickRuntime& runtime, std::uint32_t seed) {
    runtime = WorldTickRuntime{};
    runtime.jitter = Rng{seed ^ 0xC0FFEEu};
}

WorldTickResult advance_world_clock(GameState& gs, WorldTickRuntime& runtime,
                                    std::uint64_t ticks) {
    WorldTickResult result{};
    if (ticks == 0) return result;

    // The clock used to be walked forward one minute at a time so it could
    // count the rollovers on the way. It does not have to be: minutes, hours
    // and days are all linear in the tick (core/time.h), so what an advance
    // covered is a subtraction, however large the jump. A month of resting and
    // a single frame take the same three lines and the same instant lands on
    // the same tick either way — that is the drift test's whole claim.
    const std::uint64_t before = gs.worldTime.tick;
    gs.worldTime.tick = before + ticks;
    const std::uint64_t after = gs.worldTime.tick;

    result.ticksAdvanced   = int(ticks);
    result.minutesAdvanced = int(absolute_minute(after) - absolute_minute(before));
    result.hoursAdvanced   = int(absolute_hour(after)   - absolute_hour(before));
    result.daysAdvanced    = day_of(after) - day_of(before);

    // One queued daily simulation tick per day that rolled over, whether the
    // advance crossed one midnight or forty.
    for (int i = 0; i < result.daysAdvanced; ++i) {
        if (runtime.pendingDailyTicks == 0) {
            runtime.nextDailyTickDay = day_of(before) + 1 + i;
        }
        ++runtime.pendingDailyTicks;
    }
    return result;
}

int process_world_daily_ticks(GameState& gs, MacroStore& st,
                              WorldTickRuntime& runtime,
                              int max_daily_ticks, MacroWorld* macro) {
    if (max_daily_ticks <= 0) return 0;

    int processed = 0;
    // The economy's listener rides the envelope (macro_world.h): null macro
    // or null sink both read as "nobody is listening" — the honest state of
    // the live game today; the balance harness is the first subscriber.
    const EconFactSink esink = macro ? macro->econFacts : nullptr;
    void* euser = macro ? macro->econFactsUser : nullptr;
    while (runtime.pendingDailyTicks > 0 && processed < max_daily_ticks) {
        const int day = runtime.nextDailyTickDay;
        // ДЕНЬ СРОКА СВЯЗЕЙ (interests.h): срочные отношения — перемирия,
        // контракты — убавляются на день, дотикавшие снимаются. Стоит ДО
        // остального дня намеренно: истёкший вассалитет не должен успеть
        // начислить дань за день, которого у него уже нет.
        //
        // Сегодня все связи мира бессрочны (феод ставится с term = 0), и
        // проход не снимает ничего. Он существует ВМЕСТЕ со своим законом,
        // а не вместо него: колонка срока без тика была бы ровно той
        // половиной, которой §55 посвящён целиком.
        // ГЕЙТ РОДА СНЯТ (M-233, попутно): колонка `interests` стоит у
        // КАЖДОГО слота (store.h), а тик срока шёл по `for_each_place`, то
        // есть только по неподвижным. Срочные связи артели, корована и
        // сборщика не убывали НИКОГДА — их перемирия и контракты были
        // вечными не по замыслу, а потому что проход до них не доходил.
        // Проход капом, а не населением (ЗАКОН СТАБИЛЬНОСТИ).
        for (std::uint32_t slot = 0; slot < kMacroEntityCap; ++slot) {
            if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
            interests_tick_day(st.interests[slot]);
        }
        tick_squads_day_(gs, st, day, esink, euser);
        tithe_daily_    (gs, st, day);  // дань — проход рёбер рода 6 (v121)
        tick_player_daily_(gs.player);

        // The ONE growth/diffusion law (R2 track): every resource field is
        // born from time and context through the same walker — the forest
        // plants the forest, beasts breed where beasts are, wheat replants
        // its fertility, and iron strikes where the world ran scarce (the
        // old bespoke W2c roll is now the Iron row's Geology domain). The
        // cadence is GAME days, so resting a month grows what a month
        // grows, however few frames it took.
        if (macro && day > 0) {
            resource_fields_daily_growth(*macro, day);
        }

        if (macro && macro->world && macro->terrain) {
            // The fleet law (npc_spawn.h): a city without a caravan outfits
            // one from its population — losses stay permanent, the trade
            // arm regrows through the world (CANON S4).
            // Поле угрозы (threat_field.h): влить свежие Died-факты,
            // диффузия по мембранам, распад — ПЕРЕД ротацией, чтобы
            // аукцион дня (патрули стражи, страх артелей) читал уже
            // сегодняшнее поле.
            threat_field_daily(*macro, day);
            // Поля следов фракций (scent_field.h, CANON S10 «хищник-
            // жертва»): диффузия конуса запаха + распад — та же дверь дня,
            // что и threat; вклады пишет think-свип, здесь только физика.
            scent_ensure(gs.scent, gs.mapW, gs.mapH);
            scent_field_daily(gs.scent, day);
            // The dungeon garrisons regrow by the fauna law (§42): one
            // soul per epoch while alive, wiped clean stays dead.
            regrow_dungeon_populations(*macro, st, day);
            // (ЗДЕСЬ СТОЯЛИ ДВА СЕЗОННЫХ ПРОХОДА — опись округи и
            // ведомость цен, — и оба уничтожены 2026-09-30, ломтик E
            // шаг 2: они были КЭШАМИ в колонках МЕСТА, а места больше
            // нет. Цена дома считается живьём в точке решения, цена
            // чужого рынка — абсолютная стоимость строки. CANON S10.)
            // The labour rotation (npc_ai.h): yesterday's crews dissolve
            // into the population, today's are raised to its size.
            rotate_worker_squads(*macro, day);
            // Daily bag hygiene (the auto-scrap half of the old feed loop).
            squad_bags_hygiene_daily(*macro);
        }

        // ── СУД ГРАНИЦЫ СЕЗОНА — ОДНА ДВЕРЬ НА ВЕСЬ МИР, И ОНА ВНЕ ГЕЙТОВ ──
        // На границе КАЖДЫЙ контейнер платит харч и жалованье на сезон
        // вперёд; взыскание пропорционально, в рто-единицах (ЗАКОН КОНСТАНТ:
        // кромка «покрыто целиком или не списывается» и доля 1/8 умерли
        // 2026-09-21). Сквад игрока платит здесь как все («игрок == нпц»).
        //
        // ЗДЕСЬ БЫЛО ДВА ГЕЙТА, И ОБА СНЯТЫ (M-233). Первый — род: суд жил
        // ДВУМЯ дверьми, и городскую половину звал дневной проход под
        // `!= City`/`!= Village`, отчего поселение судилось дважды, а шпиль,
        // руина и логово — ни разу. Второй — КОНВЕРТ: вызов стоял внутри
        // `if (macro && macro->world && macro->terrain)`, то есть мир без
        // загруженного терраина не ел и не платил вовсе. Содержание с
        // терраином общего вопроса не имеет; гасить его вместе с ним —
        // ровно тот глобальный флаг, что запрещает ЗАКОН ДВУХ ТЕМПОВ п.5.
        // Теперь дверь просит только то, чем судит: `gs` (пулы мира) и `st`
        // (слоты), и день зовёт её всегда.
        squad_season_window(gs, st, day, esink, euser);

        --runtime.pendingDailyTicks;
        ++runtime.nextDailyTickDay;
        ++processed;
    }
    if (runtime.pendingDailyTicks == 0) runtime.nextDailyTickDay = 0;
    return processed;
}

WorldTickResult tick_world(GameState& gs, MacroStore& st,
                           WorldTickRuntime& runtime,
                           std::uint64_t ticks, int max_daily_ticks,
                           MacroWorld* macro) {
    WorldTickResult result = advance_world_clock(gs, runtime, ticks);
    result.dailyTicksProcessed =
        process_world_daily_ticks(gs, st, runtime, max_daily_ticks, macro);
    result.dailyBudgetExhausted = runtime.pendingDailyTicks > 0;
    return result;
}

WorldTickResult tick_world_time_only(GameState& gs, WorldTickRuntime& runtime,
                                     std::uint64_t ticks) {
    WorldTickResult result = advance_world_clock(gs, runtime, ticks);
    result.dailyBudgetExhausted = runtime.pendingDailyTicks > 0;
    return result;
}

WorldTickResult tick_world_subworld_steps(GameState& gs,
                                          WorldTickRuntime& runtime,
                                          std::uint64_t steps) {
    // Underground the day stretches: kSubworldTickDivisor simulation steps buy
    // one tick of world time. The leftover steps stay in the runtime as a whole
    // number, so pausing, saving or walking out mid-divisor loses nothing.
    const std::uint64_t total = runtime.subworldStepRemainder + steps;
    runtime.subworldStepRemainder = total % kSubworldTickDivisor;
    return tick_world_time_only(gs, runtime, total / kSubworldTickDivisor);
}

} // namespace sm

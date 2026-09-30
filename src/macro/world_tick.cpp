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
#include "macro/characters.h"   // landmark_sheet — анкета места (что оно умеет)
#include "macro/econ_day.h"
#include "macro/labour.h"   // souls_home / souls_flock — две двери душ места
#include "macro/currency.h"
#include "macro/fauna.h"
#include "macro/macro_stock.h"
#include "tables/npc.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/resource_field.h"   // kGrowthEpochDays — the regrow epoch
#include "tables/seasons.h"          // season_boundary — единое окно мира (S19.2)
#include "macro/anketa.h"           // the spire's tier (regrow context score)
#include "macro/zones.h"            // the ruin's danger byte (same)
#include "macro/scent_field.h"
#include "macro/threat_field.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

// (Сторож пары EconSite↔колонка реестра умер вместе с EconSite,
// 2026-09-18: что место УМЕЕТ, теперь говорит его анкета — characters.h
// landmark_sheet, — а рецепт называет ремесло и ранг.)

// (`rand01_` умер 2026-09-22 вместе с жребием состава ростера: его
// единственным читателем был `garrison_recruit_`, бросавший монетку
// «страж или крестьянин» на КАЖДУЮ набранную душу. Состав перестал быть
// случайным — и поток вместе с ним.)

} // namespace

// The shared tail of every landmark's day (W2b-4): the debt boundary off the
// UNIVERSAL inventory, then ONE measure — благополучие — driving the
// population law (owner's ruling — no flat heads per day). At namespace
// scope (external linkage, the shuffled_order pattern) so econ_v1_test can
// drive a landmark to its honest death directly.
void settle_landmark_day(GameState& gs, Landmark& lm, int day, bool& starved,
                         bool& diedOut, EconFactSink sink, void* user) {
    starved = false;
    diedOut = false;
    // Переворот населения (v122): у поселения паства — worked-ЧИСЛО фичи, и
    // всякая её убыль/прибыль идёт ПАРОЙ — число И головы в инвентаре; у
    // данжа (bornPopBase != 0) паства и есть головы, worked не трогается
    // (под FT_Spire там живёт спелл).
    const bool dungeon = landmark_def(lm.type).bornPopBase != 0;
    // THE SEASON WINDOW (CANON S19.2, единое окно мира) — теперь граница
    // ДОЛГА (CANON S10, вердикт 2026-09-19): взыскание прошлого счёта,
    // новый счёт, немедленное гашение из склада. Её вердикт — ОДНО число,
    // благополучие, которое место носит до следующей границы: по нему
    // каждый день идёт рост, и второй меры («настроение») в мире больше
    // нет (владелец 2026-09-19: «теперь только есть благополучие и оно
    // даёт рост»).
    if (season_boundary(day)) {
        const ConsumeOutcome o = econ_debt_boundary(
            lm.inventory, lm.needDebt, souls_home(lm), sink, user);
        // СМЕРТЬ — единственная кара голода: доля непогашенного хлеба
        // уходит населением здесь, в единственной двери. Умирают ДОМАШНИЕ
        // головы; у поселения то же число сходит с worked-паствы (drain:
        // списывается ФАКТ — сколько голов реально стояло).
        if (o.starvedPop > 0) {
            // ГОЛОВЫ УЖЕ СНЯТЫ ГРАНИЦЕЙ (econ_debt_boundary исполняет
            // взыскание там же, где судит его: иначе новый счёт выставлялся
            // бы по составу, которого уже нет). Здесь остаётся ВТОРОЙ
            // носитель — паства: число фичи падает на съеденных, у данжа
            // паства и есть головы, и падать ей уже не надо.
            if (!dungeon) {
                worked_write(gs, lm.x, lm.y,
                             std::max(0, worked_read(gs, lm.x, lm.y)
                                             - o.starvedPop));
            }
            diedOut = souls_flock(gs, lm) == 0;
        }
        lm.starvedYesterday = std::uint16_t(std::min(o.starvedPop, 0xFFFF));
        starved = o.starvedPop > 0;
        lm.seasonWellbeing = std::uint8_t(std::lround(
            std::clamp(o.wellbeing, 0.0f, 1.0f) * 255.0f));
    }
    // ДНЕВНОЕ ГАШЕНИЕ — страховочный такт той же двери (двери прихода гасят
    // долг сразу; этот такт кроет пути мимо них): вчерашний привоз и
    // сегодняшняя выпечка (econ_produce_day идёт ПЕРЕД этим днём) платят
    // по счёту не позже суток.
    econ_pay_debt(lm.inventory, lm.needDebt, sink, user);
    // Daily slot hygiene (CANON «Крафт/Скрап») — hygiene, not a balance.
    econ_store_hygiene(lm.inventory, sink, user);

    lm.popGrowthCarry += population_delta_per_day(
        souls_flock(gs, lm), float(lm.seasonWellbeing) / 255.0f);
    const int whole = int(lm.popGrowthCarry);
    if (whole > 0) {
        lm.popGrowthCarry -= float(whole);
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
        const int born = settle_souls(gs, lm, whole);
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
void tithe_daily_(GameState& gs, int day) {
    FactionState& f = gs.factions;
    const int season = day / kDaysPerSeason;
    const bool assess = season_boundary(day)
                     && f.fiefSeasonAssessed != season;
    if (assess) f.fiefSeasonAssessed = season;
    for (int i = 0; i < f.fiefTotal; ++i) {
        TitheEdge& e = f.fief[i];
        const Landmark* v = landmark_by_id(gs, e.vassal);
        if (!v) continue;   // вассал умер — ребро снимет смена феода
        memory_track(e.avgValue, inventory_value(v->inventory));
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

// ── Settlement daily tick ─────────────────────────────────────
// (Гарнизонная подсистема — garrison_upkeep_/recruit_/trim_/cap_ — умерла
// 2026-09-30 с контейнером Landmark::garrison, M-8: домашние души ЖИВУТ в
// инвентаре места и едят ОДНОЙ лестницей потребностей (econ_debt_boundary
// от souls_home); второе окно содержания кормило бы их дважды. Набор =
// рождение душ (settle_landmark_day), обрезки нет — оборона места вся
// толпа, излишка не существует.)

void tick_settlements_(GameState& gs, int day, WorldTickRuntime& runtime,
                       EconFactSink sink, void* user) {
    for (auto& s : gs.landmarks) {
        if (s.type != LandmarkType::City) continue;
        EconFactRelay relay{sink, user, s.id};
        const EconFactSink rs = sink ? &relay_econ_fact_ : nullptr;
        void* ru = sink ? static_cast<void*>(&relay) : nullptr;
        // The city CRAFTS before it eats: today's table first, then fair
        // shares (econ_day's three passes), off the same one inventory the
        // caravans stock and the market sells from.
        // Zero souls staff zero benches: max(1, …) alone minted a ghost
        // worker for an empty town — the same air-minting the pop floor did.
        // The production TABLE comes off the place's own registry row
        // (econSite column), not a hardcoded per-loop literal.
        // The mint right, v1: every CITY strikes its own faction's coin
        // (owner 2026-08-30; the right becomes a landmark column when a
        // place ever differs from its kind).
        econ_produce_day(s.inventory, s.needDebt,
                         landmark_sheet(s.type).skills,
                         souls_home(s) > 0
                             ? std::max(1, souls_home(s) / kHeadsPerCityWorker)
                             : 0,
                         souls_home(s), rs, ru,
                         faction_or_freefolk(s.factionIdx));

        // День границы: население ест ОДНОЙ лестницей потребностей места
        // (второго стола гарнизона больше нет — M-8, v122).
        bool famine = false, died = false;
        const int headsBefore = souls_home(s);
        settle_landmark_day(gs, s, day, famine, died, rs, ru);

        // (Дань ушла из пер-местного дня: её ведёт один проход пула
        // феодальных рёбер tithe_daily_ — v121, род 6.)
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, s.id, s.x, s.y,
                                 int(s.starvedYesterday));
        }
        if (died) {
            record_landmark_fact(gs, FactKind::Died, s.id, s.x, s.y,
                                 headsBefore);
        }
    }
}


// ── Village daily tick ────────────────────────────────────────
// No gather here any more: gathering is AGENTS now — woodcutters and
// farmers hauling real units into this same inventory (npc_ai.cpp).
void tick_villages_(GameState& gs, int day, WorldTickRuntime& runtime,
                    EconFactSink sink, void* user) {
    for (auto& v : gs.landmarks) {
        if (v.type != LandmarkType::Village) continue;
        EconFactRelay relay{sink, user, v.id};
        const EconFactSink rs = sink ? &relay_econ_fact_ : nullptr;
        void* ru = sink ? static_cast<void*>(&relay) : nullptr;
        // The village-side half of the craft door. econ_day.h promises «a
        // village-side craft later is one row with site=Village, no code» —
        // which is only true if this call exists: today no recipe carries
        // that site, so this makes nothing, and the day the row lands it
        // works with no code here either.
        econ_produce_day(v.inventory, v.needDebt,
                         landmark_sheet(v.type).skills,
                         souls_home(v) > 0
                             ? std::max(1, souls_home(v) / kHeadsPerCityWorker)
                             : 0,
                         souls_home(v), rs, ru);

        // Порядок дня границы — как у города: население ест первым (одной
        // лестницей потребностей — второго стола гарнизона больше нет, M-8).
        bool famine = false, died = false;
        const int headsBefore = souls_home(v);
        settle_landmark_day(gs, v, day, famine, died, rs, ru);

        // (Дань деревни — то же одно ребро, ведёт tithe_daily_ — v121.)
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, v.id, v.x, v.y,
                                 int(v.starvedYesterday));
        }
        if (died) {
            record_landmark_fact(gs, FactKind::Died, v.id, v.x, v.y,
                                 headsBefore);
        }
    }
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
static int landmark_context_score(const MacroWorld& w, const Landmark& lm) {
    if (lm.type == LandmarkType::Spire && w.gs) {
        // The spell is the cell's worked number (ordinal+1; 0 = drained).
        // A drained spire forgot its spell (вердикт «выкачанность = 0»), so
        // it falls to the zone byte below — the placement gate made the
        // zone track the tier anyway.
        const int orb = worked_read(*w.gs, lm.x, lm.y);
        if (orb > 0) return orb <= kSpellCount ? kSpellDefs[orb - 1].tier : 1;
    }
    return w.zones ? int(w.zones->at(lm.x, lm.y)) : 0;
}

void regrow_dungeon_populations(const MacroWorld& w, int day) {
    if (!w.gs) return;
    for (auto& lm : w.gs->landmarks) {
        const LandmarkDef& def = landmark_def(lm.type);
        if (def.bornPopBase == 0) continue;   // settlements keep their own law
        if (souls_flock(*w.gs, lm) <= 0) continue;   // wiped clean stays dead
        if ((lm.id % kGrowthEpochDays) != (day % kGrowthEpochDays)) continue;
        const int mean = int(def.bornPopBase)
                       + int(def.bornPopPerScore) * landmark_context_score(w, lm);
        if (souls_flock(*w.gs, lm) < mean) {
            // Отросшая душа данжа — ГОЛОВА его толпы (переворот v122,
            // вердикт 3): слабейшая строка полосы crowdHabitat, тем же
            // выводом, что и генезис (шпиль → Imp, руина → CaveBat).
            const NPCType kind = weakest_crowd_kind(lm.type);
            if (kind != NPCType::Count) {
                creatures_push_stack(lm.inventory, kind,
                                     npc_def(kind).baseLevel, 1);
            }
        }
    }
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

int process_world_daily_ticks(GameState& gs, WorldTickRuntime& runtime,
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
        for (Landmark& lm : gs.landmarks) interests_tick_day(lm.interests);
        tick_settlements_(gs, day, runtime, esink, euser);
        tick_villages_   (gs, day, runtime, esink, euser);
        tithe_daily_     (gs, day);   // дань — проход рёбер рода 6 (v121)
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
            regrow_dungeon_populations(*macro, day);
            // ОПИСЬ ОКРУГИ КАЖДОГО МЕСТА (npc_ai.h, владелец 2026-09-18) —
            // ПЕРЕД ротацией, потому что аукцион этой же границы читает её
            // строки вместо поиска: место описывает свою нав-округу, артель
            // только решает, куда идти. Один проход по живым клеткам родов
            // раз в сезон вместо поиска на каждую артель в каждом аукционе.
            if (season_boundary(day)) {
                survey_landmark_regions(*macro, day);
                // ВЕДОМОСТЬ (CANON S10, ярус 2) — тем же тактом и рядом:
                // опись отвечает «ЧТО рядом», ведомость — «ЧТО ПОЧЁМ».
                // Обе после дневных проходов мест, поэтому цена выписана по
                // складу и счёту УЖЕ наступившего сезона.
                publish_landmark_ledgers(gs, day);
            }
            // The labour rotation (npc_ai.h): yesterday's crews dissolve
            // into the population, today's are raised to its size.
            rotate_worker_squads(*macro, day);
            // THE SQUAD SEASON WINDOW (npc_ai.h, CANON S19.2): on the
            // boundary day every roster settles board AND pay a season
            // ahead. ВЗЫСКАНИЕ ПРОПОРЦИОНАЛЬНО (v105): доля неоплаченного
            // и есть доля ушедших. Кромка «покрыто целиком или не
            // списывается» и доля 1/8, которые здесь были описаны, умерли
            // 2026-09-21 — описание пережило закон на день.
            // The player's squad pays here like everyone («игрок == нпц»).
            squad_season_window(*macro, day);
            // Daily bag hygiene (the auto-scrap half of the old feed loop).
            squad_bags_hygiene_daily(*macro);
        }

        --runtime.pendingDailyTicks;
        ++runtime.nextDailyTickDay;
        ++processed;
    }
    if (runtime.pendingDailyTicks == 0) runtime.nextDailyTickDay = 0;
    return processed;
}

WorldTickResult tick_world(GameState& gs, WorldTickRuntime& runtime,
                           std::uint64_t ticks, int max_daily_ticks,
                           MacroWorld* macro) {
    WorldTickResult result = advance_world_clock(gs, runtime, ticks);
    result.dailyTicksProcessed =
        process_world_daily_ticks(gs, runtime, max_daily_ticks, macro);
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

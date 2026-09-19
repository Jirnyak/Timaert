// The world's clock, and the daily simulation it pays for.
//
// Drives:
//   • Ticks elapsed → minute/hour/day rollover (core/time.h owns the ladder).
//   • On day rollover: settlement + village daily simulation
//     (economy, mood, garrison, history), trade-route settlement
//     and dispatch, player upkeep + ageing.
//
// This file no longer knows how long a day is in real seconds, or how much
// slower the clock runs underground. It is handed whole ticks and moves the
// world by exactly that many.

#include "macro/world_tick.h"
#include "macro/characters.h"   // landmark_sheet — анкета места (что оно умеет)
#include "macro/econ_day.h"
#include "macro/currency.h"
#include "macro/fauna.h"
#include "macro/macro_stock.h"
#include "macro/npc.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/resource_field.h"   // kGrowthEpochDays — the regrow epoch
#include "macro/seasons.h"          // season_boundary — единое окно мира (S19.2)
#include "macro/spells.h"           // the spire's tier (regrow context score)
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

inline float rand01_(WorldTickRuntime& runtime) {
    return runtime.jitter.next_f01();
}

} // namespace

// The shared tail of every landmark's day (W2b-4): consume off the
// UNIVERSAL inventory, then let ONE continuous wellbeing drive both the
// mood and the LOGISTIC population law (owner's ruling — no flat heads per
// day). At namespace scope (external linkage, the shuffled_order pattern)
// so econ_v1_test can drive a landmark to its honest death directly.
void settle_landmark_day(Landmark& lm, int day,
                         bool& startedFamine, bool& startedRevolt,
                         bool& diedOut,
                         EconFactSink sink, void* user) {
    startedFamine = false;
    startedRevolt = false;
    diedOut = false;
    // THE SEASON WINDOW (CANON S19.2, единое окно мира) — теперь граница
    // ДОЛГА (CANON S10, вердикт 2026-09-19): взыскание прошлого счёта,
    // новый счёт, немедленное гашение из склада. The outcome is parked on
    // the landmark as a quantized wellbeing, and every day until the next
    // boundary lives off that number: the mood band and the population law
    // run daily, the boundary does not.
    if (season_boundary(day)) {
        const ConsumeOutcome o = econ_debt_boundary(
            lm.inventory, lm.needDebt, lm.population, lm.famineActive != 0,
            sink, user);
        // СМЕРТЬ — единственная кара голода (вердикт 2026-09-19): доля
        // непогашенного хлеба уходит населением здесь, в единственной
        // двери; рост выживших ниже судит только комфорт (fedPop равен
        // населению после смертей — fedFrac == 1 по построению).
        if (o.starvedPop > 0) {
            lm.population = std::max(lm.population - o.starvedPop, 0);
            diedOut = lm.population == 0;
        }
        lm.starvedYesterday = std::uint16_t(std::min(o.starvedPop, 0xFFFF));
        lm.unmetYesterday   = std::uint16_t(std::min(o.unmetComfort, 0xFFFF));
        // The TRANSITIONS are the story, not the states. A town that has been
        // hungry for a season is one famine, not thirty-two of them, and a
        // chronicle that filed the state every day would bury the day it began.
        startedFamine = o.famineActive && lm.famineActive == 0;
        lm.famineActive = o.famineActive ? 1 : 0;
        lm.seasonWellbeing = std::uint8_t(std::lround(
            std::clamp(settlement_wellbeing(o, lm.population), 0.0f, 1.0f)
            * 255.0f));
    }
    // ДНЕВНОЕ ГАШЕНИЕ — страховочный такт той же двери (порция Б проведёт
    // её через двери прихода — «сразу» без лага): вчерашний привоз и
    // сегодняшняя выпечка (econ_produce_day идёт ПЕРЕД этим днём) платят
    // по счёту не позже суток.
    econ_pay_debt(lm.inventory, lm.needDebt, sink, user);
    // Daily slot hygiene (CANON «Крафт/Скрап») — hygiene, not a balance.
    econ_store_hygiene(lm.inventory, sink, user);

    const float wellbeing = float(lm.seasonWellbeing) / 255.0f;
    const SettlementMood was = lm.mood;
    lm.mood = SettlementMood(mood_band_from_wellbeing(wellbeing));
    startedRevolt = lm.mood == SettlementMood::Revolt
                    && was != SettlementMood::Revolt;

    lm.popGrowthCarry += population_delta_per_day(lm.population, wellbeing);
    const int whole = int(lm.popGrowthCarry);
    if (whole != 0) {
        lm.popGrowthCarry -= float(whole);
        // No ceiling (CANON S25): supply is the only cap — wellbeing already
        // turned negative growth on when the fields and the trade fell short.
        // And NO FLOOR either (owner, 2026-08-29): the old minPop = 10/5
        // minted people from air and made every settlement immortal
        // (canon-audit B6). Population falls honestly to zero; zero is
        // absorbing by the law itself (population_delta_per_day(0) = 0),
        // and the DEATH is the story — the transition below files a Died
        // fact, so the chronicle mourns the place the crutch used to hide.
        // Turning the empty record into a Ruin is the S9-transition track.
        const int before = lm.population;
        lm.population = std::max(lm.population + whole, 0);
        diedOut = before > 0 && lm.population == 0;
    }
}

namespace {

// The UNIVERSAL tribute assessment, BY POSITION (owner 2026-09-02: «дают
// по 1/8 всего со склада, с округлением до меньшего»): on the place's own
// seasonal pay-day (the ONE slow cycle wages already ride) an eighth of
// EACH commodity stack — floor — and an eighth of the coin are charged
// into the per-position debts the carriers deliver IN KIND (vendor to the
// market city, courier to the capital). A slice of every stack carries the
// vassal's silver to the mint, which the old value-debt's «fattest stack»
// draw never did. Missed seasons accumulate honestly, per position. Only a
// place WITH a suzerain owes; the top of a chain is charged nothing.
void assess_tithe_(Landmark& lm, int day, bool hasSuzerain) {
    if (!hasSuzerain) return;
    // The assessment BASE is the season's AVERAGE store (owner 2026-09-02:
    // «лучше среднего склада за месяц, а то пустой склад случайно — и
    // ничего не платит, или наоборот»): a po2 EMA with the season's own
    // horizon, fed daily, so the pay day stops being a lottery of whether
    // the vendor happened to leave this morning. A young store's average
    // grows from zero, so a young world honestly owes little in its first
    // season.
    //
    // The shift is NOT symmetric, and that is the ruling, not an oversight
    // (owner 2026-09-09, audit ECON-3). A signed >> floors toward minus
    // infinity, so the step is one full unit DOWN for any shortfall at all,
    // and zero UP until the store exceeds the average by the whole horizon:
    // the memory tracks a shrinking store exactly and a growing one only
    // once the growth is worth a whole unit of memory. Two consequences,
    // both accepted: the average settles a horizon BELOW the true store, and
    // a stack that never exceeds 31 keeps an average of zero — so the small
    // stock is never tithed. Whole units are all this field can hold; a
    // rounding rule cannot invent resolution the representation does not
    // have (holding the average pre-scaled would, and was declined —
    // the tithe's weight is a balance question for a measured run, not a
    // defect to patch here).
    constexpr int kTitheAvgShift = 5;
    static_assert(1 << kTitheAvgShift == kDaysPerSeason,
                  "the tithe average's horizon IS the season");
    for (int c = 0; c < kCommodityCount; ++c) {
        lm.titheAvgGoods[c] +=
            (std::int32_t(lm.inventory.count_of(commodity_item_index(c)))
             - lm.titheAvgGoods[c]) >> kTitheAvgShift;
    }
    // The coin half of the assessment censuses COIN rows only
    // (coin_census_value) — the goods half already averages the store's
    // commodities right above, and a whole-bag valuation here would tithe
    // the same grain twice.
    lm.titheAvgCoin += (std::int64_t(coin_census_value(lm.inventory))
                        - lm.titheAvgCoin) >> kTitheAvgShift;
    // The CHARGE lands on the season boundary — the world's one window
    // (CANON S19.2; the per-ordinal pay-day smear is history, owner
    // 2026-09-17: «ДА, УМИРАЕТ»). The average above still feeds DAILY —
    // memory is not a balance.
    if (!season_boundary(day)) return;
    const int season = day / kDaysPerSeason;
    if (lm.titheSeasonAssessed == season) return;
    lm.titheSeasonAssessed = season;
    for (int c = 0; c < kCommodityCount; ++c) {
        lm.titheOwedGoods[c] += lm.titheAvgGoods[c] >> 3;
    }
    lm.titheOwedCoin += lm.titheAvgCoin >> 3;
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
// The garrison's day (§42 Инк 7) — ONE law for every kind that keeps one;
// bodies below tick_settlements_, shared by both loops.
void garrison_upkeep_(GameState& gs, Landmark& s, int day);
void garrison_recruit_(GameState& gs, Landmark& s, WorldTickRuntime& runtime);

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
                         s.population > 0
                             ? std::max(1, s.population / kHeadsPerCityWorker)
                             : 0,
                         s.population, rs, ru,
                         faction_or_freefolk(s.factionIdx));

        // Порядок дня границы: НАСЕЛЕНИЕ ЕСТ ПЕРВЫМ (settle), потом гарнизон
        // ест и ПЛАТИТ — жалованье теперь стоимостью (натурой при пустой
        // казне), и платёж раньше окна еды мог бы сжечь городской хлеб в
        // пул лута перед собственным столом.
        bool famine = false, revolt = false, died = false;
        const int headsBefore = s.population;
        settle_landmark_day(s, day, famine, revolt, died, rs, ru);

        garrison_upkeep_(gs, s, day);

        // ONE suzerain edge (S24): a place owes whoever the column names;
        // a capital (and any masterless place) names nobody.
        assess_tithe_(s, day,
                      landmark_by_id(gs, s.suzerainLandmarkId) != nullptr);
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, s.id, s.x, s.y,
                                 int(s.starvedYesterday));
        }
        if (revolt) {
            record_landmark_fact(gs, FactKind::Revolted, s.id, s.x, s.y,
                                 s.population);
        }
        if (died) {
            record_landmark_fact(gs, FactKind::Died, s.id, s.x, s.y,
                                 headsBefore);
        }

        garrison_recruit_(gs, s, runtime);
    }
}

// ── The garrison's day — ONE law for every kind that keeps one (§42) ─────
// (Defined below tick_settlements_, used by both loops — see the block.)
// Gated by the registry row's garrisonShift, never by the landmark's name:
// the City-only branches this replaces were the same class of gate the
// population door wore for two refactors.
//
// UPKEEP (owner 2026-08-30/31 + 2026-09-17; CANON S10, S19.2): an army wants
// BOARD and PAY — both are judged ONCE, at the season boundary, a season
// ahead. Each need is covered WHOLE or not debited at all («не покрыто — не
// списывать»); ANY uncovered need costs an eighth of the roster, ONCE per
// window («ВСЕ НУЖДЫ ДОЛЖНЫ БЫТЬ ПОКРЫТЫ, иначе потеря 1/8»). The paid wage
// leaves the economy INTO THE LOOT POOL. ДОМА — ВСЁ СОДЕРЖАНИЕ >>1
// («гарнизон платит пол цены содержания, как в Mount & Blade»); в поле —
// полное, и разница — «доплата за поле» в скоре патрульного аукциона.
void garrison_upkeep_(GameState& gs, Landmark& s, int day) {
    if (!season_boundary(day)) return;
    if (landmark_def(s.type).garrisonShift == 0xFFu) return;
    if (total_soldiers(s.garrison) <= 0) return;
    bool shorted = false;
    // БОРД — голодная строка лестницы, спрошенная ДВЕРЬЮ (econ_day.h
    // hunger_item_index), а не словом "bread", которое стояло здесь. Армия
    // ест ровно эту строку и ничего сверх неё — вердикт владельца
    // 2026-09-18: горожанин судится по всей лестнице, солдат по харчу и
    // плате. Разница намеренная, потому дверь и отдаёт ОДНУ строку.
    const int boardIdx = hunger_item_index();
    const int need = (total_soldiers(s.garrison) * kDaysPerSeason) >> 1;
    if (s.inventory.count_of(boardIdx) >= need) {
        s.inventory.remove_of(boardIdx, need);
    } else {
        shorted = true;
    }
    const int wage = (calculate_squad_upkeep(s.garrison) * kDaysPerSeason) >> 1;
    if (wage > 0) {
        // ОПЛАТА СТОИМОСТЬЮ (владелец 2026-09-18, currency.h
        // pay_value_dense): дефолт — монеты, но арифметикой плотности
        // value/kg, не веткой; пустая казна платит натурой. Уплаченная
        // стоимость СГОРАЕТ в пул лута, переплата хвоста — щедрость.
        if (inventory_value(s.inventory) >= wage) {
            gs.lootPoolValue += pay_value_dense(s.inventory, wage);
        } else {
            shorted = true;
        }
    }
    if (shorted) {
        int walkers = std::max(1, total_soldiers(s.garrison) / 8);
        while (walkers-- > 0 && !s.garrison.empty()) {
            SoldierRecord walker{};
            if (!s.garrison.pop_soul_back(walker)) break;
            if (!gs.deserterPool.push(walker)) {
                s.garrison.push(walker);   // pool full: the man stays
                break;
            }
        }
    }
}

// RECRUITING toward the registry target (population >> garrisonShift, §42
// Инк 7): a day's packet is at most target >> 4 — a hole cut into the
// defense heals over DAYS, the same gradualness desertion bleeds at (1/8),
// never in one morning. Souls move population → garrison as GENERIC stacks
// (CANON S4): a mass recruit has no entityId — a name is what a soul earns
// by leading, being hired into a story, or being possessed.
void garrison_recruit_(GameState& gs, Landmark& s,
                       WorldTickRuntime& runtime) {
    if (s.population < 20) return;
    const int target = garrison_target_strength(s.type, s.population);
    const int current = total_soldiers(s.garrison);
    if (current >= target) return;
    const int packet =
        std::min(target - current, std::max(1, target >> 4));
    auto gr = generate_garrison(packet,
                                [&runtime] { return rand01_(runtime); });
    const int taken = move_squad(s.garrison, gr.garrison);
    s.population = std::max(0, s.population - taken);
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
                         v.population > 0
                             ? std::max(1, v.population / kHeadsPerCityWorker)
                             : 0,
                         v.population, rs, ru);

        // Порядок дня границы — как у города: население ест первым, потом
        // гарнизон (жалованье стоимостью не выедает стол деревни), потом
        // дань (§42 Инк 7, the ONE garrison law by column).
        bool famine = false, revolt = false, died = false;
        const int headsBefore = v.population;
        settle_landmark_day(v, day, famine, revolt, died, rs, ru);

        garrison_upkeep_(gs, v, day);

        // The village owes its market city — the same one edge (CANON S24).
        assess_tithe_(v, day, landmark_by_id(gs, v.suzerainLandmarkId) != nullptr);
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, v.id, v.x, v.y,
                                 int(v.starvedYesterday));
        }
        if (revolt) {
            record_landmark_fact(gs, FactKind::Revolted, v.id, v.x, v.y,
                                 v.population);
        }
        if (died) {
            record_landmark_fact(gs, FactKind::Died, v.id, v.x, v.y,
                                 headsBefore);
        }
        garrison_recruit_(gs, v, runtime);
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
    if (lm.type == LandmarkType::Spire) {
        return lm.spellId < std::uint32_t(kSpellCount)
                   ? kSpellDefs[lm.spellId].tier
                   : 1;
    }
    return w.zones ? int(w.zones->at(lm.x, lm.y)) : 0;
}

void regrow_dungeon_populations(const MacroWorld& w, int day) {
    if (!w.gs) return;
    for (auto& lm : w.gs->landmarks) {
        const LandmarkDef& def = landmark_def(lm.type);
        if (def.bornPopBase == 0) continue;   // settlements keep their own law
        if (lm.population <= 0) continue;     // wiped clean stays dead
        if ((lm.id % kGrowthEpochDays) != (day % kGrowthEpochDays)) continue;
        const int mean = int(def.bornPopBase)
                       + int(def.bornPopPerScore) * landmark_context_score(w, lm);
        if (lm.population < mean) ++lm.population;
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
        tick_settlements_(gs, day, runtime, esink, euser);
        tick_villages_   (gs, day, runtime, esink, euser);
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

        // The deserter pool's other half. Beaten armies pour INTO it
        // (macro/squad.h) and, from here, walk back OUT of it as bands — the
        // conservation law closed. The pool is an abstract count, so the day's
        // exodus is √(pool) men and the site is the field's business, not the
        // stock's (macro/npc_spawn.h).
        if (macro && macro->world && macro->terrain) {
            raise_deserter_bands(gs, *macro->world, *macro->terrain, day);
            // The fleet law (npc_spawn.h): a city without a caravan outfits
            // one from its population — losses stay permanent, the trade
            // arm regrows through the world (CANON S4).
            replenish_caravans(gs, *macro->world, *macro->terrain);
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
            if (season_boundary(day)) survey_landmark_regions(*macro, day);
            // The labour rotation (npc_ai.h): yesterday's crews dissolve
            // into the population, today's are raised to its size.
            rotate_worker_squads(*macro, day);
            // THE SQUAD SEASON WINDOW (npc_ai.h, CANON S19.2): on the
            // boundary day every roster settles board AND pay a season
            // ahead — covered whole or not debited, any miss = 1/8 once.
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

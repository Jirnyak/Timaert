// The macro march pays the map by the HOUR (owner, 2026-10-06): a squad burns
// the ground under it every hour at the SAME weight rows the player burns,
// steered greedily or by the baked gait around expensive ground, and settled
// by the one door that also hands back rest (macro/recovery.h
// settle_pools_over_time). There is no price of a step and no wall anywhere.
//
// What is pinned here is the owner's design made live:
//   · water is dear by DATA (weight 10), so a squad walks AROUND a wet cell
//     when dry progress exists — and the ledger (SP spent) proves the detour
//     was taken for a REASON, by comparing it against a forced ford;
//   · when the map leaves no dry way, the squad FORDS and arrives: «ВЕСА БЫЛО
//     ЕДИНОЕ РЕШЕНИЕ», and a bridge becomes the CHEAP crossing instead of the
//     only one;
//   · an ocean the bar cannot pay KILLS the lord (he IS the squad): camp
//     cannot repay a sea hour, so the automaton does not pitch one, the debt
//     bites his HP by the one exhaustion law, and his death settles through
//     the standing dead-avatar doors;
//   · on LAND a spent squad makes camp: Resting, debt kept, no blood;
//   · the calibration anchor, restated on the DEAR end of the ladder: a leg
//     lasts the camp margin over its ground's burn rate, and a full bar keeps
//     a body afloat the better part of a day — one law, two walkers.
#include "check.h"
#include <vector>

#include "macro/npc_ai.h"
#include "macro/world_row.h"
#include "macro/movement_cost.h"
#include "macro/recovery.h"
#include "macro/travel.h"
#include "macro/pathfinding.h"
#include "macro/squad.h"
#include "tables/npc.h"
#include "core/torus.h"
#include "macro/store.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <utility>

namespace {

using namespace sm;

bool nearf(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

// A hand-built cost world: uniform `base` weight, cells painted wet get the
// water weight AND the water flag — the same pairing build_cost_grid bakes.
PathCostData make_grid(int w, int h, float base) {
    PathCostData g;
    g.width = w;
    g.height = h;
    g.costGrid.assign(std::size_t(w) * std::size_t(h), base);
    g.water.assign(std::size_t(w) * std::size_t(h), 0u);
    return g;
}

void paint_water(PathCostData& g, int x, int y) {
    const std::size_t i =
        std::size_t(y) * std::size_t(g.width) + std::size_t(x);
    g.costGrid[i] = biome_sp_weight(Water);
    g.water[i] = 1u;
}

// A BRIDGE, spelled the way the shipping bake spells it (pathfinding.cpp
// build_cost_grid: `costGrid[i] = cell_sp_weight(biome, feature, density)`) —
// the engineered bed goes INTO the one cost grid, and the water mask stays
// raised because the cell is still water geographically.
//
// It needs saying because until 2026-10-06 a bridge worked through a SECOND
// source of truth: `nav_can_stand` read the FeatureLayer directly, so a
// fixture could paint a bridge nowhere near the cost grid and still see a
// march cross it. That predicate is gone, the grid is the only answer, and a
// fixture that paints water over a bridge is now simply describing a world
// the generator never builds (AGENTS §8 п.8 — the фикстура-лжец).
void paint_bridge(PathCostData& g, int x, int y) {
    const std::size_t i =
        std::size_t(y) * std::size_t(g.width) + std::size_t(x);
    g.costGrid[i] = cell_sp_weight(Water, FT_Bridge);
    g.water[i] = 1u;
}

// A marching fixture: one Traveling caravan with an explicit, known sheet
// cache (bar 110 = the fresh traveller, no skills, neutral pace) so every
// number below is arithmetic, not a seed's opinion.
sm::MacroHandle make_walker(sm::MacroStore& st, int mapW, float x, float y,
                            float tx, float ty,
                            int maxSp, int hp = 100) {
    sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(int(x), int(y), mapW)};
    st.sub[h.slot] = {};   // доля клетки: фикстура рождает в центре (M-244a)
    // НОСИТЕЛЬ МАРША — ЖИВОЙ (2026-09-22). Прежде ходок ехал на роли
    // TaxCollector и сваливался в ai_nomad фолбэком снесённого ai_taxrun:
    // роль умерла, фолбэк вместе с ней, и девятнадцать законов марша разом
    // перестали проверяться ЧЕМ БЫ ТО НИ БЫЛО, оставаясь при этом целыми.
    // Теперь ходок — ТИП СКВАДА (первый ответ диспетчера): корован без
    // дома честно доходит до того же ai_nomad и просто идёт к цели.
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t{0}};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(SquadType::Caravan);
    rt.state = std::uint8_t(NPCState::Traveling);
    rt.travelRank = 0;
    rt.marathonRank = 0;
    rt.moveMult = 1.0f;
    st.runtime[h.slot] = rt;
    // ЦЕЛЬ — колонка рода 2 (M-1): марш ходит по ней.
    sm::ecs::goal_set(st.goal[h.slot],
                      ecs::cell_index(int(tx), int(ty), mapW), 0, 0,
                      std::uint8_t(NPCState::Idle));
    sm::store_rename_ordinal(st, h, 7u);
    h = sm::handle_at(st, h.slot);
    // Уровень — АНКЕТА (M-246а), той же дверью, какой рождает make_npc.
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 1, avatar_sheet_seed(st.spawnId[h.slot].index));
    // Three bars, one block (pools landing): the legs' bar is filled here
    // beside the wound, not in the march runtime.
    ecs::Pools pools{};
    pools.hp = pools.maxHp = hp;
    pools.sp = pools.maxSp = maxSp;
    st.pools[h.slot] = pools;
    return h;
}

int drive_until(GameState& gs, sm::MacroStore& st, MacroNpcAiRuntime& rt,
                const PathCostData* grid, ecs::MacroNpcRuntime& npc,
                NPCState stop, int capThinks) {
    int thinks = 0;
    while (npc.state != std::uint8_t(stop) && thinks < capThinks) {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
        ++thinks;
    }
    return thinks;
}

// Drive until the walker stands within arrival range of (tx,ty). The ledger
// (sp + carry) must be read HERE, before an arrival state starts the Idle
// regen that would quietly refill what the march charged.
bool drive_to_arrival(GameState& gs, sm::MacroStore& st, MacroNpcAiRuntime& rt,
                      const PathCostData* grid, sm::MacroHandle e,
                      float tx, float ty, int capThinks) {
    // Fresh decode EVERY look (never a reference across a tick — the
    // landing-4 grabla): the store is the cell, and a tick moves it.
    auto at = [&]() {
        const auto c = (*sm::body_state<ecs::MacroCell>(st, e));
        return MacroPos{float(ecs::cell_x(c, gs.mapW)),
                        float(ecs::cell_y(c, gs.mapW))};
    };
    for (int i = 0; i < capThinks; ++i) {
        const MacroPos p = at();
        if (torus_dist_sq(p.x, p.y, tx, ty,
                          float(gs.mapW), float(gs.mapH)) < 4.0f) {
            return true;
        }
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    const MacroPos p = at();
    return torus_dist_sq(p.x, p.y, tx, ty,
                         float(gs.mapW), float(gs.mapH)) < 4.0f;
}

// SP the whole trip charged, fractional carry included — the ledger that sees
// every sub-step a per-think position trace cannot. NEGATIVE is a legal
// reading since 2026-10-24: the regen is passive, so a march over ground
// cheaper than 13.75 SP/h ENDS WITH MORE than it started with.
float sp_spent(const ecs::Pools& pools, int fromSp) {
    return float(fromSp) - (float(pools.sp) + pools.spCarry);
}

// HALF A BAR, AND IT IS A CONDITION OF MEASURABILITY RATHER THAN A TASTE.
// Since the regen went passive a cheap march CLIMBS, and a full bar cannot
// bank rest (`settle_pools_over_time` drops the positive remainder at the
// ceiling) — so a trip begun at full reads «0 spent» whatever it did, and any
// two such trips compare as two zeros. Every ledger fixture below therefore
// starts its walker at half, and the halves are what `sp_spent` is told.
constexpr int kLedgerStartSp = 55;
void start_at_half(sm::MacroStore& st, sm::MacroHandle h) {
    st.pools[h.slot].sp = kLedgerStartSp;
    st.pools[h.slot].spCarry = 0.0f;
}

// What ONE CELL of ground this weight costs, derived from the same two doors
// the march reads (§8 п.4) rather than quoted: the hour's rate spread over the
// cells an hour covers on it. Gross — the regen belongs to the body.
float sp_per_cell(float weight) {
    return burn_stamina_per_hour(weight)
           / (kMacroWalkCellsPerHour * terrain_speed_mult(weight));
}

// A PACK THAT MAKES THE MARCH DRAIN, built the way the world builds one: a
// named back in the runtime cache and real catalogue rows in the bag, so the
// burden the sweep prices is the burden the fixture intends.
//
// It exists because of the 2026-10-24 inversion: a bare body walking ordinary
// country no longer tires AT ALL (meadow asks 4 SP/h against 13.75 of passive
// regen), so the only honest way to witness exhaustion on land is the lever
// the owner named — «если какоето метсо будет ходить то сразу перегруз будет
// жечь сп». Returns the burden, so the fixture can state it.
float load_until_the_legs_pay(sm::MacroStore& st, sm::MacroHandle h,
                              std::int32_t backG, int ironUnits) {
    (*sm::body_state<ecs::MacroNpcRuntime>(st, h)).carryPerSoulG = backG;
    auto& bag = st.inventory[h.slot].inv;
    bag.add("iron", ironUnits);             // 4 kg a row (tables/items.cpp)
    return sm::overload_burden_from_capacity(
        sm::carry_capacity_g((*sm::body_state<ecs::MacroNpcRuntime>(st, h)),
                             bag),
        bag);
}

// ── Water is walked around when dry progress exists ────────────────────────
// ...AND THE SAME LEDGER PROVES THE OTHER HALF. Avoidance alone is a weak
// claim: a greedy step that never looked at the weight grid at all would pass
// it too, because the straight line also goes around a single cell most of
// the time. So the two runs are compared — a detour that CAN be made against
// a river that must be forded — and the ford must cost strictly more. That
// comparison is the negative control, and it is immune to any recalibration
// of the knob (AGENTS §8 п.4, п.6).
void test_greedy_walks_around_a_wet_cell() {
    GameState gs{};
    gs.mapW = 32;
    gs.mapH = 32;

    const auto trip_cost = [&](bool fullRiver) {
        auto wStore_ = sm::make_macro_store();
        sm::MacroStore& st = *wStore_;
        PathCostData grid = make_grid(32, 32, 1.0f);
        if (fullRiver) {
            for (int y = 0; y < 32; ++y) paint_water(grid, 16, y);
        } else {
            paint_water(grid, 16, 10);   // one wet cell dead on the line
        }
        auto e = make_walker(st, gs.mapW, 13.0f, 10.0f, 20.0f, 10.0f, 110);
        start_at_half(st, e);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, fullRiver ? 32u : 21u);
        const bool arrived =
            drive_to_arrival(gs, st, rt, &grid, e, 20.0f, 10.0f, 64);
        const float spent =
            sp_spent((*sm::body_state<ecs::Pools>(st, e)), kLedgerStartSp);
        return std::pair<bool, float>{arrived, spent};
    };

    const auto around = trip_cost(/*fullRiver=*/false);
    const auto through = trip_cost(/*fullRiver=*/true);
    CHECK(around.first, "the walker reaches its destination past the wet cell");
    CHECK(through.first,
          "and reaches it ACROSS a full river too — water is a PRICE, not a "
          "wall (owner 2026-10-06: «ВЕСА БЫЛО ЕДИНОЕ РЕШЕНИЕ»)");
    // THE FORD COSTS STRICTLY MORE, and the margin is now DERIVED instead of
    // being a 1.5× band: since the passive regen made a road trip a net GAIN,
    // both ledgers read negative, and a multiplicative band on negatives says
    // the opposite of what it used to. Half a water cell is the floor of the
    // difference, read from the same doors the march reads.
    CHECK(through.second > around.second
                               + sp_per_cell(biome_sp_weight(Water)) * 0.5f,
          "and the ford costs strictly more than the detour, by at least half "
          "a water cell: the ledger is what proves the detour was taken for a "
          "REASON. If this fires with the two equal, the weight grid is not "
          "being read at all");
}

// ── No dry way: the river is FORDED, and a bridge is the cheaper crossing ──
// THE headline of M-236, and the exact reversal of what stood here until
// 2026-10-06 («the river is a WALL for a walker; a bridge is the door»). The
// owner retired the wall by name — «да уничтодить вторую стену она портит всё
// (ВЕСА БЫЛО ЕДИНОЕ РЕШЕНИЕ» — because a veto living beside a price meant two
// answers to one question about the world, and because it held 4328 land
// cells of every measured world outside all navigation. A bridge does not stop
// being worth building: it stops being the ONLY way across.
void test_river_is_forded_and_a_bridge_is_cheaper() {
    GameState gs{};
    gs.mapW = 32;
    gs.mapH = 32;

    const auto cross = [&](bool bridged, unsigned seed) {
        auto wStore_ = sm::make_macro_store();
        sm::MacroStore& st = *wStore_;
        PathCostData grid = make_grid(32, 32, 1.0f);
        for (int y = 0; y < 32; ++y) paint_water(grid, 16, y);   // full river
        if (bridged) paint_bridge(grid, 16, 10);   // dead on the straight line
        auto e = make_walker(st, gs.mapW, 13.0f, 10.0f, 20.0f, 10.0f, 110);
        start_at_half(st, e);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, seed);
        bool crossed = false;
        for (int i = 0; i < 96 && !crossed; ++i) {
            MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
            crossed = float(ecs::cell_x(
                          (*sm::body_state<ecs::MacroCell>(st, e)),
                          gs.mapW)) >= 18.0f;
        }
        const auto& pools = (*sm::body_state<ecs::Pools>(st, e));
        return std::tuple<bool, float, int>{
            crossed, sp_spent(pools, kLedgerStartSp), pools.hp};
    };

    const auto bare = cross(/*bridged=*/false, 22u);
    const auto bridged = cross(/*bridged=*/true, 45u);

    CHECK(std::get<0>(bare),
          "A RIVER WITH NO BRIDGE IS CROSSED. The wall is gone: a ford is "
          "expensive, and expensive is not forbidden");
    CHECK(std::get<2>(bare) >= 1,
          "and a narrow river is survived — 8 SP of water out of a 110-SP "
          "bar, so the ford costs sweat, not life (CANON S7)");
    CHECK(std::get<0>(bridged),
          "the bridge carries the same march");
    CHECK(std::get<1>(bridged) < std::get<1>(bare),
          "AND IT IS CHEAPER, which is the honest reason to build one: a bed "
          "of 1.0 over water is a cheap crossing instead of the only one");
}

// ── Deep water: the crossing is paid in BLOOD, and DROWNING is a question ─
// A walker DOES enter water since 2026-10-06 (the ford test above), so this is
// the ordinary price of a wide crossing: a body wades out bleeding, because
// the sea burns 20 SP an hour against 13.75 of regen and the quadratic bite
// follows every point of the debt.
//
// WHAT THIS FILE NO LONGER ASSERTS, AND WHY IT IS A COMMENT RATHER THAN A
// CHECK (§8 п.7 — a witness states what is honestly promised and says aloud
// what it deliberately leaves alone). Until 2026-10-24 the second half of this
// test drowned a squad outright: `camp_repays_its_hour` refused a camp in open
// water, so a body whose legs ran out at sea kept wading and died. The owner's
// inversion took the burn out of standing — a camp in the sea now RECOVERS
// (witnessed in test_road_bar_lasts_a_days_march) — so the automaton pitches
// one, wakes at half a bar, wades on, and the ocean has become slow rather
// than lethal. **CANON S7 «неоплатный океан топит лорда» / «ночёвка в море
// смертельна» is therefore no longer true of the code**, and whether it should
// be is the owner's verdict, not this file's: asserting EITHER polarity here
// would freeze an answer nobody has given.
void test_ocean_crossing_is_paid_in_blood() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    PathCostData grid = make_grid(64, 64, 1.0f);
    // A wide strait: land only at x ≥ 26.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 26; ++x) paint_water(grid, x, y);

    // THE TWO CLAIMS SPLIT INTO TWO BODIES ON 2026-10-24, and the split is the
    // law talking. They used to be one fixture — «wades out AND bleeds doing
    // it» — and that fixture is now unbuildable: what makes a body bleed is a
    // pack heavy enough to clear the camp margin in a single think, and a pack
    // that heavy makes the same body too slow to reach any shore inside a test
    // budget (measured: one march-camp cycle per ~50 thinks, two cells of
    // progress in sixty). Forcing both into one body would have meant tuning a
    // pack until the two barely coexisted — a fixture balanced on a knife, red
    // on the next recalibration for no reason about the world.
    {   // A BARE body fords the strait and comes out whole.
        auto e = make_walker(st, gs.mapW, 23.0f, 10.0f, 30.0f, 10.0f,
                             /*maxSp*/110, /*hp*/30.0f);
        start_at_half(st, e);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 23u);
        for (int i = 0; i < 60 && st.valid(e); ++i) {
            MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
        }
        CHECK(st.valid(e)
                  && float(ecs::cell_x(
                         (*sm::body_state<ecs::MacroCell>(st, e)), gs.mapW))
                         >= 26.0f,
              "a floating body wades OUT: water is exited, never entered");
        CHECK((*sm::body_state<ecs::Pools>(st, e)).hp == 30,
              "and a BARE body pays the strait in sweat alone: three cells of "
              "open water against a 110-SP bar never reach the debt the "
              "quadratic bite needs");
    }
    {   // A LADEN body in the same water goes into debt and BLEEDS for it.
        // The pack is the owner's own lever («если какоето метсо будет ходить
        // то сразу перегруз будет жечь сп»): fifty backs of iron multiply the
        // sea's hour to 1000 SP, 92 of them in one think, so the bar clears the
        // camp margin (maxSp/kCampBarDivisor = 13) in a single stride and lands
        // deep in debt instead of stopping just short of it. The bar is drawn
        // to JUST ABOVE the margin so the depth is arithmetic rather than
        // wherever a descending sequence happens to land.
        auto e = make_walker(st, gs.mapW, 10.0f, 30.0f, 20.0f, 30.0f,
                             /*maxSp*/110, /*hp*/1000000);
        const float burden = load_until_the_legs_pay(st, e, 10'000, 125);
        st.pools[e.slot].sp = 20;
        const int margin = 110 / kCampBarDivisor;
        const float drainPerThink =
            (burn_stamina_per_hour(biome_sp_weight(Water), burden)
             - rest_stamina_per_hour(110, 0)) * kAiTickGameHours;
        CHECK(drainPerThink > float(20 - margin) + 32.0f,
              "фикстура честна: одна думка под этой пачкой перешагивает маржу "
              "лагеря и уходит ГЛУБОКО за ноль — иначе тело встало бы лагерем "
              "и долга не увидело вовсе (порог укуса — 32, kExhaustion"
              "BiteDivisor)");
        const int hpBefore = (*sm::body_state<ecs::Pools>(st, e)).hp;
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 23u);
        {
            MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
        }
        CHECK((*sm::body_state<ecs::Pools>(st, e)).sp < 0,
              "одна думка марша под пачкой увела планку за ноль");
        CHECK((*sm::body_state<ecs::Pools>(st, e)).hp < hpBefore - 100,
              "and the unpayable stride was paid in blood — the quadratic "
              "bite reaches an NPC through the sweep, not only the player");
    }
    {   // THE NEGATIVE CONTROL OF THE WHOLE VERDICT, at the SWEEP level: the
        // SAME pack, the SAME open water, and the body is STANDING WHERE IT
        // MEANT TO BE. It must lose nothing — not a point of stamina, not a
        // drop of blood. This is «город может быть технически перегружен, но
        // ему всё равно» said about a squad instead of a place, and it is the
        // one assertion in this file that the 2026-10-06 order forbade.
        auto e = make_walker(st, gs.mapW, 10.0f, 50.0f, 10.0f, 50.0f,
                             /*maxSp*/110, /*hp*/1000000);
        load_until_the_legs_pay(st, e, 10'000, 125);
        st.pools[e.slot].sp = 20;
        const int hpBefore = (*sm::body_state<ecs::Pools>(st, e)).hp;
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 77u);
        for (int i = 0; i < 8; ++i) {
            MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
            tick_macro_npc_ai(mw, rt, kAiTicks);
        }
        CHECK((*sm::body_state<ecs::Pools>(st, e)).hp == hpBefore,
              "a HALTED body under fifty backs of iron, adrift in open water, "
              "loses no blood at all: the burden is a factor of a burn it does "
              "not pay");
        CHECK((*sm::body_state<ecs::Pools>(st, e)).sp > 20,
              "...and it RECOVERS while it stands there — the passive regen "
              "runs for the overloaded exactly as for the empty-handed");
    }
    // (ЗДЕСЬ СТОЯЛ ВТОРОЙ БЛОК — «далеко от берега: переправа неоплатна и
    // УБИВАЕТ», со сквадом на баре 8 и двумя душами в сумке, и он утверждал
    // `!valid(eh)`: сквад обязан был умереть и уйти с карты. Он СНЯТ, а не
    // перевёрнут, вердиктом владельца 2026-10-24.
    //
    // ПОЧЕМУ СНЯТ, А НЕ ПЕРЕПИСАН НА «НЕ УБИВАЕТ»: умирал этот сквад РОВНО
    // потому, что `camp_repays_its_hour` отказывал лагерю в воде, и тело,
    // которому кончились ноги, продолжало брести. Предикат мёртв (у привала
    // жжения нет вовсе), и теперь такое тело встаёт лагерем, добирает
    // половину планки и бредёт дальше — океан стал МЕДЛЕННЫМ, а не
    // смертельным. Утверждать «не убивает» значило бы ОХРАНЯТЬ исход, который
    // владелец не выбирал: он менял форму двух процессов, а не закон CANON S7
    // «ночёвка в море смертельна». Пока он не сказал, какой из двух миров
    // ему нужен, у этого вопроса не должно быть свидетеля ни одной полярности
    // — §8 п.7, «утверждай честно обещанное и скажи, что остальное
    // сознательно не утверждается». Вопрос лежит в отчёте сессии.)
}

// ── One exhaustion law: the step in debt bleeds, the camp does not ────────
// Owner, 2026-08-27: «истощение — это когда SP кончилось, и тогда отнимается
// HP от ДВИЖЕНИЯ по миру; остановился — отдыхаешь». On land a spent squad
// still makes camp — that is the AI's DECISION — but the step that emptied
// the bar is paid for in flesh, exactly as the player's identical step is.
// The old shape had the bite belong to water alone, so a squad could march
// itself into the ground on dry meadow for free while the player bled.
void test_land_exhaustion_makes_camp_without_blood() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    PathCostData grid = make_grid(64, 64, 2.0f);   // meadow everywhere

    // A REAL bar (110 — the minimum any row in the creature table carries,
    // measured) drawn down to just above the camp margin. The old fixture used
    // a bar of FOUR to reach the margin in a handful of thinks, and since
    // 2026-10-06 that body is not a body: the regen is a PERCENT of the bar
    // while the burn is ABSOLUTE, so four points of bar earn 0.5 SP/h against
    // the meadow's 4 and no real creature row is in that class.
    //
    // ...AND SINCE 2026-10-24 IT ALSO CARRIES A PACK, for the reason the whole
    // inversion exists: a bare body on open country no longer tires at ALL
    // (meadow asks 4 SP/h against 13.75 of passive regen), so the leg would
    // never end and this test would hang on its own cap. Six backs of iron
    // bring the meadow's hour to 28 SP, which outruns the regen and makes the
    // camp arrive — the owner's own lever, «если какоето метсо будет ходить то
    // сразу перегруз будет жечь сп», standing in for a law the ground alone no
    // longer carries.
    auto e = make_walker(st, gs.mapW, 10.0f, 10.0f, 60.0f, 10.0f, /*maxSp*/110);
    const float packBurden = load_until_the_legs_pay(st, e, 10'000, 18);
    CHECK(burn_stamina_per_hour(2.0f, packBurden)
              > rest_stamina_per_hour(110, 0),
          "фикстура честна: с этой пачкой луг наконец дороже пассивного "
          "регена, иначе марш был бы бесконечным и лагеря не случилось бы");
    st.pools[e.slot].sp = 20;   // a leg's worth above the margin
    auto& npc = (*sm::body_state<ecs::MacroNpcRuntime>(st, e));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 24u);
    const int thinks = drive_until(gs, st, rt, &grid, npc,
                                   NPCState::Resting, 32);

    CHECK(thinks < 32 && npc.state == std::uint8_t(NPCState::Resting),
          "a bar spent on land is a camp, not a catastrophe");
    const auto& campPools = (*sm::body_state<ecs::Pools>(st, e));
    CHECK(campPools.sp <= campPools.maxSp / kCampBarDivisor,
          "the legs stopped at the camp margin, the automaton's own answer");
    const float bled = 100.0f - (*sm::body_state<ecs::Pools>(st, e)).hp;
    // The camp decision lands BEFORE debt (npc_ai.h kCampBarDivisor): on
    // campable ground nobody bleeds — the bite stays a LAW for whoever
    // cannot stop (the ocean section above drowns a lord through it) or
    // chooses to keep walking (the player).
    CHECK(bled == 0.0f,
          "camp is pitched before the debt: no blood on campable ground");

    // The camp itself is free — «остановился, значит отдыхаешь». Thinks spent
    // Resting must cost nothing, or a tired squad would bleed out standing
    // still. This is the control that separates "moving in debt" from
    // "being in debt".
    const float campedAt = (*sm::body_state<ecs::Pools>(st, e)).hp;
    // The LEDGER, not the bar: regen is fractional (kRestRegenPctPerHour of a
    // 4-point bar per game hour), so eight thinks may not add a WHOLE point.
    // The file's own convention — sp + carry — is what actually moved.
    const float ledgerAt = float((*sm::body_state<ecs::Pools>(st, e)).sp) + (*sm::body_state<ecs::Pools>(st, e)).spCarry;
    for (int i = 0; i < 8; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK((*sm::body_state<ecs::Pools>(st, e)).hp == campedAt,
          "eight thinks in camp cost no blood at all");
    CHECK(float((*sm::body_state<ecs::Pools>(st, e)).sp) + (*sm::body_state<ecs::Pools>(st, e)).spCarry > ledgerAt,
          "negative control: those thinks DID pass — the bar was refilling");
}

// ── The world does not bleed out (the new law's blast radius) ─────────────
// Making the exhaustion bite universal means every squad on the map now pays
// flesh for marching in debt, where before only a drowning one did. That is a
// change to a law thousands of bodies live under, so it is not enough to
// prove one walker behaves — the question is whether a MAP of them survives
// an ordinary long haul. A hundred caravans, a season of thinks, ordinary
// ground: they may bleed, they must not die.
void test_a_map_of_marchers_survives_the_new_law() {
    GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    // MOUNTAIN ground, not meadow, and that is what forces the case: with a
    // real bar (110) the burn has to be heavy enough that 116 cells of haul
    // cannot be walked in one leg. Mountain burns 10 SP/h, so a leg lasts
    // ~9.6 game hours and the haul takes three of them — run out, camp,
    // refill, run out again. The old fixture got there with a bar of 20 on
    // meadow, and a 20-point bar is not a body any more (see the camp test
    // above): its 2.5 SP/h of regen cannot repay a meadow, so a hundred of
    // them would march until the debt killed every one.
    PathCostData grid = make_grid(128, 128, biome_sp_weight(Mountain));

    // Every one of these WILL run out, camp, refill and run out again. That is
    // the worst honest case the law can be put to, and it is the case the old
    // water-only bite never charged at all.
    constexpr int kWalkers = 100;
    std::vector<sm::MacroHandle> walkers;
    walkers.reserve(kWalkers);
    for (int i = 0; i < kWalkers; ++i) {
        const float y = float(i % 100) + 8.0f;
        sm::MacroHandle e =
            make_walker(st, gs.mapW, 4.0f, y, 120.0f, y, /*maxSp*/110);
        // Each one hauls right across the map on its own line.
        sm::store_rename_ordinal(st, e, std::uint32_t(100 + i));
        e = sm::handle_at(st, e.slot);
        walkers.push_back(e);
    }

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 77u);
    for (int think = 0; think < 600; ++think) {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    int alive = 0, bled = 0, moved = 0;
    for (const sm::MacroHandle e : walkers) {
        // A dead squad LEAVES the map (S4): a destroyed walker counts as
        // neither alive nor moved, so a regression fails the checks below
        // loudly instead of dereferencing a gone entity.
        if (!st.valid(e)) continue;
        if (st.dead[e.slot] == 0
            && (*sm::body_state<ecs::Pools>(st, e)).hp > 0.0f) ++alive;
        if ((*sm::body_state<ecs::Pools>(st, e)).hp < 100.0f) ++bled;
        if (float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(st, e)), gs.mapW)) != 4.0f) ++moved;
    }
    CHECK(alive == kWalkers,
          "a season of honest marching kills nobody: the bite is a cost, "
          "not a cull");
    CHECK(moved == kWalkers,
          "negative control: they all actually WALKED — the survival above "
          "is not the survival of a hundred bodies standing still");
    // The camp decision (kCampBarDivisor) spares every one of them: camp
    // is pitched before the debt, so dry-land marching draws no blood at
    // all — 447 caravans died of the chronic-exhaustion shape this pins
    // against (measured 2026-08-30). The bite's own negative control is
    // the ocean section above, where stopping is impossible.
    CHECK(bled == 0,
          "the camp decision spares them all: no blood where camp is "
          "possible");
}

// ── THE anchor, RESTATED ON THE DEAR END (2026-10-06) ─────────────────────
// It used to read «a fresh bar buys ~8 game hours of road, squad or player».
// That sentence cannot survive the hour quantum: the road burns 2 SP an hour
// against 13.75 of rest, so a body ON a road gains by standing there and its
// marching hours are not what the economy balances against. What the economy
// balances now is the dear end — how long a bar keeps a body going where rest
// cannot repay the burn — and movement_cost.h asserts that at compile time
// (kWaterDriftHoursPerFreshBar). The owner ruled the shift ACCEPTED, not
// tuned: «если сдвинет мир не важно не надо даже подгонять».
//
// So what the SQUAD is asked here is the other half, the one no constant can
// state: that the body walking on those constants agrees with them — its leg
// ends in a camp, after the hours the bar's burn rate says, over the cells the
// pace says. The design literals stay below, on the ladder rather than on the
// road.
void test_road_bar_lasts_a_days_march() {
    GameState gs{};
    // ЗАКОН АДРЕСА: мир ВСЕГДА квадрат и степень двойки (было 1024x8).
    // Полуширина тора не изменилась (512 > 300 целевых клеток), а
    // адресация только пришла в согласие с собой: ecs::cell_index и так
    // маскировал ОБЕ оси по mapW, то есть мир уже был квадратным для
    // адреса и тонким только для аллокации.
    gs.mapW = 1024;
    gs.mapH = 1024;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    // OPEN WATER, and the bed has moved TWICE for the same reason — the leg
    // has to END. Until 2026-10-06 it was a road; then a road's own hour
    // (2 SP) fell below the hour of rest (13.75) and the bed moved to MEADOW,
    // whose 4 SP a marching body still paid in full because the regen was off
    // for legs in motion. On 2026-10-24 the regen became passive, so the
    // meadow's 4 is now paid against the same 13.75 and open country sustains
    // an endless march too. WATER is the one row of the weight table left
    // where a march outruns the regen (20 against 13.75), which makes it the
    // only honest bed for a fixture about a leg that ends from the GROUND
    // alone — and it is why the laden fixtures elsewhere in this file carry
    // packs instead.
    const float bed = biome_sp_weight(Water);
    PathCostData grid = make_grid(1024, 1024, bed);

    // Target 300 cells EAST — well beyond what the bar can pay, and well
    // under the torus half-width so the straight step never discovers a
    // short way west around the seam.
    auto e = make_walker(st, gs.mapW, 10.0f, 4.0f, 310.0f, 4.0f, /*maxSp*/110);
    auto& npc = (*sm::body_state<ecs::MacroNpcRuntime>(st, e));
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 25u);
    const int thinks = drive_until(gs, st, rt, &grid, npc,
                                   NPCState::Resting, 400);

    const auto& pcell = (*sm::body_state<ecs::MacroCell>(st, e));
    const MacroPos p{float(ecs::cell_x(pcell, gs.mapW)),
                     float(ecs::cell_y(pcell, gs.mapW))};
    const float cells = p.x - 10.0f;
    const float hours = float(thinks) * kAiTickGameHours;
    // The leg ends at the CAMP MARGIN, not at zero (npc_ai.h
    // kCampBarDivisor — the same number the automaton reads), and the hours
    // it lasts are the margin over the NET DRAIN. The pace does not appear:
    // that is the hour quantum, stated by its absence. NET since 2026-10-24 —
    // the passive regen runs under the marching body too, and leaving it out
    // would predict a leg three times shorter than the world walks.
    const float expectHours =
        (110.0f - 110.0f / float(kCampBarDivisor))
        / (burn_stamina_per_hour(bed) - rest_stamina_per_hour(110, 0));
    // ...and the CELLS are those hours walked at the ground's own pace, which
    // is the only place terrain_speed_mult enters the economy now.
    const float expectCells =
        expectHours * kMacroWalkCellsPerHour * terrain_speed_mult(bed);
    CHECK(npc.state == std::uint8_t(NPCState::Resting),
          "the march ends in a camp, not in infinity");
    CHECK(hours > expectHours * 0.9f && hours < expectHours * 1.1f,
          "a leg lasts the camp margin over its ground's BURN RATE — hours, "
          "not cells");
    CHECK(cells > expectCells * 0.85f && cells < expectCells * 1.15f,
          "and covers those hours at the ground's own pace");

    // ── THE DESIGN, in literals ──────────────────────────────────────────
    // The ladder, said twice on purpose: movement_cost.h asserts it over the
    // constants at compile time, and THIS guards the squad that walks on them.
    CHECK(kWaterDriftHoursPerFreshBar > 8.0f
              && kWaterDriftHoursPerFreshBar < 32.0f,
          "a full bar keeps a body afloat the better part of a day — the one "
          "number the economy is anchored on now that the road is cheap");
    CHECK(burn_stamina_per_hour(biome_sp_weight(Water))
              > kFreshBarSp * kRestRegenPctPerHour,
          "open water out-burns any rest: the ocean is lethal by PRICE");
    CHECK(burn_stamina_per_hour(feature_bed_weight(FT_Road))
              < kFreshBarSp * kRestRegenPctPerHour,
          "...and a road does not, or there would be nowhere in the world to "
          "rest at all");

    // The bar the ladder is stated against is the bar the SHEET hands a fresh
    // level-1 body — the literal in movement_cost.h cannot drift away from
    // attributes.h without this line saying so.
    CHECK(bar_ceilings(Attributes{}, Skills{}, 100, 100, 100).maxSp
              == int(kFreshBarSp),
          "kFreshBarSp is the bare level-1 bar the sheet actually builds");

    // THE CAMP DECISION HAS NO SECOND QUESTION LEFT (снос `camp_repays_its_hour`
    // 2026-10-24). It asked «окупается ли здесь привал», and the inversion made
    // the answer yes everywhere: a halted body has no burn, so its bar can only
    // rise. What used to need a predicate is now ARITHMETIC OF THE DOOR, and
    // that is asserted rather than assumed — on the WORST ground in the world,
    // because the whole point of the dead predicate was that one row.
    sm::ecs::Pools camper{};
    camper.maxSp = 110;
    camper.maxHp = 1;
    camper.hp = 1;
    camper.sp = 110 / kCampBarDivisor;   // ровно порог лагеря автоматом
    const int campedFrom = camper.sp;
    sm::settle_pools_over_time(
        camper, 1.0f, 0, burn_stamina_per_hour(biome_sp_weight(Water)),
        /*resting=*/true);
    CHECK(camper.sp > campedFrom,
          "a camp in the OPEN SEA gains stamina, so the half-bar wake-up is "
          "reachable from anywhere and the drown-in-your-sleep deadlock the "
          "dead predicate guarded against is now unexpressible");
}

// ── The regen gate is «остановился», not «не сдвинулся в этот раз» ────────
// The pace is fractional, so a marching squad banks part-cells and stands on
// a quarter of its thinks. If those counted as rest, the road would pay for
// itself and no squad would ever run out of legs — which is exactly what the
// first cut of this law did. Pinned here with its negative control.
void test_banking_a_part_cell_is_not_resting() {
    GameState gs{};
    // Wide enough that the target is far inside the torus half-width: at 512
    // the greedy straight step found the SEAM a shorter way and walked west,
    // which is correct behaviour and a useless fixture.
    // ЗАКОН АДРЕСА: мир ВСЕГДА квадрат и степень двойки (было 1024x8).
    // Полуширина тора не изменилась (512 > 300 целевых клеток), а
    // адресация только пришла в согласие с собой: ecs::cell_index и так
    // маскировал ОБЕ оси по mapW, то есть мир уже был квадратным для
    // адреса и тонким только для аллокации.
    gs.mapW = 1024;
    gs.mapH = 1024;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    PathCostData grid = make_grid(1024, 1024, 1.0f);   // one long road

    auto e = make_walker(st, gs.mapW, 10.0f, 4.0f, 400.0f, 4.0f, /*maxSp*/110);
    // TEN BACKS OF IRON, so that the road DRAINS and the ledger below has a
    // signal at all. Since 2026-10-24 a bare body gains stamina on a road
    // (2 SP/h of burn against 13.75 of passive regen) and a full bar cannot
    // bank the gain, so an unladen fixture would read exactly zero spent
    // whatever the gate did — the measurement, not the law, is what the pack
    // restores. The burden is a FACTOR, so the road's ladder position is
    // untouched: this is still the cheapest ground in the world, just carried
    // across under a load.
    const float packBurden = load_until_the_legs_pay(st, e, 10'000, 25);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 91u);

    // Forty thinks of unbroken marching, far short of what the bar can pay, so
    // nothing here is confused by exhaustion or camp.
    for (int i = 0; i < 40; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    const float cells = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(st, e)), gs.mapW)) - 10.0f;
    const float ledgerSpent = 110.0f - (float((*sm::body_state<ecs::Pools>(st, e)).sp) + (*sm::body_state<ecs::Pools>(st, e)).spCarry);

    CHECK(cells > 0.0f, "the walker is on the road");
    // THE gate, measured in the quantum it is paid in: forty thinks of
    // marching cost forty thinks of NET drain, to the point — not one slice
    // less. A think quietly settled as rest would cost nothing AND earn a full
    // slice of regen, so it shows up here as a double shortfall, which is
    // exactly the defect this test was built for (the first cut of the law let
    // the road pay for itself, because a marcher banking a part-cell stands
    // still on roughly a quarter of its thinks).
    //
    // NET, because since 2026-10-24 both processes run on a marching body: the
    // expectation is burn MINUS regen, and that difference is what the bar
    // actually moves by.
    const float expectBurn =
        (burn_stamina_per_hour(1.0f, packBurden)
         - rest_stamina_per_hour(110, 0)) * kAiTickGameHours * 40.0f;
    CHECK(expectBurn > 1.0f,
          "негативный контроль фикстуры: под этой пачкой дорога ЧЕСТНО тает, "
          "значит у измерения ниже есть что измерять");
    CHECK(std::fabs(ledgerSpent - expectBurn) < 0.01f,
          "forty thinks of marching cost exactly forty thinks of net drain — "
          "not one slice less, so no think on the road was quietly paid as "
          "rest");

    // The control: the SAME body, standing at its target, DOES recover.
    sm::ecs::goal_set(st.goal[e.slot],
                      (*sm::body_state<ecs::MacroCell>(st, e)).idx, 0, 0,
                      std::uint8_t(NPCState::Idle));
    const float restingFrom = float((*sm::body_state<ecs::Pools>(st, e)).sp) + (*sm::body_state<ecs::Pools>(st, e)).spCarry;
    for (int i = 0; i < 8; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
    CHECK(float((*sm::body_state<ecs::Pools>(st, e)).sp) + (*sm::body_state<ecs::Pools>(st, e)).spCarry > restingFrom,
          "negative control: standing where it meant to be, it recovers — "
          "the gate is «остановился», and it is open");
}

// ── Перегруз универсальный всем: the pack is part of the price ────────────
// Owner's ruling, 2026-08-27. The overload surcharge was the PLAYER's alone —
// a squad hauling a ton of iron marched exactly as briskly as an empty scout,
// so the weight in its bag was a number in a panel and not a cost. Pinned
// here with the control that makes it a claim: the SAME body, the SAME road,
// the SAME distance, one of them laden.
void test_a_laden_squad_pays_for_its_load() {
    GameState gs{};
    gs.mapW = 256;
    gs.mapH = 256;   // ЗАКОН АДРЕСА: квадрат, po2
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    PathCostData grid = make_grid(256, 256, 1.0f);   // one long road

    // Bodies that SURVIVE the measurement: an overloaded march in debt bites
    // HP every moving think, and this fixture used to let the laden walker
    // march itself to death and then read the corpse-row's ledger. A dead
    // squad LEAVES the map at tick end now (CANON S4, 2026-08-29), so the
    // fixture gives both walkers the health to outlive the 30 thinks — what
    // is measured here is the PRICE of the load, not the death it can buy.
    auto light = make_walker(st, gs.mapW, 10.0f, 4.0f, 60.0f, 4.0f, /*maxSp*/110,
                             /*hp*/1e6f);
    auto heavy = make_walker(st, gs.mapW, 10.0f, 6.0f, 60.0f, 6.0f, /*maxSp*/110,
                             /*hp*/1e6f);
    // Both from HALF a bar: on a road the passive regen now outruns either
    // body's burn, and a full bar discards the gain at its ceiling — so two
    // full-bar walkers would both read «0 spent» and the gap below would be a
    // comparison of two zeros (see start_at_half).
    start_at_half(st, light);
    start_at_half(st, heavy);
    sm::store_rename_ordinal(st, heavy, 8u);
    heavy = sm::handle_at(st, heavy.slot);
    // Цели уже положены make_walker в колонку рода 2 (ряды 4 и 6).

    // A back a person actually has, and a load well past it.
    // СПИНА КЛАДЕТСЯ В КОЛОНКУ АНКЕТЫ, а обоз складывает
    // дверь `carry_capacity_g`: колонки `carryCap` больше нет.
    // Голов в контейнере у этих двух нет, значит обоз равен
    // ровно спине — тот же 40 кг, что и до правки.
    const std::int64_t capG = 40'000;
    (*sm::body_state<ecs::MacroNpcRuntime>(st, light)).carryPerSoulG =
        std::int32_t(capG);
    (*sm::body_state<ecs::MacroNpcRuntime>(st, heavy)).carryPerSoulG =
        std::int32_t(capG);
    auto& load =
        st.inventory[heavy.slot].inv;
    // A BEARABLE overload: past the back, but a price the bar can pay per
    // step. (An unbearable pack now honestly refuses to march at all — the
    // legs decline a step they cannot pay for, by the same pre-priced law.)
    load.add("iron", 15);
    CHECK(inventory_mass_g(load) > capG,
          "the fixture is honest: this load IS over the back carrying it");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 93u);
    for (int i = 0; i < 30; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const auto& lrt = (*sm::body_state<ecs::Pools>(st, light));
    const auto& hrt = (*sm::body_state<ecs::Pools>(st, heavy));
    const float lightSpent = sp_spent(lrt, kLedgerStartSp);
    const float heavySpent = sp_spent(hrt, kLedgerStartSp);
    const float lightCells = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(st, light)), gs.mapW)) - 10.0f;
    const float heavyCells = float(ecs::cell_x((*sm::body_state<ecs::MacroCell>(st, heavy)), gs.mapW)) - 10.0f;

    // STRICTLY MORE, and both numbers may legally be NEGATIVE now: on a road
    // the passive regen outruns either burn, so both bodies END UP RICHER and
    // what the pack buys is a SMALLER gain. The claim is the ordering, which is
    // the claim it always was — «the pack is a cost» means «the laden body is
    // worse off than the bare one», never «the laden body loses points».
    CHECK(heavySpent > lightSpent,
          "the laden squad paid more for the same road — the pack is a cost");
    // НОСИТЕЛЬ ЭТОГО УТВЕРЖДЕНИЯ ПЕРЕЕХАЛ, А ЗАКОН ОСТАЛСЯ (§5 п.6). Здесь
    // стояло `hrtRun.overloadCost > 0 && lrtRun.overloadCost == 0` — чтение
    // КОЛОНКИ, снесённой в M-302. Спрашивать вместо неё ту же цену у двери
    // значило бы пересчитать продакшен-формулу (§8 п.5), а цену уже
    // утверждает строка выше (`heavySpent > lightSpent`). Поэтому утверждается
    // ровно то, чем владелец определил перегруз 2026-10-21: «достаточно
    // хранить 2 значения — вес который может нести сквад и вес который
    // несёт», перегруз есть «когда больше». Это НЕЗАВИСИМЫЙ факт о фикстуре,
    // а не копия арифметики.
    const auto& lrtRun = (*sm::body_state<ecs::MacroNpcRuntime>(st, light));
    const auto& hrtRun = (*sm::body_state<ecs::MacroNpcRuntime>(st, heavy));
    const auto& lBag = st.inventory[light.slot].inv;
    const auto& hBag = st.inventory[heavy.slot].inv;
    CHECK(inventory_mass_g(hBag) > sm::carry_capacity_g(hrtRun, hBag)
              && inventory_mass_g(lBag)
                     <= sm::carry_capacity_g(lrtRun, lBag),
          "and the surcharge is on the laden one alone: перегруз есть ровно "
          "«несомый вес больше спины», и носителей у него два, не три");
    CHECK(lightCells > 0.0f && heavyCells > 0.0f,
          "negative control: BOTH of them actually walked, so the gap above "
          "is a price and not a body standing still");
}

// ── ЦЕНА ПЕРЕГРУЗА НЕ ОБРЕЗАЕТСЯ, И СПРОШЕНО ЭТО У СВИПА НА КАПАХ ────────
// M-302. До 2026-10-21 цена оседала в колонке `overloadCost` (`int16`) через
// НЕМОЙ `std::clamp(cost, 0, 32767)`, и зонд поймал обрезание В ПРОДАКШЕНЕ:
// 2 попадания из 454 679 260 пересчётов (3 сида × 128 дней), в худшем
// настоящая цена 64 489 против потолка 32 767 — час марша стоил ВДВОЕ меньше
// должного. Колонка снесена, цена едет параметром.
//
// ПОЧЕМУ СВИДЕТЕЛЬ ГОНИТ СВИП, А НЕ СПРАШИВАЕТ ДВЕРЬ ЦЕНЫ. Первая его версия
// стояла в `macro_travel_parity_test` и спрашивала
// `overload_charge_from_capacity` напрямую — на капах движка, с независимым
// якорем, выглядела образцово и ПРОШЛА МУТАЦИЮ ЗЕЛЁНОЙ (М1b: кламп возвращён
// в цепочку свипа). Причина ровно §8 п.6: дверь цены всегда была честной,
// обрезал её ПУТЬ СВИПА, куда тот свидетель не заходил. Утверждать надо то,
// что видит МИР, — SP, сгоревшие за думку.
//
// КАПЫ ЗДЕСЬ НАШИ ОБЪЯВЛЕННЫЕ ЧИСЛА, А НЕ УДОБНЫЕ (подводный камень M-298:
// две мутации ширины прошли зелёными ровно потому, что фикстуры не доходили
// до капов): `kMaxInventorySlots` слотов × самая тяжёлая строка каталога.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ, ПРОГНАНА 2026-10-21:
//   М1a  кламп возвращён, свидетель спрашивает ДВЕРЬ ЦЕНЫ напрямую
//        (первая версия, стояла в macro_travel_parity_test)  → ЗЕЛЁНЫЙ, ДЫРА
//   М1b  кламп возвращён, свидетель ГОНИТ СВИП (этот)        → КРАСНЫЙ  2/41
//   М2   носитель цены `OverloadCharge::cost` сужен до int16 → НЕСОБИРАЕМО
//        (два стража: -Wc++11-narrowing на записи + static_assert на sizeof)
//   М3   `&standing` снят в award_kill_xp (M-301)            → КРАСНЫЙ  2/69
//        (свидетель — test_a_level_does_not_strip_what_is_worn@squad_war_test)
//   М4   та же сумма standing пересчитана ВТОРОЙ раз
//        (ОБЯЗАНА МОЛЧАТЬ: форма иная, смысл тот же)         → ЗЕЛЁНЫЙ, как и
//                                                              должно
void test_an_engine_cap_load_is_priced_whole() {
    GameState gs{};
    gs.mapW = 256;
    gs.mapH = 256;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    PathCostData grid = make_grid(256, 256, 1.0f);

    // Тело, которое ПЕРЕЖИВЁТ замер: на капах жжение рвёт планку в клочья и
    // долг кусает HP, а мерить надо ЦЕНУ, а не смерть, которую она покупает.
    auto ox = make_walker(st, gs.mapW, 10.0f, 4.0f, 60.0f, 4.0f,
                          /*maxSp*/110, /*hp*/1e9f);
    sm::store_rename_ordinal(st, ox, 8u);
    ox = sm::handle_at(st, ox.slot);

    const sm::ItemDef* heaviest = nullptr;
    for (const sm::ItemDef& d : sm::item_catalog())
        if (!heaviest || d.massG > heaviest->massG) heaviest = &d;
    CHECK(heaviest && heaviest->massG == 64'000,
          "самая тяжёлая строка каталога — статуя, 64 000 г");

    auto& rtOx = (*sm::body_state<ecs::MacroNpcRuntime>(st, ox));
    // СПИНА РОВНО В ОДНУ СТРОКУ ГРУЗА — и это не удобство, а то, что делает
    // якорь независимым: трюм из `kMaxInventorySlots` таких же строк есть
    // РОВНО столько же спин, то есть множитель равен НАЗВАННОМУ капу слотов.
    // Прежде здесь стояло `carryPerSoulG = 0` («спины нет вовсе»), и под новой
    // дверью это больше не фикстура перегруза, а страж области деления: без
    // спины делить не на что, и множитель честно нейтрален.
    rtOx.carryPerSoulG = std::int32_t(heaviest->massG);
    auto& hold = st.inventory[ox.slot].inv;
    hold.add(heaviest->id, sm::kMaxInventorySlots);

    // ЯКОРЬ НЕЗАВИСИМЫЙ: масса трюма есть произведение двух НАЗВАННЫХ капов,
    // а не пересчёт продакшен-формулы (§8 п.6).
    const std::int64_t holdG = inventory_mass_g(hold);
    CHECK(holdG == std::int64_t(sm::kMaxInventorySlots) * 64'000,
          "трюм на капах движка: 1024 слота × 64 кг = 65 536 000 г");
    CHECK(sm::carry_capacity_g(rtOx, hold) == 64'000,
          "фикстура: спина ровно одна статуя, значит трюм есть 1024 спины");
    const float burden = sm::overload_burden_from_capacity(
        sm::carry_capacity_g(rtOx, hold), hold);
    CHECK(nearf(burden, float(sm::kMaxInventorySlots)),
          "множитель на капах движка равен КАПУ СЛОТОВ: тысяча двадцать "
          "четыре спины — тысяча двадцать четыре раза дороже час");

    const auto& pools0 = (*sm::body_state<ecs::Pools>(st, ox));
    const float spBefore = float(pools0.sp) + pools0.spCarry;

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 94u);
    {
        MacroWorld mw{.gs = &gs, .store = &st, .pathCost = &grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }

    const auto& pools1 = (*sm::body_state<ecs::Pools>(st, ox));
    const float spAfter = float(pools1.sp) + pools1.spCarry;
    const float burned = spBefore - spAfter;

    // ЧТО ОБЯЗАН БЫЛ СЖЕЧЬ ОДИН ПЕРЕГРУЗ: множитель входит в час СРЕДИ
    // МНОЖИТЕЛЕЙ (M-310), поэтому вклад есть ровно грунт × множитель × часы
    // думки, минус пассивный реген того же часа.
    const float hours = sm::kAiTickGameHours;
    const float wholePrice =
        (burn_stamina_per_hour(1.0f, burden) - rest_stamina_per_hour(110, 0))
        * hours;
    // ЕСЛИ МНОЖИТЕЛЬ НЕ ДОЕДЕТ ДО ЖЖЕНИЯ — а это ровно дефект M-310 наизнанку
    // и ровно то, что обрезанный носитель сделал бы молча, — час будет стоить
    // НЕЙТРАЛЬНУЮ дорогу, то есть уйдёт в ПЛЮС (реген её обгоняет).
    const float neutralPrice =
        (burn_stamina_per_hour(1.0f) - rest_stamina_per_hour(110, 0)) * hours;
    CHECK(burned > wholePrice * 0.99f && burned < wholePrice * 1.01f,
          "множитель перегруза на капах движка дошёл до жжения ЦЕЛИКОМ: "
          "сожжённое есть грунт × 1024 × часы думки, с точностью до процента");
    // НЕГАТИВНЫЙ КОНТРОЛЬ САМОГО ДЕТЕКТОРА: два кандидата обязаны РАЗЛИЧАТЬСЯ
    // в наблюдаемом, иначе утверждение выше не способно покраснеть.
    CHECK(wholePrice > 100.0f && neutralPrice < 0.0f,
          "негативный контроль: целый множитель жжёт, а потерянный — ВОЗВРАЩАЕТ "
          "планку, значит два исхода различаются даже знаком");
}

} // namespace

int main() {
    test_greedy_walks_around_a_wet_cell();
    test_river_is_forded_and_a_bridge_is_cheaper();
    test_ocean_crossing_is_paid_in_blood();
    test_land_exhaustion_makes_camp_without_blood();
    test_banking_a_part_cell_is_not_resting();
    test_a_laden_squad_pays_for_its_load();
    test_an_engine_cap_load_is_priced_whole();
    test_a_map_of_marchers_survives_the_new_law();
    test_road_bar_lasts_a_days_march();
    return sm::test::report("squad_travel_test");
}

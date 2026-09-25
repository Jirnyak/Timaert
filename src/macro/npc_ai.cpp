// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "core/stacks.h"           // kWorldSquads — резерв скрэтчей порядка
#include "macro/roster_window.h"   // ОДИН суд границы на всякий ростер
#include "macro/agent_memory.h"
#include "macro/characters.h"  // стол анкет — ступень лестницы поведения
#include "macro/chronicle.h"
#include "macro/currency.h"
#include "macro/economy.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/macro_stock.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "macro/labour.h"           // ОДИН пул рук места (CANON S4)
#include "macro/landmark_registry.h"
#include "macro/movement_cost.h"
#include "macro/npc.h"
#include "macro/nav_field.h"        // локальные поля-округи (CANON S7)
#include "macro/player_entity.h"
#include "macro/npc_spawn.h"
#include "macro/recovery.h"  // recover_bar — ОДНА дверь отдыха на все тела
#include "macro/seasons.h"   // season_boundary — окно сквадов (CANON S19.2)
#include "macro/politik.h"          // derive_city_spacing — времянка §34.1
#include "macro/settlement_score.h" // kSettlementReach — the home-field box
#include "macro/spawners.h"
#include "macro/squad.h"
#include "macro/threat_field.h"     // поле угрозы: скор патруля, страх артелей
#include "macro/travel.h"
#include "ecs/components.h"
#include "core/torus.h"
#include "core/rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace sm {

namespace {

// TS uses Math.random() inside behaviours. The C++ port uses App-owned
// MacroNpcAiRuntime RNG so world resets do not inherit hidden module state.
inline int rand_int(const TickContext& ctx, int range) {
    if (range <= 0) return 0;
    return int(ctx.rng->next_u32() % std::uint32_t(range));
}

inline float rand_f01(const TickContext& ctx) { return ctx.rng->next_f01(); }

// ── Helpers shared by all behaviours ──────────────────────────

struct XY { float x, y; };

// (`fold_d` — торова складка разности координат — умерла вместе со сканом жил:
// обход бокса от дома строит смещения сам, складывать нечего.)

// (КОРАБЛИ, ПОРТЫ И МОРСКОЙ ХОД ВЫРЕЗАНЫ 2026-09-22, сессия 14, вердикт
// владельца: «никакого транспорта корабли механики и всё пока вырезаем
// полностью из кода потому что не дошли до них». Здесь жили ships_at /
// ships_add над слоем разработки и цена корпуса kShipWoodUnits.)

// (`stamp_feature_if_bare` ВЫРЕЗАНА 2026-09-22: фичи ставит генерация мира,
// а не сквады — артель больше не строит ничего.)

XY pick_random_nearby(float cx, float cy, int range, const TickContext& ctx) {
    float nx = wrapf(cx + float(rand_int(ctx, range * 2) - range),
                     float(ctx.mapW));
    float ny = wrapf(cy + float(rand_int(ctx, range * 2) - range),
                     float(ctx.mapH));
    return {nx, ny};
}

bool home_pos(const ecs::MacroNpcRuntime& rt, const TickContext& ctx, XY& out) {
    // ONE landmark roster (v62): the id alone names the place.
    const Landmark* lm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    if (!lm) return false;
    out = {float(lm->x), float(lm->y)};
    return true;
}

// The agent's HOME STORE — where a gatherer's haul lands. The same universal
// Inventory the market sells from, resolved by the honest id.
// THE home place of a crew — one resolver, because two readers now ask for
// different parts of it (the store it delivers into, the ОПИСЬ ОКРУГИ it
// reads its worksite from).
Landmark* home_landmark(const ecs::MacroNpcRuntime& rt,
                        const TickContext& ctx) {
    return ctx.mw.gs ? landmark_by_id(*ctx.mw.gs, rt.homeSettlementId)
                     : nullptr;
}

Inventory* home_inventory(const ecs::MacroNpcRuntime& rt,
                          const TickContext& ctx) {
    Landmark* lm = home_landmark(rt, ctx);
    return lm ? &lm->inventory : nullptr;
}

// ТАКТ 1 — СДАЧА (двухтактный обоз, вердикт владельца 2026-09-19):
// зеркало разгрузки сумки для РОСТЕРА. Отряд сдаёт домой ВСЁ ездовое, ровно
// как рудокоп сдаёт всю руду: табун — имущество МЕСТА, а не личная
// собственность артели. Артель не торгуется со своей деревней — это
// перенос, а не сделка (закон займа S5).
//
// ПОЧЕМУ 100%, А НЕ ИЗЛИШЕК (измерено до правки): при «оставь себе по коню
// на душу» ловцы приватизировали упряжку и переставали ловить — за 128 дней
// сид 7 в стойлах мира стояла ОДНА голова против 11 964 в артелях, то есть
// табун не принадлежал никому, кроме тех, кто его поймал. Выдача обратно —
// такт 2 (outfit_crew_mounts ниже).
void deliver_mounts_home(entt::entity self, const ecs::MacroNpcRuntime& rt,
                         const TickContext& ctx) {
    if (!ctx.mw.world) return;
    auto* bag = ctx.mw.world->reg.try_get<ecs::NpcInventory>(self);
    if (!bag) return;
    Landmark* lm = home_landmark(rt, ctx);
    if (!lm) return;
    bool moved = false;
    // Область существ единого контейнера (M-71); обход first → 1023 =
    // старый порядок «новейший первым». Снятие слота приводит на его место
    // УЖЕ осмотренный новейший (ремонт плотности) — курсор шагает дальше.
    for (int i = bag->inv.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef stall = bag->inv.slots[std::size_t(i)];
        if (!is_mount_kind(std::uint16_t(creature_of_world_row(stall.def)))) {
            continue;
        }
        // Credit BEFORE debit (S5): a full garrison leaves the beasts IN
        // the roster rather than burning them.
        if (!creatures_push_slot(lm->inventory, stall)) continue;
        bag->inv.remove_at(i, stall.count);
        moved = true;
    }
    if (moved) refresh_squad_carry(*ctx.mw.world, self);
}

// Empty the gatherer's own bag of `id` into his home store — the shared
// arrival half of every honest work-loop (woodcutter, farmer).
void deliver_bag_home(entt::entity self, const ecs::MacroNpcRuntime& rt,
                      const TickContext& ctx, const char* id) {
    if (!ctx.mw.world) return;
    auto* bag = ctx.mw.world->reg.try_get<ecs::NpcInventory>(self);
    if (!bag) return;
    const int n = bag->inv.count(id);
    Inventory* store = home_inventory(rt, ctx);
    // Credit BEFORE debit (economy.md's conservation law): the store accepts
    // first, the bag pays only what was accepted — a full store leaves the
    // haul ON THE GATHERER'S BACK instead of burning it. (Near-unreachable
    // with 1024 slots and stack-merging, but the law is the law.)
    if (n > 0 && store && store->add(id, n)) {
        bag->inv.remove(id, n);
        // The arrival IS the gather flow: the pure econ steps announce their
        // own facts, but the agent work-loop lands its haul here — without
        // this fact every *_gathered column of the дубль-прогон reads zero
        // (measured: 4 years × 4 seeds of zeros, 2026-08-31).
        if (ctx.mw.econFacts) {
            EconFact f{};
            f.kind = EconFact::Kind::Gathered;
            f.commodity = commodity_index(id);
            f.amount = n;
            f.landmarkId = rt.homeSettlementId;
            ctx.mw.econFacts(ctx.mw.econFactsUser, f);
        }
    }
}

// The RIPEST home field, not the nearest (owner increment 2026-08-31) —
// fields live within the home's ±3 box (the worldgen stamp and the plough
// both land there). The old nearest-pick was measured to waste the whole
// ring: crews hammered the one closest parcel bare while every other field
// stood ripe and unvisited, so the village's grain flow was pinned to a
// single cell's regrowth however much land it ploughed. Standing stock
// picks the parcel now; distance only breaks ties. A box with nothing
// standing offers no work — the crew is honestly not raised that day.
bool find_home_field(const TickContext& ctx, float px, float py,
                     const XY& home, XY& out, FeatureType parcel = FT_Field) {
    if (!ctx.mw.features) return false;
    bool found = false;
    int bestStock = 0;
    float best = 1e30f;
    // Бокс — арифметика ИНДЕКСА через cell_step (ЗАКОН АДРЕСА); наружу
    // уходит ЗАВЁРНУТАЯ клетка — ровно та форма, что у find_home_deposit.
    const std::uint32_t homeIdx = cell_of(int(home.x), int(home.y), ctx.mapW);
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(homeIdx, dx, dy, ctx.mapW);
            const int cx = cell_x(n, ctx.mapW);
            const int cy = cell_y(n, ctx.mapW);
            if (ctx.mw.features->at(cx, cy) != std::uint8_t(parcel)) continue;
            const int stock =
                resource_field_read(ctx.mw, ResourceFieldId::Wheat, cx, cy);
            if (stock <= 0) continue;   // eaten bare — nothing to reap here
            const float d = torus_dist_sq(px, py, float(cx), float(cy),
                                          float(ctx.mapW), float(ctx.mapH));
            if (stock > bestStock || (stock == bestStock && d < best)) {
                bestStock = stock;
                best = d;
                out = {float(cx), float(cy)};
                found = true;
            }
        }
    }
    return found;
}

// ГДЕ ОХОТИТЬСЯ: лучшая по поголовью клетка своей округи. Зверь — природа на
// КАЖДОЙ клетке (биом задаёт вместимость), поэтому это не поиск редкости, а
// выбор лучшей из ближних — тот же бокс и тот же вид ответа, что у пашни.
bool find_home_fauna(const TickContext& ctx, float px, float py,
                     const XY& home, XY& out) {
    bool found = false;
    int bestStock = 0;
    float best = 1e30f;
    const std::uint32_t homeIdx = cell_of(int(home.x), int(home.y), ctx.mapW);
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(homeIdx, dx, dy, ctx.mapW);
            const int cx = cell_x(n, ctx.mapW);
            const int cy = cell_y(n, ctx.mapW);
            const int stock =
                resource_field_read(ctx.mw, ResourceFieldId::Fauna, cx, cy);
            if (stock <= 0) continue;   // выбито — здесь не охотятся
            const float d = torus_dist_sq(px, py, float(cx), float(cy),
                                          float(ctx.mapW), float(ctx.mapH));
            if (stock > bestStock || (stock == bestStock && d < best)) {
                bestStock = stock;
                best = d;
                out = {float(cx), float(cy)};
                found = true;
            }
        }
    }
    return found;
}

bool at_target(const MacroPos& p, const ecs::MacroNpcRuntime& rt,
               const TickContext& ctx) {
    return torus_dist_sq(p.x, p.y, rt.targetX, rt.targetY,
                         float(ctx.mapW), float(ctx.mapH)) < 4.0f;
}

// Settle the fractional SP carry into whole points — BOTH directions (march
// costs push it negative, rest regen positive), the player's fractional-carry
// idiom. The bar clamps at maxSp above and keeps its DEBT below zero, exactly
// like the player's (movement_cost.h apply_stamina_cost).
void settle_sp_carry(ecs::Pools& pools) {
    // The bar is a plain int now that it lives with its siblings — the int16
    // narrowing this wrapper existed to own went with the field it guarded.
    sm::settle_sp_carry(pools.sp, pools.maxSp, pools.spCarry);
}

// Why a think is or is not dispatched. A corpse is skipped WHOLE; a camping
// body skips only the behaviour — it still lives through the think, and the
// rhythm below still pays it. Telling the two apart is the whole reason this
// is an enum and not a bool: as one, a resting squad "failed to prepare" and
// was `continue`d past its own recovery.
enum class ThinkGate : std::uint8_t { Dead, Rest, Think };

void settle_exhaustion(entt::entity e, const MacroPos& p,
                       ecs::MacroNpcRuntime& rt, ecs::Pools& hp,
                       bool canCamp, const TickContext& ctx);

// THE standing predicate (owner 2026-08-30; CANON S7): the cell types a
// walking NPC almost never enters are exactly the cells where NO CAMP CAN
// STAND — water today, lava tomorrow, one data-driven answer. A BRIDGE is
// dry masonry over water, so it both carries a march and holds a camp. The
// player is not gated here: stepping into the sea stays his own decision,
// and the ocean drowns him by the same bar law as ever.
bool can_stand_at(const TickContext& ctx, int x, int y);

// What this leader's load is costing him per cell, asked once per think: a
// bag changes at every market, so unlike the rest of the sheet cache this one
// cannot be refreshed at birth and left alone.
void refresh_overload_cost(ecs::MacroNpcRuntime& rt,
                           const ecs::NpcInventory* bag) {
    if (!bag) { rt.overloadCost = 0; return; }
    const int cost = overload_charge_from_capacity(rt.carryCap, bag->inv).cost;
    rt.overloadCost = std::int16_t(std::clamp(cost, 0, 32767));
}

ThinkGate prepare_macro_npc_tick(ecs::MacroNpcRuntime& rt,
                                 const ecs::Pools& hp) {
    if (hp.hp <= 0) {
        rt.visualSpeed = 0.0f;
        return ThinkGate::Dead;
    }

    // Time-in-cell advances every AI tick (both sweep drivers pass through
    // here); a try_move that changes cell resets it right after.
    rt.entryTicks = saturate_entry_ticks(rt.entryTicks);

    const auto state = static_cast<NPCState>(rt.state);
    const int maxSp = std::max<int>(1, hp.maxSp);

    // Getting up is a DECISION (owner: «до скольки отдыхать — решение
    // конечного автомата»), and half a bar is this AI's answer to it. The
    // player's answer is his own (aim_rest_until_rested runs to a full bar);
    // both drink from the one regen law below, which is where the mechanic
    // ends and the decider begins.
    if (state == NPCState::Resting) {
        if (int(hp.sp) >= maxSp / 2) {
            // ЛАГЕРЬ — ПАУЗА, НЕ АМНЕЗИЯ (components.h stateAfterRest):
            // подъём возвращает ПРЕРВАННУЮ ногу — Returning остаётся
            // Returning. Старая побудка в Idle посреди дороги домой
            // отдавала артель правилу «Idle вне дома = продолжить рейс к
            // рынку», и рейс не завершался никогда.
            rt.state = rt.stateAfterRest;
            rt.stateAfterRest = std::uint8_t(NPCState::Idle);
            rt.stateTimer = 0;
        }
        rt.visualSpeed = 0.0f;
        return ThinkGate::Rest;
    }

    return ThinkGate::Think;
}

// THE rhythm of a think, settled after it, for every behaviour: a body that
// MOVED pays (and, on an empty bar, pays in flesh); a body that STOOD rests.
// One sentence, «остановился — отдыхаешь», and its exact negation.
//
// The regen used to live before the think and be gated on a STATE WHITELIST
// (Idle or Resting), which is the squads' own dialect of the same idea: a
// body doing anything else — standing at a market, waiting out a siege —
// recovered nothing, while the player recovered whenever his route was empty.
// Now both ask the one question the law actually asks: did you move?
//
// Standing where no camp is possible is not rest either — the same decision
// the bite below consults, so an ocean stays lethal without the mechanic ever
// naming water.
//
// STOPPED is not the same question as "did not change cell this think", and
// getting that wrong is how the first cut of this law paid marchers to march:
// the pace is fractional (kMacroWalkCellsPerHour against a think's slice of
// the day), so a body on the road banks part-cells and stands still on maybe a
// quarter of its thinks. Reading those as rest handed a walking squad free
// stamina and no squad ever ran out again. A body has stopped when it is where
// it meant to be, or when it has DECIDED to stop — which is exactly the
// player's own gate, «маршрут пуст», said in the squads' words.
void settle_march_rhythm(entt::entity e, const MacroPos& p,
                         ecs::MacroNpcRuntime& rt, ecs::Pools& hp,
                         bool moved, const TickContext& ctx) {
    const int maxSp = std::max<int>(1, hp.maxSp);
    const bool canCamp = can_stand_at(ctx, int(p.x), int(p.y));

    // The automaton's CAMP decision, BEFORE debt (npc_ai.h kCampBarDivisor):
    // legs below an eighth of the bar on campable ground pitch camp now; the
    // half-bar wake-up (prepare_) resumes the leg. The regen gate below
    // stays strict — banking a part-cell on the road is NOT rest (the first
    // cut of this law let the road pay for itself; its test still stands).
    if (canCamp && int(hp.sp) <= maxSp / kCampBarDivisor
        && rt.state != std::uint8_t(NPCState::Resting)) {
        rt.stateAfterRest = rt.state;   // пауза, не амнезия (components.h)
        rt.state = std::uint8_t(NPCState::Resting);
        rt.stateTimer = 0;
    }

    // A body is STOPPED when it is where it meant to be, when it DECIDED to
    // stop — or when its legs were REFUSED (owner 2026-08-31: «если встал —
    // безусловно, агностично, сразу реген»): a full step's budget standing
    // unspent after the think means the march found no step to take (no
    // standable cell closer, or a climb the bar cannot pay) — that body is
    // standing, not banking a part-cell, and the one regen law owes it rest.
    // The banking marcher never trips this: his budget is spent below one.
    // Measured 2026-08-31: a silver crew froze at 48/110 SP for days at a
    // river bank — walking nowhere, resting never.
    const bool stopped = rt.state == std::uint8_t(NPCState::Resting)
                         || at_target(p, rt, ctx)
                         || (!moved && rt.moveBudget >= 1.0f);

    // ...and `!moved` on top, because a think that arrived still MARCHED: you
    // do not walk two cells and take a slice of rest in the same breath. Rest
    // begins on the first think after the legs stop.
    if (stopped && !moved && canCamp) {
        // THE rest law, THE implementation (recovery.h rest_pools):
        // all three bars, a percent of themselves per game hour, paid out in
        // this think's slice of the day. This block used to restate the three
        // formulas inline — a drifted copy of attributes.h held together by a
        // parity test — and before that it fed only stamina, so a wounded
        // lord stayed wounded until something killed him: `ecs::Pools` had no
        // writer anywhere in the game that moved it UP. The old 5%-per-think
        // was ~53% of the bar per game HOUR — a rest that cost nothing.
        rest_pools(hp, kAiTickGameHours, int(rt.marathonRank));
    }

    // A body that took a step with its bar already spent pays for it, whether
    // or not it also counts as stopped — the two halves answer two questions.
    if (moved) settle_exhaustion(e, p, rt, hp, canCamp, ctx);
}

void set_visual_speed(ecs::MacroNpcRuntime& rt, float oldX, float oldY,
                      float newX, float newY) {
    float dx = newX - oldX, dy = newY - oldY;
    float dist = std::sqrt(dx * dx + dy * dy);
    rt.visualSpeed = dist > 0.0f ? dist / kAiPeriodSeconds : 0.0f;
}

// Weight of a macro cell from the baked grid the player's A* walks
// (ctx.mw.pathCost); a missing grid reads as a featureless free-road world.
float cell_weight(const TickContext& ctx, int x, int y) {
    const PathCostData* pc = ctx.mw.pathCost;
    if (!pc || pc->width <= 0 || pc->height <= 0
        || pc->costGrid.size()
               != std::size_t(pc->width) * std::size_t(pc->height)) {
        return 1.0f;
    }
    return pc->cost_at(x, y);
}

// The EDGE weight of one greedy step — the cell half from the baked grid
// plus the uphill climb half (movement_cost.h: the law's two halves; the
// squad walks the same slopes the player and both A*s pay for).
float edge_weight(const TickContext& ctx, int fx, int fy, int tx, int ty) {
    const PathCostData* pc = ctx.mw.pathCost;
    float w = cell_weight(ctx, tx, ty);
    if (pc && pc->width > 0 && pc->height > 0
        && pc->height8.size() == pc->costGrid.size()) {
        // Адрес клетки — одна дверь cell_of (здесь стояли две рукописные
        // композиции wrapi·w + wrapi, деление на пути каждого шага марша).
        const std::size_t fi = cell_of(fx, fy, pc->width);
        const std::size_t ti = cell_of(tx, ty, pc->width);
        w += pc->climb(fi, ti);
    }
    return w;
}

void try_move(MacroPos& p, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
              float tx, float ty, const TickContext& ctx) {
    int ix = int(p.x), iy = int(p.y);
    int itx = int(tx), ity = int(ty);
    // НАСТОЯЩАЯ цель рейса — посадка судится по ней, а не по вейпоинту:
    // сквад, ДОШЕДШИЙ до своего дока, проверял «нужна ли вода до дока»
    // (нет) и не садился на собственный корабль (измерено: out=63, вся
    // деревня вечно в отлучке).
    const int realTx = itx, realTy = ity;
    // ЛЕТУН (v93): воздух — его стихия, как вода у паруса. Море ему не
    // «недостижимо посуху», док не нужен, рельеф не платится, любая клетка
    // держит — ОДИН цикл марша, меняется только стихия (образец кораблей).
    const bool flying = rt.flying != 0;
    (void)realTx; (void)realTy;
    const float oldX = p.x, oldY = p.y;
    const float mapWf = float(ctx.mapW), mapHf = float(ctx.mapH);

    // Cells this think may cover — the SAME march the player walks
    // (kMacroWalkCellsPerHour per game hour), paced by the leader's own sheet
    // (moveMult: spd × athletics) and by the ground underfoot
    // (terrain_speed_mult of the CURRENT cell — one sample per think, the
    // same approximation the player's per-frame walk makes). Base numbers:
    // 3 cells per think on a road, ~1 in open water.
    const float perThink = kMacroWalkCellsPerHour * kAiTickGameHours
                           * std::max(0.0f, rt.moveMult)
                           * (flying ? 1.0f
                                     : terrain_speed_mult(
                                           cell_weight(ctx, ix, iy)));
    rt.moveBudget += perThink;
    // Banking bound, not a speed limit: at most one whole spare cell rides
    // across thinks on top of this think's own production.
    if (rt.moveBudget > perThink + 1.0f) rt.moveBudget = perThink + 1.0f;

    // What the leader's own training says a cell costs him (travel skill).
    const float efficiency = skill_mult_of(SkillId::Travel, int(rt.travelRank));
    const int playerCellX = wrap_axis(int(ctx.playerX), ctx.mapW);
    const int playerCellY = wrap_axis(int(ctx.playerY), ctx.mapH);

    while (rt.moveBudget >= 1.0f && (ix != itx || iy != ity)) {
        // Greedy steering (Session 21, owner's choice over per-squad A*):
        // among the eight neighbours, keep those strictly CLOSER to the
        // target and step onto the cheapest by the one weight grid. The
        // straight torus step wins ties, so a featureless world walks
        // exactly the line the old flat step walked — and a coast is walked
        // AROUND (land is 2-5×, water 10×), while a river with no cheap way
        // through is finally forded at its honest price. O(8) per cell:
        // 16384 squads can afford it where a pathfind each would starve
        // the frame.
        const bool standingDry = can_stand_at(ctx, ix, iy);
        int bx = -1, by = -1;
        float bw = 1e30f;
        // Сосед шага — арифметика ИНДЕКСА (cell_step, ЗАКОН АДРЕСА): здесь
        // стояло четыре wrapi — аппаратное деление на каждом шаге марша
        // каждого сквада.
        const std::uint32_t hereIdx = cell_of(ix, iy, ctx.mapW);

        // ЗАПЕЧЁННАЯ ПОХОДКА (CANON S7, 2026-09-02): округи + порталы +
        // граф — три чтения, ни волны, ни поиска. Жадный шаг остаётся миру
        // без запечённого слоя.
        if (!flying && standingDry && ctx.mw.nav && ctx.mw.nav->baked()) {
            int fdx = 0, fdy = 0;
            if (nav_step(*ctx.mw.nav, ix, iy, itx, ity, fdx, fdy)) {
                const std::uint32_t n = cell_step(hereIdx, fdx, fdy, ctx.mapW);
                bx = cell_x(n, ctx.mapW);
                by = cell_y(n, ctx.mapW);
                bw = edge_weight(ctx, ix, iy, bx, by);
            }
        }

        if (bx < 0) {
            // Жадный шаг — закон дальних маршей вне округ (Session 21).
            const Step straight =
                torus_step_toward(ix, iy, itx, ity, ctx.mapW, ctx.mapH);
            // The straight step is a CANDIDATE, not a right: it obeys the
            // same standing predicate as every neighbour. The old shape let
            // it through unfiltered — «a river is forded at its honest
            // price» was Session 21's design, and the owner's 2026-08-30
            // ruling ended it: ground a walker cannot stop on is ground it
            // does not enter, so a squad with no standable step simply
            // halts at the bank.
            // …and the gate binds only DRY feet: a body already floating (a
            // genesis accident, a shipwreck) may step wherever gets it out —
            // its unpayable steps bleed by the sea-bite law below.
            const auto walker_w = [&](int nx2, int ny2) {
                if (flying) return 1.0f;   // воздух — дорога летуна
                return edge_weight(ctx, ix, iy, nx2, ny2);
            };
            // БРОД — исключение РЕФЛЕКСА, не маршрута (владелец 2026-09-02):
            // БЕГЛЕЦУ терять нечего — Fleeing может шагнуть в воду, платя
            // существующим законом (на воде нет лагеря, долг кусает HP):
            // узкую реку переходит с парой клеток долга, загнанный в океан
            // тонет. ПОГОНЕ вода запрещена — река спасает беглеца (по-M&B),
            // и это же держит брод от превращения в общий маршрут: закон
            // стояния мирных рейсов (2026-08-30, утопленники-торговцы
            // кормили пул лута) нетронут.
            const bool fleeingFord =
                rt.state == std::uint8_t(NPCState::Fleeing);
            const auto walker_stands = [&](int nx2, int ny2) {
                return flying || fleeingFord || can_stand_at(ctx, nx2, ny2);
            };
            if (!standingDry || walker_stands(straight.nx, straight.ny)) {
                bx = straight.nx;
                by = straight.ny;
                bw = walker_w(bx, by);
            }
            const float dHere =
                torus_dist_sq(float(ix), float(iy), float(itx), float(ity),
                              mapWf, mapHf);
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox == 0 && oy == 0) continue;
                    const std::uint32_t n =
                        cell_step(hereIdx, ox, oy, ctx.mapW);
                    const int nx = cell_x(n, ctx.mapW);
                    const int ny = cell_y(n, ctx.mapW);
                    if (nx == bx && ny == by) continue;
                    const float d = torus_dist_sq(float(nx), float(ny),
                                                  float(itx), float(ity),
                                                  mapWf, mapHf);
                    if (d >= dHere) continue;   // only steps that progress
                    // The standing predicate: a walking NPC does not
                    // consider ground it could not stop on (water without a
                    // bridge) — the drowned-trader flood this closes fed
                    // 70% of the world's money into the loot pool
                    // (measured 2026-08-30).
                    if (standingDry && !walker_stands(nx, ny)) continue;
                    const float w = walker_w(nx, ny);
                    if (w < bw) { bx = nx; by = ny; bw = w; }
                }
            }
        }
        if (bx < 0) break;   // nowhere to stand: the leg ends at the bank

        // Entry-side stamp: the signed step of THIS cell change, torus-folded
        // (stepping east off the map's edge is still +1, not -(w-1)).
        int dx = bx - ix;
        if (dx > 1) dx = -1; else if (dx < -1) dx = 1;
        int dy = by - iy;
        if (dy > 1) dy = -1; else if (dy < -1) dy = 1;
        // The legs REFUSE a step they cannot pay for, wherever a camp can
        // stand: the step's price is known BEFORE it is taken, so dry-land
        // debt is impossible by construction — the bar used to walk into
        // minus in per-step slices INSIDE one think (693 bites in 3 days,
        // measured), past every after-the-fact camp check. On WATER there
        // is no stopping: the unpayable step goes through and pays the
        // bite — «неоплатный океан топит лорда» (S7), verbatim.
        const float stepCost = travel_stamina_cost(
            bw, 1.0f, int(rt.overloadCost), efficiency);
        if (float(pools.sp) + pools.spCarry < stepCost
            && (flying || can_stand_at(ctx, ix, iy))) {
            break;
        }

        rt.entryDir = pack_entry_dir(dx, dy);
        rt.entryTicks = 0;

        ix = bx; iy = by;
        rt.moveBudget -= 1.0f;

        // The step pays THE cell price — the same rows and formula the
        // player's march is charged (travel_stamina_cost), through the
        // fractional carry. The flat `sp -= 10` dialect dies here.
        // The step pays the load too (owner: перегруз универсальный) —
        // `overloadCost` is this think's surcharge, refreshed from the bag by
        // the sweep before the behaviour ran, so a caravan hauling more than
        // its leader's back can hold buys the trip at the honest price.
        pools.spCarry -= stepCost;
        settle_sp_carry(pools);
        if (int(pools.sp) < 0) break;   // spent: the think's march ends

        // Never hop OVER the player's cell in a multi-cell think: the forced
        // encounter (Inc 6) is geometric, so the squad stops ON the meeting
        // cell where the door can see it.
        if (ix == playerCellX && iy == playerCellY) break;
    }

    p.x = float(ix);
    p.y = float(iy);
    set_visual_speed(rt, oldX, oldY, p.x, p.y);

    // Exhaustion is settled by the DISPATCHER after the think (it owns the
    // entity + Health this function never sees): rest on land, the debt's
    // bite on water — one door for every behaviour that marched.
}

bool find_nearest_tree_grid(const TreeGrid& g, float px, float py,
                            int mw, int mh, XY& out) {
    if (!g.trees || g.trees->empty()) return false;
    float halfW = float(mw) * 0.5f;
    float halfH = float(mh) * 0.5f;
    const CellBuckets& b = g.grid;
    int cx0 = int(std::floor(px / float(b.cellSize)));
    int cy0 = int(std::floor(py / float(b.cellSize)));
    float best = 901.0f;        // 30² + 1
    bool found = false;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int gx = wrapi(cx0 + ox, b.cols);
            int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const auto& t = (*g.trees)[*it];
                float dx = std::fabs(px - float(t.x));
                float dy = std::fabs(py - float(t.y));
                if (dx > halfW) dx = float(mw) - dx;
                if (dy > halfH) dy = float(mh) - dy;
                if (dx > 30.0f || dy > 30.0f) continue;
                float d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    out = {float(t.x), float(t.y)};
                    found = true;
                }
            }
        }
    }
    return found;
}

// ── Behaviours ────────────────────────────────────────────────

using NS = NPCState;

void ai_home_wanderer(MacroPos& p, ecs::MacroNpcRuntime& rt,
                      ecs::Pools& pools, const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            float dsq = torus_dist_sq(p.x, p.y, home.x, home.y,
                                      float(ctx.mapW), float(ctx.mapH));
            if (dsq > 400.0f) {
                rt.targetX = home.x; rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
            } else {
                XY t = pick_random_nearby(home.x, home.y, 12, ctx);
                rt.targetX = t.x; rt.targetY = t.y;
                rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

// ── The ONE gatherer loop — a GOAL is a ROW, never a branch ───────────────
// (owner: «у сквада не должно быть специализации — они берут контекстно
// самую выгодную цель», CANON S10 аукцион; resources.md). Idle → find the
// worksite the row names → travel → WORK (take from the resource-field
// registry into the OWN bag — kill the man on the road and the haul is
// loot, not bookkeeping) → return (deliver into the HOME store).
// kGatherPerWorkerDay is the same anchor the economy day-loop gathers by:
// one law of labour. No worksite or no wired layer = the home wander, fail
// closed — nothing is conjured.
//
// The `type` column died with the professions (owner 2026-09-02, «сносим
// полностью»): a row names WHAT to gather, and WHO gathers it is whichever
// peasant crew the auction handed this row as its errand
// (rt.errandObject = the row index below).

// How a goal finds its worksite. Three shapes, priced by the world:
enum class Worksite : std::uint8_t {
    ForestCell,   // nearest forest-class cell (the TreeGrid index)
    HomeField,    // the home's nearest FT_Field parcel
    HomeFlaxField,// ...и льняная парцелла: та же земля, своя культура
    Deposit,      // the home's nearest deposit cell of the row's kind
    // ОХОТА: зверь живёт на КАЖДОЙ клетке (поле фауны — природа, не фича),
    // поэтому «где охотиться» — это лучшая по поголовью клетка своей округи,
    // а не поиск редкости. Рыба придёт третьим таким же источником.
    HomeFauna,
    // ПАСТБИЩЕ (CANON S10 «ЛОШАДЬ — ЮНИТ»): стоящая FT_Pasture с поголовьем,
    // а без неё — лучшая огораживаемая клетка того же ±3 бокса: артель
    // СНАЧАЛА поднимает пастбище (S10 «фичи создаются сквадами» — тот же
    // бутстрап, что у шахты над жилой), назавтра ловит.
    HomePasture,
};

struct GathererDef {
    ResourceFieldId row;        // what leaves the world
    const char*     commodity;  // what rides the bag and lands in the store
                                // (nullptr when the yield is a CREATURE)
    Worksite        worksite;
    // СКОЛЬКО ОДИН РАБОТНИК БЕРЁТ ЗА ДЕНЬ — темп ЭТОГО источника. Колонка
    // появилась с ПИЩЕЙ (владелец, 2026-09-18: «просто сделать, что охота не
    // такая выгодная по добыче, как поле»): один и тот же товар честно
    // приходит из разных источников с разной отдачей, и это ЧИСЛО, а не
    // ветка. Якорь мира — kGatherPerWorkerDay (S10, «добытчик кормит 32»).
    int perWorkerDay;
    // ОДНА новая колонка канона (S10 «ЛОШАДЬ — ЮНИТ», дословно: «выход
    // ложится в РОСТЕР, а не в сумку»): Count = обычный товар в сумку;
    // строка существа = добытое встаёт ДУШОЙ в ростер артели, спина сразу
    // в обозе (refresh_squad_carry). Овцы — следующая такая же строка.
    NPCType rosterYield = NPCType::Count;
};

constexpr GathererDef kGathererDefs[] = {
    // ПИЩА из двух источников: пашня — полный якорь, охота — четверть его
    // (зверь бегает, а колос стоит; число — крутилка дубль-прогона). Именно
    // это кормит хутор на скале, где пашни нет, а зверь есть.
    {ResourceFieldId::Wheat,  "food",   Worksite::HomeField,
     kGatherPerWorkerDay},
    {ResourceFieldId::Fauna,  "food",   Worksite::HomeFauna,
     kGatherPerWorkerDay / 4},
    // ЛЁН — ТА ЖЕ ЗЕМЛЯ, СВОЯ ПАРЦЕЛЛА (владелец, 2026-09-20: «на пашне уже
    // пшеница-пища, значит нужна фича льняное поле»). Ряд тот же арабельный:
    // клетка под льном — это клетка, не занятая хлебом, и конкуренция за
    // землю выходит сама, без второго поля и без нового ресурса. Четверть
    // якоря — тем же числом охота слабее пашни (крутилка дубль-прогона).
    {ResourceFieldId::Wheat,  "fibre",  Worksite::HomeFlaxField,
     kGatherPerWorkerDay / 4},
    {ResourceFieldId::Silver, "silver", Worksite::Deposit,
     kGatherPerWorkerDay},
    {ResourceFieldId::Trees,  "wood",   Worksite::ForestCell,
     kGatherPerWorkerDay},
    {ResourceFieldId::Iron,   "iron",   Worksite::Deposit,
     kGatherPerWorkerDay},
    {ResourceFieldId::Stone,  "stone",  Worksite::Deposit,
     kGatherPerWorkerDay},
    {ResourceFieldId::Clay,   "clay",   Worksite::Deposit,
     kGatherPerWorkerDay},
    // ЛОШАДЬ-ЮНИТ: темп 1 — поимка стоит работнику ДЕНЬ (цикл = целый бар
    // по закону цены труда S14.1), против 32 колосьев той же ценой; выход
    // не товар, а существо (колонка rosterYield).
    {ResourceFieldId::Horses, nullptr,  Worksite::HomePasture,
     1, NPCType::Horse},
};
constexpr int kGathererGoalCount =
    int(sizeof(kGathererDefs) / sizeof(kGathererDefs[0]));

// The crew's OWN goal row, named by its errand — nullptr when the errand is
// not a gather (no goal = no conjured work, the fail-closed rule).
const GathererDef* gatherer_def_of(const ecs::MacroNpcRuntime& rt) {
    if (rt.squadType != std::uint8_t(SquadType::Artel)) return nullptr;
    if (rt.errandObject >= std::uint32_t(kGathererGoalCount)) return nullptr;
    return &kGathererDefs[rt.errandObject];
}

// Пастбище артели: стоящая FT_Pasture с живым поголовьем — а если её нет,
// лучшая ОГОРАЖИВАЕМАЯ клетка (pasture_cell_ok) того же бокса: вернувшись
// с аукциона, артель первым делом её поднимет (акт ниже). Зеркало
// find_home_field с bootstrap-веткой вместо мёртвого отказа.
bool find_home_pasture(const TickContext& ctx, float px, float py,
                       const XY& home, XY& out) {
    if (!ctx.mw.features) return false;
    bool found = false;
    int bestStock = 0;
    float best = 1e30f;
    const std::uint32_t homeIdx = cell_of(int(home.x), int(home.y), ctx.mapW);
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(homeIdx, dx, dy, ctx.mapW);
            const int cx = cell_x(n, ctx.mapW);
            const int cy = cell_y(n, ctx.mapW);
            if (ctx.mw.features->at(cx, cy) != FT_Pasture) continue;
            const int stock =
                resource_field_read(ctx.mw, ResourceFieldId::Horses, cx, cy);
            if (stock <= 0) continue;   // grazed bare — nothing to catch
            const float d = torus_dist_sq(px, py, float(cx), float(cy),
                                          float(ctx.mapW), float(ctx.mapH));
            if (stock > bestStock || (stock == bestStock && d < best)) {
                bestStock = stock;
                best = d;
                out = {float(cx), float(cy)};
                found = true;
            }
        }
    }
    if (found) return true;
    // A pasture stands but is grazed bare: the herd law regrows it by the
    // season walker — fencing a SECOND meadow would sprawl one parcel per
    // trip (measured: 8 928 pastures in eight days, сид 7).
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(homeIdx, dx, dy, ctx.mapW);
            if (ctx.mw.features->at(cell_x(n, ctx.mapW), cell_y(n, ctx.mapW))
                == FT_Pasture)
                return false;
        }
    }
    // No pasture stands yet: the goal opens on the best fence-able cell.
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            if (dx == 0 && dy == 0) continue;  // the town
            const std::uint32_t n = cell_step(homeIdx, dx, dy, ctx.mapW);
            const int cx = cell_x(n, ctx.mapW);
            const int cy = cell_y(n, ctx.mapW);
            int herd = 0;
            if (!pasture_cell_ok(*ctx.mw.features, ctx.mw, cx, cy, herd))
                continue;
            if (herd > bestStock) {
                bestStock = herd;
                out = {float(cx), float(cy)};
                found = true;
            }
        }
    }
    return found;
}

} // namespace

// Контракт npc_ai.h: цель называется РЕСУРСОМ — порядок таблицы остаётся
// деталью этого файла.
int gather_goal_row(ResourceFieldId row) {
    for (int g = 0; g < kGathererGoalCount; ++g)
        if (kGathererDefs[g].row == row) return g;
    return -1;
}

namespace {

// ── «Руки достают = руки ДОЙДУТ» — читается из ОКРУГИ (nav_field.h) ──────
// Волна достижимости влита в мировое разбиение (CANON S7, 2026-09-02):
// рабочая зона добытчика = СВОЯ округа ∩ радиус рук — две деревни никогда
// не доят одно гнездо, «чья жила» решает поле тяготения. Радиус рук ОБЯЗАН
// совпадать с навигационным законом труда.
static_assert(kNavHandReach == kGathererReach,
              "радиус рук артелей — один на гейт и навигацию (CANON S7)");

// The home's nearest REACHABLE live cell of the row's deposit kind — ДРУГИХ
// случаев не осталось: мостовой случай («жила за одним водным разрывом»)
// вырезан 2026-09-22 вместе со стройкой артелей. Without a baked NavWorld
// (bare test fixtures) the pick degrades to the nearest vein in the box by
// straight line — the pre-v72 behaviour.
// The sustained march pace every plan walks by: kMacroWalkCellsPerHour × 24
// game hours, halved because the automaton rests to half a bar between
// marches (think_gate) — half the calendar walks, half sleeps.
constexpr float kSustainedMarchCellsPerDay =
    kMacroWalkCellsPerHour * 24.0f / 2.0f;

// (Тут стояла ДАЛЬНОСТЬ ИЗ ЦЕНЫ РЕЙСА — попытка вывести границу поиска из
// окупаемости. Она снята 2026-09-18 в тот же день: у драгоценной строки
// окупаемость покрывает весь мир, и поиск на артель стал стоить миллион
// чтений — сюита перестала отвечать. Границу заменил не другой радиус, а
// КАРТА ОКРУГИ: место описывает свою округу раз в сезон, артель читает
// строку, поиска в тике не остаётся. Урок метода: когда закон упирается в
// цену ПОИСКА, снимать надо сам поиск, а не подпирать его радиусом.)

bool find_home_deposit(const TickContext& ctx, ResourceFieldId row,
                       const XY& home, XY& out) {
    if (!ctx.mw.deposits || ctx.mapW <= 0) return false;
    const DepositKind kind =
        DepositKind(std::uint8_t(row) - std::uint8_t(ResourceFieldId::Clay));
    const ResourceGrid& cells = ctx.mw.deposits->grid(kind);
    if (cells.liveCells == 0) return false;
    const int hx = int(home.x), hy = int(home.y);
    NavWorld* nv = ctx.mw.nav;
    const bool navReady = nv && nav_ensure(ctx.mw, *nv);
    const std::uint16_t myRegion =
        navReady ? nav_region_at(*nv, hx, hy) : kNavNoRegion;
    std::uint32_t bestDry = ~0u;
    float bestSq = 1e30f;
    XY dryAt{};
    bool haveDry = false, haveNear = false;
    XY nearAt{};
    // THE ERRAND IS A NEIGHBOURHOOD QUESTION, so it walks a neighbourhood: the
    // hand's own box (kNavHandReach), which is all this door answers now —
    // «где ближайшая жила моей округи» переехало в ОПИСЬ МЕСТА
    // (survey_landmark_regions), и здесь остался ровно мостовой случай, жила
    // за одним водным разрывом, которая по определению лежит вплотную. A field
    // is indexed by the torus, so the box IS the loop: 33×33 reads, no
    // candidate discarded (problems.md §52 is the same lesson, one door over).
    const std::uint32_t homeIdx = cell_of(hx, hy, ctx.mapW);
    const auto consider = [&](int dx, int dy) {
        const std::uint32_t n = cell_step(homeIdx, dx, dy, ctx.mapW);
        const int x = cell_x(n, ctx.mapW);
        const int y = cell_y(n, ctx.mapW);
        if (cells.at_index(n) == 0) return;   // no vein standing here
        if (!navReady) {
            const float dsq = float(dx * dx + dy * dy);
            if (dsq < bestSq) {
                bestSq = dsq;
                nearAt = XY{float(x), float(y)};
                haveNear = true;
            }
            return;
        }
        const std::uint16_t reg = nav_region_at(*nv, x, y);
        if (reg == myRegion) {
            const std::uint32_t d = nv->distHome[nv->cell(x, y)];
            if (d < bestDry) {
                bestDry = d;
                dryAt = XY{float(x), float(y)};
                haveDry = true;
            }
            return;
        }
        return;   // не моя округа — сосед возьмёт
    };
    for (int dy = -kNavHandReach; dy <= kNavHandReach; ++dy) {
        for (int dx = -kNavHandReach; dx <= kNavHandReach; ++dx) {
            consider(dx, dy);
        }
    }
    if (!navReady) {
        if (haveNear) out = nearAt;
        return haveNear;
    }
    if (haveDry) {
        out = dryAt;
        return true;
    }
    return false;
}

bool find_worksite(const GathererDef& def, const TickContext& ctx,
                   const MacroPos& p, const XY& home, XY& out) {
    switch (def.worksite) {
        case Worksite::ForestCell: {
            if (!ctx.mw.treeGrid
                || !find_nearest_tree_grid(*ctx.mw.treeGrid, p.x, p.y,
                                           ctx.mapW, ctx.mapH, out))
                return false;
            // Рабочая зона — СВОЯ округа (CANON S7, тот же закон, что у
            // жил): «чей лес» решает поле тяготения. Без гейта лесоруб
            // острова ежедневно уплывал за материковым деревом, и деревня
            // вымирала в отлучку (измерено: 158 душ → 1 за 13 дней).
            if (NavWorld* nv = ctx.mw.nav; nv && nv->baked()) {
                const std::uint16_t rh =
                    nav_region_at(*nv, int(home.x), int(home.y));
                const std::uint16_t rw =
                    nav_region_at(*nv, int(out.x), int(out.y));
                if (rh != kNavNoRegion && rw != rh) return false;
            }
            return true;
        }
        case Worksite::HomeField:
            return find_home_field(ctx, p.x, p.y, home, out);
        case Worksite::HomeFlaxField:
            return find_home_field(ctx, p.x, p.y, home, out, FT_FlaxField);
        case Worksite::HomeFauna:
            return find_home_fauna(ctx, p.x, p.y, home, out);
        case Worksite::HomePasture:
            return find_home_pasture(ctx, p.x, p.y, home, out);
        case Worksite::Deposit:
            return find_home_deposit(ctx, def.row, home, out);
    }
    return false;
}

// Defined with the trade behaviours below; the crews share both laws.
bool march_is_stuck_(const MacroPos& p, float oldX, float oldY,
                     const ecs::MacroNpcRuntime& rt,
                     const ecs::Pools& pools);
int haul_between(Inventory& from, Depot to, const char* id,
                 int maxUnits, float capacityLeftKg);

// Приёмник-МЕСТО (CANON S10): склад + счёт + канал фактов мира — дверь
// прихода гасит долг СРАЗУ тем, что упало. Сумки в Depot не заворачиваются
// (неявная конверсия из Inventory&, долга нет).
inline Depot depot_(Landmark& lm, const MacroWorld& mw) {
    return Depot(lm.inventory, lm.needDebt, mw.econFacts, mw.econFactsUser);
}

// The sell-run machine (defined with the trade behaviours below): the
// peasant crew whose errand is Sell walks the SAME machine the vendor
// walked — reuse, not a second copy (CANON S26).
void ai_vendor(entt::entity self, MacroPos& p,
               ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx);

void ai_gatherer(entt::entity self, MacroPos& p,
                 const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                 ecs::Pools& pools, const TickContext& ctx) {
    (void)kind;   // the errand, not the type, names the work (CANON S10)
    XY home;
    if (!home_pos(rt, ctx, home)) return;
    // (ЗДЕСЬ СТОЯЛА ВЕТКА «squadType == Caravan → отдать управление
    // ai_vendor» — пятая машина ИИ, спрятанная ВНУТРИ первой. Снята
    // 2026-09-21: тип сквада разбирает `dispatch`, и корован попадает в свою
    // машину напрямую. Артель теперь занимается только добычей — ровно то
    // строгое разделение, которого требует замысел.)
    const GathererDef* def = gatherer_def_of(rt);
    if (!def) { ai_home_wanderer(p, rt, pools, ctx); return; }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // ПОЛНАЯ СПИНА ИДЁТ ДОМОЙ (S10 «возвращается, кладёт на склад»):
            // отдых посреди дорогого маршрута будит артель в Idle, а Idle
            // слал её к жиле — даже с грузом, которому на жиле нечего взять.
            // Вечный челнок «шахта→привал→шахта» держал 536 серебра в одной
            // сумке 48 дней при пустом складе (измерено, сид 7).
            if (auto* bagIdle = def->rosterYield == NPCType::Count
                    && ctx.mw.world
                    ? ctx.mw.world->reg.try_get<ecs::NpcInventory>(self)
                    : nullptr) {
                // (a CREATURE yield rides the roster, not the bag — there
                // is no «full back» to send home early)
                const ItemDef* idef = item_def(def->commodity);
                const float unitKg =
                    idef && idef->weight > 0.0f ? idef->weight : 1.0f;
                if (bagIdle->inv.count(def->commodity) > 0
                    && rt.carryCap - inventory_weight(bagIdle->inv)
                           < unitKg) {
                    rt.targetX = home.x;
                    rt.targetY = home.y;
                    rt.state = std::uint8_t(NS::Returning);
                    return;
                }
            }
            XY site;
            bool found = false;
            if (def->worksite == Worksite::Deposit) {
                // СНАЧАЛА ОПИСЬ СВОЕЙ ОКРУГИ: место уже нашло — артель
                // читает. Поиск остаётся ровно на МОСТОВОЙ случай (жила за
                // одним водным разрывом), который по определению лежит
                // вплотную к своей округе, поэтому ему честно хватает бокса
                // руки.
                if (const Landmark* homeLm = home_landmark(rt, ctx)) {
                    const SurveyRow& sr =
                        homeLm->survey.rows[std::size_t(def->row)];
                    if (!sr.none()) {
                        site = XY{float(sr.x), float(sr.y)};
                        found = true;
                    }
                }
                if (!found) {
                    found = find_home_deposit(ctx, def->row, home, site);
                }
            } else {
                found = find_worksite(*def, ctx, p, home, site);
            }
            if (found) {
                rt.targetX = site.x; rt.targetY = site.y;
                rt.state = std::uint8_t(NS::Traveling);
            } else if (torus_dist_sq(p.x, p.y, home.x, home.y,
                                     float(ctx.mapW), float(ctx.mapH))
                       > 400.0f) {
                // No work and far afield: come home first — the exact
                // HomeWanderer rule every gatherer degrades to.
                rt.targetX = home.x; rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
            } else {
                XY t = pick_random_nearby(home.x, home.y, 12, ctx);
                rt.targetX = t.x; rt.targetY = t.y;
                rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 8));
            return;
        }
        const float ox = p.x, oy = p.y;
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        // A leg that cannot advance gives the run up (the vendor's own
        // law): the reach wave keeps this rare, but a concave shore can
        // still wedge a greedy march — better home tonight than frozen at
        // the bank forever (measured 2026-08-31).
        if (march_is_stuck_(p, ox, oy, rt, pools)) {
            rt.targetX = home.x;
            rt.targetY = home.y;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // WORK COSTS SP through the one stamina law (CANON S14.1): the
            // bar divided by the rate. A gatherer's rate is OBJECTS IN A DAY
            // — the very number the player pays by when he fells a tree in
            // the subworld — so a crew and a hero now price one act of
            // taking identically, and the crew's advantage is its HANDS,
            // never a cheaper hand. A squad too spent for one goes home to
            // rest instead of working on an empty bar.
            // ЦЕНА ЦИКЛА — ПО ТЕМПУ СВОЕГО ИСТОЧНИКА (S14.1: цена = бар /
            // темп). Рука берёт один объект всегда (вердикт 2026-09-16), а
            // различие источников живёт в ЦЕНЕ: пашня 32 в день, охота 8 —
            // значит зверь стоит вчетверо дороже за единицу, и день охоты
            // честно даёт вчетверо меньше пищи. Ни ветки, ни второго закона.
            const int cycleCost =
                sp_price(int(pools.maxSp), def->perWorkerDay);
            if (int(pools.sp) < cycleCost) {
                rt.targetX = home.x;
                rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
                return;
            }
            // The take is REAL: the trip's yield leaves the world through
            // the profession's registry row and rides home in the OWN bag.
            // A row whose layers are not wired reads 0 and takes nothing —
            // the fail-closed rule every gatherer shares.
            bool tookSomething = false;
            if (ctx.mw.world) {
                const int tx = int(rt.targetX);
                const int ty = int(rt.targetY);
                // (ЗДЕСЬ АРТЕЛЬ СТРОИЛА ШАХТУ НАД ГОЛОЙ ЖИЛОЙ И ЗАГОН ПОД
                // ТАБУН. ВЫРЕЗАНО 2026-09-22, сессия 14, вердикт владельца:
                // «СТРОИТЕЛЬСТВО КРЕСТЬЯНАМИ НЕ РАБОТАЕТ НА НЕГО НЕЛЬЗЯ
                // ПОЛАГАТЬСЯ… ПОЛЯ ШАХТЫ и тд буду прегенериться с миром».
                // Фичи ставит генерация мира; артель только добывает.)
                MacroWorld mw = ctx.mw;  // the envelope, whole — never a
                                         // partial re-pick of its layers
                const int have = resource_field_read(mw, def->row, tx, ty);
                // Every soul in the squad works: headcount multiplies the
                // cycle's yield at the squad's one fixed SP price (owner:
                // «SP тратится столько же, добывают кратно больше»).
                auto* bag = ctx.mw.world->reg.try_get<ecs::NpcInventory>(self);
                // Руки — ЛЮДИ: лошадь в ростере — спина и рот, не рука
                // (count_human_souls, world_row.h) — иначе пойманный табун
                // сам становился бы добытчиком и контур шёл вразнос.
                const int workers = production_hands(
                    bag ? count_human_souls(bag->inv) : 0);
                // «Берёт ПО СВОЕЙ ГРУЗОПОДЪЁМНОСТИ» — CANON S10 дословно:
                // спины сквада ограничивают тейк. Без этой скобы артель
                // грузила цикл×души невзирая на вес и каменела перегрузом
                // на обратном пути НАВСЕГДА (наценка 256 SP/клетку при баре
                // 110 неоплатна и после полного отдыха — измерено: рудокоп
                // с 2400 кг серебра на спине в 2145 кг, сид 7).
                int carryMax = have;
                if (bag && def->rosterYield == NPCType::Count) {
                    const ItemDef* idef = item_def(def->commodity);
                    const float unitKg =
                        idef && idef->weight > 0.0f ? idef->weight : 1.0f;
                    const float freeKg =
                        rt.carryCap - inventory_weight(bag->inv);
                    carryMax = std::max(0, int(freeKg / unitKg));
                }
                // ONE OBJECT PER HAND (owner, 2026-09-16). Each worker takes
                // one — exactly what the player takes for exactly the same
                // price — and the crew's yield is that times its hands. The
                // batch of eight this replaced was a HAUL, and the haul is not
                // gone: it emerges below from the backs and the bar instead of
                // being declared by a constant nobody could derive.
                int take = std::min(std::min(workers, have), carryMax);
                tookSomething = take > 0;
                // «ВЫХОД ЛОЖИТСЯ В РОСТЕР, А НЕ В СУМКУ» (CANON S10,
                // дословно): существо встаёт ДУШОЙ в отряд — генерик-стаком
                // по закону слота — и его спина сразу считается в обозе
                // (refresh_squad_carry, та же дверь, что у добора). Credit
                // BEFORE debit: поле платит только за вставших.
                if (take > 0 && def->rosterYield != NPCType::Count) {
                    // ПОТОЛКА ЛОВЛИ НЕТ (двухтактный обоз): ловец гонит
                    // ПАЧКУ и сдаёт её домой целиком, поэтому «не больше,
                    // чем душ» тут было бы вечным кругом «поймал — отдал».
                    // Тормоз — ЦЕНА: аукцион дешевеет по мере насыщения
                    // стойла (стоимость строки × та же кривая дефицита),
                    // и рулетка сама уводит руки в другую цель.
                    if (take > 0 && bag && creatures_push_stack(
                            bag->inv, def->rosterYield,
                            npc_def(def->rosterYield).baseLevel,
                            take)) {
                        resource_field_apply(mw, def->row, tx, ty, -take);
                        pools.spCarry -= float(cycleCost);
                        settle_sp_carry(pools);
                        refresh_squad_carry(*ctx.mw.world, self);
                    } else {
                        tookSomething = false;   // no slot — nothing conjured
                    }
                }
                // Credit BEFORE debit (CANON S5): the field pays only what
                // the OWN bag actually took — a bagless walker, or a bag
                // with no room, drains nothing and writes no Drained fact.
                else if (take > 0 && bag
                         && bag->inv.add(def->commodity, take)) {
                    resource_field_apply(mw, def->row, tx, ty, -take);
                    // The cycle is PAID the moment it produced — through
                    // the same fractional carry the march charges
                    // (settle_sp_carry above): work and walking drain one
                    // purse, which is the whole law.
                    pools.spCarry -= float(cycleCost);
                    settle_sp_carry(pools);
                    // A DEPOSIT worked down to nothing is a fact of the world
                    // (FactKind::Drained: "a vein worked out") — and by the
                    // annihilation law the cell itself leaves the map, so
                    // "why did the town grow poor" is answered by THIS record
                    // and nothing else. The daily haul is weather, not
                    // history (S20.1: the TRANSITION is the story); forest
                    // and wheat regrow by their own law, so their emptied
                    // cells write nothing. amount = resource registry row +1:
                    // the land has no ordinal to ride as the object (a spire
                    // does, and its drain points at the place instead), and
                    // after annihilation the world no longer holds the
                    // "what" — the fact is its only carrier.
                    if (def->worksite == Worksite::Deposit && take == have) {
                        record_landmark_fact(*ctx.mw.gs, FactKind::Drained,
                                             rt.homeSettlementId, tx, ty,
                                             int(def->row) + 1);
                    }
                }
            }
            // THE HAUL IS EMERGENT (owner, 2026-09-16). The crew used to walk
            // home after ONE take because the take was a whole trip's worth by
            // declaration — `kGatherPerCycle`, a number derived from a second
            // number («four cycles to a bar») that was itself only declared.
            // Both are gone. A worker takes one object, and the crew keeps
            // taking while its BACKS have room and its BAR has another act in
            // it; the trip home is what happens when one of those runs out.
            // The day's yield is unchanged — a full bar still buys the same
            // number of objects — but no constant says how many trips that is.
            {
                const auto* bagNow = ctx.mw.world
                    ? ctx.mw.world->reg.try_get<ecs::NpcInventory>(self)
                    : nullptr;
                bool backsFull = false;
                if (bagNow && def->rosterYield == NPCType::Count) {
                    const ItemDef* idef = item_def(def->commodity);
                    const float unitKg =
                        idef && idef->weight > 0.0f ? idef->weight : 1.0f;
                    backsFull =
                        rt.carryCap - inventory_weight(bagNow->inv) < unitKg;
                }
                const bool barSpent = int(pools.sp) < cycleCost;
                // GROUND THAT GAVE NOTHING is the third reason to leave, and
                // the one that matters most: without it a crew standing on an
                // emptied cell would work forever, taking nothing and paying
                // nothing, and never walk home again. The old code could not
                // meet this case because it left after a single act whatever
                // happened.
                if (tookSomething && !backsFull && !barSpent) {
                    // Still standing at the worksite, still able: work again.
                    rt.stateTimer = 1;
                    return;
                }
            }
            rt.targetX = home.x; rt.targetY = home.y;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            // Home with the haul: everything gathered lands in the HOME
            // store — the same universal inventory the market sells from.
            if (rt.state == std::uint8_t(NS::Returning)) {
                // ОДИН ПРИХОД, ДВА КОНТЕЙНЕРА (S25): груз — на склад,
                // лишние спины — в стойло. У существа нет товарной колонки,
                // поэтому сумка его и не видит.
                if (def->commodity != nullptr)
                    deliver_bag_home(self, rt, ctx, def->commodity);
                deliver_mounts_home(self, rt, ctx);
            }
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(6 + rand_int(ctx, 12));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

// ── The city's trading agent (W2b) ───────────────────────────────────────
// An honest caravan: no TradeRoute abstraction settles anything — the goods
// ride in the caravan's OWN bag between real inventories, so a robbery on
// the road takes REAL cargo. What to haul is decided by the home's LEDGER —
// the place's own running account of what it lacks (LandmarkLedger): it can
// be stale by the time the crew returns, and that is a trader's life.
//
// How much it hauls is its OWN carry law and nothing else: rt.carryCap =
// get_carry_capacity(sheet) × the row's haulMult (squad.h
// refresh_leader_travel_stats — a caravan is wagons and mules, ×32). A flat
// `kCaravanCapacityKg = 256` lived here until 2026-08-29: a SECOND capacity
// beside the legal one, so the hold neither grew with the leader's back nor
// answered to the overload law that priced the very same cargo's march.

void ai_nomad(MacroPos& p, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx);

// Move up to `maxUnits` of `id` between inventories, bounded by the cargo
// hold's remaining weight. Returns units moved.
// The city's purchase PRIORITY: the needs ladder unrolled to its recipe
// INPUTS (bread ← grain first, then cloth's, bricks'…), then every other
// commodity in table order. Derived once from kNeeds × kRecipes — no
// commodity is named in code. Shared by the deal (what to buy first) and
// the route choice (which village is worth the ride): without the shared
// weight the caravans twice ran for whatever was merely PLENTIFUL — wood —
// while the granary held one unit (measured, balance_run 2026-08-30).
// Load order by VALUE DENSITY (value per kg, dearest first): what a trader
// packs when the cart is smaller than the warehouse. Universal — silver
// rides before timber because the table prices it so, never because code
// names a metal (the wood-first table order left the mint's ore stranded in
// the villages while the carts hauled logs; measured 2026-08-30).
const std::array<int, std::size_t(kCommodityCount)>& value_dense_order() {
    static const auto kOrder = [] {
        std::array<int, std::size_t(kCommodityCount)> order{};
        for (int i = 0; i < kCommodityCount; ++i) order[std::size_t(i)] = i;
        const auto density = [](int i) {
            const ItemDef* d = item_def(kCommodities[i].id);
            if (!d || d->value <= 0) return 0.0f;
            return float(d->value) / (d->weight > 0.0f ? d->weight : 1.0f);
        };
        std::sort(order.begin(), order.end(),
                  [&](int a, int b) { return density(a) > density(b); });
        return order;
    }();
    return kOrder;
}
// (caravan_buy_order — белый список закупки «нужды дома → входы рецептов» —
// СНЕСЁН 2026-09-18 по вердикту владельца: купец берёт то, что выгоднее по
// цене относительно стоимости, и список решал бы за него, что бывает товаром.)


int haul_between(Inventory& from, Depot to, const char* id,
                 int maxUnits, float capacityLeftKg) {
    if (maxUnits <= 0 || capacityLeftKg <= 0.0f) return 0;
    const ItemDef* def = item_def(id);
    const float unitKg = def && def->weight > 0.0f ? def->weight : 1.0f;
    const int byWeight = int(capacityLeftKg / unitKg);
    const int n = std::min({maxUnits, byWeight, from.count(id)});
    if (n <= 0) return 0;
    // Credit before debit (CANON S5): a hold with no free slot refuses, the
    // cargo stays where it was, and "units moved" is never said of goods
    // that evaporated between two bags.
    if (!to.inv.add(id, n)) return 0;
    from.remove(id, n);
    // Приход в МЕСТО гасит долг СРАЗУ (CANON S10): упавшее по счёту
    // съедено с фактом Consumed, на полке остаётся излишек. Вернувшееся n
    // честно: сделка/дань состоялась — судьба груза дальше дело приёмника.
    if (to.needDebt) econ_pay_debt(to.inv, to.needDebt, to.sink, to.user);
    return n;
}

// (transfer_worth — the value-tribute's «coin, then fattest stacks» door —
// died 2026-09-02 with the per-position tithe: the fattest-first draw paid
// the debt in grain while the silver stayed home, and no caller remains.)

// A leg that cannot advance (every candidate step refused — the target is
// beyond water with no bridge) reads as: position pinned while the move
// budget stands whole and the bar is fresh. The rider gives the run up
// instead of pacing the surf forever.
bool march_is_stuck_(const MacroPos& p, float oldX, float oldY,
                     const ecs::MacroNpcRuntime& rt,
                     const ecs::Pools& pools) {
    return p.x == oldX && p.y == oldY && rt.moveBudget >= 1.0f
           && int(pools.sp) > int(pools.maxSp) / 2;
}

}   // namespace — дверь станции живёт СНАРУЖИ: у закона тора обязан
    // быть прямой свидетель (npc_ai.h), а не свидетель через чей-то ИИ.

// СЛЕДУЮЩАЯ СТАНЦИЯ ТОРГОВЦА — СОСЕД ПО ГРАФУ ОКРУГ (владелец 2026-09-20:
// «пусть горожане несут не ближайшего, а просто тоже гуляют как караван —
// та же диффузия, зато единообразно»).
//
// ОДИН закон движения на всякого, кто возит товар: караван, деревенский
// вендор, артель горожан. Место в СОСЕДНЕЙ округе через мембрану, кроме
// той, откуда пришёл; несколько прыжков — и домой (заём склада обязан
// вернуться, S5). Решает всё не маршрут, а ЦЕНА на месте: продал там, где
// дорого, купил там, где дёшево, — это и есть диффузия вдоль ценового
// градиента, а голодное место дорого по построению (непогашенный счёт).
//
// ЧТО УМЕРЛО ЗДЕСЬ (всё три — назначенные правила, CANON S26):
//  · «ближайший вассал» у артели горожан — он был ПОЛНЫМ СКАНОМ всех мест
//    (1738 записей на город) плюс нелокальностью: место читало состояние
//    двух десятков соседей разом, и городской хлеб тёк ровно в одну
//    деревню из двадцати шести (замер 2026-09-20: 63 обслуженных деревни
//    из 1675, гора в 45 млн стоит);
//  · «станция бывает только городом» — фильтр по виду места, тот же класс
//    ворот, что «рынок бывает только городом», снесённый 2026-09-19;
//  · выбор станции полным сканом ландмарков по прямой — на тысячах
//    сквадов это горячий цикл, а у мира есть свой граф соседства.
// Цена шага теперь — перебор мембран своей округи (единицы), и знание
// агента строго локально: видно только соседей.
//
// ВЕС ЕСТЬ ДНИ ПУТИ (владелец 2026-09-20, второе уточнение — вердикт S4
// читается дословно: «ходит ПО ВЕСУ случайно к соседям»). Одна рулетка с
// ядром `1/(1 + дни)` на ОБЕ ветки двери: «мир без дорог» и «мир с графом»
// перестали быть двумя законами — они отличаются только тем, откуда взяты
// дни (цена пути или хорда), а форма выбора одна.
//
// ЧТО ЗДЕСЬ УМЕРЛО И ПОЧЕМУ (три находки одного вскрытия, 2026-09-20):
//  · РАВНОВЕРОЯТНЫЙ выбор соседа — веса не было вовсе, хотя вердикт его
//    называл: дальний сосед за хребтом тянулся из урны ровно так же часто,
//    как соседняя деревня за рекой, и рейс сгорал в дороге;
//  · КАП «первые восемь мембран» — он был НЕ произволом порядка хранения:
//    порталы округи отсортированы по цене перехода (nav_field.cpp,
//    `distHome[c] + ребро + distHome[n]` — цена пути от моего ландмарка до
//    соседского), поэтому кап означал «восемь БЛИЖАЙШИХ соседей». Это тот
//    же закон «не ходи далеко», только ступенькой вместо веса — и потому
//    снятый ОТДЕЛЬНО он и оказался хуже (замер: сделок 8523 против 8977,
//    голод +19 %): урна впустила самых дорогих соседей РАВНОПРАВНО. С весом
//    ступенька лишняя: дальний сосед получает малую вероятность вместо
//    нулевой, и цепочка прыжков достаёт всю карту, не платя временем;
//  · ДЕТЕРМИНИЗМ ВЕНДОРСКОГО КАНАЛА — аукцион заявок звал дверь без RNG
//    (`TickContext{}` в rotate_worker_squads), поэтому 98 % торговли мира
//    (~3040 крю против 67 караванов) брали `cand[0]` — ландмарк САМОГО
//    дешёвого портала. Каждая деревня возила в ОДНОГО И ТОГО ЖЕ соседа всю
//    свою жизнь. Бросок теперь есть (дневная струя `worldTickRt.jitter`),
//    и вырождение «нет броска → ближний» осталось только там, где кидать
//    нечем.
//
// Дальний хвост, съевший рулетку по ВСЕЙ карте (CANON S4, пять замеров),
// здесь не воскресает: урна — только соседние округи, мест в ней единицы, и
// самый дальний кандидат всё равно сосед.
int pick_next_station_(const TickContext& ctx, const MacroPos& p,
                       int currentId, int prevId, float& outX, float& outY) {
    if (!ctx.mw.gs) return -1;
    const NavWorld* nv = ctx.mw.nav;
    const std::size_t R = nv ? nv->regionLandmarkId.size() : 0;
    const bool baked = nv && nv->baked();
    const std::uint16_t here = baked ? nav_region_at(*nv, int(p.x), int(p.y))
                                     : kNavNoRegion;
    // ДНИ ДО КАНДИДАТА — одна мера решения (та же, что у road_days_ в
    // аукционе): цена пути, когда мир запечён, хорда — когда дорог ещё нет.
    // В одну сторону: крю идёт ТУДА и едет дальше, а не возвращается.
    // Отрицательное = пути нет (kNavFar), и молчаливой подмены геометрией в
    // запечённом мире не бывает — кандидат просто выбывает.
    const auto days_to_ = [&](int tx, int ty) -> float {
        if (baked) {
            const std::uint32_t c =
                nav_path_cost(*nv, int(p.x), int(p.y), tx, ty);
            if (c == kNavFar) return -1.0f;
            return float(c) / 16.0f / kSustainedMarchCellsPerDay;
        }
        return std::sqrt(torus_dist_sq(p.x, p.y, float(tx), float(ty),
                                       float(ctx.mapW), float(ctx.mapH)))
               / kSustainedMarchCellsPerDay;
    };
    // РУЛЕТКА ОДНИМ ПРОХОДОМ (взвешенный резервуар): кандидат берёт урну с
    // вероятностью своего веса в накопленной сумме. Второй проход по
    // ростеру мира стоил бы вдвое, а закон от порядка не зависит.
    // Без броска (дверь зовут без RNG) рулетка честно вырождается в ПЕРВЫЙ
    // вес — у запечённого мира это ближайший сосед, потому что порталы
    // округи отсортированы по цене.
    float total = 0.0f;
    int pickId = -1;
    const auto offer_ = [&](const Landmark& c) {
        const float days = days_to_(c.x, c.y);
        if (days < 0.0f) return;              // пути нет — не кандидат
        const float w = 1.0f / (1.0f + days);
        total += w;
        bool take = pickId < 0;   // первый кандидат берёт урну целиком
        if (!take && ctx.rng) {
            const float roll =
                float(rand_int(ctx, 1 << 20)) / float(1 << 20);
            take = roll * total < w;
        }
        if (take) {
            pickId = c.id;
            outX = float(c.x);
            outY = float(c.y);
        }
    };
    if (!baked || std::size_t(here) >= R) {
        // МИР БЕЗ ДОРОГ (граф округ не запечён — синтетическая фикстура,
        // молодой мир): соседство спрашивается у ГЕОМЕТРИИ. Урна — весь
        // ростер, потому что другого понятия соседства здесь нет.
        for (const Landmark& c : ctx.mw.gs->landmarks) {
            if (c.id == currentId || c.id == prevId) continue;
            if (!landmark_is_settlement(c.type) || souls_flock(c) <= 0)
                continue;
            offer_(c);
        }
        return pickId;
    }
    // Кандидаты — жилые места ВСЕХ соседних округ (кап мембран снят: его
    // работу делает вес). Дубли по паре округ — два сегмента общей границы
    // на торе — считаются один раз: это одно место, а не два шанса.
    std::uint16_t seen[kNavMaxPortalsPerRegion];
    int seenCount = 0;
    int portalCount = 0;
    const NavPortal* membranes =
        nav_region_portals(*nv, std::uint16_t(here), portalCount);
    for (int pi = 0; pi < portalCount; ++pi) {
        const std::uint16_t to = membranes[pi].toRegion;
        if (std::size_t(to) >= R) continue;
        bool dup = false;
        for (int s = 0; s < seenCount; ++s) dup = dup || seen[s] == to;
        if (dup) continue;
        if (seenCount < kNavMaxPortalsPerRegion) seen[seenCount++] = to;
        const int lmId = int(nv->regionLandmarkId[to]);
        if (lmId < 0 || lmId == currentId || lmId == prevId) continue;
        const Landmark* lm = landmark_by_id(*ctx.mw.gs, lmId);
        if (!lm || !landmark_is_settlement(lm->type) || souls_flock(*lm) <= 0)
            continue;
        offer_(*lm);
    }
    return pickId;   // -1 = тупик: рейс кончается, крю идёт домой
}

int market_price_seen(const MacroWorld& mw, int fromX, int fromY,
                      const Landmark& at, int commodityIdx) {
    if (!mw.gs) return 0;
    if (commodityIdx < 0 || commodityIdx >= kCommodityCount) return 0;
    const std::size_t ci = std::size_t(commodityIdx);
    // ЯРУС 2, БЛИЖНЯЯ ПОЛОВИНА: место в горизонте — его СОБСТВЕННАЯ
    // ведомость. Горизонт спрашивается из точки, где стоит спрашивающий.
    if (at.ledger.published() && mw.nav && mw.nav->baked()) {
        const std::uint16_t rFrom = nav_region_at(*mw.nav, fromX, fromY);
        const std::uint16_t rAt = nav_region_at(*mw.nav, at.x, at.y);
        if (nav_regions_adjacent(*mw.nav, rFrom, rAt))
            return at.ledger.price[ci];
    }
    // ЯРУС 2, ДАЛЬНЯЯ ПОЛОВИНА: за горизонтом — мировое среднее, и оттого
    // дальнее место выглядит «обычным рынком»: туда ездят, но без
    // предпочтения (CANON S10). Ноль — цены нет ни на одном ярусе.
    return mw.gs->worldLedger.published()
               ? mw.gs->worldLedger.price[ci]
               : 0;
}

namespace {

// ── СТОИМОСТЬ СДЕЛКИ, ОЖИДАЕМОЙ В ТОЧКЕ `at` (CANON S10) ────────────────
// ОДНА арифметика на всех, кто решает «стоит ли туда ехать»: аукцион дома и
// сам рейс на станции. Расходятся они только ВХОДОМ, и это законно —
// решающий дома видит свой склад живьём, решающий в поле видит его
// ВЕДОМОСТЬЮ (ярус 2). Поэтому цена дома и нехватка дома приходят
// массивами, а не читаются внутри: второго правила о спреде не заводится
// (S26), но и вранья «крю видит дом насквозь через полмира» не возникает.
//
//   выручка строки = единицы × (цена ТАМ − цена ДОМА), и ноль, если там не
//   дороже; обратный конец — то же самое другой стороной.
//
// `mine[c]`     — единицы строки, которые решатель повезёт на продажу;
// `homePrice[c]`— почём строка дома; `homeLack[c]` — сколько дому ещё надо.
// Кошелёк рейса — то, что груз выручит ТАМ (бартер, S25): решение весит
// только то, что можно оплатить (S10).
// Вес страха на маршруте: threat худшей округи маршрута >> shift — минусом
// в скор (те же деньги против той же ценности рейса; скор ≤ 0 = отказ рейса
// ценой). Стартовая четверть — крутилка дубль-прогона: полный вес после
// любой резни морил бы округу голодом дольше, чем горюет летопись.
// ПЕРЕЕХАЛ СЮДА 2026-09-22: страх стал нужен не только аукциону, но и
// развилке рейса на станции, а она выше по файлу.
constexpr int kThreatFearShift = 2;

// ДНИ МАРША МЕЖДУ ДВУМЯ ТОЧКАМИ, В ОДНУ СТОРОНУ (CANON S7, та же дверь
// nav_path_cost, что у аукциона; хорда — только в мире без дорог). Рейсу со
// станции нужны ноги ОТ МЕСТА СТОЯНИЯ, а лямбда аукциона считает от дома —
// это одна мера с двумя концами, а не второй закон.
float march_days_(const TickContext& ctx, int ax, int ay, int bx, int by) {
    const NavWorld* nv = ctx.mw.nav;
    float cells = -1.0f;
    if (nv && nv->baked()) {
        const std::uint32_t c = nav_path_cost(*nv, ax, ay, bx, by);
        if (c != kNavFar) cells = float(c) / 16.0f;
    }
    if (cells < 0.0f)
        cells = std::sqrt(torus_dist_sq(float(ax), float(ay), float(bx),
                                        float(by), float(ctx.mapW),
                                        float(ctx.mapH)));
    return cells / kSustainedMarchCellsPerDay;
}

// СТРАХ МАРШРУТА между двумя точками — тот же терм, что в аукционе: худшая
// округа маршрута платит стоимостью погибших там душ.
float route_fear_(const TickContext& ctx, int ax, int ay, int bx, int by) {
    NavWorld* nv = ctx.mw.nav;
    if (!nv || !nv->baked() || nv->threat.empty()) return 0.0f;
    const std::uint16_t ra = nav_region_at(*nv, ax, ay);
    const std::uint16_t rb = nav_region_at(*nv, bx, by);
    if (ra == kNavNoRegion || rb == kNavNoRegion) return 0.0f;
    return float(threat_on_route(*nv, ra, rb) >> kThreatFearShift);
}

long long trade_bid_value_(const MacroWorld& mw, int fromX, int fromY,
                           const Landmark& at, const int* mine,
                           const int* homePrice, const int* homeLack) {
    long long value = 0;
    long long purse = 0;
    int therePrice[kCommodityCount];
    for (int c = 0; c < kCommodityCount; ++c) {
        therePrice[c] = market_price_seen(mw, fromX, fromY, at, c);
        const long long units = mine ? (long long)mine[c] : 0;
        if (units <= 0 || therePrice[c] <= 0) continue;
        purse += units * therePrice[c];
        if (therePrice[c] > homePrice[c])
            value += units * (therePrice[c] - homePrice[c]);
    }
    for (int c = 0; c < kCommodityCount && purse > 0; ++c) {
        const long long lack = homeLack ? (long long)homeLack[c] : 0;
        if (lack <= 0 || therePrice[c] <= 0) continue;
        if (homePrice[c] <= therePrice[c]) continue;
        const long long buyable =
            std::min<long long>(lack, purse / therePrice[c]);
        if (buyable <= 0) continue;
        value += buyable * (homePrice[c] - therePrice[c]);
        purse -= buyable * therePrice[c];
    }
    return value;
}

// ТОРГОВАЯ СИЛА ТОРГОВЦА — ОДНО число с его листа (CANON S25): финальное
// производное (attributes.h, харизма × торговля), то же, что показывает
// панель. Две половины — атрибут и скилл — больше не ходят по коду порознь:
// они сложены там, где считается весь производный блок, и сделка спрашивает
// ИТОГ. Отсюда спеллы, артефакты и перки входят в цену бесплатно.
int leader_trade_power_(ecs::World& w, entt::entity self) {
    const CharacterSheet& sh = sheet_of(w, self);
    return calculate_derived(sh.attributes, sh.skills).tradeDiscountPct;
}

// ...и ТОРГОВАЯ СИЛА МЕСТА — та же дверь над анкетой ландмарка (S25: у
// сделки две макросущности, и место — полноправная сторона, а не «лавка»).
int landmark_trade_power_(const Landmark& lm) {
    const CharacterSheet& sh = landmark_sheet(lm.type);
    return calculate_derived(sh.attributes, sh.skills).tradeDiscountPct;
}

// (No own_type_ any more: sheet_of asks the entity itself — «the haggler's
// numbers are whoever's numbers they are» is the door's own law now.)

// ЧТО ГРУЗИТ РЕЙС — один закон двух погрузок, артели и каравана (владелец
// 2026-09-18: «если деревня добывает что угодно, она это и продаёт,
// буквально живёт этим»): едет то, что ДОМА ДЕШЕВЛЕ БАЗЫ — затоваривание
// по той же кривой цены, что судит сделку на месте. Дефицитное дома (цена
// выше базы) не грузится вовсе — оно нужно здесь.
// ВЫЧИТАНИЕ СЕЗОННОГО АМБАРА УМЕРЛО С ДОЛГОМ (CANON S10, 2026-09-19):
// нужда съедается в момент прихода, на складе лежит только ИЗЛИШЕК —
// «незачем беречь то, что уже проедено по дороге». Всё видимое свободно;
// судит одна цена.
// ── ГРУЗ ДОМА — ОДНА ДВЕРЬ, ДВА ЧИТАТЕЛЯ (CANON S10) ────────────────────
// Вердикт владельца 2026-09-22, дословно: «надо универсально — город грузит
// в путь ВСЕ товары, то есть у нас просто система, что всё в инвентаре это
// товар (так и должно быть)».
//
// Обход идёт по ПЛОТНОСТИ СТОИМОСТИ (тот же закон, каким платит всякая
// сделка — currency.h densest_value_slot), а НЕ по пятнадцати строкам
// kCommodities, как было до 2026-09-22. Следствие, ради которого это и
// сделано: МОНЕТА ПЕРЕСТАЁТ БЫТЬ НЕВИДИМОЙ. Прежний обход брал строку,
// только если `stock_price(...) < base`, а у монеты цена И ЕСТЬ база —
// условие не выполнялось никогда, и казна физически не могла уехать в рейс
// (Х-13, «названо вслух, не проверено» — проверено замером 2026-09-22:
// у медианного города еда 0 при 3 682 монетах, и он не мог купить хлеба).
// Никакого «пути для монет» при этом не заводится: монета просто самый
// плотный груз на складе (S10 «спецпутей и ворот для монет не существует»).
//
// Строка, у которой есть СЕЗОННАЯ НУЖДА, грузится только СВЕРХ неё — дом не
// вывозит то, чего ему самому не хватает. Всё прочее (монета, инструмент,
// трофей) нужды не имеет и едет целиком.
//
// `bag == nullptr` — НИЧЕГО НЕ ДВИГАТЬ, только посчитать стоимость, которую
// рейс увезёт: так дверь спрашивает АУКЦИОН. Оттого «что решили» и «что
// повезли» — одно число, посчитанное одним кодом, а не два (S26).
long long plan_home_load_(Inventory& store, const std::int32_t* needDebt,
                          int population, const Skills& site, float capKg,
                          Inventory* bag) {
    long long planned = 0;
    bool done[kMaxInventorySlots] = {};
    float used = bag ? inventory_weight(*bag) : 0.0f;
    while (used < capKg) {
        int best = -1, bestV = 0;
        float bestW = 0.0f;
        for (int i = 0; i < int(store.slots.size()); ++i) {
            if (done[std::size_t(i)]) continue;
            const ItemRef& sl = store.slots[std::size_t(i)];
            if (sl.empty()) continue;
            const int v = value_of(sl);
            if (v <= 0) continue;
            const ItemDef* d = item_def_at(int(sl.def));
            const float w = d && d->weight > 0.0f ? d->weight : 0.0f;
            const bool denser =
                best < 0 || float(v) * bestW > float(bestV) * w
                || (float(v) * bestW == float(bestV) * w && v > bestV);
            if (denser) { best = i; bestV = v; bestW = w; }
        }
        if (best < 0) break;
        done[std::size_t(best)] = true;
        const ItemRef& sl = store.slots[std::size_t(best)];
        const ItemDef* d = item_def_at(int(sl.def));
        if (!d) continue;
        int count = int(sl.count);
        // Сезонная нужда дома неприкосновенна — вывозится только излишек.
        if (commodity_index(d->id) >= 0) {
            const int have = store.count(d->id);
            const int demand = season_demand_for(d->id, needDebt, population,
                                                 site, &store);
            const int surplus = have - demand;
            if (surplus <= 0) continue;
            if (count > surplus) count = surplus;
        }
        int fit = count;
        if (bestW > 0.0f) {
            const int byWeight = int((capKg - used) / bestW);
            if (fit > byWeight) fit = byWeight;
        }
        if (fit <= 0) continue;
        planned += (long long)fit * bestV;
        used += float(fit) * bestW;
        if (bag)
            haul_between(store, *bag, d->id, fit,
                         capKg - inventory_weight(*bag));
    }
    return planned;
}

// (ЗДЕСЬ ЖИЛ ai_caravan — 163 строки рейса «город → город со станциями».
// Снесён 2026-09-21 вместе с родом NPCType::Caravan: караван оказался
// СКВАДОМ, притворившимся видом существа, и его обоз был вписан в породу
// лидера 32 спинами вопреки закону «обоз = сумма спин ростера» (squad.h).
// Торговый канал мира держат рейсы сбыта артелей — ai_vendor, одна машина
// рейса на любые два места. Караван вернётся сквадом: люди плюс лошади.)

// РЕЙС К РЫНКУ — одна машина для ЛЮБЫХ двух мест (владелец 2026-09-19:
// «добавим сквад горожан, которые идут в деревню закупаться»). Рынок берётся
// из ПОРУЧЕНИЯ (errandObject = ординал рынка — так глагол Sell и описан в
// npc_ai.h), а не из феодального ребра: деревня едет к сюзерену, горожане —
// в деревню домена, и это ОДИН рейс, а не два ИИ.
// ПОЧЕМУ ЭТО НЕ УДОБСТВО, А НЕОБХОДИМОСТЬ, циферью (замер 2026-09-19):
// продать еду ГОЛОДНОМУ городу невозможно арифметически — при сезонной
// гиперболе первая буханка у пустой полки стоит база × сезонная нужда / 2
// (≈185 000 у города на 1200 душ), и кошелька на неё нет ни у кого. Зонд
// показал ловушку ликвидности прямо: 4035 артелей в рейсах сбыта — 45
// сделок в день на весь мир, деревни с 3.6 млн хлеба, города выедены в
// ноль. Сделка обязана происходить НА ДЕШЁВОМ КОНЦЕ: покупатель едет туда,
// где товар изобилен (цена 1-2), и его кошелька хватает на горы. Один
// закон цены, ни одного нового — просто рейс в правильную сторону.
// Рейс: погрузить дешёвое дома, дойти до рынка поручения, продать, купить
// домашние нехватки, вернуться. Ротация поднимает и судит крю как любую.
void ai_vendor(entt::entity self, MacroPos& p,
               ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || !ctx.mw.world) {
        ai_nomad(p, rt, pools, ctx);
        return;
    }
    auto& reg = ctx.mw.world->reg;
    auto* bag = reg.try_get<ecs::NpcInventory>(self);
    auto* mem = reg.try_get<AgentMemory>(self);
    Landmark* homeLm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    if (!bag || !mem || !homeLm) {
        ai_home_wanderer(p, rt, pools, ctx);
        return;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // ПОГРУЗКА — ТОЛЬКО ДОМА («ничего не телепортируется», S5): Idle
        // вне дома — это пробуждение после привала в пути (Resting будит в
        // Idle), и рейс ПРОДОЛЖАЕТСЯ, а не грузится телепатией со склада —
        // измерено: вендор в горах перегружался с домашнего склада на
        // каждом привале.
        // «ДОМА» — ОДИН предикат прибытия (радиус at_target): марш
        // финиширует в ±2 клетках от цели, и точное равенство клетке здесь
        // объявляло финишировавшего «не дома» — рейс возобновлялся К
        // ГОРОДУ из клетки у крыльца, челнок не завершался никогда
        // (измерено: артели 1299 заперты в поле 30 дней).
        if (torus_dist_sq(p.x, p.y, home.x, home.y,
                          float(ctx.mapW), float(ctx.mapH)) >= 4.0f) {
            if (Landmark* m2 = landmark_by_id(*ctx.mw.gs,
                                              rt.targetSettlementId);
                m2 && landmark_is_settlement(m2->type)) {
                rt.targetX = float(m2->x);
                rt.targetY = float(m2->y);
                rt.state = std::uint8_t(NS::Traveling);
            } else {
                rt.targetX = home.x;
                rt.targetY = home.y;
                rt.state = std::uint8_t(NS::Returning);
            }
            return;
        }
        // РЫНОК — ИЗ ПОРУЧЕНИЯ (аукцион его и выбрал: деревне — сюзерен,
        // городу — деревня домена). Прежний жёсткий сюзерен был хардкодом
        // «рынок бывает только городом»: с ним горожанам было некуда ехать.
        Landmark* market = landmark_by_id(*ctx.mw.gs, int(rt.errandObject));
        if (!market || !landmark_is_settlement(market->type)
            || market->id == homeLm->id) {
            ai_home_wanderer(p, rt, pools, ctx);
            return;
        }
        // (Здесь крю снимало СНИМОК своего рынка на выезде. Снимок вырезан
        // 2026-09-22: на вопрос «чего дому не хватает» отвечает ВЕДОМОСТЬ
        // места, и отвечала всегда она — снимок писался и не читался.)
        // (ШОВ ДАНИ ВЫРЕЗАН 2026-09-22. Здесь рейс сбыта грузил долг
        // вассала и вёз его НА ЛЮБОЙ рынок, куда ехал сам, — то есть дань
        // рассеивалась случайному соседу вместо сюзерена, и это был
        // главный «врёт читателю» проекта, живший с сессии 7 (§55).
        // Дань теперь ходит своей машиной СВЕРХУ ВНИЗ: сюзерен поднимает
        // сборщика на каждого должника, CANON S4 «Сборщик идёт вниз».)
        // Load what is cheap at home — the one loading law
        // (load_cheap_at_home_): never a row the home itself is short of
        // (склад держит только излишек — долг съел нужду приходом).
        const Skills& homeSite = landmark_sheet(homeLm->type).skills;
        plan_home_load_(homeLm->inventory, homeLm->needDebt,
                        souls_home(*homeLm), homeSite, rt.carryCap,
                        &bag->inv);
        if (inventory_weight(bag->inv) <= 0.0f
            && inventory_value(bag->inv) <= 0) {
            // Nothing to sell and nothing owed: wait out the morning.
            rt.stateTimer = std::int16_t(40 + rand_int(ctx, 40));
            return;
        }
        rt.targetSettlementId = market->id;
        rt.targetX = float(market->x);
        rt.targetY = float(market->y);
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(4 + rand_int(ctx, 4));
            return;
        }
        const float ox = p.x, oy = p.y;
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        if (march_is_stuck_(p, ox, oy, rt, pools)) {
            rt.targetX = home.x;
            rt.targetY = home.y;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        if (Landmark* market = landmark_by_id(*ctx.mw.gs,
                                              rt.targetSettlementId);
            market && landmark_is_settlement(market->type)) {
            // ЧТО ВЕЗТИ ДОМОЙ судит ВЕДОМОСТЬ ДОМА, а не память крю
            // (CANON S10, ярус 2): дом сам выписал свои цены точным
            // складом и своим счётом.
            const CaravanDeal deal = trade_vendor_at_market(
                bag->inv, rt.carryCap, *market, &homeLm->ledger,
                leader_trade_power_(*ctx.mw.world, self),
                landmark_trade_power_(*market),
                ctx.mw.econFacts, ctx.mw.econFactsUser);
            if (deal.movedTableValue > 0) {
                record_landmark_fact(*ctx.mw.gs, FactKind::Traded,
                                     rt.homeSettlementId,
                                     int(rt.targetX), int(rt.targetY),
                                     deal.movedTableValue,
                                     rt.targetSettlementId);
            }
        // ── РАЗВИЛКА РЕЙСА: «ДАЛЬШЕ» ПРОТИВ «ДОМОЙ» (CANON S10) ──────────
        // Здесь до 2026-09-22 стоял безусловный разворот домой — рейс
        // кончался ПРИБЫТИЕМ, один рынок за выезд. Это был один из четырёх
        // ответов на «пора ли кончать» (problems §55-III); теперь ответ один
        // на всех: ДОМОЙ — ЭТО ПРОСТО ЕЩЁ ОДНА ЗАЯВКА В ТОМ ЖЕ АУКЦИОНЕ,
        // обе в «монетах на душу в день», выбор — рулетка.
        // Ни счётчика станций, ни таймера рейса: таймер живёт в знаменателе,
        // и потому у дальности рейса нет потолка (владелец 2026-09-22).
        {
            // ЧТО ДОМУ НУЖНО И ПОЧЁМ — ВЕДОМОСТЬ ДОМА (ярус 2): крю в поле
            // не видит домашний склад живьём и видеть не должно.
            int homePrice[kCommodityCount] = {};
            int homeLack[kCommodityCount] = {};
            int cargo[kCommodityCount] = {};
            const bool haveLedger = homeLm->ledger.published();
            long long homeValue = 0;
            for (int c = 0; c < kCommodityCount; ++c) {
                const char* id = kCommodities[c].id;
                const ItemDef* d = item_def(id);
                const int base = d ? d->value : 0;
                homePrice[c] = haveLedger
                                   ? homeLm->ledger.price[std::size_t(c)]
                                   : base;
                const int lack =
                    haveLedger
                        ? homeLm->ledger.demand[std::size_t(c)]
                              - homeLm->inventory.count(id)
                        : 0;
                homeLack[c] = lack > 0 ? lack : 0;
                cargo[c] = bag->inv.count(id);
                // ЗАЯВКА «ДОМОЙ» — «что уже в трюме стоит ДЛЯ НУЖДЫ ДОМА»,
                // а не вся стоимость груза: серебро, которого дому не надо,
                // домой не торопится. Оттого числитель растёт ровно по мере
                // того, как трюм набивается НУЖНЫМ, — и рейс кончается сам,
                // без квоты и без счётчика.
                const long long fit =
                    std::min<long long>(cargo[c], homeLack[c]);
                if (fit > 0) homeValue += fit * homePrice[c];
            }
            const float daysHome =
                march_days_(ctx, int(p.x), int(p.y), int(home.x),
                            int(home.y));
            const float bidHome =
                daysHome > 0.0f ? float(homeValue) / daysHome : 0.0f;
            // КАНДИДАТ — СОСЕД ПО МЕМБРАНЕ, ИЗ ТОЧКИ СТОЯНИЯ (S7,
            // диффузия), и НЕ тот, откуда пришли: рынок, который только что
            // обслужили, шанса не получает.
            float nextX = 0.0f, nextY = 0.0f;
            const int nextId = pick_next_station_(ctx, p, market->id,
                                                  rt.prevStationId,
                                                  nextX, nextY);
            float bidGo = 0.0f;
            const Landmark* next =
                nextId >= 0 ? landmark_by_id(*ctx.mw.gs, nextId) : nullptr;
            if (next && next->id != homeLm->id) {
                const long long gain =
                    trade_bid_value_(ctx.mw, int(p.x), int(p.y), *next,
                                     cargo, homePrice, homeLack);
                // ДЛИТЕЛЬНОСТЬ — ВЕСЬ ОСТАТОК РЕЙСА: туда И оттуда домой.
                // Иначе «дальше» дешевело бы по построению, и крю уходило бы
                // от дома бесконечно — знаменатель обязан расти с отъездом.
                const float daysGo =
                    march_days_(ctx, int(p.x), int(p.y), next->x, next->y)
                    + march_days_(ctx, next->x, next->y, int(home.x),
                                  int(home.y));
                if (daysGo > 0.0f)
                    bidGo = (float(gain)
                             - route_fear_(ctx, int(p.x), int(p.y),
                                           next->x, next->y))
                            / daysGo;
            }
            // РУЛЕТКА, А НЕ ARGMAX — закон аукциона целей. Ни одной
            // положительной заявки = домой: сквад без возвращения есть
            // молчаливая утечка склада душ (S4).
            bool goOn = false;
            if (bidGo > 0.0f) {
                const float total =
                    bidGo + (bidHome > 0.0f ? bidHome : 0.0f);
                const float roll =
                    ctx.rng ? float(rand_int(ctx, 1 << 20)) / float(1 << 20)
                            : 1.0f;
                goOn = roll * total < bidGo;
            }
            if (goOn && next) {
                rt.prevStationId = market->id;
                rt.errandObject = std::uint32_t(next->id);
                rt.targetSettlementId = next->id;
                rt.targetX = nextX;
                rt.targetY = nextY;
                rt.state = std::uint8_t(NS::Traveling);
                return;
            }
        }
        }
        rt.prevStationId = -1;
        rt.targetX = home.x;
        rt.targetY = home.y;
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            if (rt.state == std::uint8_t(NS::Returning)) {
                // Home: purchases and earnings land on the home store; the
                // rotation dissolves the crew at dawn.
                for (int i = 0; i < kCommodityCount; ++i) {
                    haul_between(bag->inv, depot_(*homeLm, ctx.mw),
                                 kCommodities[i].id, 1 << 30, 1e9f);
                }
                transfer_value_dense(bag->inv, depot_(*homeLm, ctx.mw),
                               inventory_value(bag->inv));
            }
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

// The SUZERAIN landmark a town owes — a place's Stance::Suzerain row in its
// (`capital_of_` СНЕСЁН 2026-09-22 вместе с дорогой дани ВВЕРХ: он отвечал
//  «кому этот город платит», а платить наверх больше некому — сюзерен сам
//  посылает сборщика вниз. Наряд сессии 7 требовал этого сноса, и вот он.)

// ── СБОРЩИК ИДЁТ ВНИЗ (CANON S4 «Сборщик идёт вниз», Б-4) ───────────────
// Здесь стоял `ai_taxrun` — 145 строк, и он носил дань ВВЕРХ: вассал сам
// снаряжал курьера к сюзерену. Канон говорит обратное, и владелец повторил
// это дословно: «город рождает… сборщика налогов, который ходит в
// вассальные деревни». Машина перевёрнута 2026-09-22.
//
// Поручение ставит ТОТ ЖЕ аукцион, что поднимает артель и корована: заявка
// сборщика — одна на КАЖДОГО должника, объект = ординал вассала, скор —
// та же выработка в день рейса (стоимость долга / дни пути). Пул рук
// урезает, как и всех (CANON S4 «рождение сквада — один закон»).
//
// ДОЛГ БЕРЁТСЯ ПО ПЛОТНОСТИ СТОИМОСТИ (владелец 2026-09-22): первым уходит
// самое ценное, что есть у вассала. Это строже прежнего «доля каждого
// стака» — заплатить зерном, оставив серебро, невозможно.
void ai_collector(entt::entity self, MacroPos& p,
                  ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                  const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || !ctx.mw.world) {
        ai_nomad(p, rt, pools, ctx);
        return;
    }
    auto& reg = ctx.mw.world->reg;
    auto* bag = reg.try_get<ecs::NpcInventory>(self);
    Landmark* homeLm = landmark_by_id(*ctx.mw.gs, rt.homeSettlementId);
    if (!bag || !homeLm) {
        ai_nomad(p, rt, pools, ctx);
        return;
    }
    Landmark* vassal = landmark_by_id(*ctx.mw.gs, int(rt.errandObject));
    if (!vassal || vassal->id == homeLm->id) {
        ai_home_wanderer(p, rt, pools, ctx);
        return;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // Дома и с пустыми руками — идём за долгом; дома с грузом — сдаём.
        if (torus_dist_sq(p.x, p.y, home.x, home.y,
                          float(ctx.mapW), float(ctx.mapH)) >= 4.0f) {
            rt.targetX = float(vassal->x);
            rt.targetY = float(vassal->y);
            rt.state = std::uint8_t(NS::Traveling);
            return;
        }
        if (!owes_tithe(*vassal)) {
            // Должник рассчитался (собрали или простили) — ждать нечего,
            // ротация завтра переторгует эту строку заново.
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 8));
            return;
        }
        rt.targetSettlementId = vassal->id;
        rt.targetX = float(vassal->x);
        rt.targetY = float(vassal->y);
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(2 + rand_int(ctx, 3));
            return;
        }
        // НОГИ. Без этой строки машина СТОИТ, вечно числясь «в пути»:
        // ровно так и было при первой сборке 2026-09-22 — гистограмма
        // состояний показала Traveling 799 563 при Working РОВНО НОЛЬ, и
        // канал дани был пуст, хотя заявки и рождение работали.
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // ВЗЫСКАНИЕ И ПОГАШЕНИЕ — В ОДНОЙ ТОЧКЕ: сколько увёз, столько и
        // списал, поэтому шва между «взято» и «зачтено» физически нет.
        long long owed = vassal->titheOwedValue;
        long long took = 0;
        // ── СНАЧАЛА ПО НУЖДЕ ДОМА, ОСТАТОК — ПО ПЛОТНОСТИ ───────────────
        // Вердикт владельца 2026-09-21, дословно: «грузит ПО НУЖДЕ ДОМА,
        // остаток — по value_dense_order. Иначе сборщик везёт домой
        // серебро, а меряем мы еду». Это ровно то, что измерилось
        // 2026-09-22, когда порядок был только по плотности: город получал
        // казну и продолжал голодать (food_city −99.8 %).
        // Чего дому не хватает — говорит ЕГО ВЕДОМОСТЬ (ярус 2): крю в поле
        // домашний склад живьём не видит.
        if (owed > 0 && homeLm->ledger.published()) {
            // ПОРЯДОК НУЖДЫ — ПО ТОМУ, ЧЕГО ДОМУ НЕ ХВАТАЕТ БОЛЬШЕ ВСЕГО
            // В СТОИМОСТИ (нехватка × домашняя цена), а НЕ по плотности.
            // Измерено 2026-09-22: с плотностью еда стоит последней (она
            // самая дешёвая на килограмм), долг кончался на серебре, и
            // город продолжал голодать при работающем сборщике
            // (food_city −99.4 %). Цена дома уже несёт срочность —
            // непокрытая нужда сама дорожает, — поэтому ноль новых правил.
            int order[kCommodityCount];
            long long urgency[kCommodityCount];
            for (int c = 0; c < kCommodityCount; ++c) {
                order[c] = c;
                const char* cid = kCommodities[c].id;
                const int lk = homeLm->ledger.demand[std::size_t(c)]
                               - homeLm->inventory.count(cid);
                urgency[c] = lk > 0
                    ? (long long)lk
                          * homeLm->ledger.price[std::size_t(c)]
                    : 0;
            }
            for (int a = 1; a < kCommodityCount; ++a)
                for (int b = a; b > 0
                                && urgency[order[b]] > urgency[order[b - 1]];
                     --b)
                    std::swap(order[b], order[b - 1]);
            for (int oi = 0; oi < kCommodityCount && owed > 0; ++oi) {
                const int c = order[oi];
                const char* id = kCommodities[c].id;
                const ItemDef* d = item_def(id);
                const int base = d ? d->value : 0;
                if (base <= 0) continue;
                const int lack = homeLm->ledger.demand[std::size_t(c)]
                                 - homeLm->inventory.count(id);
                if (lack <= 0) continue;
                const long long affordable = owed / base;
                if (affordable <= 0) continue;
                const int want = int(std::min<long long>(lack, affordable));
                const int moved = haul_between(
                    vassal->inventory, bag->inv, id, want,
                    rt.carryCap - inventory_weight(bag->inv));
                if (moved <= 0) continue;
                owed -= (long long)moved * base;
                took += (long long)moved * base;
            }
        }
        // ОСТАТОК ДОЛГА — ПО ПЛОТНОСТИ: самое ценное, что осталось у
        // вассала. Заплатить зерном, оставив серебро, нельзя ни с какой
        // стороны — нужда дома уже взяла своё зерно выше.
        if (owed > 0) {
            const int dense = transfer_value_dense(
                vassal->inventory, Depot(bag->inv),
                int(std::min<long long>(owed, 1 << 30)));
            if (dense > 0) {
                owed -= dense;
                took += dense;
            }
        }
        if (took > 0) {
            vassal->titheOwedValue -= took;
            if (vassal->titheOwedValue < 0) vassal->titheOwedValue = 0;
            record_landmark_fact(*ctx.mw.gs, FactKind::Taxed,
                                 vassal->id, int(p.x), int(p.y),
                                 int(std::min<long long>(took, 1 << 30)),
                                 rt.homeSettlementId);
        }
        rt.targetX = home.x;
        rt.targetY = home.y;
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            for (int i = 0; i < kCommodityCount; ++i)
                haul_between(bag->inv, depot_(*homeLm, ctx.mw),
                             kCommodities[i].id, 1 << 30, 1e9f);
            transfer_value_dense(bag->inv, depot_(*homeLm, ctx.mw),
                                 inventory_value(bag->inv));
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return;
    }
    rt.state = std::uint8_t(NS::Idle);
    rt.stateTimer = std::int16_t(4);
}

void ai_trader(MacroPos& p, ecs::MacroNpcRuntime& rt,
               ecs::Pools& pools, const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home)) return;
    auto& settles = ctx.mw.gs->landmarks;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // Pick another city (id != home).
            int candidates = 0;
            for (auto& s : settles) {
                if (s.type != LandmarkType::City) continue;
                if (s.id != rt.homeSettlementId) ++candidates;
            }
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                for (auto& s : settles) {
                    if (s.type != LandmarkType::City) continue;
                    if (s.id == rt.homeSettlementId) continue;
                    if (pick-- == 0) {
                        rt.targetSettlementId = s.id;
                        rt.targetX = float(s.x); rt.targetY = float(s.y);
                        rt.state  = std::uint8_t(NS::Traveling);
                        break;
                    }
                }
            } else {
                rt.stateTimer = 20;
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Working);
            rt.stateTimer = std::int16_t(15 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            rt.targetX = home.x; rt.targetY = home.y;
            rt.targetSettlementId = -1;
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Returning)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(20 + rand_int(ctx, 30));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

void ai_nomad(MacroPos& p, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    auto& settles = ctx.mw.gs->landmarks;
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            int candidates = 0;
            for (auto& s : settles) {
                if (s.type != LandmarkType::City) continue;
                if (s.id != rt.targetSettlementId) ++candidates;
            }
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                for (auto& s : settles) {
                    if (s.type != LandmarkType::City) continue;
                    if (s.id == rt.targetSettlementId) continue;
                    if (pick-- == 0) {
                        rt.targetSettlementId = s.id;
                        rt.targetX = float(s.x); rt.targetY = float(s.y);
                        rt.state  = std::uint8_t(NS::Traveling);
                        break;
                    }
                }
            } else {
                rt.stateTimer = 10;
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

void ai_aggressive(MacroPos& p, ecs::MacroNpcRuntime& rt,
                   ecs::Pools& pools, const TickContext& ctx) {
    // No private player-channel here any more (owner, 2026-08-29: «игрок
    // ничем не особенен»). Perception and pursuit are squad_threat_step's —
    // the player's squad sits in the SAME SquadIndex at the SAME
    // kSquadSightCells as every other squad, so an aggressive row that can
    // see the player chases him through the one law it chases anyone by.
    // The old channel saw the player at 10 cells against everyone else's 6.
    // What is left below is the row's untroubled day: wander.
    if (rt.state == std::uint8_t(NS::Chasing)) {
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = std::int16_t(5 + rand_int(ctx, 10));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 20, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

// ── Царь-крестьянин: роамер-охотник на магов (стол анкет) ────────────────
// Слово владельца (2026-09-10): «культистов он не трогает, только магов
// Магики — но НЕ крестьян фракции магики»; поводка нет — чистый роамер по
// всей карте. Кто цель — решение ЭТОЙ функции (закон стола: данные в
// строке — решения в функции): тело мага (Witch/Sorceress) фракции
// magika. Встреча решается ТОЙ ЖЕ дверью боя, что у threat step — один
// закон битвы (CANON S13), никакого второго резолвера.

// Охотничий глаз: в полтора раза дальше сквадного (kSquadSightCells = 8) —
// охотник ИЩЕТ, а не натыкается; ±2 бакета грида (cellSize 8) покрывают
// радиус целиком.
constexpr float kMageHuntSightCells = 12.0f;

entt::entity nearest_magika_mage(entt::entity self, const MacroPos& p,
                                 const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return entt::null;
    auto& reg = ctx.mw.world->reg;
    const int magika = faction_index("magika");
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    float best = kMageHuntSightCells * kMageHuntSightCells + 1.0f;
    entt::entity found = entt::null;
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const entt::entity e = entt::entity(*it);
                if (e == self || !reg.valid(e)) continue;
                if (reg.any_of<ecs::Dead>(e)) continue;
                const auto* oc = reg.try_get<ecs::MacroCell>(e);
                const auto* ok = reg.try_get<ecs::NPCKind>(e);
                if (!oc || !ok) continue;
                // Маг = род тела, не флаг: ведьма и чародейка — строки
                // реестра. Крестьянин Магики проходит мимо этого фильтра
                // ЖИВЫМ — ровно то, что владелец назвал важным.
                if (int(ok->factionIdx) != magika) continue;
                const NPCType t = NPCType(std::uint8_t(ok->type));
                if (t != NPCType::Witch && t != NPCType::Sorceress) continue;
                const float d = torus_dist_sq(
                    p.x, p.y,
                    float(ecs::cell_x(*oc, ctx.mapW)),
                    float(ecs::cell_y(*oc, ctx.mapW)),
                    float(ctx.mapW), float(ctx.mapH));
                if (d >= best) continue;
                best = d;
                found = e;
            }
        }
    }
    return found;
}

void ai_mage_hunt(entt::entity self, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    if (ctx.squads && ctx.mw.world && ctx.mw.gs) {
        const entt::entity prey = nearest_magika_mage(self, p, ctx);
        if (prey != entt::null) {
            auto& reg = ctx.mw.world->reg;
            const auto& ecell = reg.get<ecs::MacroCell>(prey);
            const MacroPos ep{float(ecs::cell_x(ecell, ctx.mapW)),
                              float(ecs::cell_y(ecell, ctx.mapW))};
            if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
                // Игрок в теле ведьмы: встреча принадлежит форс-двери
                // игрока (main.cpp detect_forced_encounter), не тихому
                // резолву — тот же гард, что у threat step.
                if (reg.any_of<ecs::PlayerTag, ecs::PlayerSquadTag>(prey)) {
                    rt.visualSpeed = 0.0f;
                    return;
                }
                if (!ctx.allowAutoBattle) return;
                const AutoBattleOutcome o = resolve_auto_battle(
                    auto_battle_side_of(*ctx.mw.world, self),
                    auto_battle_side_of(*ctx.mw.world, prey),
                    Ambush::None, *ctx.rng);
                settle_auto_battle(ctx.mw, self, prey, o);
                rt.visualSpeed = 0.0f;
                if (!reg.all_of<ecs::Dead>(self)) {
                    rt.state = std::uint8_t(NS::Idle);
                    rt.stateTimer = std::int16_t(3 + rand_int(ctx, 5));
                }
                return;
            }
            rt.targetX = ep.x;
            rt.targetY = ep.y;
            rt.state = std::uint8_t(NS::Chasing);
            try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
            return;
        }
    }
    // Некого бить — чистый роам: дальняя случайная цель по ВСЕЙ карте, не
    // прогулка по околице. Поводка нет — слово владельца.
    if (rt.state == std::uint8_t(NS::Chasing)) {
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = 0;
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            rt.targetX = float(rand_int(ctx, ctx.mapW));
            rt.targetY = float(rand_int(ctx, ctx.mapH));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

// ── Вылеты из логова (стол анкет — модель дракона) ───────────────────────
// Дом — КЛЕТКА (rt.lairX/Y, гора — не ландмарк), радиус вылетов — агенда
// строки анкеты. В вылете бьёт любой сквад СЛАБЕЕ себя (слово владельца) —
// той же дверью встречи, что threat step; наевшись или улетев за радиус —
// домой. Игрокова встреча — форс-двери игрока, как у всех.
entt::entity nearest_weaker_squad(entt::entity self, const MacroPos& p,
                                  float sightCells, const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return entt::null;
    auto& reg = ctx.mw.world->reg;
    const float myPower =
        squad_power(auto_battle_side_of(*ctx.mw.world, self));
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    float best = sightCells * sightCells + 1.0f;
    entt::entity found = entt::null;
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const entt::entity e = entt::entity(*it);
                if (e == self || !reg.valid(e)) continue;
                if (reg.any_of<ecs::Dead>(e)) continue;
                const auto* oc = reg.try_get<ecs::MacroCell>(e);
                if (!oc) continue;
                const float d = torus_dist_sq(
                    p.x, p.y,
                    float(ecs::cell_x(*oc, ctx.mapW)),
                    float(ecs::cell_y(*oc, ctx.mapW)),
                    float(ctx.mapW), float(ctx.mapH));
                if (d >= best) continue;
                if (squad_power(auto_battle_side_of(*ctx.mw.world, e))
                        >= myPower) {
                    continue;   // добыча — только слабее
                }
                best = d;
                found = e;
            }
        }
    }
    return found;
}

void ai_lair_sorties(entt::entity self, MacroPos& p,
                     ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                     const TickContext& ctx) {
    // Логово самозалечивается: тело без дома объявляет домом место, где
    // проснулось (спавн-дверь анкеты пишет честную клетку раньше).
    if (rt.lairX < 0 || rt.lairY < 0) {
        rt.lairX = std::int16_t(wrap_axis(int(p.x), ctx.mapW));
        rt.lairY = std::int16_t(wrap_axis(int(p.y), ctx.mapH));
    }
    // Радиус вылетов — агенда СТРОКИ анкеты (данные в строке — решения в
    // функции); тело без анкеты, если когда-то получит эту модель, кружит
    // дефолтной восьмёркой.
    float radius = 8.0f;
    if (const auto* dc =
            ctx.mw.world->reg.try_get<ecs::DesignCharacterTag>(self)) {
        if (const DesignCharacterDef* row = design_character(dc->ordinal)) {
            if (row->agenda.radiusCells > 0) {
                radius = float(row->agenda.radiusCells);
            }
        }
    }
    const float fromLair = std::sqrt(torus_dist_sq(
        p.x, p.y, float(rt.lairX), float(rt.lairY),
        float(ctx.mapW), float(ctx.mapH)));

    if (ctx.squads && ctx.mw.world && ctx.mw.gs && fromLair < radius) {
        const entt::entity prey =
            nearest_weaker_squad(self, p, radius, ctx);
        if (prey != entt::null) {
            auto& reg = ctx.mw.world->reg;
            const auto& ecell = reg.get<ecs::MacroCell>(prey);
            const MacroPos ep{float(ecs::cell_x(ecell, ctx.mapW)),
                              float(ecs::cell_y(ecell, ctx.mapW))};
            if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
                if (reg.any_of<ecs::PlayerTag, ecs::PlayerSquadTag>(prey)) {
                    rt.visualSpeed = 0.0f;
                    return;
                }
                if (!ctx.allowAutoBattle) return;
                const AutoBattleOutcome o = resolve_auto_battle(
                    auto_battle_side_of(*ctx.mw.world, self),
                    auto_battle_side_of(*ctx.mw.world, prey),
                    Ambush::SideA, *ctx.rng);
                settle_auto_battle(ctx.mw, self, prey, o);
                rt.visualSpeed = 0.0f;
                if (!reg.all_of<ecs::Dead>(self)) {
                    rt.state = std::uint8_t(NS::Idle);
                    rt.stateTimer = std::int16_t(5 + rand_int(ctx, 8));
                }
                return;
            }
            rt.targetX = ep.x;
            rt.targetY = ep.y;
            rt.state = std::uint8_t(NS::Chasing);
            try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
            return;
        }
    }

    // Улетел за радиус или добычи нет и стоит не дома — домой.
    if (fromLair >= radius
        || (rt.state != std::uint8_t(NS::Wandering)
            && (int(p.x) != int(rt.lairX) || int(p.y) != int(rt.lairY)))) {
        rt.targetX = float(rt.lairX);
        rt.targetY = float(rt.lairY);
        rt.state = std::uint8_t(NS::Returning);
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return;
    }
    // Дома и сыт: кружи по округе логова.
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(float(rt.lairX), float(rt.lairY),
                                      int(radius), ctx);
            rt.targetX = float(t.x); rt.targetY = float(t.y);
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return;
    }
    rt.state = std::uint8_t(NS::Idle);
    rt.stateTimer = std::int16_t(3 + rand_int(ctx, 5));
}

// (ЗДЕСЬ ЖИЛА ПАТРУЛЬНАЯ ВЫЛАЗКА — снесена 2026-09-21 вместе с механикой.
// Ушли: kPatrolReachHops / kPatrolDwellDays / kPatrolDwellThinks,
// PatrolRoamCells, collect_trouble_cells_, ai_patrol_errand (91 строка),
// патрульная урна run_patrol_auction и подъём вылазки из гарнизона.
//
// А 2026-09-22 ушёл и `ai_patrol` — последний легаси-круг у дома, который
// ходили генезисные одиночки-стражники. Их вырезали тем же ходом
// (npc_spawn.cpp), и машина осталась без единого носителя. Вместе с ней
// умерли значение `AIBehaviour::Patrol` и состояние `NPCState::Patrolling`:
// колонка без писателя — дефект (AGENTS §9), а не задел.
//
// СЦЕНА ЭТОГО НЕ ЗАМЕТИТ, и это проверено, а не обещано: стойку тела даёт
// `combatant_behaviour`, она отвечала `true` и на Patrol, и на Aggressive, а
// строка Guard переведена именно в Aggressive — `subworld_ai_for` вернёт тот
// же `SubworldAi::Combat`.
// Поле угрозы и страх артелей ЖИВЫ — kThreatFearShift ниже читает аукцион.)

void ai_teleporter(MacroPos& p, ecs::MacroNpcRuntime& rt,
                   ecs::Pools& pools, const TickContext& ctx) {
    if (rt.teleportCooldown > 0) --rt.teleportCooldown;
    if (rt.teleportCooldown <= 0 && rand_f01(ctx) < 0.005f) {
        XY t = pick_random_nearby(p.x, p.y, 40, ctx);
        p.x = t.x; p.y = t.y;
        // A jump has no entry edge — the sentinel degrades placement to the
        // whole-cell scatter instead of inventing a side.
        rt.entryDir = kEntryDirNone;
        rt.entryTicks = 0;
        rt.teleportCooldown = 50;
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = 10;
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 15, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(12 + rand_int(ctx, 20));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

void ai_wanderer(MacroPos& p, ecs::MacroNpcRuntime& rt,
                 ecs::Pools& pools, const TickContext& ctx) {
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 25, ctx);
            rt.targetX = t.x; rt.targetY = t.y;
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_target(p, rt, ctx)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
    }
}

// ── Squad↔squad perception and war (Session 15) ───────────────────────────
//
// ONE universal step, run before every role behaviour: does a hostile squad
// stand near me, and what do I do about it? Before this, macro NPCs were
// ghosts to each other — the only "other" any behaviour ever saw was the
// player. The rules are the owner's design (macrosim.md):
//   · perception through the transient SquadIndex, hostility through the ONE
//     relation matrix at the ONE line (faction.h kHostileThreshold) — the
//     same numbers the subworld battle masks read;
//   · flee or fight decided by squad_power — the SAME strength law the
//     resolver uses, so a squad never runs from a fight the law says it
//     wins. Traits modulate courage (Cowardly breaks early, Brave stands);
//   · fighters pursue (the same data column subworld hostility reads:
//     subworld_ai_for(row.ai) — bandits raid, patrols hunt), civilians run;
//   · a geometric meeting (same macro cell) IS the fight: resolved by the
//     one auto-battle law, settled through the one ledger.

constexpr float kSquadSightCells = 6.0f;

// ВОСПРИЯТИЕ — свойство сквада, не спец-механика (владелец 2026-09-03,
// CANON S10: «это контекстно … из свойств самого сквада и ролевой системы»).
// Phase 6: the ROLE layer arrived — the leader's Scouting rank widens the
// base radius by THE skill law (the row's pctPerRank, through the table
// door — never an inline percent), read off the runtime's cached rank
// (refresh_leader_travel_stats, the one refresh door) because this query
// runs per think. A rankless squad sees the v1 constant exactly.
inline float squad_sight_cells(const ecs::NPCKind&,
                               const ecs::MacroNpcRuntime* rt) {
    const int rank = rt ? int(rt->scoutRank) : 0;
    return kSquadSightCells
        * float(skill_mult_pct_of(SkillId::Scouting, rank)) / 100.0f;
}

// Flee when the enemy is this many times stronger; trait-scaled below.
constexpr float kBraveryBase   = 1.5f;
constexpr float kBraveryCoward = 0.6f;   // Cowardly: breaks far earlier
constexpr float kBraveryBrave  = 1.8f;   // Brave: stands into worse odds
// Pursue only fights the law says we win with margin.
constexpr float kPursueMargin  = 1.1f;

float bravery_of(const ecs::NpcTraits* traits) {
    float b = kBraveryBase;
    if (!traits) return b;
    for (std::uint8_t i = 0; i < traits->count; ++i) {
        if (traits->traits[i] == std::uint8_t(NPCTrait::Cowardly))
            b *= kBraveryCoward;
        if (traits->traits[i] == std::uint8_t(NPCTrait::Brave))
            b *= kBraveryBrave;
    }
    return b;
}

entt::entity nearest_hostile_squad(entt::entity self, const MacroPos& p,
                                   const ecs::NPCKind& kind,
                                   const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return entt::null;
    auto& reg = ctx.mw.world->reg;
    const char* myFaction = faction_id_for_index(kind.factionIdx);
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    const float sight =
        squad_sight_cells(kind, reg.try_get<ecs::MacroNpcRuntime>(self));
    float best = sight * sight + 1.0f;
    entt::entity found = entt::null;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const entt::entity e = entt::entity(*it);
                if (e == self || !reg.valid(e)) continue;
                const auto* oc = reg.try_get<ecs::MacroCell>(e);
                const auto* ok = reg.try_get<ecs::NPCKind>(e);
                if (!oc || !ok) continue;
                const float d = torus_dist_sq(
                    p.x, p.y,
                    float(ecs::cell_x(*oc, ctx.mapW)),
                    float(ecs::cell_y(*oc, ctx.mapW)),
                    float(ctx.mapW),
                    float(ctx.mapH));
                if (d >= best) continue;
                if (!factions_hostile(ctx.mw.gs, myFaction,
                                      faction_id_for_index(ok->factionIdx))) {
                    continue;
                }
                best = d;
                found = e;
            }
        }
    }
    return found;
}

// Returns true when the threat consumed this think (fled, pursued or
// fought); the role behaviour then waits for a calmer half hour.
bool squad_threat_step(entt::entity self, MacroPos& p,
                       const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                       ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.world || !ctx.squads || !ctx.mw.gs) return false;

    const entt::entity enemy = nearest_hostile_squad(self, p, kind, ctx);
    if (enemy == entt::null) {
        // Threat gone: a fleeing squad calms down and resumes its life.
        if (rt.state == std::uint8_t(NS::Fleeing)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = 0;
        }
        return false;
    }

    auto& reg = ctx.mw.world->reg;
    const auto& ecell = reg.get<ecs::MacroCell>(enemy);
    const MacroPos ep{float(ecs::cell_x(ecell, ctx.mapW)),
                      float(ecs::cell_y(ecell, ctx.mapW))};
    const float myPower = squad_power(auto_battle_side_of(*ctx.mw.world, self));
    const float theirPower =
        squad_power(auto_battle_side_of(*ctx.mw.world, enemy));

    // The geometric meeting: same macro cell = the fight happens, resolved
    // by the ONE law and settled through the ONE ledger. An ambush is a
    // pursuer catching a squad that never saw it coming.
    if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
        // A player-controlled squad's meetings belong to the forced-encounter
        // door (Inc 6, main.cpp detect_forced_encounter): the squad stands ON
        // the meeting cell and the door opens the pre-battle screen — never
        // the silent auto-resolve. Both flags, because possession moves
        // PlayerTag while PlayerSquadTag stays home (components.h).
        if (reg.any_of<ecs::PlayerTag, ecs::PlayerSquadTag>(enemy)) {
            rt.visualSpeed = 0.0f;
            return true;
        }
        if (!ctx.allowAutoBattle) return false;
        auto* ert = reg.try_get<ecs::MacroNpcRuntime>(enemy);
        const bool ambush =
            rt.state == std::uint8_t(NS::Chasing) && ert
            && ert->state != std::uint8_t(NS::Chasing)
            && ert->state != std::uint8_t(NS::Fleeing);
        const AutoBattleOutcome o = resolve_auto_battle(
            auto_battle_side_of(*ctx.mw.world, self),
            auto_battle_side_of(*ctx.mw.world, enemy),
            ambush ? Ambush::SideA : Ambush::None, *ctx.rng);
        settle_auto_battle(ctx.mw, self, enemy, o);
        rt.visualSpeed = 0.0f;
        if (!reg.all_of<ecs::Dead>(self)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(3 + rand_int(ctx, 5));
        }
        // A beaten-but-alive enemy runs; distance is what prevents an
        // immediate rematch, and the winner's next think re-evaluates.
        if (ert && reg.valid(enemy) && !reg.all_of<ecs::Dead>(enemy)) {
            ert->state = std::uint8_t(NS::Fleeing);
        }
        return true;
    }

    const float bravery =
        bravery_of(reg.try_get<ecs::NpcTraits>(self));
    if (theirPower > myPower * bravery) {
        // Run directly away, torus-folded, a screen's worth of cells out.
        float dx = p.x - ep.x, dy = p.y - ep.y;
        if (dx > float(ctx.mapW) * 0.5f) dx -= float(ctx.mapW);
        if (dx < -float(ctx.mapW) * 0.5f) dx += float(ctx.mapW);
        if (dy > float(ctx.mapH) * 0.5f) dy -= float(ctx.mapH);
        if (dy < -float(ctx.mapH) * 0.5f) dy += float(ctx.mapH);
        const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        rt.targetX = wrapf(p.x + dx / len * 8.0f, float(ctx.mapW));
        rt.targetY = wrapf(p.y + dy / len * 8.0f, float(ctx.mapH));
        rt.state = std::uint8_t(NS::Fleeing);
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return true;
    }

    // Fighters close in when the law says they win: the same data column
    // that decides subworld combat stance decides who is a fighter at all.
    const bool fighter = combatant_behaviour(kNpcTypeDefs[kind.type].ai);
    if (fighter && myPower > theirPower * kPursueMargin) {
        rt.state = std::uint8_t(NS::Chasing);
        rt.targetX = ep.x;
        rt.targetY = ep.y;
        try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
        return true;
    }

    if (rt.state == std::uint8_t(NS::Fleeing)) {
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = 0;
    }
    return false;
}

} // namespace

// ── СЛЕД И ОХОТА (scent_field.h, CANON S10 «хищник-жертва», 2026-09-03) ──
// Поле отвечает «куда», закон боя — «бить или бежать». Писатель один: каждый
// think сквад кладёт в СВОЮ клетку два вклада — силу (squad_power, тот же
// единственный закон, что решает автобой) и цену (души по строкам найма +
// ценность груза по таблице цен). Хищник — незанятый боем combatant — идёт
// ВВЕРХ по градиенту чужой ЦЕНЫ под фильтром СИЛЫ; макроцель (errand) при
// этом не трогается — отвлечение временно по построению (владелец: «по ходу
// дела их может временно отвлечь враг»). Публично — по прецеденту
// trade_caravan_at_station: тест водит один think охоты руками.

// Цена душ сквада по строкам найма — та же таблица, что жалованье и threat
// (лидер — субъект, как в законе хлеба: 0 бойцов = 0 запаха богатства).
static std::uint32_t roster_worth(ecs::World& w, entt::entity e) {
    std::uint32_t worth = 0;
    if (const auto* bag = w.reg.try_get<ecs::NpcInventory>(e)) {
        for (int i = bag->inv.creature_first(); i < kMaxInventorySlots; ++i) {
            const ItemRef& r = bag->inv.slots[std::size_t(i)];
            const std::uint16_t kind =
                std::uint16_t(creature_of_world_row(r.def));
            if (!valid_npc_kind(kind)) continue;
            worth += std::uint32_t(
                std::max(0, npc_def(NPCType(kind)).hireGold))
                * std::uint32_t(r.count);
        }
    }
    return worth;
}

void scent_squad_deposit(entt::entity e, const MacroPos& p,
                         const ecs::NPCKind& kind, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.world) return;
    ScentField& sf = ctx.mw.gs->scent;
    const int f = int(kind.factionIdx);
    if (f < 0 || f >= sf.factions) return;   // kNoFaction не следит
    const float power =
        squad_power(auto_battle_side_of(*ctx.mw.world, e));
    std::uint32_t worth = roster_worth(*ctx.mw.world, e);
    if (const auto* bag = ctx.mw.world->reg.try_get<ecs::NpcInventory>(e)) {
        worth += std::uint32_t(std::max(0, inventory_value(bag->inv)));
    }
    scent_deposit(sf, f, wrap_axis(int(p.x), sf.w), wrap_axis(int(p.y), sf.h),
                  power <= 0.0f ? 0u : std::uint32_t(power), worth);
}

// След игрока: его сквад — обычный сквад (auto_battle_side_of отвечает за
// обоих), но think-свип осознанно не водит ДВА сквада — держателя флажка
// (им ходит игрок) и брошенный оригинал (стоит без сознания). Пахнут ОБА
// (2026-09-17): ходящий — там, где идёт, кем бы он ни был (запись ФЛАГА, а
// не ординал: до правки одержимый лорд шёл без следа, а стоящий оригинал
// пах как ходячий); стоящий — там, где стоит: бандит находит по следу и
// того, и другого. Петля демо «убегай от бандитов к страже».
void scent_player_deposit(const TickContext& ctx) {
    if (!ctx.mw.world) return;
    auto& reg = ctx.mw.world->reg;
    const auto put = [&](entt::entity e) {
        if (e == entt::null || !reg.valid(e)) return;
        if (reg.any_of<ecs::Dead>(e)) return;
        if (!reg.all_of<ecs::MacroCell, ecs::NPCKind>(e)) return;
        const auto& c = reg.get<ecs::MacroCell>(e);
        const MacroPos p{float(ecs::cell_x(c, ctx.mapW)),
                         float(ecs::cell_y(c, ctx.mapW))};
        scent_squad_deposit(e, p, reg.get<ecs::NPCKind>(e), ctx);
    };
    const entt::entity flag = player_flag_entity(*ctx.mw.world);
    put(flag);
    for (auto e : reg.view<ecs::PlayerSquadTag>()) {
        if (e != flag) put(e);
    }
}

// (Крутилки охоты — kHuntBoldShift и kHuntScentFloor — в npc_ai.h: их
// читает тест и вертит дубль-прогон.)
bool scent_hunt_step(entt::entity self, MacroPos& p,
                     const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                     ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.world) return false;
    if (!combatant_behaviour(kNpcTypeDefs[kind.type].ai)) return false;
    const ScentField& sf = ctx.mw.gs->scent;
    if (sf.factions <= 0 || sf.w != ctx.mapW || sf.h != ctx.mapH)
        return false;
    const int f = int(kind.factionIdx);
    if (f < 0 || f >= kFactionCount) return false;
    const std::uint64_t hostile = ctx.factionHostileMask[f];
    if (hostile == 0ull) return false;

    const float myPower =
        squad_power(auto_battle_side_of(*ctx.mw.world, self));
    if (myPower <= 0.0f) return false;
    const std::uint32_t fearCap =
        (std::uint32_t(myPower) >> kScentQuantShift) << kHuntBoldShift;

    // Сумма враждебных каналов клетки — фракции по битам запечённой маски.
    const auto sniff = [&](int x, int y, std::uint32_t& outStr,
                           std::uint32_t& outWea) {
        outStr = 0;
        outWea = 0;
        for (int b = 0; b < sf.factions; ++b) {
            if (!(hostile & (1ull << b))) continue;
            outStr += scent_strength_at(sf, b, x, y);
            outWea += scent_wealth_at(sf, b, x, y);
        }
    };

    const int x0 = wrap_axis(int(p.x), sf.w);
    const int y0 = wrap_axis(int(p.y), sf.h);
    std::uint32_t hereStr = 0, hereWea = 0;
    sniff(x0, y0, hereStr, hereWea);

    // Строго ВВЕРХ по цене: лучший из восьми соседей, что богаче моей клетки,
    // не ниже пола и не страшнее моей смелости. Нет такого — следа нет,
    // думает роль (подъём заканчивается на локальном максимуме сам, и это
    // и есть конец погони: либо жертва в радиусе зрения — рефлекс выше уже
    // перехватил, — либо след остыл и артель возвращается к делу).
    std::uint32_t best = std::max(hereWea, kHuntScentFloor - 1u);
    int bx = -1, by = -1;
    // Сосед по запаху — шаг ИНДЕКСА над полем мира (cell_step, ЗАКОН АДРЕСА).
    const std::uint32_t at0 = cell_of(x0, y0, sf.w);
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) continue;
            const std::uint32_t n = cell_step(at0, ox, oy, sf.w);
            const int x = cell_x(n, sf.w);
            const int y = cell_y(n, sf.w);
            std::uint32_t s = 0, v = 0;
            sniff(x, y, s, v);
            if (v <= best) continue;
            if (s > fearCap) continue;   // туда мне страшно — пусть решает
                                         //   визуальный рефлекс, не запах
            best = v;
            bx = x;
            by = y;
        }
    }
    if (bx < 0) return false;
    try_move(p, rt, pools, float(bx), float(by), ctx);
    return true;
}

namespace {

// Follow the waypoint route in the squad's orders (Session 15, Inc 7): walk
// to the current waypoint, arrive, take the next, loop. A squad ordered onto
// a route with no route wanders — a degraded order is a visible NPC, not a
// frozen one.
void ai_waypoints(entt::entity e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadOrders* orders = ctx.mw.world
        ? ctx.mw.world->reg.try_get<ecs::SquadOrders>(e) : nullptr;
    if (!orders || orders->waypointCount == 0) {
        ai_wanderer(p, rt, pools, ctx);
        return;
    }
    const int i = orders->currentWaypoint % orders->waypointCount;
    rt.targetX = wrapf(float(orders->waypoints[std::size_t(i * 2)]),
                       float(ctx.mapW));
    rt.targetY = wrapf(float(orders->waypoints[std::size_t(i * 2 + 1)]),
                       float(ctx.mapH));
    if (at_target(p, rt, ctx)) {
        orders->currentWaypoint =
            std::uint8_t((i + 1) % orders->waypointCount);
        rt.state = std::uint8_t(NS::Idle);
        rt.stateTimer = std::int16_t(2 + rand_int(ctx, 4));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle) && rt.stateTimer > 0) {
        --rt.stateTimer;
        return;
    }
    rt.state = std::uint8_t(NS::Traveling);
    try_move(p, rt, pools, rt.targetX, rt.targetY, ctx);
}

} // namespace

// The behaviour a squad ACTUALLY lives by: its type row's ai column, unless
// it CARRIES a waypoint route — the route's presence is the order (owner's
// ruling: one knob, not two). New kinds of squad AI are rows, never fields.
// ЛЕСТНИЦА ПРИОРИТЕТОВ — ЗАКОН этой двери (владелец, 2026-09-10).
// Поведение не хранится — оно ВЫВОДИТСЯ каждый think из данных на
// сущности, ступени сверху вниз:
//   1. ПРИКАЗ: маршрут в SquadOrders (его наличие И ЕСТЬ приказ);
//   2. (будущие перекрытия контекста — событие/факт пишет данные,
//      ступень читает их здесь, никогда веткой в диспетче);
//   3. АНКЕТА: строка стола дизайн-персонажей по ординалу тега;
//   4. строка типа (kNpcTypeDefs.ai) — род как он есть.
// Новая модель ИИ = функция + строка enum; новая ступень = данные на
// сущности + одна строка здесь.
// ПОВЕДЕНИЕ НЕТИПИЗИРОВАННОГО СКВАДА — и это ЕДИНСТВЕННАЯ ТОЧКА, ГДЕ
// МАКРОМИР ЕЩЁ ЧИТАЕТ КАТАЛОГ ТЕЛ (CANON S2 «Протокол двух миров»; до
// 2026-09-21 таких точек было четыре, и ни одна не была названа).
// Зовётся ТОЛЬКО из ветки SquadType::ByKind — то есть для сквадов, у которых
// нет макро-источника поведения вовсе: генезисных одиночек и квестовой цели.
// Лестница честная и по убыванию authority: маршрут в приказе (он и есть
// приказ — вердикт владельца), строка стола анкет (МАКРО-таблица, законно),
// и только потом мобная строка лидера — та самая течь.
// С их сносом (порция Б-6, «всё через универсальную систему анкет») эта
// функция умирает целиком, а не переезжает.
AIBehaviour untyped_squad_behaviour(entt::registry& reg, entt::entity e,
                                     const ecs::NPCKind& kind) {
    if (const auto* orders = reg.try_get<ecs::SquadOrders>(e)) {
        if (orders->waypointCount > 0) return AIBehaviour::Waypoints;
    }
    if (const auto* dc = reg.try_get<ecs::DesignCharacterTag>(e)) {
        if (const DesignCharacterDef* row = design_character(dc->ordinal)) {
            return row->behaviour;
        }
    }
    return kNpcTypeDefs[kind.type].ai;
}

namespace {

// Exhaustion, settled once per think AFTER the behaviour marched — the one
// door for every behaviour, where the dispatcher holds the entity and its
// Health (try_move deliberately sees neither).
//
// ONE LAW, both scales (owner's ruling, 2026-08-27): «истощение — это когда
// SP кончилось, и тогда отнимается HP от ДВИЖЕНИЯ по миру; остановился —
// отдыхаешь». So the bite is owed by a body that MOVED this think with its
// bar already spent, wherever it stands — the same `exhaustion_bite` the
// player's every step pays (movement_cost.h). It used to be gated on WATER:
// a squad that marched itself into the ground on dry meadow simply made camp
// and paid nothing, while the player bled for the identical step. That gate
// is gone; drowning is no longer a special case, it is the general case
// happening on the most expensive ground there is.
//
// The bite lands on the LORD's HP because the lord IS the squad — the roster
// is a row inside him, macro damage lands on the avatar. A march the bar
// cannot pay therefore kills, through the same tracked-death door an
// auto-battle uses; the dead lord's men settle by the standing rule.
//
// MAKING CAMP is a DECISION, not a mechanic (owner: «до скольки отдыхать —
// решение конечного автомата, а не механики»), so it stays here as what the
// AI chooses when its legs are gone, and the player keeps his own aim. What
// is one law is the PRICE; what is two is who decides to stop paying it.
bool can_stand_at(const TickContext& ctx, int x, int y) {
    // ОДИН закон стояния на марш и запекание округи (nav_field.h).
    return nav_can_stand(ctx.mw, x, y);
}


void settle_exhaustion(entt::entity e, const MacroPos& p,
                       ecs::MacroNpcRuntime& rt, ecs::Pools& hp,
                       bool canCamp, const TickContext& ctx) {
    if (int(hp.sp) >= 0) return;

    // The AI's DECISION: legs gone, make camp — wherever a camp is possible.
    // Standing still costs nothing; that is the same sentence as «остановился
    // — отдыхаешь». Open water offers no camp, so a squad caught mid-ocean
    // does not get to stop, and the one mechanic below bills it for every
    // further step until it makes a shore or drowns. That is the SAME outcome
    // the old water-only bite produced, arrived at by the right layer: the
    // price is a law, the choice of where to stop is a decision.
    if (canCamp && rt.state != std::uint8_t(NPCState::Resting)) {
        rt.stateAfterRest = rt.state;   // пауза, не амнезия (components.h)
        rt.state = std::uint8_t(NPCState::Resting);
        rt.stateTimer = 0;
    }

    const int bite = exhaustion_bite(int(hp.sp));
    if (bite <= 0) return;
    hp.hp -= bite;
    if (hp.hp <= 0 && ctx.mw.world && ctx.mw.gs) {
        settle_leader_fraction(*ctx.mw.world, e, 0.0f);
        drain_dead_leader_squads(*ctx.mw.world, ctx.mw.gs->deserterPool);
    }
}

// ОДНА ДВЕРЬ «ЧТО ДЕЛАЕТ ЭТОТ СКВАД» (владелец 2026-09-21: «сквад должен
// знать, кто он — тип задаёт поведение аи агента»). Прежде на этот вопрос
// отвечали ЧЕТЫРЕ словаря, и `dispatch` знал только один из них: поведение
// приходило ему АРГУМЕНТОМ, уже посчитанным из мобной строки, а тип сквада
// разбирался ВЕТКОЙ ВНУТРИ ai_gatherer — пятой машиной внутри первой.
// Теперь порядок обратный и единственный: сперва ТИП (макро-колонка), и лишь
// нетипизированный сквад падает на каталог тел.
void dispatch(entt::entity e, MacroPos& p,
              const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    // Каждый думающий сквад следит — писатель полей следов один (CANON S10).
    scent_squad_deposit(e, p, kind, ctx);
    if (squad_threat_step(e, p, kind, rt, pools, ctx)) return;
    // Визуального врага нет — может, есть запах: охота съедает think, роль
    // ждёт получаса потише (макроцель в rt не тронута — пауза, не амнезия).
    if (scent_hunt_step(e, p, kind, rt, pools, ctx)) return;
    // ТИП СКВАДА — ПЕРВЫЙ И ГЛАВНЫЙ ОТВЕТ.
    switch (SquadType(rt.squadType)) {
        case SquadType::Artel:   ai_gatherer(e, p, kind, rt, pools, ctx); return;
        case SquadType::Caravan: ai_vendor  (e, p, rt, pools, ctx);       return;
        case SquadType::Collector: ai_collector(e, p, rt, pools, ctx);    return;
        case SquadType::ByKind:  break;   // ниже — течь, названная по имени
    }
    switch (untyped_squad_behaviour(ctx.mw.world->reg, e, kind)) {
        // ПОСЛЕДНИЕ ДВЕ МАКРО-РОЛИ, ЕЩЁ ЖИВУЩИЕ В КАТАЛОГЕ ТЕЛ (CANON S2):
        // `Gatherer` на строке Peasant и `Trader` на строке Merchant. Они
        // здесь не потому, что нетипизированный сквад должен добывать, а
        // потому что их несут СТРОКИ, и снести значение раньше строки нельзя.
        // Нетипизированный крестьянин проваливается в ai_gatherer до
        // gatherer_def_of, тот отдаёт nullptr (тип не Artel) — и артель живёт
        // домоседом, ровно как до переезда. Обе уйдут вместе со своими
        // строками (порция Б-6), и тогда этот switch перестанет знать про
        // макро-роли вовсе.
        case AIBehaviour::Gatherer:     ai_gatherer(e, p, kind, rt, pools, ctx); break;
        // (`AIBehaviour::TaxRun` БОЛЬШЕ НЕ ДИСПЕТЧЕРИЗУЕТСЯ 2026-09-22:
        //  сборщик стал ТИПОМ СКВАДА, а не ролью тела. Значение умрёт со
        //  строкой TaxCollector в каталоге тел, порция Б-6.)
        // `TaxRun` ОСТАЛСЯ БЕЗ МАКРО-СМЫСЛА 2026-09-22 (сборщик стал ТИПОМ
        // сквада). Значение ещё несёт строка TaxCollector каталога тел,
        // поэтому ветка обязана существовать — но ведёт она туда же, куда
        // ведёт всякая роль без занятия. Умрёт вместе со строкой (Б-6).
        case AIBehaviour::TaxRun:       ai_wanderer     (p, rt, pools, ctx); break;
        case AIBehaviour::Trader:       ai_trader       (p, rt, pools, ctx); break;
        case AIBehaviour::Aggressive:   ai_aggressive   (p, rt, pools, ctx); break;
        case AIBehaviour::Teleporter:   ai_teleporter   (p, rt, pools, ctx); break;
        case AIBehaviour::Wanderer:     ai_wanderer     (p, rt, pools, ctx); break;
        // Prey. Running is not its own errand: the threat step above already
        // makes ANY row run from what it cannot beat (squad_power), so what
        // this column says about an untroubled day is "it roams" — the same
        // walk a wanderer takes. The difference between a fox and a rabbit is
        // what happens when something appears, and that is decided above.
        case AIBehaviour::Flee:         ai_wanderer     (p, rt, pools, ctx); break;
        case AIBehaviour::Waypoints:    ai_waypoints (e, p, rt, pools, ctx); break;
        case AIBehaviour::MageHunt:     ai_mage_hunt (e, p, rt, pools, ctx); break;
        case AIBehaviour::LairSorties:  ai_lair_sorties(e, p, rt, pools, ctx); break;
        case AIBehaviour::Count:        break;
    }
}

} // namespace

// ── Trading at a market, two strategies over ONE price law ───────────────
// (owner 2026-08-30: «караван приходит в город и НА МЕСТЕ смотрит, что
// выгодно продать и купить» — locality, no omniscience, no rumours.)
// Both functions price through economy.h stock_price at POST-TRADE supply
// (every lot pays its own slippage), and coin travels by transfer_value:
// conservation is by construction, not by audit.

// НАИБОЛЬШИЙ ЛОТ ПО КОШЕЛЬКУ — точная граница двоичным поиском по
// монотонной стоимости лота `n × цена(послесделочная полка)`. Прежняя пара
// «потолок → одно уточнение» была построена под коридор [0.25…4.0]: без
// него цена ПЕРВОЙ единицы на голодной полке (база × сезонный спрос) давала
// afford = 0, и рынок в голоде не мог купить НИЧЕГО — хотя честная точка
// n > 0 существует, потому что лот целиком оплачивается по полке ПОСЛЕ
// сделки и с каждой единицей дешевеет. selling: полка растёт (have + n);
// покупка у рынка: полка тает (have − n) — стоимость растёт в обе стороны
// монотонно, и максимум по кошельку ищется за log шагов.
int max_affordable_lot_(int base, int have, int demand, bool selling,
                        int wallet, int cap) {
    if (cap <= 0 || wallet <= 0) return 0;
    int lo = 0, hi = cap;
    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        const int shelf = selling ? have + mid : have - mid;
        const long long cost =
            (long long)mid * stock_price(base, shelf, demand);
        if (cost <= wallet) lo = mid; else hi = mid - 1;
    }
    return lo;
}

// The STATION deal — a caravan standing on a CITY market. Symmetric and
// purely local: a market short of a good (its SEASONAL need above supply —
// the price law's own horizon, S19.2) BUYS from the hold up to that need; a
// market glutted (supply above the seasonal need) SELLS the surplus into
// the hold. The bounds fall straight out of the price law — price > base ⇔
// seasonal need > supply — so «продай что здесь дорого, купи что дёшево»
// needs no threshold constants at all, and the trade drives every market it
// touches TOWARD its own need. Arbitrage is emergent: the surplus bought
// cheap here is exactly what the next hungry station pays above base for.
CaravanDeal trade_caravan_at_station(Inventory& hold, float capacityKg,
                                     Landmark& market,
                                     int myTradePct, int theirTradePct,
                                     EconFactSink sink, void* user) {
    CaravanDeal out{};
    Inventory& ms = market.inventory;
    // Рынок — МЕСТО (CANON S10): проданное ему падает в Depot и гасит его
    // долг СРАЗУ — город, купивший хлеб, хлеб уже проел.
    const Depot msd(market.inventory, market.needDebt, sink, user);
    const Skills& site = landmark_sheet(market.type).skills;
    for (int i = 0; i < kCommodityCount; ++i) {
        const char* id = kCommodities[i].id;
        const ItemDef* def = item_def(id);
        const int base = def ? def->value : 0;
        if (base <= 0) continue;
        const int demand = season_demand_for(id, market.needDebt,
                                             souls_home(market), site, &ms);
        // Спрос уже СЕЗОННЫЙ (остаток счёта + производный) — прежний
        // множитель горизонта умер вместе с календарём кривой.
        const int need = demand;
        const int have = ms.count(id);
        if (need > have) {
            // SELL into the shortage, up to the market's own seasonal need,
            // bounded by what its whole store can PAY (max_affordable_lot_).
            int n = std::min(hold.count(id), need - have);
            if (n <= 0) continue;
            n = max_affordable_lot_(base, have, demand, /*selling=*/true,
                                    inventory_value(ms), n);
            if (n <= 0) continue;
            const int moved = haul_between(hold, msd, id, n, 1e9f);
            if (moved <= 0) continue;
            // The ONE trade-price law (economy.h): the seller's charisma
            // and trade skill claw back part of the house margin — a
            // caravan out-trades a peasant because its ROW rolls a better
            // sheet, never because the code knows who it is (owner,
            // 2026-08-30).
            const int price = trade_sell_price(
                stock_price(base, have + moved, demand),
                myTradePct, theirTradePct);
            out.soldValue += transfer_value_dense(ms, hold, moved * price);
            out.movedTableValue += base * moved;
        } else if (have > need) {
            // BUY the surplus above the market's own seasonal need — never
            // its living stock; the lot is bounded by the caravan's purse
            // through the same exact door (max_affordable_lot_).
            const float kg = def->weight > 0.0f ? def->weight : 1.0f;
            int n = std::min(have - need,
                             int((capacityKg - inventory_weight(hold)) / kg));
            n = max_affordable_lot_(base, have, demand, /*selling=*/false,
                                    inventory_value(hold), n);
            if (n <= 0) continue;
            const int moved =
                haul_between(ms, hold, id, n,
                             capacityKg - inventory_weight(hold));
            if (moved <= 0) continue;
            const int cost =
                moved * trade_buy_price(
                            stock_price(base, have - moved, demand),
                            myTradePct, theirTradePct);
            out.boughtValue += transfer_value_dense(hold, msd, cost);
            out.movedTableValue += base * moved;
        }
    }
    return out;
}

// The VENDOR deal — a village crew at its NEAREST city's market (владелец:
// «крестьяне просто всегда идут продавать на рынок ближайшего города»).
// Not an arbitrageur: sells EVERYTHING it carried (the village's surplus,
// at whatever the local law prices it), then spends the earnings down the
// home's needs ladder — what the home's own LEDGER says the village lacks.
// Ведомость принадлежит МЕСТУ, а не слуху: крю читает счёт своего дома.
CaravanDeal trade_vendor_at_market(Inventory& bag, float capacityKg,
                                   Landmark& market,
                                   const LandmarkLedger* homeLedger,
                                   int myTradePct, int theirTradePct,
                                   EconFactSink sink, void* user) {
    CaravanDeal out{};
    Inventory& ms = market.inventory;
    // Рынок — МЕСТО (CANON S10): проданное гасит его долг сразу.
    const Depot msd(market.inventory, market.needDebt, sink, user);
    const Skills& site = landmark_sheet(market.type).skills;
    const auto base_value = [](const char* id) {
        const ItemDef* d = item_def(id);
        return d ? d->value : 0;
    };
    // SELL the whole load first — the coin below buys the home's lacks.
    for (int i = 0; i < kCommodityCount; ++i) {
        const char* id = kCommodities[i].id;
        const int base = base_value(id);
        if (base <= 0) continue;
        int n = bag.count(id);
        if (n <= 0) continue;
        const int demand = season_demand_for(id, market.needDebt,
                                             souls_home(market), site, &ms);
        const int have = ms.count(id);
        // Affordability by the exact door (max_affordable_lot_), as at the
        // station: the lot pays the post-trade shelf, so a famine ceiling
        // price never blanks a sale the market can genuinely afford.
        n = max_affordable_lot_(base, have, demand, /*selling=*/true,
                                inventory_value(ms), n);
        if (n <= 0) continue;
        const int moved = haul_between(bag, msd, id, n, 1e9f);
        if (moved <= 0) continue;
        // Same ONE trade-price law as the station: a village hand haggles
        // with a peasant's charisma, a caravan with a trader's — the sheet
        // is the whole difference.
        const int price = trade_sell_price(
            stock_price(base, have + moved, demand), myTradePct, theirTradePct);
        out.soldValue += transfer_value_dense(ms, bag, moved * price);
        out.movedTableValue += base * moved;
    }
    // BUY with the WHOLE purse (owner 2026-08-30: «деревня не копит
    // капитал» — the earnings leave with the goods, and scarce town wares
    // are exactly what drains the village's coin back into the city). The
    // shopping list is the home's needs ladder unrolled to recipe inputs
    // (caravan_buy_order), each line topped up to a SEASON's stock at home
    // — the one stint of foresight a place is allowed; whatever the market
    // cannot supply leaves the rest of the purse to ride home, where the
    // tax graph will claim it (CANON S24).
    // ── ЧТО КУПЕЦ ПОКУПАЕТ: ОДИН ЗАКОН, А НЕ СПИСОК ────────────────────
    // Владелец, 2026-09-18: «почему корованы не могут покупать вообще всё
    // что угодно — покупай дёшево, продавай дорого, просто смотреть, что им
    // выгоднее всего купить по цене относительно стоимости?». Белый список
    // «нужды дома → входы рецептов» (caravan_buy_order) был спецпутём той же
    // породы, что монетные ворота: он решал за купца, ЧТО бывает товаром, и
    // поэтому руда никогда им не была.
    //
    // Закон один и обе его половины уже живут в коде: товар стоит СТОЛЬКО,
    // сколько за него дадут ДОМА (домашняя цена по классу памяти — та же
    // scarcity-кривая), и стоит СТОЛЬКО, сколько просят ЗДЕСЬ. Выгода рейса
    // = разница, а ранжирование — на КИЛОГРАММ, потому что дорога везёт вес,
    // а не строки. Отсюда даром: домашние нужды выигрывают САМИ (голодный
    // дом = дефицит = высокая домашняя цена, до ×4), затоваренное отсеивается
    // САМО (×0.25), а серебро с горы становится товаром без единой строки
    // «металл — это нужда».
    if (homeLedger && homeLedger->published()) {
        struct Lot { int i; float gainPerKg; int homeCap; };
        Lot lots[std::size_t(kCommodityCount)];
        int lotCount = 0;
        for (int i = 0; i < kCommodityCount; ++i) {
            const char* id = kCommodities[i].id;
            const ItemDef* def = item_def(id);
            const int base = def ? def->value : 0;
            if (base <= 0) continue;
            const int have = ms.count(id);
            if (have <= 0) continue;
            const int demand = season_demand_for(id, market.needDebt,
                                                 souls_home(market), site,
                                                 &ms);
            const int buyHere = trade_buy_price(
                stock_price(base, have, demand), myTradePct, theirTradePct);
            // Чего это стоит ДОМА — ВЕДОМОСТЬ ДОМА (CANON S10, ярус 2):
            // цена, которую дом выписал сам, своим точным складом и своим
            // счётом, с неттингом производного спроса. Ни огрубления, ни
            // второго диалекта цены: ведомость посчитана той же кривой
            // (stock_price), что и цена сделки здесь.
            const int worthHome = homeLedger->price[std::size_t(i)];
            if (worthHome <= 0) continue;         // дома этой строке нет цены
            if (worthHome <= buyHere) continue;   // рейс не окупает закупку
            const float kg = def->weight > 0.0f ? def->weight : 1.0f;
            // ПОТОЛОК СТРОКИ — сезон домашней нужды (спрос уже сезонный),
            // но он больше НЕ ворота: у товара, который дома никто не ест,
            // потолок — только трюм и кошелёк.
            const int homeDemand = homeLedger->demand[std::size_t(i)];
            const int seasonCap = homeDemand > 0 ? homeDemand : (1 << 20);
            lots[std::size_t(lotCount++)] =
                Lot{i, float(worthHome - buyHere) / kg, seasonCap};
        }
        // Выгоднейшее — первым: трюм и кошелёк конечны, и купец грузит их
        // тем, что несёт больше всего ценности на килограмм.
        std::sort(lots, lots + lotCount, [](const Lot& a, const Lot& b) {
            return a.gainPerKg > b.gainPerKg;
        });
        for (int li = 0; li < lotCount; ++li) {
            const int i = lots[li].i;
            const char* id = kCommodities[i].id;
            const ItemDef* def = item_def(id);
            const int base = def->value;
            const int demand = season_demand_for(id, market.needDebt,
                                                 souls_home(market), site,
                                                 &ms);
            const int have = ms.count(id);
            const float kg = def->weight > 0.0f ? def->weight : 1.0f;
            int n = std::min({have, lots[li].homeCap,
                              int((capacityKg - inventory_weight(bag)) / kg)});
            // Точная граница по кошельку (max_affordable_lot_): срез лота
            // по цене ДО среза недокупал — оставшаяся полка дешевле, и
            // честная точка выше.
            n = max_affordable_lot_(base, have, demand, /*selling=*/false,
                                    inventory_value(bag), n);
            if (n <= 0) continue;
            const int moved =
                haul_between(ms, bag, id, n,
                             capacityKg - inventory_weight(bag));
            if (moved <= 0) continue;
            const int cost =
                moved * trade_buy_price(
                            stock_price(base, have - moved, demand),
                            myTradePct, theirTradePct);
            out.boughtValue += transfer_value_dense(bag, msd, cost);
            out.movedTableValue += base * moved;
        }
    }
    return out;
}

// ── Squads eat (contract in npc_ai.h) ────────────────────────────────────
// ТАКТ 2 — СНАРЯЖЕНИЕ ТЯГЛОМ (двухтактный обоз). Место выдаёт уходящей
// артели ездовых из своего стойла по ЗАКОНУ УПРЯЖКИ (npc.h
// mount_allowance: по одному на душу) — и столько, сколько в стойле стоит.
// Стоит рядом с провиантом (provision_squad) намеренно: это тот же акт —
// дом снаряжает свою артель на рейс из своих запасов, просто второй
// контейнер (S25 «одна форма, разные контейнеры»).
//
// Тяжёлым работам тягло достаётся САМО, без прогноза груза: обоз есть сумма
// спин (squad.h refresh_squad_carry), поэтому выданный конь — это +8 спин
// тому, кто сегодня идёт за рудой, и ни одного нового числа.
int outfit_crew_mounts(ecs::World& w, Landmark& home, entt::entity crew) {
    auto* bag = w.reg.try_get<ecs::NpcInventory>(crew);
    if (!bag) return 0;
    int want = mount_allowance(bag->inv) - count_mount_souls(bag->inv);
    int given = 0;
    while (want > 0) {
        // Новейший ездовой слот стойла — наименьший индекс области (старый
        // обход slot_count-1 → 0 = здесь first → 1023).
        int si = -1;
        for (int i = home.inventory.creature_first();
             i < kMaxInventorySlots; ++i) {
            if (is_mount_kind(std::uint16_t(creature_of_world_row(
                    home.inventory.slots[std::size_t(i)].def)))) {
                si = i;
                break;
            }
        }
        if (si < 0) break;   // стойло пусто — артель идёт пешей
        SoldierRecord mount{};
        if (!creatures_take_at(home.inventory, si, mount)) break;
        if (!creatures_push(bag->inv, mount)) {
            creatures_push(home.inventory, mount);   // нет слота — конь дома
            break;
        }
        ++given;
        --want;
    }
    if (given > 0) refresh_squad_carry(w, crew);
    return given;
}

int provision_squad(Inventory& store, Inventory& bag, int soldiers,
                    float roundtripCells, float freeCarryKg) {
    if (soldiers <= 0) return 0;   // the leader is a subject — he needs nothing
    const int days =
        1 + int(std::ceil(roundtripCells / kSustainedMarchCellsPerDay));
    const int portion = soldiers * days;
    // The haul door already speaks credit-before-debit and respects the
    // carry the loaf must ride on.
    return haul_between(store, bag, hunger_item_id(), portion, freeCarryKg);
}

// ЗДЕСЬ СТОЯЛ `squad_season_needs` — ВТОРОЙ ЗАКОН СОДЕРЖАНИЯ, И ОН УМЕР
// 2026-09-22 вместе со своим близнецом у мест. Он расходился с ним в двух
// местах, и оба расхождения были дефектом, а не контекстом:
//   · РОТ. Здесь ртом считалась только строка с `upkeepGoldPerDay >= 0`, то
//     есть зверьё с `kNpcUpkeepNone` в поле НЕ ЕЛО ВОВСЕ, а у места ело.
//     Комментарий этого места сам признавал долг: «сам сигнал −1 ждёт своей
//     порции (CANON S4 «едят все живые»)». Порция пришла: рот — это колонка
//     рациона строки (npc.h `npc_board_per_day`), а плата — своя колонка, и
//     «ест, но не получает» говорится ДАННЫМИ, а не пропуском итерации.
//   · ФУРАЖИР. Скидка ведущего по SkillId::Foraging была вторым ответом на
//     «сколько ест ростер». Вердикт владельца 2026-09-22: «оба снести — один
//     закон без исключений». СЛЕДСТВИЕ, НАЗВАННОЕ ВСЛУХ (§55): у навыка
//     Foraging не осталось НИ ОДНОГО механического читателя в мире — строка
//     в таблице навыков и бонус к ней живы, читателя нет. Его законное место
//     — добыча пищи В ПУТИ, а не скидка на счёт; до тех пор это известная
//     спящая колонка, а не забытая.
// Счёт теперь один: `roster_bill` (macro/roster_window.h).

int squad_season_window(MacroWorld& mw, int day) {
    if (!mw.gs || !mw.world) return 0;
    if (!season_boundary(day)) return 0;
    GameState& gs = *mw.gs;
    auto& reg = mw.world->reg;
    int deserted = 0;
    // Окно делит ОДИН пул дезертиров и один пул лута на всех — порядок суда
    // есть закон мира (squad_walk.h): по ординалу. Скрэтч локальный, как у
    // прочих дневных проходов.
    auto view = reg.view<ecs::NPCKind, ecs::MacroNpcRuntime,
                         ecs::NpcInventory, ecs::SquadRoster>();
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(reg, view, order);
    for (const SquadWalkEntry& sw : order) {
        const entt::entity e = sw.e;
        auto& rt     = reg.get<ecs::MacroNpcRuntime>(e);
        auto& bag    = reg.get<ecs::NpcInventory>(e);
        auto& roster = reg.get<ecs::SquadRoster>(e);
        // СУД И СЧЁТ — ОДНА ДВЕРЬ НА ВЕСЬ МИР (macro/roster_window.h).
        // Артель судится тем же телом и тем же счётом, что ростер места и
        // армия игрока: своего у неё здесь не осталось ничего.
        const RosterWindowOutcome out = roster_season_window(
            roster, bag.inv, gs.deserterPool, gs.lootPoolValue,
            mw.econFacts, mw.econFactsUser);
        deserted += out.walked;
        // ВЕДОМОСТЬ СКЛАДА ДУШ (econ_day.h): ОДИН факт = ОДНА артель,
        // провалившая окно, поэтому слушатель считает и артели (числом
        // фактов), и души (суммой). Адрес — ДОМ артели: по канону S9 это
        // его ресурс ушёл, а не «мировой».
        if (out.walked > 0 && mw.econFacts) {
            EconFact f{};
            f.kind = out.byWage ? EconFact::Kind::SoulsDesertedUnpaid
                                : EconFact::Kind::SoulsDesertedUnfed;
            f.amount = out.walked;
            f.landmarkId = rt.homeSettlementId;
            mw.econFacts(mw.econFactsUser, f);
        }
        // Состав изменился — обоз заново (squad.h): ушедшая душа унесла и
        // свою спину.
        refresh_squad_carry(*mw.world, e);
    }
    return deserted;
}

// ── ОПИСЬ ОКРУГИ (контракт в npc_ai.h) ───────────────────────────────────
// Счётчики прибора: почему живая клетка рода не попала в опись.
static long gSurveyDry = 0, gSurveyNoRegion = 0, gSurveyInRegion = 0,
            gSurveyNoOwner = 0;

int survey_landmark_regions(MacroWorld& mw, int day) {
    gSurveyDry = gSurveyNoRegion = gSurveyInRegion = gSurveyNoOwner = 0;
    if (!mw.gs || !mw.deposits) return 0;
    GameState& gs = *mw.gs;
    NavWorld* nv = mw.nav;
    const bool navReady = nv && nav_ensure(mw, *nv);
    // Чистый лист: карта — ОТВЕТ на состояние мира, а не его память. Жила,
    // выработанная за сезон, обязана из описи уйти.
    for (Landmark& lm : gs.landmarks) {
        lm.survey = LandmarkSurvey{};
        lm.survey.day = day;
    }
    if (!navReady) return 0;   // без запечённой навигации «своей земли» нет
    // Кто владеет какой округой: у одной округи законно бывает несколько
    // мест — они делят землю и честно за неё конкурируют (вердикт владельца).
    std::vector<std::pair<std::uint16_t, int>> owners;   // (округа, индекс)
    owners.reserve(gs.landmarks.size());
    for (std::size_t i = 0; i < gs.landmarks.size(); ++i) {
        const Landmark& lm = gs.landmarks[i];
        const std::uint16_t r = nav_region_at(*nv, lm.x, lm.y);
        if (r != kNavNoRegion) owners.push_back({r, int(i)});
    }
    if (owners.empty()) return 0;
    std::sort(owners.begin(), owners.end());
    // Каждая живая клетка рода относит себя к своей округе — и находит там
    // хозяев. Обход идёт ПО ПОЛЮ рода (for_each_live), потому что поле
    // индексировано тором и знает свои живые клетки: миллион пустых клеток
    // мира никто не читает.
    for (int f = 0; f < int(ResourceFieldId::Count); ++f) {
        const ResourceFieldId row = ResourceFieldId(f);
        if (!resource_row_is_vein(row)) continue;   // у леса своя дверь
        const ResourceGrid& cells =
            mw.deposits->grid(DepositKind(deposit_kind_ordinal(row)));
        if (!cells.live()) continue;
        cells.for_each_live([&](std::uint32_t idx, std::int32_t amount) {
            if (amount <= 0) { ++gSurveyDry; return; }
            const int x = cells.x_of(idx), y = cells.y_of(idx);
            const std::uint16_t r = nav_region_at(*nv, x, y);
            if (r == kNavNoRegion) { ++gSurveyNoRegion; return; }
            ++gSurveyInRegion;
            const std::uint32_t d = nv->distHome[nv->cell(x, y)];
            const std::uint16_t dist =
                d > 0xFFFEu ? 0xFFFEu : std::uint16_t(d);
            bool anyOwner = false;
            for (auto it = std::lower_bound(owners.begin(), owners.end(),
                                            std::make_pair(r, 0));
                 it != owners.end() && it->first == r; ++it) {
                anyOwner = true;
                SurveyRow& sr = gs.landmarks[std::size_t(it->second)]
                                    .survey.rows[std::size_t(f)];
                if (!sr.none() && sr.dist <= dist) continue;
                sr.x = std::int16_t(x);
                sr.y = std::int16_t(y);
                sr.dist = dist;
            }
            if (!anyOwner) ++gSurveyNoOwner;
        });
    }
    // ОТПЕЧАТОК ОПИСИ — прибор калибровки этого закона, как [deposits] у
    // геологии: сколько мест ВИДИТ каждый род и на каком расстоянии лежит
    // ближайшая жила. Без него «мир не добывает железо» остаётся догадкой.
    {
        int seen[std::size_t(ResourceFieldId::Count)] = {};
        std::uint32_t nearest[std::size_t(ResourceFieldId::Count)];
        for (auto& n : nearest) n = 0xFFFFu;
        for (const Landmark& lm : gs.landmarks) {
            for (int f = 0; f < int(ResourceFieldId::Count); ++f) {
                const SurveyRow& sr = lm.survey.rows[std::size_t(f)];
                if (sr.none()) continue;
                ++seen[std::size_t(f)];
                if (sr.dist < nearest[std::size_t(f)])
                    nearest[std::size_t(f)] = sr.dist;
            }
        }
        std::fprintf(stderr, "[survey] day=%d places=%zu", day,
                     gs.landmarks.size());
        for (int f = 0; f < int(ResourceFieldId::Count); ++f) {
            if (!resource_row_is_vein(ResourceFieldId(f))) continue;
            std::fprintf(stderr, " %s=%d/min%u",
                         resource_field_def(ResourceFieldId(f)).id,
                         seen[std::size_t(f)], nearest[std::size_t(f)]);
        }
        std::fprintf(stderr, "  | cells inRegion=%ld noRegion=%ld noOwner=%ld"
                             " owners=%zu\n",
                     gSurveyInRegion, gSurveyNoRegion, gSurveyNoOwner,
                     owners.size());
        std::fflush(stderr);
    }
    return int(gs.landmarks.size());
}

int squad_bags_hygiene_daily(MacroWorld& mw) {
    if (!mw.gs || !mw.world) return 0;
    auto& reg = mw.world->reg;
    int melted = 0;
    // Порядок по ординалу (squad_walk.h): авто-скрап и гашение счёта трогают
    // цену дня через факты — одна очередь фактов на всех.
    auto view = reg.view<ecs::NPCKind, ecs::MacroNpcRuntime,
                         ecs::NpcInventory>();
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(reg, view, order);
    for (const SquadWalkEntry& sw : order) {
        const entt::entity e = sw.e;
        auto& bag = reg.get<ecs::NpcInventory>(e);
        // Camp-life slot hygiene (CANON «Крафт/Скрап»: авто-скрап ИИ по
        // порогу >50% — «склад города ИЛИ МЕШОК СКВАДА»): the same daily
        // overflow law the settlement store runs. The gate is not a player
        // privilege but the seam of DECISION: this loop is the AI deciding
        // for its bag, and the PlayerTag bag's decisions come from input —
        // «автоматическое уничтожение вещей игрока строго запрещено».
        if (!reg.any_of<ecs::PlayerTag>(e)) melted += auto_scrap_overflow(bag.inv);
        // ── ПРИХОД ГАСИТ СЧЁТ ВЕСЬ СЕЗОН (CANON S10, v105) ───────────────
        // Страховочный дневной такт гашения — ровно тот же, что у места
        // (world_tick settle_landmark_day). Двери прихода у сквада разные
        // (добыл, купил, отнял), и городить у каждой свою уплату значило бы
        // писать закон пятый раз; вместо этого ростер ест то, что приехало,
        // тем же вечером. «Добыча привезла — часть съелась» (владелец,
        // 2026-09-21), и это ТОТ ЖЕ econ_pay_debt, которым платит ландмарк.
        if (auto* ro = reg.try_get<ecs::SquadRoster>(e)) {
            econ_pay_debt(bag.inv, ro->needDebt, mw.econFacts,
                          mw.econFactsUser);
        }
    }
    return melted;
}

// ── The daily labour rotation (contract in npc_ai.h) ─────────────────────
int rotate_worker_squads(MacroWorld& mw, int day) {
    if (!mw.gs || !mw.world || !mw.terrain) return 0;
    GameState& gs = *mw.gs;
    auto& reg = mw.world->reg;
    TickContext ctx{};
    ctx.mw = mw;
    ctx.mapW = gs.mapW;
    ctx.mapH = gs.mapH;
    if (mw.nav) nav_ensure(mw, *mw.nav);   // гейты читают округи

    const auto row_of = [&](int id) -> int {
        // O(1) дверью «ординал и есть адрес» (landmark_by_id) вместо чистого
        // линейного скана, звавшегося на КАЖДУЮ сущность дважды в день
        // (O(сущности × N) — худшая точка переписи M-90). Результат тот же:
        // ординал уникален, а дверь несёт тот же скан-фоллбек.
        const Landmark* lm = landmark_by_id(gs, id);
        return lm ? int(lm - gs.landmarks.data()) : -1;
    };
    // A type is a CREW exactly when some landmark's registry row raises it —
    // the old hand-kept list (professions + Vendor + TaxCollector) is now a
    // question to the same table that raises them (owner 2026-08-31, CANON
    // S10: «какие сквады кто спавнит — таблично по видам ландмарков»).
    const auto is_crew = [](std::uint16_t type) {
        for (const LandmarkDef& ld : kLandmarks)
            for (int i = 0; i < int(ld.crewCount); ++i)
                if (std::uint16_t(ld.crews[i].npc) == type) return true;
        return false;
    };

    // 1) COLLECT crews standing home Idle. С 2026-09-17 (CANON S19.2) они
    //    БЕССРОЧНЫ: домой пришла — НЕ исчезла. Растворяет их только суд
    //    перекомплекта ниже — стоящая артель, которой не досталось строки.
    //    Collect first, mutate after — the registry is never touched under
    //    its own view.
    //    (ВЕТКА ВЫЛАЗОК ГАРНИЗОНА — вектор `done` и предикат
    //    garrison_row_of_type — снесена 2026-09-21: garrison-строк в
    //    реестре не осталось ни одной, и она была недостижима. Её ЗАКОН
    //    растворения — «зверь не душа населения» — не потерян: он переехал
    //    в dissolve_population_crew ниже, где и оказался единственной
    //    живой дверью распуска. До переезда живая дверь считала табун
    //    людьми, а правильный закон стоял в недостижимой ветке.)
    std::vector<entt::entity> homeIdle;
    // exclude<Dead>: a dead crew at its home cell is NOT a crew coming home —
    // it is a corpse-row awaiting the drain (AI-2). Without the exclusion a
    // dead leader and his dead men dissolved into the landmark as living
    // souls.
    // Порядок по ординалу (squad_walk.h): idleByRow ниже раздаётся законом
    // «первая подходящая» (claim_standing) и растворяется в том же порядке —
    // «кто первым встал» обязан быть законом мира, не кишкой EnTT.
    auto idleView = reg.view<ecs::NPCKind, ecs::MacroNpcRuntime,
                             ecs::MacroCell>(entt::exclude<ecs::Dead>);
    std::vector<SquadWalkEntry> idleOrder;
    collect_squads_by_ordinal(reg, idleView, idleOrder);
    for (const SquadWalkEntry& sw : idleOrder) {
        const entt::entity e = sw.e;
        const auto& kind = reg.get<ecs::NPCKind>(e);
        const auto& rt   = reg.get<ecs::MacroNpcRuntime>(e);
        const auto& cell = reg.get<ecs::MacroCell>(e);
        if (!is_crew(kind.type)) continue;
        if (rt.state != std::uint8_t(NS::Idle)) continue;
        const int row = row_of(rt.homeSettlementId);
        if (row < 0) continue;
        const Landmark& lm = gs.landmarks[std::size_t(row)];
        // «Дома» = радиус прибытия марша (at_target, ±2 клетки) — ОДИН
        // предикат с вендорской погрузкой: точное равенство клетке
        // оставляло финишировавшую у крыльца артель нерастворённой
        // навсегда (души не возвращались, пере-аукцион не наступал).
        if (torus_dist_sq(float(ecs::cell_x(cell, gs.mapW)),
                          float(ecs::cell_y(cell, gs.mapW)),
                          float(lm.x), float(lm.y),
                          float(gs.mapW), float(gs.mapH)) >= 4.0f)
            continue;
        homeIdle.push_back(e);
    }

    // Which crew rows have a squad truly OUT (on the road, at the field):
    // those are closed today. A crew standing home Idle does NOT close its
    // row — it is the row's own crew awaiting the day's re-auction (S19.2,
    // артели бессрочны). СЧЁТ ПО ТИПУ (владелец 2026-09-02, снос профессий):
    // строки списка могут ПОВТОРЯТЬ тип (N×Peasant), так что живой крю типа
    // T занимает ПЕРВУЮ свободную строку типа T своего дома — bit i =
    // строка i занята. И СКОЛЬКО ДУШ КАЖДОГО ДОМА СЕЙЧАС В ПОЛЕ (владелец,
    // 2026-09-02): сквады знают свой ландмарк — считаем на месте, ничего
    // не помним.
    // СКОЛЬКО ЭКЗЕМПЛЯРОВ КАЖДОЙ СТРОКИ УЖЕ В ПОЛЕ (CANON S4 «строка крю
    // становится ШАБЛОНОМ, число экземпляров говорят аукцион и пул рук»).
    // ЗДЕСЬ БЫЛ `outMask` — БИТ на строку, то есть потолок «один сквад на
    // строку» и заодно потолок 8 на всё место. Он и был тем, что делало
    // строку СЛОТОМ: город с одной торговой строкой физически не мог
    // поднять двух корованов, сколько бы излишка ни лежало на складе.
    std::vector<std::array<std::uint8_t, 8>> outCount(gs.landmarks.size());
    std::vector<int> afield(gs.landmarks.size(), 0);
    // Души артелей, СТОЯЩИХ ДОМА, — часть базы пула труда: суд границы,
    // меривший пул одним населением, ужимал составы каждый сезон (души
    // стоящих выпадали из базы — поймано свидетелем resize).
    std::vector<int> standingSouls(gs.landmarks.size(), 0);
    // Табун, УЖЕ стоящий в артелях этого дома (лошади живут в отрядах —
    // «армия крестьян»), — вторая половина склада для дросселя ловли;
    // овцы получат такой же счёт своей строкой.
    std::vector<int> horsesStanding(gs.landmarks.size(), 0);
    std::vector<std::pair<int, entt::entity>> idleByRow;
    std::sort(homeIdle.begin(), homeIdle.end());
    const auto is_home_idle = [&](entt::entity e) {
        return std::binary_search(homeIdle.begin(), homeIdle.end(), e);
    };
    // Тот же закон порядка: этот проход заполняет idleByRow.
    auto crewView = reg.view<ecs::NPCKind, ecs::MacroNpcRuntime>();
    std::vector<SquadWalkEntry> crewOrder;
    collect_squads_by_ordinal(reg, crewView, crewOrder);
    for (const SquadWalkEntry& sw : crewOrder) {
        const entt::entity e = sw.e;
        const auto& kind = reg.get<ecs::NPCKind>(e);
        const auto& rt   = reg.get<ecs::MacroNpcRuntime>(e);
        const int row = row_of(rt.homeSettlementId);
        if (row < 0) continue;
        const LandmarkDef& ld =
            landmark_def(gs.landmarks[std::size_t(row)].type);
        bool standingHome = false;
        if (is_crew(kind.type)) {
            int souls = 1;
            if (const auto* bag = reg.try_get<ecs::NpcInventory>(e)) {
                // Труд-гроссбух считает ЛЮДЕЙ; табун отряда — в дроссель.
                souls += count_human_souls(bag->inv);
                horsesStanding[std::size_t(row)] +=
                    creature_heads_of(bag->inv, NPCType::Horse);
            }
            afield[std::size_t(row)] += souls;
            if (is_home_idle(e)) {
                standingHome = true;
                standingSouls[std::size_t(row)] += souls;
                idleByRow.push_back({row, e});
            }
        }
        if (standingHome) continue;   // its row stays OPEN for re-dispatch
        for (int i = 0; i < int(ld.crewCount); ++i) {
            if (std::uint16_t(ld.crews[i].npc) != kind.type) continue;
            // Экземпляр приписывается строке СВОЕГО ТИПА: у места строки
            // теперь различаются типом сквада, а не позицией в списке.
            const SquadType rowType = ld.crews[i].type;
            if (rowType != SquadType::ByKind
                && std::uint8_t(rowType) != rt.squadType)
                continue;
            if (outCount[std::size_t(row)][std::size_t(i)] < 255)
                ++outCount[std::size_t(row)][std::size_t(i)];
            break;
        }
    }

    // Роспуск населенской артели: души и остатки — домой (существующие
    // двери), сущность умирает. Зовёт суд границы: «лишний сквад» = стоящая
    // дома артель, которой не досталось СТРОКИ (закрылся гейт, строку
    // держит полевая, пул ужался) — «просто распускает» (S19.2). Оба рычага
    // живы (владелец 2026-09-18): строки правят ЧИСЛОМ сквадов, пул — их
    // РАЗМЕРОМ (добор/ссадка в ветке стоящих ниже).
    const auto dissolve_population_crew = [&](entt::entity e, Landmark& lm) {
        if (auto* bag = reg.try_get<ecs::NpcInventory>(e)) {
            // Leftovers home: cargo by the haul door, coin by the wallet
            // door — a dissolved crew owns nothing (CANON S5, the loan law).
            for (int c = 0; c < kCommodityCount; ++c)
                haul_between(bag->inv, depot_(lm, mw), kCommodities[c].id,
                             1 << 30, 1e9f);
            transfer_value_dense(bag->inv, depot_(lm, mw),
                                 inventory_value(bag->inv));
        }
        // ЗВЕРЬ — НЕ ДУША НАСЕЛЕНИЯ (вердикт владельца 2026-09-21: «популяция
        // считает только типа HUMAN из таблицы существ»). Лошади (и всякий
        // не-людской род) растворяющейся артели встают в ГАРНИЗОН места —
        // «гарнизон = армия ландмарка», табун города живёт в его армии, виден,
        // продаётся и грабится (вердикт 2026-09-19).
        //
        // ЗДЕСЬ БЫЛО `souls += roster->squad.size()`, а size() суммирует count
        // ВСЕХ слотов (army.h) — то есть каждая граница сезона превращала
        // табун распущенной артели в горожан: кони выходили из мира людьми.
        // Правильный закон существовал всё это время в ветке растворения
        // вылазок гарнизона — В НЕДОСТИЖИМОЙ, — и снос патрулей сделал его
        // ЕДИНСТВЕННЫМ, вместо того чтобы унести с собой. Двух дверей
        // «артель пришла домой» больше нет.
        //
        // ФОРМА, КОТОРАЯ ПРИДЁТ С ПЕРЕВОРОТОМ НАСЕЛЕНИЯ (владелец, тот же
        // день: «буквально перенос между гарнизоном — весь гарнизон артели
        // отдаётся в город, и артель пустая удаляется»): когда души дома
        // станут РОСТЕРОМ места, обе половины ниже сольются в один перенос
        // ростер→ростер, и человек с лошадью поедут одной дверью. Сегодня
        // население и гарнизон — два разных склада, поэтому и переносов два.
        int souls = 1;
        if (const auto* bag = reg.try_get<ecs::NpcInventory>(e)) {
            // Обход области существ 1023 → first = старый порядок слотов
            // (старейший первым); источник не мутируется — энтити умирает.
            for (int i = kMaxInventorySlots - 1;
                 i >= bag->inv.creature_first(); --i) {
                const ItemRef& sl = bag->inv.slots[std::size_t(i)];
                if (is_folk_kind(
                        std::uint16_t(creature_of_world_row(sl.def)))) {
                    souls += sl.count;
                } else if (!creatures_push_slot(lm.inventory, sl)) {
                    // Гарнизону тесно (кап контейнера) — лишние честно
                    // уходят в пул, никто не испаряется.
                    creatures_push_slot(gs.deserterPool, sl);
                }
            }
        }
        lm.population += souls;
        reg.destroy(e);
        return souls;
    };
    // ── Сезонная погрузка содержания (S19.2): та же арифметика нужд, что
    // у окна (roster_bill — вторых правд содержания не бывает);
    // погрузка — перенос со склада в сумку, судит ОКНО в тот же день.
    const bool boundary = season_boundary(day);
    // ДОМ ГАСИТ СЧЁТ СВОЕЙ АРТЕЛИ — И ДЕЛАЕТ ЭТО ПРИ ВЫХОДЕ, А НЕ ТОЛЬКО НА
    // ГРАНИЦЕ (владелец 2026-09-21: «снаряжать норм»). Прежде эта дверь
    // звалась ровно из двух мест, и оба — «дома, на границе»: артель,
    // ушедшая в рейс, встречала следующую границу с пустой сумкой (она
    // везёт руду и лес, а не еду) и теряла душу. Измерено: 154 385 душ в
    // пуле дезертиров за 512 дней при НУЛЕВОМ голоде мест.
    //
    // Снаряжение — не костыль, а часть закона «у всякого, кто кормит, есть
    // счёт»: дом гасит долг уходящей артели ВПЕРЁД, как уже снаряжает её
    // тяглом из стойла (outfit_crew_mounts, двухтактный обоз). Физика
    // проверена: сезон харча души — 32 кг при спине 154 кг, пятая часть.
    // Берётся РОВНО НЕДОСТАЮЩЕЕ по счёту, поэтому повторный вызов в тот же
    // день ничего не грузит и склад не сосётся дважды.
    const auto load_season_upkeep = [&](Landmark& lm, entt::entity e) {
        auto* bag = reg.try_get<ecs::NpcInventory>(e);
        auto* roster = reg.try_get<ecs::SquadRoster>(e);
        if (!bag || !roster) return;
        const int boardOrd = hunger_commodity_ordinal();
        const int owed = boardOrd >= 0 ? roster->needDebt[boardOrd] : 0;
        const int haveBoard = bag->inv.count_of(hunger_item_index());
        if (owed > haveBoard) {
            haul_between(lm.inventory, bag->inv, hunger_item_id(),
                         owed - haveBoard, 1e9f);
        }
        const std::int64_t haveCoin = inventory_value(bag->inv);
        if (roster->wageDebt > haveCoin) {
            transfer_value_dense(lm.inventory, bag->inv,
                                 int(roster->wageDebt - haveCoin));
        }
        // Погасить тем, что только что легло в сумку: долг умирает в ту же
        // минуту, что и приход (одна дверь на весь мир).
        econ_pay_debt(bag->inv, roster->needDebt, mw.econFacts,
                      mw.econFactsUser);
    };

    // 2) RAISE today's crews off the place's OWN registry row (owner
    //    2026-08-31, CANON S10): the crew pool is pop >> labourShift, split
    //    EVENLY across the rows whose gates are open today — a worksite gate
    //    walks the same find_worksite the working AI walks by (ore near home
    //    IS the presence of miners); solo rows ride alone (the tax courier).
    int raised = 0;
    for (std::size_t row = 0; row < gs.landmarks.size(); ++row) {
        Landmark& s = gs.landmarks[row];
        const LandmarkDef& ld = landmark_def(s.type);
        if (ld.crewCount == 0 || souls_flock(s) <= 0) continue;
        static_assert(sizeof(LandmarkDef::crews) / sizeof(LandmarkCrewRow)
                          <= 8,
                      "outCount — восемь счётчиков на место: по строке");
        const XY home{float(s.x), float(s.y)};
        const MacroPos homePos{home.x, home.y};
        // БРОСОК СТАНЦИИ ЭТОГО ДОМА — тот же детерминизм, что у рулетки
        // заявок ниже: (сид, день, дом). Ротация не трогает мировые
        // RNG-потоки, поэтому решение места не зависит от того, сколько мест
        // прошло до него в свипе.
        //
        // ЗАЧЕМ ОН ПОЯВИЛСЯ: дверь выбора станции звалась с ПУСТЫМ
        // TickContext, то есть без RNG, и рулетка честно вырождалась в
        // первый вес — ландмарк самого дешёвого портала. Каждая деревня всю
        // свою жизнь возила товар к ОДНОМУ И ТОМУ ЖЕ соседу, а это 98 %
        // торгового канала мира (~3040 вендорских крю против 67 караванов).
        // Случайность здесь не «добавлена» — её отсутствие было дефектом
        // проводки, а не законом.
        //
        // Номер потока = первый номер ЗА строками ростера: третий аргумент
        // hash3 в этой функции всюду означает СТРОКУ (0-7, static_assert
        // выше), и поток станции по построению не может столкнуться ни с
        // одной из них.
        Rng stationRoll(hash3(gs.worldSeed ^ std::uint32_t(day),
                              std::uint32_t(s.id),
                              sizeof(LandmarkDef::crews)
                                  / sizeof(LandmarkCrewRow)));
        ctx.rng = &stationRoll;
        // ЭКЗЕМПЛЯРЫ, А НЕ СТРОКИ: `live` хранит индекс СТРОКИ столько раз,
        // сколько сквадов она сегодня поднимает. Потолок — не закон, а
        // предохранитель от рулевой ошибки; настоящий потолок один и он
        // назван владельцем: половина паствы (labour.h field_pool).
        static constexpr int kMaxCrewInstances = 64;
        int live[kMaxCrewInstances];
        int liveCount = 0;
        int solo[8];
        int soloCount = 0;
        // Сколько экземпляров строка ХОЧЕТ сегодня — СПРОС, а не реестр.
        int want[8] = {};
        // (Здесь стоял `dest[8]` — «цель поручения на строку, чтобы провиант
        // мерился тем же маршем». У него не было НИ ОДНОГО читателя во всём
        // src/: колонка-призрак, §55. Снесена вместе с переездом на
        // экземпляры, а не оставлена «на всякий случай».)

        // ── АУКЦИОН ЦЕЛЕЙ этого дома (CANON S10, владелец 2026-09-02) ────
        // Кандидаты считаются ОДИН раз на ландмарк в день (лениво — только
        // если есть свободная Auction-строка); каждая артель тянет СВОЮ
        // рулетку по скору — диверсификация без координации. Скор деньгами
        // по закону цены: нужда дома сама дорожает дефицитом (stock_price),
        // дорога роняет делителем. Терм опасности (Died-факты у маршрута)
        // добавляется ПОСЛЕ, вес — дубль-прогоном.
        struct GoalBid {
            std::uint8_t  type;   // SquadType — кем встанет взявший заявку
            std::uint32_t object;
            XY            site;
            float         score;
        };
        GoalBid bids[kGathererGoalCount + 1];
        int bidCount = -1;   // -1 = аукцион ещё не считан
        // СПРОС НА КОРОВАНЫ — число трюмов излишка (см. run_auction ниже).
        int caravanHolds = 0;
        const Skills& homeSite = landmark_sheet(s.type).skills;
        const auto run_auction = [&] {
            if (bidCount >= 0) return;
            bidCount = 0;
            // ── ТЕРМ ОПАСНОСТИ (CANON S10, шаг 4 инкремента стражи) ─────
            // Худшая округа маршрута платит страхом: деньги поля угрозы
            // (стоимость погибших там душ) против денег рейса, вес —
            // kThreatFearShift. Скор, съеденный страхом, роняет кандидата
            // ЦЕЛИКОМ — это и есть отказ рейса ценой.
            NavWorld* nvF = ctx.mw.nav;
            const bool fearOn =
                nvF && nvF->baked() && !nvF->threat.empty();
            const std::uint16_t homeRF =
                fearOn ? nav_region_at(*nvF, int(home.x), int(home.y))
                       : kNavNoRegion;
            const auto fear_of = [&](const XY& site) -> float {
                if (!fearOn || homeRF == kNavNoRegion) return 0.0f;
                const std::uint16_t r =
                    nav_region_at(*nvF, int(site.x), int(site.y));
                if (r == kNavNoRegion) return 0.0f;
                return float(threat_on_route(*nvF, homeRF, r)
                             >> kThreatFearShift);
            };
            // ДОРОГА В СКОРЕ — ЦЕНА ПУТИ, А НЕ ПРЯМАЯ (CANON S7, дверь
            // nav_path_cost), И В ДНЯХ ТУДА-ОБРАТНО: рейс возвращается.
            // Жила за горой и рынок за заливом перестали выглядеть
            // близкими. Прямая остаётся там, где другого ответа нет, — в
            // мире без запечённой навигации (голые фикстуры).
            //
            // ЦЕНА ПРАВДЫ, ИЗМЕРЕННАЯ И ПРИНЯТАЯ (64 дня, сид 7): мир
            // добывает меньше — дерево 139 014 → 79 987, железо 1070 → 210.
            // Вердикт владельца: «добыча упала, но не сломалась; если по
            // архитектуре и математике верно — так и надо».
            //
            // ── ОДНА ВЕЛИЧИНА НА ВЕСЬ МИР: МОНЕТ НА ДУШУ В ДЕНЬ ─────────
            // (владелец 2026-09-21: «чинить, одна размерность у обеих
            // заявок».) Прежде здесь стояло признание, что скор размерно НЕ
            // сходится: у добычи числитель — стоимость одной СПИНЫ, у сбыта
            // ПОЛНАЯ маржа склада, а страх вычитался НЕДЕЛЁНЫМ. Следствие
            // было не «неточность», а перекос ЗНАКА: маржа целого склада
            // делает страх неразличимым, а у добычи тот же страх глушит
            // заявку целиком — то есть решение «ехать или нет» принималось
            // разными единицами. Под новым законом рождения крю это стало бы
            // хуже, чем неточность: число сквадов равно числу заявок с
            // ПОЛОЖИТЕЛЬНЫМ скором, то есть несогласованная единица начала бы
            // решать, сколько душ место выводит в поле.
            //
            //     скор = (что произведёт ОДНА СПИНА за рейс − страх) / дни
            //
            // Оба слагаемых числителя — монеты, делитель — дни; ноль новых
            // констант. «Одна спина» у добычи была и раньше (carryPerSoul /
            // вес единицы), у сбыта её вводит проход ниже — ТОЙ ЖЕ дверью
            // value_dense_order(), какой крю потом и грузится.
            const auto road_days_ = [&](const XY& site) -> float {
                float cells = -1.0f;
                if (nvF && nvF->baked()) {
                    const std::uint32_t c =
                        nav_path_cost(*nvF, int(home.x), int(home.y),
                                      int(site.x), int(site.y));
                    if (c != kNavFar) cells = float(c) / 16.0f;
                }
                if (cells < 0.0f)
                    cells = std::sqrt(torus_dist_sq(
                        home.x, home.y, site.x, site.y,
                        float(ctx.mapW), float(ctx.mapH)));
                return 2.0f * cells / kSustainedMarchCellsPerDay;
            };
            // ── ЦЕННОСТЬ И ДЛИТЕЛЬНОСТЬ РЕЙСА — ИЗ МИРА (CANON S10) ─────
            // Артель копает, пока не полна спина (ai_gatherer «backsFull»),
            // значит рейс привозит домой ПОЛНУЮ СПИНУ, чего бы он ни копал:
            //     ценность рейса = цена единицы × (спина / вес единицы)
            //     дни работы     = спина / (дневной тейк × вес единицы)
            // Души сокращаются: и обоз, и дневной тейк растут с их числом,
            // поэтому число душ в скор не входит и знать его до подъёма
            // артели не нужно. Строка, чей выход ложится в РОСТЕР (лошадь),
            // спиной не ограничена — её мера по закону упряжки одна голова
            // на душу, то есть ровно одна за рейс на душу.
            const NPCType crewKind = [&]() -> NPCType {
                const LandmarkDef& ldc = landmark_def(s.type);
                for (int i = 0; i < int(ldc.crewCount); ++i)
                    if (!ldc.crews[i].solo) return ldc.crews[i].npc;
                return NPCType::Peasant;
            }();
            const CharacterSheet crewSheet =
                make_character_sheet(crewKind, 1, 0u);
            const float crewHaul = npc_def(crewKind).haulMult;
            const float carryPerSoul =
                get_carry_capacity(crewSheet.attributes, crewSheet.skills)
                * (crewHaul > 0.0f ? crewHaul : 1.0f);
            // Цели добычи: строка открыта, когда мир предъявил рабочее
            // место (find_worksite — тот же предикат, каким работает сама
            // артель); ценность = домашняя цена товара × дневной тейк
            // одного работника (kGatherPerWorkerDay — сквозной якорь S10).
            for (int g = 0; g < kGathererGoalCount; ++g) {
                const GathererDef& gd = kGathererDefs[g];
                int unitPrice = 0;
                if (gd.rosterYield != NPCType::Count) {
                    // ЛОШАДЬ-ЮНИТ (вердикт 2026-09-19): существо ценится
                    // своей строкой найма через ТУ ЖЕ кривую дефицита —
                    // «склад» = табун, уже стоящий в гарнизоне места,
                    // «нужда» = спины, которых просят рейсы: в поле выходит
                    // pop >> labourShift рук, одна лошадь несёт haulMult
                    // спин, значит табуну есть смысл расти до пул/haulMult.
                    // Насытился — цель дешевеет, рулетка уводит руки.
                    const NpcTypeDef& yieldRow = npc_def(gd.rosterYield);
                    const int base = yieldRow.hireGold;
                    if (base <= 0) continue;
                    const int herd =
                        creature_heads_of(s.inventory, gd.rosterYield)
                        + horsesStanding[row];
                    // НУЖДА — ТОТ ЖЕ ЗАКОН УПРЯЖКИ (владелец 2026-09-19:
                    // «по лошадке на душу»): месту нужно столько ездовых,
                    // сколько душ оно выводит в поле — пул труда. Прежнее
                    // «пул / haulMult» было вторым правилом о той же вещи
                    // (дефект S26) и просило вшестеро меньше, чем отряды
                    // способны вести.
                    // Пул — ОДНОЙ дверью (labour.h): это было третье в мире
                    // прочтение `pop >> labourShift`, и теперь оно то же
                    // самое число, что судит рождения ниже.
                    const int wanted = std::max(
                        1, field_pool(s, afield[row], standingSouls[row]));
                    unitPrice = stock_price(base, herd, wanted);
                } else {
                    const ItemDef* idef = item_def(gd.commodity);
                    const int base = idef ? idef->value : 0;
                    if (base <= 0) continue;
                    const int have = s.inventory.count(gd.commodity);
                    const int demand = season_demand_for(
                        gd.commodity, s.needDebt, souls_home(s), homeSite,
                        &s.inventory);
                    unitPrice = stock_price(base, have, demand);
                    // ЛУЧШАЯ ИЗВЕСТНАЯ ЦЕНА, А НЕ ТОЛЬКО СВОЯ (владелец,
                    // 2026-09-20, ПОД ГРИФОМ «НЕ УВЕРЕНЫ» — единственное
                    // место этого хода, которое владелец согласился строить
                    // с сомнением; если замер скажет «хуже», снимать первым).
                    //
                    // ЗАЧЕМ. С третьим потоком у РЕСУРСА нет своей нужды, и
                    // домашняя цена железа у деревни падает до база/(запас+1):
                    // деревня копает первую партию и бросает — ровно тот
                    // дефект, ради которого когда-то и завели пол спроса
                    // («деревня, СТОЯЩАЯ НА ЖЕЛЕЗЕ, никогда его не копала»).
                    // Подменить цену на СТОИМОСТЬ нельзя: на локальной цене
                    // держится закон «руки идут туда, где выше стоимость на
                    // рабочий день» (город с полным амбаром уводит руки в
                    // шахту именно потому, что видит цену хлеба на полу).
                    //
                    // ЧТО ЭТО НЕ ЕСТЬ: не всеведение и не «маршрут знает
                    // цену». Вассал читает ПРЕЙСКУРАНТ СВОЕГО СЮЗЕРЕНА по
                    // существующему феодальному ребру — один слой отношений
                    // (CANON S7: «отношение знает КТО, путь знает КАК») и
                    // ярус 2 знания о цене (ведомость, S10). Незнакомых мест
                    // деревня по-прежнему не видит.
                    if (const Landmark* suz =
                            landmark_by_id(gs, suzerain_of(s));
                        suz && suz->ledger.published()) {
                        const int ci = commodity_index(gd.commodity);
                        if (ci >= 0) {
                            const int there = suz->ledger.price[std::size_t(ci)];
                            if (there > unitPrice) unitPrice = there;
                        }
                    }
                }
                // МЕСТО УЖЕ ИСКАЛО — артель ЧИТАЕТ (владелец, 2026-09-18).
                // Жилы приходят строкой описи своей округи (survey_landmark_
                // regions на границе сезона); у леса и домашнего поля свои
                // дешёвые двери, и они остаются ими.
                XY site;
                if (gd.worksite == Worksite::Deposit) {
                    const SurveyRow& sr =
                        s.survey.rows[std::size_t(gd.row)];
                    if (sr.none()) continue;   // в округе такого рода нет
                    site = XY{float(sr.x), float(sr.y)};
                } else if (!find_worksite(gd, ctx, homePos, home, site)) {
                    continue;
                }
                // СКОР = ВЫРАБОТКА В ДЕНЬ РЕЙСА. Одна величина на все
                // заявки — стоимость в день, — поэтому добыча и сбыт
                // наконец сравнимы в одной рулетке. Назначенного `1 +`
                // больше нет: делитель есть настоящая длина рейса.
                const ItemDef* gdef =
                    gd.commodity ? item_def(gd.commodity) : nullptr;
                const float unitKg =
                    gdef && gdef->weight > 0.0f ? gdef->weight : 1.0f;
                const float perSoul = gd.rosterYield != NPCType::Count
                                          ? 1.0f
                                          : carryPerSoul / unitKg;
                const float workDays =
                    gd.perWorkerDay > 0 ? perSoul / float(gd.perWorkerDay)
                                        : 0.0f;
                const float tripDays = road_days_(site) + workDays;
                if (!(tripDays > 0.0f)) continue;
                const float score =
                    (float(unitPrice) * perSoul - fear_of(site)) / tripDays;
                if (score <= 0.0f) continue;
                bids[bidCount++] = GoalBid{
                    std::uint8_t(SquadType::Artel), std::uint32_t(g),
                    site, score};
            }
            // Рейс сбыта-закупки (цели крестьян, вердикты 2026-09-02 и
            // 2026-09-18: «рейс поднимать не по нужде, а просто — на
            // складе то всяко что-то есть»; «голодный дом должен ехать
            // ПОКУПАТЬ»): ценность рейса = долг дани ПОЛНОЙ стоимостью
            // (долг обязан ехать) + ОБА конца одной кривой цены. Продать:
            // что дома дешевле базы, сверх сезонного амбара (S19.2) —
            // затоваривание само зовёт в город. Купить: чего дома не
            // хватает до сезонной нужды, весом его дефицитной цены — тот
            // самый хутор на дорогой руде едет за хлебом, потому что право
            // на рейс растёт с голодом, а не с излишком. Гейт «только при
            // излишке» умер этим вердиктом.
            // Локальность закона: никакого знания цен рынка — только СВОЙ
            // склад; сама сделка честно решится на месте (ai_vendor).
            // ПЕРВАЯ СТАНЦИЯ РЕЙСА — СОСЕД ПО ГРАФУ ОКРУГ, как у всякого
            // торговца (владелец 2026-09-20, диффузия): ни феодального
            // ребра, ни «ближайшего вассала» полным сканом мест. Дальше
            // крю идёт той же диффузией (pick_next_station_), а КУДА течёт
            // товар, решает цена на месте, а не выбор маршрута.
            const MacroPos homePos{home.x, home.y};
            float stX = 0.0f, stY = 0.0f;
            const int firstId = pick_next_station_(ctx, homePos, s.id, -1,
                                                   stX, stY);
            const Landmark* partner =
                firstId >= 0 ? landmark_by_id(gs, firstId) : nullptr;
            if (partner && landmark_is_settlement(partner->type)
                && partner->id != s.id) {
                const Landmark* city = partner;
                // (ДАНЬ ОТСЮДА УШЛА 2026-09-22: она больше не едет
                // попутным грузом рейса сбыта — у неё своя заявка и своя
                // машина, CANON S4 «Сборщик идёт вниз».)
                long long value = 0;
                // Проход ПРОДАЖИ: что повезём (дома дешевле базы, сверх
                // сезонного амбара) — и это же ПОКУПАТЕЛЬНАЯ СПОСОБНОСТЬ
                // рейса (владелец 2026-09-18: «у нас бартер-система —
                // покупательная способность это суммарная стоимость
                // инвентаря»). Дань — долг, не кошелёк.
                // КОШЕЛЁК РЕЙСА — ТА ЖЕ ДВЕРЬ ПОГРУЗКИ, СПРОШЕННАЯ
                // БЕЗ ДВИЖЕНИЯ: что крю реально увезёт из дома, включая
                // КАЗНУ (владелец 2026-09-22: всё в инвентаре — товар).
                // Оттого голодный богатый город впервые видит положительный
                // скор «поехать КУПИТЬ»: покупательная способность больше
                // не равна нулю от того, что продавать ему нечего.
                long long purse = plan_home_load_(
                    s.inventory, s.needDebt, souls_home(s), homeSite,
                    carryPerSoul, nullptr);
                const long long purseAtHome = purse;   // трюм ОДНОЙ спины
                // СКОЛЬКО ТРЮМОВ ИЗЛИШКА ЛЕЖИТ ДОМА — это и есть СПРОС на
                // корованы (CANON S4 «число караванов — функция контекста
                // места», владелец: «население и склада да»). Обе величины
                // в ДОМАШНИХ ценах, поэтому отношение — чистое число спин.
                long long surplusValue = 0;   // весь излишек на вывоз
                long long needValue = 0;      // вся нехватка на ввоз
                // ПО ОДНОЙ СПИНЕ И В ПОРЯДКЕ ПОГРУЗКИ. Обход идёт
                // value_dense_order() — ТОЙ ЖЕ дверью, какой крю грузится
                // дома (load_cheap_at_home_) и какой везёт дань, — и
                // останавливается, когда спина полна. Поэтому «что решили» и
                // «что повезли» перестали быть двумя разными числами: скор
                // больше не обещает маржу, которая в обоз не влезет.
                // Дань в спине ПЕРВАЯ: долг обязан ехать (тот же приоритет,
                // что у погрузки), и она же съедает место у излишка.
                float freeKg = carryPerSoul;
                for (int oi = 0; oi < kCommodityCount && freeKg > 0.0f; ++oi) {
                    const int c = value_dense_order()[std::size_t(oi)];
                    const char* id = kCommodities[c].id;
                    const ItemDef* d = item_def(id);
                    const int base = d ? d->value : 0;
                    if (base <= 0) continue;
                    const float kg = d->weight > 0.0f ? d->weight : 1.0f;
                    const auto fits = [&](long long want) -> long long {
                        const long long cap = (long long)(freeKg / kg);
                        return want < cap ? want : cap;
                    };
                    const int have = s.inventory.count(id);
                    // Спрос уже СЕЗОННЫЙ (остаток счёта + производный).
                    const int demand = season_demand_for(id, s.needDebt,
                                                         souls_home(s),
                                                         homeSite,
                                                         &s.inventory);
                    const int homePrice =
                        stock_price(base, have, demand);
                    // ЦЕНА ТАМ — ЯРУС 2, ИЗ ТОЧКИ ДОМА (CANON S10). До
                    // 2026-09-22 здесь стояло `base − homePrice`, то есть
                    // «насколько дёшево моё излишнее добро У МЕНЯ ДОМА» —
                    // мера ГОТОВНОСТИ СБРОСИТЬ, а не выручки рейса
                    // (problems §55-II). Спред против прейскуранта партнёра
                    // — это и есть выручка, и знание на него законно.
                    const int therePrice =
                        market_price_seen(ctx.mw, int(home.x), int(home.y),
                                          *city, c);
                    if (have > demand && therePrice > homePrice
                        && freeKg > 0.0f) {
                        const long long fit = fits(have - demand);
                        if (fit > 0) {
                            value += fit * (therePrice - homePrice);
                            // (Кошелёк сюда БОЛЬШЕ НЕ ПРИБАВЛЯЕТСЯ: он уже
                            // посчитан ОДНОЙ дверью погрузки выше, и второе
                            // слагаемое было бы тем же грузом, посчитанным
                            // дважды.)
                            freeKg -= float(fit) * kg;
                        }
                    }
                    if (have > demand && homePrice > 0)
                        surplusValue +=
                            (long long)(have - demand) * homePrice;
                }
                // Проход ЗАКУПКИ, капнутый кошельком: дефицитная цена может
                // быть сколь угодно громкой (коридора нет — и не будет), но
                // РЕШЕНИЕ весит только то, что рейс способен ОПЛАТИТЬ своим
                // грузом. Без капа пустая полка города весила миллионы и
                // глушила и страх, и добычу (измерено, guard_patrol).
                for (int c = 0; c < kCommodityCount && purse > 0; ++c) {
                    const char* id = kCommodities[c].id;
                    const ItemDef* d = item_def(id);
                    const int base = d ? d->value : 0;
                    if (base <= 0) continue;
                    const int have = s.inventory.count(id);
                    const int demand = season_demand_for(id, s.needDebt,
                                                         souls_home(s),
                                                         homeSite,
                                                         &s.inventory);
                    const int homePrice =
                        stock_price(base, have, demand);
                    // Тот же спред другим концом: везти домой стоит то, что
                    // ТАМ дешевле, чем дома. База заменена ценой партнёра по
                    // той же причине, что и в проходе продажи.
                    const int therePrice =
                        market_price_seen(ctx.mw, int(home.x), int(home.y),
                                          *city, c);
                    if (have >= demand || therePrice <= 0
                        || homePrice <= therePrice)
                        continue;
                    const long long buyable = std::min<long long>(
                        demand - have, purse / therePrice);
                    if (buyable <= 0) continue;
                    value += buyable * (homePrice - therePrice);
                    purse -= buyable * therePrice;
                }
                // НЕХВАТКА ДОМА — второй конец той же работы: город,
                // которому нечего вывозить, но нужно ВВЕЗТИ, снаряжает
                // обозы ровно так же (иначе спрос считался бы только у
                // продавца, и голодный богатый город остался бы с одним
                // корованом — измерено 2026-09-22).
                for (int c = 0; c < kCommodityCount; ++c) {
                    const char* id = kCommodities[c].id;
                    const ItemDef* d = item_def(id);
                    const int base = d ? d->value : 0;
                    if (base <= 0) continue;
                    const int have = s.inventory.count(id);
                    const int demand = season_demand_for(id, s.needDebt,
                                                         souls_home(s),
                                                         homeSite,
                                                         &s.inventory);
                    if (demand <= have) continue;
                    needValue += (long long)(demand - have)
                                 * stock_price(base, have, demand);
                }
                if (value > 0) {
                    const XY citySite{float(city->x), float(city->y)};
                    // Та же величина: стоимость сделки, размазанная по
                    // длине рейса. Торг не занимает дней — сделка
                    // заключается в момент прибытия, — поэтому вся
                    // длительность рейса это дорога туда-обратно.
                    const float tripDays = road_days_(citySite);
                    const float score =
                        tripDays > 0.0f
                            ? (float(value) - fear_of(citySite)) / tripDays
                            : 0.0f;
                    if (score > 0.0f) {
                        bids[bidCount++] = GoalBid{
                            std::uint8_t(SquadType::Caravan),
                            std::uint32_t(city->id), citySite, score};
                        // Излишка на N спин — значит и обозов до N. Пул рук
                        // ниже это число только УРЕЗАЕТ, но не назначает
                        // (та же форма, что у числа сборщиков, CANON S4).
                        // СКОЛЬКО ТРЮМОВ РАБОТЫ ЕСТЬ У ЭТОГО МЕСТА —
                        // обоими концами: вывезти излишек ИЛИ ввезти
                        // нехватку. Мера трюма — то, что одна спина реально
                        // увозит из дома (та же дверь погрузки). Пул рук
                        // ниже это число только УРЕЗАЕТ (CANON S4).
                        const long long work =
                            std::max(surplusValue, needValue);
                        caravanHolds =
                            purseAtHome > 0
                                ? int(std::min<long long>(
                                      work / purseAtHome,
                                      kMaxCrewInstances))
                                : 1;
                        if (caravanHolds < 1) caravanHolds = 1;
                    }
                }
            }
            // ── ЗАЯВКИ СБОРЩИКА: ПО ОДНОЙ НА КАЖДОГО ДОЛЖНИКА ──────────────
        // CANON S4 «ЧИСЛО СБОРЩИКОВ = ЧИСЛО ВАССАЛОВ-ДОЛЖНИКОВ» (владелец:
        // «по сборщику на вассала… по числу вассалов должников если быть
        // точным»). Урна ОБЩАЯ и единица ОДНА — «выработка в день рейса»:
        // стоимость долга, делённая на дни пути. Приоритета как сущности
        // нет: заявка дани конкурирует со сбытом и добычей в той же
        // рулетке, и это ровно то, что записано каноном.
            for (int k = 0; k < kMaxInterests; ++k) {
                const Interest& it = s.interests.slots[std::size_t(k)];
                if (it.stance == std::uint8_t(Stance::None)) break;
                if (it.stance != std::uint8_t(Stance::Vassal)) continue;
                const Landmark* v = landmark_by_id(gs, it.object);
                if (!v || !owes_tithe(*v)) continue;
                if (bidCount >= int(sizeof(bids) / sizeof(bids[0]))) break;
                const XY site{float(v->x), float(v->y)};
                const float tripDays = road_days_(site);
                if (!(tripDays > 0.0f)) continue;
                const float score =
                    (float(v->titheOwedValue) - fear_of(site)) / tripDays;
                if (score <= 0.0f) continue;
                bids[bidCount++] = GoalBid{std::uint8_t(SquadType::Collector),
                                           std::uint32_t(v->id), site, score};
            }
        };

        // (ЗДЕСЬ СТОЯЛА ПАТРУЛЬНАЯ УРНА run_patrol_auction — 86 строк,
        // снесена 2026-09-21: её звала только строка с garrison=true, а
        // таких в реестре не осталось. Она же была единственным писателем
        // ErrandVerb::Patrol. Патруль вернётся своей строкой вместе со
        // своей механикой — и тогда его заявка встанет в ОБЩУЮ урну выше
        // одной размерностью со всеми, а не отдельным аукционом.)

        // Строка берёт только заявки своего типа; ByKind = вся урна.
        const auto row_takes_ = [](const LandmarkCrewRow& cr,
                                   const GoalBid& b) -> bool {
            return cr.type == SquadType::ByKind
                   || b.type == std::uint8_t(cr.type);
        };
        // БРОСОК НА КАЖДЫЙ ЭКЗЕМПЛЯР, А НЕ НА СТРОКУ: два корована одного
        // города тянут РАЗНЫЕ станции, две артели одной деревни — разные
        // цели. Детерминизм тот же (сид, день, дом), плюс номер экземпляра.
        const auto draw_bid_ = [&](const LandmarkCrewRow& cr,
                                   int nonce) -> const GoalBid* {
            float total = 0.0f;
            for (int b = 0; b < bidCount; ++b)
                if (row_takes_(cr, bids[b])) total += bids[b].score;
            if (!(total > 0.0f)) return nullptr;
            Rng roll(hash3(gs.worldSeed ^ std::uint32_t(day),
                           std::uint32_t(s.id), std::uint32_t(nonce)));
            float draw = roll.next_f01() * total;
            const GoalBid* pick = nullptr;
            for (int b = 0; b < bidCount; ++b) {
                if (!row_takes_(cr, bids[b])) continue;
                pick = &bids[b];
                draw -= bids[b].score;
                if (draw <= 0.0f) break;
            }
            return pick;
        };

        for (int i = 0; i < int(ld.crewCount); ++i) {
            const LandmarkCrewRow& cr = ld.crews[i];
            bool open = false;
            switch (cr.gate) {
                case CrewGate::Auction: {
                    run_auction();
                    if (bidCount <= 0) break;   // отказ = вывод аукциона
                    // ── СТРОКА БЕРЁТ ТОЛЬКО ЗАЯВКИ СВОЕГО ТИПА (CANON S4
                    //    «ЗАНЯТИЕ — ЭТО СТРОКА РОСТЕРА») ──────────────────
                    // Деревенская строка объявлена `Artel`, городская —
                    // `Caravan`, и разделение живёт ДАННЫМИ: ни одной ветки
                    // по роду места. `ByKind` = тип не объявлен, урна вся.
                    int mine = 0;
                    for (int b = 0; b < bidCount; ++b)
                        if (row_takes_(cr, bids[b])) ++mine;
                    if (mine <= 0) break;   // своих заявок нет
                    // ── СКОЛЬКО ЭКЗЕМПЛЯРОВ: СПРОС, А НЕ РЕЕСТР ─────────
                    // Добыча: сколько целей с ПОЛОЖИТЕЛЬНЫМ скором — столько
                    // артелей и есть смысл поднять (каждая возьмёт свою
                    // рулеткой, диверсификация без координации).
                    // Сбыт: сколько ТРЮМОВ излишка лежит на складе.
                    // Дань (Б-4) встанет сюда же своим ответом: сколько
                    // вассалов-должников. Пул рук ниже только УРЕЗАЕТ.
                    want[i] = cr.type == SquadType::Caravan
                                  ? std::max(1, caravanHolds)
                                  : mine;
                    open = true;
                    break;
                }
                case CrewGate::Suzerain: {
                    // The capital pays nobody above itself — the old
                    // hardcode raised a courier in EVERY city, and the
                    // capital's one walked to its own gate. The edge is
                    // the landmark's own column now (S24).
                    open = suzerain_of(s) >= 0 && suzerain_of(s) != s.id;
                    break;
                }
            }
            if (!open) continue;
            if (cr.solo) {
                if (outCount[row][std::size_t(i)] == 0 && soloCount < 8)
                    solo[soloCount++] = i;
                continue;
            }
            // Уже в поле — не поднимаем заново: строка хочет `want`, в поле
            // стоит `outCount`, разница и есть сегодняшний наряд.
            int need = want[i] - int(outCount[row][std::size_t(i)]);
            while (need-- > 0 && liveCount < kMaxCrewInstances)
                live[liveCount++] = i;
        }
        // (ЗДЕСЬ ПОДНИМАЛАСЬ ВЫЛАЗКА ГАРНИЗОНА — 81 строка, снесена
        // 2026-09-21 вместе с патрульной механикой: строк garrison в
        // реестре не осталось, guardCount был вечным нулём.)
        // СЕЗОННАЯ ПОГРУЗКА стоящих дома артелей (S19.2): на границе дом
        // грузит каждую свою стоящую артель содержанием на сезон вперёд —
        // ДО окна сквадов (порядок дня: ротация раньше окна). Погрузка —
        // перенос, судья — окно; артель В ПОЛЕ на границе платит из того,
        // что несёт (локальность: чужих складов на расстоянии не бывает).
        if (boundary) {
            for (auto& [r2, e2] : idleByRow)
                if (r2 == int(row) && e2 != entt::null)
                    load_season_upkeep(s, e2);
        }
        // Заявка строки закрывается СТОЯЩЕЙ артелью первой — это и есть
        // пере-аукцион дня живой артели (S19.2: «рейс → дом → пере-аукцион
        // → новый рейс, домой вернулась — не исчезла»).
        const auto claim_standing = [&](std::uint16_t type) -> entt::entity {
            for (auto& [r2, e2] : idleByRow) {
                if (r2 != int(row) || e2 == entt::null) continue;
                if (reg.get<ecs::NPCKind>(e2).type != type) continue;
                const entt::entity found = e2;
                e2 = entt::null;
                return found;
            }
            return entt::null;
        };
        // ДВА РЕГУЛЯТОРА, оба от состояния и контекста (владелец,
        // 2026-09-18): строки × гейты дня = СКОЛЬКО сквадов, пул труда =
        // КАКОГО РАЗМЕРА. Оба выводятся здесь и сейчас, ничего не хранится.
        // База пула — ПОЛНОЕ число душ в распоряжении дома: население плюс
        // души стоящих дома артелей (они не в population, но они дома).
        // ОДИН ПУЛ РУК МЕСТА (macro/labour.h, владелец 2026-09-21): половина
        // его паствы — и всё. Доля реестра `pop >> labourShift` из этого
        // счёта ушла: она была числом с потолка, и она же отвечала на второй
        // вопрос — сколько душ стоит у станков города (там и осталась).
        const int pool =
            field_pool(s, afield[row], standingSouls[row]);
        // Соло-строка стоит РОВНО ОДНУ душу и берётся из того же пула первой:
        // курьер дешевле артели, но не бесплатен — прежде соло-рождения шли
        // мимо всякого счёта рук (одна из девяти половин, §55).
        const int soloCost = std::min(soloCount, pool);
        int soloBudget = soloCost;
        // ── ПУЛ РУК УРЕЗАЕТ ЧИСЛО, А НЕ ТОЛЬКО РАЗМЕР (CANON S4) ────────
        // «Пул труда места не НАЗНАЧАЕТ это число, а только УРЕЗАЕТ его,
        // когда рук не хватает.» Без этой строки спрос на много экземпляров
        // ронял `perCrew` в НОЛЬ, и место не поднимало НИ ОДНОГО сквада —
        // то есть изобилие целей читалось как «рук нет вовсе» (измерено:
        // корованов 63 из спроса в сотни).
        const int handsForCrews = std::max(0, pool - soloCost);
        if (liveCount > handsForCrews) liveCount = handsForCrews;
        const int perCrew =
            liveCount > 0 ? handsForCrews / liveCount : 0;
        for (int li = 0; li < liveCount; ++li) {
            const int i = live[li];
            // Своё поручение на экземпляр (nonce = строка × потолок + номер):
            // сквады одной строки расходятся по разным целям, а не идут
            // колонной в одно место.
            const GoalBid* myBid =
                draw_bid_(ld.crews[i], i * kMaxCrewInstances + li);
            if (!myBid) continue;
            const std::uint8_t myType = myBid->type;
            const std::uint32_t myObject = myBid->object;
            const entt::entity standing =
                claim_standing(std::uint16_t(ld.crews[i].npc));
            if (standing != entt::null) {
                // ПОРУЧЕНИЕ НА СПИНУ (аукцион, CANON S10): пара {глагол,
                // объект} — рулетка этой строки уже решила; рефлекс
                // прерывает не спрашивая.
                auto& prt = reg.get<ecs::MacroNpcRuntime>(standing);
                prt.squadType = myType;
                prt.errandObject = myObject;
                prt.stateTimer = 0;   // новый рейс — этим же думом
                // НОВОЕ ПОРУЧЕНИЕ НАЧИНАЕТСЯ С НАЧАЛА (найдено замером
                // 2026-09-22): здесь сбрасывался ТОЛЬКО таймер, а состояние
                // оставалось от прошлого рейса. Переторгованная крю,
                // стоявшая в `Working`, входила в новую машину сразу
                // «на месте работы» — и сборщики зависали в Working, ни
                // разу не тронувшись в путь (гистограмма состояний:
                // 899 559 вызовов Working против НУЛЯ Traveling).
                prt.state = std::uint8_t(NS::Idle);
                // ТАКТ 2: дом снаряжает уходящую артель тяглом из стойла
                // (по коню на душу, сколько стоит) — рядом с провиантом
                // ниже, тот же акт над вторым контейнером.
                outfit_crew_mounts(*mw.world, s, standing);
                // ...И СЧЁТОМ (v105): тот же такт снаряжения, второй
                // контейнер. Уходящая артель уносит непогашенный харч и
                // плату, поэтому граница застаёт её не с пустой сумкой.
                load_season_upkeep(s, standing);
                // ПРИВЕДЕНИЕ СОСТАВА (S19.2, 2026-09-18): на границе
                // стоящая артель дышит к пулу — добор из населения (дома,
                // сколько прокормит склад: окно этого же дня спишет сезон
                // с ПОЛНОГО состава), ссадка лишних обратно в население
                // (перенос, не баланс). В поле состав не трогается.
                if (boundary && perCrew > 0) {
                    if (auto* bg = reg.try_get<ecs::NpcInventory>(standing)) {
                        const int want = perCrew - 1;   // члены без лидера
                        int have = count_human_souls(bg->inv);
                        const int canFeed =
                            s.inventory.count_of(hunger_item_index())
                                / kDaysPerSeason;
                        int take = std::min(want - have,
                                            std::max(0, canFeed - have));
                        take = std::min(take, souls_home(s) - 1);
                        while (take-- > 0) {
                            // ГЕНЕРИК (CANON S4): массовый добор — стак,
                            // без ординала; имя душа зарабатывает историей
                            // (лидерство, найм в сюжет, вселение).
                            SoldierRecord rec{};
                            rec.kind = std::uint16_t(ld.crews[i].npc);
                            rec.level = 1;
                            if (!creatures_push(bg->inv, rec)) break;
                            s.population -= 1;
                        }
                        // ССАДКА СУДИТ ЛЮДЕЙ: последняя человеческая
                        // душа сходит в население; табун артели суду
                        // состава не подсуден — лошадь не человек и в
                        // want не входит (дроссель ловли — в аукционе).
                        // Новейший людской слот = наименьший индекс области
                        // (старый обход slot_count-1 → 0 = first → 1023).
                        for (have = count_human_souls(bg->inv);
                             have > want; --have) {
                            int si = -1;
                            for (int k = bg->inv.creature_first();
                                 k < kMaxInventorySlots; ++k) {
                                if (is_folk_kind(std::uint16_t(
                                        creature_of_world_row(
                                            bg->inv.slots[std::size_t(k)]
                                                .def)))) {
                                    si = k;
                                    break;
                                }
                            }
                            SoldierRecord off{};
                            if (si < 0 || !creatures_take_at(bg->inv, si, off))
                                break;
                            s.population += 1;
                        }
                        // Приведённый состав — приведённый обоз (squad.h):
                        // добранная душа несёт свою спину, ссаженная уносит.
                        refresh_squad_carry(*mw.world, standing);
                    }
                }
                continue;
            }
            // СОЗДАНИЕ — только на границе сезона (S19.2: «на начало
            // сезона либо создаёт новых, если его состояние требует
            // больше»); погибшая в поле артель — дыра до следующего суда.
            if (!boundary) continue;
            // ГЕЙТ ОТЛУЧКИ («дома должно быть больше, чем в поле», владелец
            // 2026-09-02) ВЫРЕЗАН 2026-09-21: это ровно половина паствы,
            // сказанная вторым голосом, и она теперь ЖИВЁТ В ПУЛЕ
            // (labour.h field_pool — потолок считается от паствы, то есть
            // «в поле не больше, чем дома» выполняется по построению).
            // Минус одна из девяти половин §55, без единого нового правила.
            if (perCrew <= 0 || souls_home(s) < perCrew) continue;
            // СОЗДАНИЕ БЕЗ ПРЕДОПЛАТЫ СЕЗОНА (владелец 2026-09-19,
            // отменяет гейт 2026-09-17 «сезон содержания или не
            // поднимается»): «условие поднятия артели — это сколько ей
            // надо на сезон, а внутрь её загружать уже не обязательно;
            // худшее — если задержится, потеряет 1/8 по общему закону».
            // Сезонная нужда остаётся МЕРОЙ (roster_bill — её судит
            // окно), погрузка ниже — сколько склад даёт (haul_between
            // берёт что есть), недостача на окне = 1/8 ростера в дезертиры
            // (squad_season_window) — тот же закон, каким кровит гарнизон.
            // ПОЧЕМУ ГЕЙТ УБИТ, циферью: пропорциональная сытость (S25)
            // оставляет после КАЖДОЙ границы меньше одного душевого сезона
            // хлеба (остаток < 32), а суд артелей идёт в тот же день ПОСЛЕ
            // еды — гейт мерил склад в единственный момент, когда тот пуст
            // по построению, и с честным съеданием не открывался больше
            // НИКОГДА: ноль артелей со второго сезона, ноль пахоты, мир
            // умирал на сеяных запасах (замер 2026-09-19: 76 тыс. душ из
            // 310 тыс., 5 сделок в день на весь мир). Прежде мир жил
            // только потому, что кромка «всё или ничего» не списывала
            // хлеб голодавших — несписанное было скрытым капиталом ворот.
            // Голодная деревня обязана ПАХАТЬ, а не ждать сытости, чтобы
            // начать пахать.
            SquadSpec spec{};
            spec.leaderType = ld.crews[i].npc;
            spec.x = s.x;
            spec.y = s.y;
            spec.homeSettlementId = s.id;
            for (int m = 1; m < perCrew; ++m) {
                // ГЕНЕРИК (CANON S4): члены артели — один стак, не
                // per-душевые ординалы (тот поток остаётся ИМЕНАМ:
                // лидерам и душам с историей — CANON S20.1 не про толпу).
                SoldierRecord rec{};
                rec.kind = std::uint16_t(spec.leaderType);
                rec.level = 1;
                if (!spec.members.push(rec)) break;
            }
            const entt::entity ent =
                spawn_squad(gs, *mw.world, *mw.terrain, spec);
            if (ent != entt::null) {
                s.population -= 1 + spec.members.size();
                ++raised;
                auto& prt = reg.get<ecs::MacroNpcRuntime>(ent);
                prt.squadType = myType;
                prt.errandObject = myObject;
                // Сезонный груз содержания вместо провианта на рейс: еда —
                // баланс окна теперь, рейсовый ломоть умер у артелей
                // (остался у вылазок гарнизона — они не подсудны суду
                // состава).
                load_season_upkeep(s, ent);
                // ТАКТ 2 для новорождённой артели: то же стойло, тот же
                // закон упряжки — дом снаряжает её тяглом, если оно есть.
                outfit_crew_mounts(*mw.world, s, ent);
            }
        }
        for (int si = 0; si < soloCount; ++si) {
            // Живой одиночка (курьер дани) продолжает службу — его строка
            // закрыта им самим; новый — только на границе.
            if (claim_standing(std::uint16_t(ld.crews[solo[si]].npc))
                != entt::null) {
                continue;
            }
            if (!boundary || souls_home(s) <= 0) continue;
            // ДУША КУРЬЕРА — ИЗ ТОГО ЖЕ ПУЛА (labour.h): соло-рождение
            // прежде шло мимо всякого счёта рук вовсе (npc_ai.cpp:5124 в
            // переписи девяти половин, §55) — «население <= 0» и всё.
            if (soloBudget <= 0) continue;
            SquadSpec spec{};
            spec.leaderType = ld.crews[solo[si]].npc;
            spec.x = s.x;
            spec.y = s.y;
            spec.homeSettlementId = s.id;
            if (spawn_squad(gs, *mw.world, *mw.terrain, spec)
                != entt::null) {
                s.population -= 1;
                --soloBudget;
                ++raised;
            }
        }
        // ЛИШНИЙ СКВАД — «просто распускает» (S19.2, суд границы): стоящей
        // дома артели не досталось СТРОКИ (гейт закрылся, строку держит
        // полевая, пул ужался до меньшего числа сквадов) — души и остатки
        // домой. Вне границы неприкаянная артель просто стоит до суда.
        if (boundary) {
            for (auto& [r2, e2] : idleByRow) {
                if (r2 != int(row) || e2 == entt::null) continue;
                dissolve_population_crew(e2, s);
                e2 = entt::null;
            }
        }
    }
    return raised;
}

void bucket_reset(CellBuckets& g, int mapW, int mapH, int cellSize) {
    g.cellSize = std::max(1, cellSize);
    g.cols = std::max(1, (mapW + g.cellSize - 1) / g.cellSize);
    g.rows = std::max(1, (mapH + g.cellSize - 1) / g.cellSize);
    const std::size_t n = std::size_t(g.cols) * std::size_t(g.rows);
    // assign() over the SAME size keeps the capacity, so a grid that is not
    // resized never allocates again after its first build.
    g.begin.assign(n + 1, 0u);
    g.cursor.assign(n, 0u);
}

void bucket_count(CellBuckets& g, int gx, int gy) {
    // Counts land at begin[cell + 1] so the prefix pass can sum in place.
    ++g.begin[g.cell_of(gx, gy) + 1];
}

void bucket_prefix(CellBuckets& g, std::size_t itemCount) {
    for (std::size_t i = 1; i < g.begin.size(); ++i) g.begin[i] += g.begin[i - 1];
    g.items.resize(itemCount);
    for (std::size_t i = 0; i < g.cursor.size(); ++i) g.cursor[i] = g.begin[i];
}

void bucket_scatter(CellBuckets& g, int gx, int gy, std::uint32_t item) {
    const std::size_t c = g.cell_of(gx, gy);
    g.items[g.cursor[c]++] = item;
}

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize) {
    g.trees = &trees;
    CellBuckets& b = g.grid;
    bucket_reset(b, mapW, mapH, cellSize);
    for (const TreePoint& t : trees) {
        bucket_count(b, wrapi(t.x / b.cellSize, b.cols),
                     wrapi(t.y / b.cellSize, b.rows));
    }
    bucket_prefix(b, trees.size());
    for (std::uint32_t i = 0; i < trees.size(); ++i) {
        bucket_scatter(b, wrapi(trees[i].x / b.cellSize, b.cols),
                       wrapi(trees[i].y / b.cellSize, b.rows), i);
    }
}

void reset_macro_npc_ai_runtime(MacroNpcAiRuntime& runtime,
                                std::uint32_t seed) {
    runtime = MacroNpcAiRuntime{};
    runtime.jitter = Rng{seed ^ 0xA1F0u};
    // Аллокация на сборке мира, не в тике (DOD п.4): скрэтчи закона порядка
    // (squad_walk.h) греются до капа один раз, свипы дальше zero-alloc.
    runtime.sweepOrder.reserve(kWorldSquads);
    runtime.squadIndex.order.reserve(kWorldSquads);
}

void build_squad_index(SquadIndex& g, ecs::World& w, int mapW, int mapH,
                       int cellSize) {
    CellBuckets& b = g.grid;
    bucket_reset(b, mapW, mapH, cellSize);

    // Every live macro squad — INCLUDING the player's (owner, 2026-08-29:
    // «игрок ничем не особенен», one law of sight for all). His squad is
    // perceived through this index at the same kSquadSightCells as anyone;
    // what stays special is only the MEETING, which belongs to Inc 6's
    // forced-encounter door (squad_threat_step stops short of auto-battling
    // a player-controlled squad). The Dead are no squads at all.
    auto view = w.reg.view<ecs::MacroCell, ecs::NPCKind,
                           ecs::MacroNpcRuntime>(
        entt::exclude<ecs::Dead>);
    // ОДИН проход по view — в порядок закона (squad_walk.h), потом count и
    // scatter идут по собранному: содержимое бакета отсортировано по
    // ординалу, и читатели «первого подходящего» (threat step, охота)
    // перестают зависеть от внутренностей EnTT. Скрэтч — член, пересборка
    // на свип по-прежнему аллокаций не делает.
    collect_squads_by_ordinal(w.reg, view, g.order);
    for (const SquadWalkEntry& s : g.order) {
        const auto& c = w.reg.get<ecs::MacroCell>(s.e);
        bucket_count(b, wrapi(ecs::cell_x(c, mapW) / b.cellSize, b.cols),
                     wrapi(ecs::cell_y(c, mapW) / b.cellSize, b.rows));
    }
    bucket_prefix(b, g.order.size());
    for (const SquadWalkEntry& s : g.order) {
        const auto& c = w.reg.get<ecs::MacroCell>(s.e);
        bucket_scatter(b, wrapi(ecs::cell_x(c, mapW) / b.cellSize, b.cols),
                       wrapi(ecs::cell_y(c, mapW) / b.cellSize, b.rows),
                       std::uint32_t(entt::to_integral(s.e)));
    }
}

// ONE assembly of the AI think's view (canon-audit H2: this block used to
// exist twice, line for line, one copy per driver, and the copies drifted —
// the budgeted twin once shipped without `deposits`, so every miner in the
// world stopped digging while the player was underground). The layer envelope
// arrives assembled by its owner; this adds only the drive-state.
static TickContext make_tick_context(MacroWorld& mw,
                                     MacroNpcAiRuntime& runtime,
                                     bool allowAutoBattle) {
    TickContext ctx{};
    ctx.mw      = mw;
    ctx.mapW    = mw.gs->mapW;
    ctx.mapH    = mw.gs->mapH;
    ctx.rng     = &runtime.jitter;
    // The player is a squad: WHERE he is = his flag holder's cell, the same
    // one-number truth every squad keeps (подпосадка 4).
    if (mw.world) {
        if (const ecs::MacroCell* pc = player_flag_cell(*mw.world)) {
            ctx.playerX = float(ecs::cell_x(*pc, ctx.mapW));
            ctx.playerY = float(ecs::cell_y(*pc, ctx.mapW));
        }
    }
    ctx.squads  = &runtime.squadIndex;
    ctx.allowAutoBattle = allowAutoBattle;
    // Запечь враждебность реестра на свип (CANON S10 «хищник-жертва»): F²
    // вопросов к ОДНОЙ матрице раз в свип — копейки, и охота по следу читает
    // бит вместо строкового слота отношений на каждом think.
    for (int a = 0; a < kFactionCount; ++a) {
        std::uint64_t m = 0;
        for (int b = 0; b < kFactionCount; ++b) {
            if (a != b && factions_hostile(mw.gs, kFactionDefs[a].id,
                                           kFactionDefs[b].id))
                m |= 1ull << b;
        }
        ctx.factionHostileMask[a] = m;
    }
    return ctx;
}

namespace {

// THE settlement of the dead, shared by both tick drivers (AI-2; owner
// 2026-09-10: «мёртвые не должны стоять вообще», survivors to the pool
// UNIVERSALLY). The pool CAN refuse (its slot ceiling) — and a refusal used
// to leave the dead lord's band standing until the daily rotation returned
// dead souls to a village as living population. Now the refusal UNLOADS the
// pool on the spot through the very door the daily sim uses
// (raise_deserter_bands: the freshest men walk off as a bandit band), then
// drains again. Terminates by conservation: every pass either fails to
// raise (spawn refused — the honest stderr case) or moves at least one man
// out of a dead roster. Spawning here is safe: both drivers call this AFTER
// their sweep, где никто не держит ссылок на компоненты (грабля посадки 4:
// спавн реаллоцирует хранилище).
void settle_dead_squads(MacroWorld& mw) {
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    drain_dead_leader_squads(w, gs.deserterPool);
    destroy_dead_macro_squads(w, &gs.lootPoolValue);
}

} // namespace

void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks,
                       bool allowAutoBattle) {
    if (!mw.gs || !mw.world) return;   // no world, no thinking (fail-closed)
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    auto& reg = w.reg;
    auto view = reg.view<ecs::MacroCell, ecs::NPCKind,
                         ecs::MacroNpcRuntime, ecs::Pools>(
        entt::exclude<ecs::Dead, ecs::PlayerTag, ecs::PlayerSquadTag>);  // never AI-drive the player: the flag OR his own squad

    build_squad_index(runtime.squadIndex, w, gs.mapW, gs.mapH);

    // Свежесть запечённой навигации — раз на свип, не в шаге (тор-закон
    // gigahrush2 «never re-bake per tick»: перепёк только на границах).
    if (mw.nav) nav_ensure(mw, *mw.nav);
    scent_ensure(gs.scent, gs.mapW, gs.mapH);

    TickContext ctx = make_tick_context(mw, runtime, allowAutoBattle);
    scent_player_deposit(ctx);   // игрок следит наравне со всеми (CANON S10)

    // Свип делит ОДИН RNG на всех — порядок обхода есть закон мира
    // (squad_walk.h): по ординалу, не по кишке EnTT.
    collect_squads_by_ordinal(reg, view, runtime.sweepOrder);
    for (const SquadWalkEntry& sw : runtime.sweepOrder) {
        const entt::entity e = sw.e;
        auto& cell = reg.get<ecs::MacroCell>(e);
        auto& kind = reg.get<ecs::NPCKind>(e);
        auto& rt   = reg.get<ecs::MacroNpcRuntime>(e);
        auto& hp   = reg.get<ecs::Pools>(e);

        // One think per call at most, as before: a caller that hands over a
        // huge jump does not get a burst of catch-up thinking, it gets one.
        rt.tickAccum += std::uint32_t(std::min<std::uint64_t>(ticks, kAiTicks));
        if (rt.tickAccum < kAiTicks) continue;
        rt.tickAccum -= kAiTicks;

        if (kind.type >= std::uint16_t(NPCType::Count)) continue;
        // A battle earlier in this very sweep may have killed this squad —
        // the view's Dead exclusion was evaluated at entry, so re-check.
        if (reg.all_of<ecs::Dead>(e)) continue;
        const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
        if (gate == ThinkGate::Dead) continue;
        refresh_overload_cost(rt, reg.try_get<ecs::NpcInventory>(e));
        // Decode → think in fractional scratch → encode (the scale split):
        // the STORE is one whole-cell number; the march's own float math
        // lives only on this think's stack.
        MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                   float(ecs::cell_y(cell, gs.mapW))};
        const float x0 = p.x, y0 = p.y;
        if (gate == ThinkGate::Think)
            dispatch(e, p, kind, rt, hp, ctx);
        settle_march_rhythm(e, p, rt, hp, p.x != x0 || p.y != y0, ctx);
        cell.idx = ecs::cell_index(int(p.x), int(p.y), gs.mapW);
    }

    // The END of every dead squad's story, once per tick (CANON S4): any
    // stragglers' survivors to the pool (idempotent for the already-drained;
    // it also catches a dead=1 roster a save carried across the sweep
    // window), then the drained corpse-rows leave the map. Deferred to HERE
    // because the settle doors' callers still hold the entities mid-tick.
    settle_dead_squads(mw);
}

void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt) {
    if (mapW <= 0 || mapH <= 0 || dt <= 0.0f) return;

    // No player exclusion (подпосадка 4, owner: «универсально без игрокового
    // кода»): his squad and a possessed lord glide by the SAME law as every
    // sprite on the map — the walker moves the cell, this pass moves the eye.
    auto view = w.reg.view<ecs::MacroCell, ecs::MacroVisual,
                           ecs::MacroNpcRuntime, ecs::Pools>(
        entt::exclude<ecs::Dead>);
    for (auto e : view) {
        const auto& c = view.get<ecs::MacroCell>(e);
        const MacroPos p{float(ecs::cell_x(c, mapW)),
                         float(ecs::cell_y(c, mapW))};
        auto& v = view.get<ecs::MacroVisual>(e);
        const auto& rt = view.get<ecs::MacroNpcRuntime>(e);
        const auto& hp = view.get<ecs::Pools>(e);
        if (hp.hp <= 0 || !std::isfinite(v.vx) || !std::isfinite(v.vy)) {
            v.vx = p.x;
            v.vy = p.y;
            v.speed = 0.0f;
            continue;
        }

        const float dx = p.x - v.vx;
        const float dy = p.y - v.vy;
        const float dSq = dx * dx + dy * dy;
        // The BACKSTOP, not the smoothing: only true jumps — a teleporter's
        // hop, a torus seam remap, a snapshot restore — land further than six
        // cells from where the eye last saw the body, and those must not be
        // walked across.
        //
        // Deliberately generous, and deliberately NOT re-derived from the pace.
        // The number was sized when a think covered ~3 cells (the old 32
        // cells/h); a think is under one cell now, so it looks 6× too big — and
        // tightening it would be treating the symptom of a bug that lived
        // elsewhere. A visual DESYNC used to accumulate here until it tripped
        // this line, which is why the bound felt load-bearing; the cause was
        // the caller feeding one tick of dt while the world lived several
        // (app/main.cpp frame(), fixed 2026-09-09). With the smoothing honest,
        // this only ever sees real teleports, and a tight bound would start
        // snapping the fast legal marches a quick leader is entitled to.
        if (dSq > 36.0f) {
            v.vx = p.x;
            v.vy = p.y;
            v.speed = 0.0f;
            continue;
        }

        const float speed = rt.visualSpeed > 0.0f ? rt.visualSpeed : 2.0f;
        v.speed = speed;
        const float step = speed * dt;
        if (step <= 0.0f || dSq <= 0.000001f) continue;

        const float d = std::sqrt(dSq);
        if (d <= step) {
            v.vx = p.x;
            v.vy = p.y;
        } else {
            const float ratio = step / d;
            v.vx += dx * ratio;
            v.vy += dy * ratio;
        }
    }
}

MacroNpcAiSliceResult tick_macro_npc_ai_budgeted(
    MacroWorld& mw,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks,
    bool allowAutoBattle) {
    MacroNpcAiSliceResult result{};
    if (max_npc_ticks <= 0) return result;
    if (!mw.gs || !mw.world) return result;  // no world, no thinking
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;

    constexpr int kMaxQueuedSweeps = 4;
    if (ticks > 0) {
        runtime.sweepAccum +=
            std::uint32_t(std::min<std::uint64_t>(ticks, kAiTicks * kMaxQueuedSweeps));
        while (runtime.sweepAccum >= kAiTicks) {
            runtime.sweepAccum -= kAiTicks;
            if (runtime.pendingSweeps < kMaxQueuedSweeps) {
                ++runtime.pendingSweeps;
            } else {
                result.backlog = true;
            }
        }
    }
    if (runtime.pendingSweeps <= 0) return result;

    auto& reg = w.reg;
    auto view = reg.view<ecs::MacroCell, ecs::NPCKind,
                         ecs::MacroNpcRuntime, ecs::Pools>(
        entt::exclude<ecs::Dead, ecs::PlayerTag, ecs::PlayerSquadTag>);  // never AI-drive the player: the flag OR his own squad

    build_squad_index(runtime.squadIndex, w, gs.mapW, gs.mapH);

    // The same ONE assembly as the map-view driver. This used to be a paste
    // that had drifted (the deposits epitaph now lives on TickContext itself,
    // npc_ai.h); the two drivers may differ in HOW they walk the entities —
    // never in what world the entities think about (CANON.md S2).
    if (mw.nav) nav_ensure(mw, *mw.nav);
    scent_ensure(gs.scent, gs.mapW, gs.mapH);
    TickContext ctx = make_tick_context(mw, runtime, allowAutoBattle);
    scent_player_deposit(ctx);   // игрок следит наравне со всеми (CANON S10)

    // Тот же закон порядка, что у карт-драйвера (squad_walk.h): курсор —
    // позиция В ЭТОМ порядке. Лист собран на вызов; умерший внутри свипа
    // отсеивается проверкой Dead ниже, ровно как раньше.
    collect_squads_by_ordinal(reg, view, runtime.sweepOrder);

    while (runtime.pendingSweeps > 0
           && result.npcsProcessed < max_npc_ticks) {
        bool reachedEnd = true;

        for (std::size_t i = runtime.sweepCursor;
             i < runtime.sweepOrder.size(); ++i) {
            const entt::entity e = runtime.sweepOrder[i].e;

            auto& cell = reg.get<ecs::MacroCell>(e);
            auto& kind = reg.get<ecs::NPCKind>(e);
            auto& rt   = reg.get<ecs::MacroNpcRuntime>(e);
            auto& hp   = reg.get<ecs::Pools>(e);
            if (kind.type < std::uint16_t(NPCType::Count)
                && !reg.all_of<ecs::Dead>(e)) {   // may have died this sweep
                const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
                if (gate != ThinkGate::Dead) {
                    refresh_overload_cost(rt,
                                          reg.try_get<ecs::NpcInventory>(e));
                    // Decode → fractional scratch → encode (the scale split).
                    MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                               float(ecs::cell_y(cell, gs.mapW))};
                    const float x0 = p.x, y0 = p.y;
                    if (gate == ThinkGate::Think) {
                        dispatch(e, p, kind, rt, hp, ctx);
                    }
                    settle_march_rhythm(e, p, rt, hp,
                                        p.x != x0 || p.y != y0, ctx);
                    cell.idx = ecs::cell_index(int(p.x), int(p.y), gs.mapW);
                }
                ++result.npcsProcessed;
            }
            ++runtime.sweepCursor;

            if (result.npcsProcessed >= max_npc_ticks) {
                reachedEnd = false;
                break;
            }
        }

        if (runtime.sweepOrder.empty()) {
            runtime.pendingSweeps = 0;
            runtime.sweepCursor = 0;
            break;
        }
        if (!reachedEnd) break;

        --runtime.pendingSweeps;
        ++result.sweepsCompleted;
        runtime.sweepCursor = 0;
    }
    // Same end-of-tick settlement as the map-view driver above (S4): the
    // macro clock ticks underground too, and a lord felled down there must
    // leave the map by the same law. (The positional sweep cursor already
    // tolerates the view shrinking — every death mid-sweep shrinks it.)
    settle_dead_squads(mw);
    result.backlog = result.backlog || runtime.pendingSweeps > 0;
    return result;
}

} // namespace sm

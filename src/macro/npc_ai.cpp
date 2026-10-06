// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "core/stacks.h"           // kWorldSquads — резерв скрэтчей порядка
#include "macro/upkeep_window.h"   // ОДИН суд границы на всякий контейнер
#include "macro/agent_memory.h"
#include "macro/characters.h"  // стол анкет — ступень лестницы поведения
#include "macro/chronicle.h"
#include "macro/currency.h"
#include "macro/economy.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/macro_stock.h"
#include "macro/entry_context.h"
#include "tables/faction.h"
#include "macro/labour.h"           // ОДИН пул рук места (CANON S4)
#include "macro/landmark_iter.h"    // for_each_place — обход мест по оси рода
#include "macro/landmark_registry.h"
#include "macro/movement_cost.h"
#include "tables/npc.h"
#include "macro/nav_field.h"        // локальные поля-округи (CANON S7)
#include "macro/player_entity.h"
#include "macro/npc_spawn.h"
#include "macro/recovery.h"  // recover_bar — ОДНА дверь отдыха на все тела
#include "tables/seasons.h"   // season_boundary — окно сквадов (CANON S19.2)
#include "macro/politik.h"          // derive_city_spacing — времянка §34.1
#include "macro/settlement_score.h" // kSettlementReach — the home-field box
#include "macro/spawners.h"
#include "macro/squad.h"
#include "macro/store.h"
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

// Store свипа — из конверта (make_tick_context кладёт адрес в ctx.mw.store);
// фикстура, зовущая публичную дверь напрямую, несёт store в ctx мира —
// фоллбек читает его оттуда. Звонящий обязан был проверить ctx.mw.world.
inline MacroStore& store_mw(const MacroWorld& mw) {
    return mw.store ? *mw.store : store_of(mw.world->reg);
}
inline MacroStore& store_ctx(const TickContext& ctx) {
    return store_mw(ctx.mw);
}

// ── РЕЗОЛВ МЕСТА В ДУМКЕ: ОРДИНАЛ → СЛОТ (ломтик F) ─────────────────────
// Думка зовёт этот резолв по нескольку раз за think, поэтому он обязан идти
// БИНАРНЫМ поиском по порядку закона, который драйв уже собрал
// (SquadIndex.order, ординально отсортирован) — скан капа здесь был бы
// O(кап) на каждый think, то есть ровно та граница, которую §6 запрещает.
// Фикстура, водящая одну думку без каркаса, падает в скан — у неё порядка
// нет, и это её цена, не цена мира.
// Ось рода гейтится здесь: пространство ординалов ОДНО (M-37), и «это
// место» отвечает только ось тела.
inline std::uint16_t place_slot_by_id(const TickContext& ctx, int id) {
    if (id <= 0) return kMacroNoSlot;   // 0 = «никого» (ЗАКОН НУЛЯ-ОРДИНАЛА)
    const MacroStore& st = store_ctx(ctx);
    const MacroHandle h = ctx.squads
        ? macro_handle_by_spawn_id(st, ctx.squads->order, std::uint32_t(id))
        : macro_handle_by_spawn_id(st, std::uint32_t(id));
    if (!st.valid(h)) return kMacroNoSlot;
    return is_settlement_kind(SquadType(st.runtime[h.slot].squadType))
        ? h.slot : kMacroNoSlot;
}

// Клетка слота парой координат — геометрия марша и округи (ЗАКОН АДРЕСА:
// пара законна там, где идёт ГЕОМЕТРИЯ, а не доступ к полю).
inline int slot_x(const MacroStore& st, std::uint16_t slot, int mapW) {
    return ecs::cell_x(st.cell[slot], mapW);
}
inline int slot_y(const MacroStore& st, std::uint16_t slot, int mapW) {
    return ecs::cell_y(st.cell[slot], mapW);
}

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
    // ОДИН эмитент ординалов (M-37): ординал и есть имя места.
    const std::uint16_t slot = place_slot_by_id(ctx, rt.homeSettlementId);
    if (slot == kMacroNoSlot) return false;
    const MacroStore& st = store_ctx(ctx);
    out = {float(slot_x(st, slot, ctx.mapW)),
           float(slot_y(st, slot, ctx.mapW))};
    return true;
}

// ДОМ АРТЕЛИ — склад, куда ложится груз (home_inventory).
// Отдаёт СЛОТ тела места; kMacroNoSlot = дома нет.
std::uint16_t home_place_slot(const ecs::MacroNpcRuntime& rt,
                              const TickContext& ctx) {
    return ctx.mw.gs ? place_slot_by_id(ctx, rt.homeSettlementId)
                     : kMacroNoSlot;
}

Inventory* home_inventory(const ecs::MacroNpcRuntime& rt,
                          const TickContext& ctx) {
    const std::uint16_t slot = home_place_slot(rt, ctx);
    return slot == kMacroNoSlot ? nullptr
                                : &store_ctx(ctx).inventory[slot].inv;
}

// Empty the gatherer's own bag of the catalog row `defIdx` into his home
// store — the shared arrival half of every honest work-loop (woodcutter,
// farmer). ОРДИНАЛ, А НЕ СТРОКА (ЗАКОН СЛОВАРЯ): строка цели переводится в
// ординал один раз рядом со своей таблицей (gatherer_item_index).
void deliver_bag_home(MacroHandle self, const ecs::MacroNpcRuntime& rt,
                      const TickContext& ctx, int defIdx) {
    if (!ctx.mw.world) return;
    Inventory& bag = store_ctx(ctx).inventory[self.slot].inv;
    const int n = bag.count_of(defIdx);
    Inventory* store = home_inventory(rt, ctx);
    // Credit BEFORE debit (CANON S10 (бывший economy.md)'s conservation law): the store accepts
    // first, the bag pays only what was accepted — a full store leaves the
    // haul ON THE GATHERER'S BACK instead of burning it. (Near-unreachable
    // with 1024 slots and stack-merging, but the law is the law.)
    if (n > 0 && store && store->add_of(defIdx, n)) {
        bag.remove_of(defIdx, n);
        // The arrival IS the gather flow: the pure econ steps announce their
        // own facts, but the agent work-loop lands its haul here — without
        // this fact every *_gathered column of the дубль-прогон reads zero
        // (measured: 4 years × 4 seeds of zeros, 2026-08-31).
        if (ctx.mw.econFacts) {
            EconFact f{};
            f.kind = EconFact::Kind::Gathered;
            f.commodity = commodity_of_item(defIdx);
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

void settle_exhaustion(MacroHandle e, const MacroPos& p,
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
void settle_march_rhythm(MacroHandle e, const MacroPos& p,
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
        && pc->height16.size() == pc->costGrid.size()) {
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
// самую выгодную цель», CANON S10 аукцион; CANON S10 (бывший resources.md)). Idle → find the
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
    // ложится в КОНТЕЙНЕР, а не в сумку»): Count = обычный товар в сумку;
    // строка существа = добытое встаёт ДУШОЙ в контейнер артели, спина сразу
    // в обозе (refresh_squad_carry). Овцы — следующая такая же строка.
    NPCType creatureYield = NPCType::Count;
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
    // не товар, а существо (колонка creatureYield).
    {ResourceFieldId::Horses, nullptr,  Worksite::HomePasture,
     1, NPCType::Horse},
};
constexpr int kGathererGoalCount =
    int(sizeof(kGathererDefs) / sizeof(kGathererDefs[0]));

// ОРДИНАЛ ВЫХОДА СТРОКИ ЦЕЛИ — РЕЗОЛВ ОДИН РАЗ, РЯДОМ СО СВОЕЙ ТАБЛИЦЕЙ
// (ЗАКОН СЛОВАРЯ И ОРДИНАЛА п.1: строка живёт на границе, внутри мира — число;
// образец — `r.needIdx` в econ_day.cpp). -1 у строки, чей выход СУЩЕСТВО
// (`commodity == nullptr`): у него нет каталожной строки товара, он встаёт
// душой в контейнер. Резолв ленивый, потому что каталог виден только своей
// единице трансляции — `constexpr` тут недостижим, а вывод из ТОЙ ЖЕ строки
// сохранён: разъехаться с таблицей эта колонка не может по построению.
int gatherer_item_index(int goal) {
    static const std::array<int, std::size_t(kGathererGoalCount)> kOut = [] {
        std::array<int, std::size_t(kGathererGoalCount)> m{};
        for (int g = 0; g < kGathererGoalCount; ++g) {
            m[std::size_t(g)] = kGathererDefs[g].commodity
                ? item_index(kGathererDefs[g].commodity) : -1;
        }
        return m;
    }();
    return (goal >= 0 && goal < kGathererGoalCount)
        ? kOut[std::size_t(goal)] : -1;
}

// Та же строка цели ординалом ТОВАРА — для фактов и прейскуранта.
int gatherer_commodity_index(int goal) {
    return commodity_of_item(gatherer_item_index(goal));
}

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
    // hand's own box (kNavHandReach). «Где ближайшая жила моей округи»
    // уезжало отсюда в ОПИСЬ МЕСТА, а опись уничтожена 2026-09-30 (ломтик E
    // шаг 2, наряд M-191) — значит эта дверь снова ЕДИНСТВЕННАЯ, и её бокс
    // есть весь горизонт артели. Дыра названа в CANON S10 «КАРТА ОКРУГИ». A field
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
int haul_between(Inventory& from, Depot to, int defIdx,
                 int maxUnits, float capacityLeftKg);

// Приёмник-МЕСТО (CANON S10): склад + счёт + канал фактов мира — дверь
// прихода гасит долг СРАЗУ тем, что упало. Сумки в Depot не заворачиваются
// (неявная конверсия из Inventory&, долга нет).
inline Depot depot_(std::uint16_t slot, const MacroWorld& mw) {
    MacroStore& st = store_mw(mw);
    return Depot(st.inventory[slot].inv, st.upkeep[slot].needDebt,
                 mw.econFacts, mw.econFactsUser);
}

// The sell-run machine (defined with the trade behaviours below): the
// peasant crew whose errand is Sell walks the SAME machine the vendor
// walked — reuse, not a second copy (CANON S26).
void ai_vendor(MacroHandle self, MacroPos& p,
               ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx);

void ai_gatherer(MacroHandle self, MacroPos& p,
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
    // Выход строки цели ОРДИНАЛОМ, один резолв на таблицу (ЗАКОН СЛОВАРЯ):
    // -1 = выход существо, у него каталожной строки товара нет.
    const int outIdx = gatherer_item_index(int(rt.errandObject));

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // ПОЛНАЯ СПИНА ИДЁТ ДОМОЙ (S10 «возвращается, кладёт на склад»):
            // отдых посреди дорогого маршрута будит артель в Idle, а Idle
            // слал её к жиле — даже с грузом, которому на жиле нечего взять.
            // Вечный челнок «шахта→привал→шахта» держал 536 серебра в одной
            // сумке 48 дней при пустом складе (измерено, сид 7).
            if (const Inventory* bagIdle = def->creatureYield == NPCType::Count
                    && ctx.mw.world
                    ? &store_ctx(ctx).inventory[self.slot].inv
                    : nullptr) {
                // (a CREATURE yield rides the creatures, not the bag — there
                // is no «full back» to send home early)
                const ItemDef* idef = item_def_at(outIdx);
                const float unitKg =
                    idef && idef->weight > 0.0f ? idef->weight : 1.0f;
                if (bagIdle->count_of(outIdx) > 0
                    && rt.carryCap - inventory_weight(*bagIdle)
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
                // ОПИСЬ ОКРУГИ УНИЧТОЖЕНА (ломтик E шаг 2): жила ищется
                // боксом руки своей округи. Это ЗАВЕДОМО УЖЕ прежнего
                // ответа — и дыра названа вслух в CANON S10 («КАРТА
                // ОКРУГИ»), а не залатана здесь радиусом: закон, упершийся
                // в цену поиска, лечится сносом поиска, не подпоркой.
                found = find_home_deposit(ctx, def->row, home, site);
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
                Inventory* bag = &store_ctx(ctx).inventory[self.slot].inv;
                // Руки — ЛЮДИ: лошадь в контейнере — спина и рот, не рука
                // (count_human_souls, world_row.h) — иначе пойманный табун
                // сам становился бы добытчиком и контур шёл вразнос.
                const int workers = production_hands(
                    bag ? count_human_souls(*bag) : 0);
                // «Берёт ПО СВОЕЙ ГРУЗОПОДЪЁМНОСТИ» — CANON S10 дословно:
                // спины сквада ограничивают тейк. Без этой скобы артель
                // грузила цикл×души невзирая на вес и каменела перегрузом
                // на обратном пути НАВСЕГДА (наценка 256 SP/клетку при баре
                // 110 неоплатна и после полного отдыха — измерено: рудокоп
                // с 2400 кг серебра на спине в 2145 кг, сид 7).
                int carryMax = have;
                if (bag && def->creatureYield == NPCType::Count) {
                    const ItemDef* idef = item_def_at(outIdx);
                    const float unitKg =
                        idef && idef->weight > 0.0f ? idef->weight : 1.0f;
                    const float freeKg =
                        rt.carryCap - inventory_weight(*bag);
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
                // «ВЫХОД ЛОЖИТСЯ В КОНТЕЙНЕР, А НЕ В СУМКУ» (CANON S10,
                // дословно): существо встаёт ДУШОЙ в отряд — генерик-стаком
                // по закону слота — и его спина сразу считается в обозе
                // (refresh_squad_carry, та же дверь, что у добора). Credit
                // BEFORE debit: поле платит только за вставших.
                if (take > 0 && def->creatureYield != NPCType::Count) {
                    // ПОТОЛКА ЛОВЛИ НЕТ, И ТОРМОЗА ТОЖЕ НЕТ — ДЫРА НАЗВАНА
                    // (M-230, вердикт владельца 2026-10-06 «сноси
                    // полностью»). Прежде ловца останавливала ЦЕНА: кривая
                    // дефицита над табуном, чья НУЖДА была мерой упряжки
                    // «по лошадке на душу». Мера снесена вместе с законом,
                    // и цена цели теперь плоская (цена найма строки), то
                    // есть поголовье растёт, пока в поле есть звери. Нож
                    // излишка отменён владельцем, рацион поголовье не
                    // режет. Обратная связь — отдельный наряд; костыля на
                    // её место не ставим (AGENTS §1).
                    if (take > 0 && bag && creatures_push_stack(
                            *bag, def->creatureYield,
                            npc_def(def->creatureYield).baseLevel,
                            take)) {
                        resource_field_apply(mw, def->row, tx, ty, -take);
                        pools.spCarry -= float(cycleCost);
                        settle_sp_carry(pools);
                        refresh_squad_carry(store_ctx(ctx), self);
                    } else {
                        tookSomething = false;   // no slot — nothing conjured
                    }
                }
                // Credit BEFORE debit (CANON S5): the field pays only what
                // the OWN bag actually took — a bagless walker, or a bag
                // with no room, drains nothing and writes no Drained fact.
                // `outIdx >= 0` сказано ВСЛУХ: ординальная дверь `add_of`
                // на -1 отвечает «положил» молча (ничего не положив), и без
                // этой проверки строка без выхода-товара опустошала бы поле
                // в пустоту. Строковая дверь на этом месте падала в UB
                // (std::string из nullptr) — тот же дефект, только громче.
                else if (take > 0 && bag && outIdx >= 0
                         && bag->add_of(outIdx, take)) {
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
                        record_landmark_fact(store_ctx(ctx), *ctx.mw.gs,
                                             FactKind::Drained,
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
                const Inventory* bagNow = ctx.mw.world
                    ? &store_ctx(ctx).inventory[self.slot].inv
                    : nullptr;
                bool backsFull = false;
                if (bagNow && def->creatureYield == NPCType::Count) {
                    const ItemDef* idef = item_def_at(outIdx);
                    const float unitKg =
                        idef && idef->weight > 0.0f ? idef->weight : 1.0f;
                    backsFull =
                        rt.carryCap - inventory_weight(*bagNow) < unitKg;
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
                // Груз — на склад дома. Пойманное СУЩЕСТВО остаётся в
                // инвентаре артели: двухтактного обоза больше нет (M-230),
                // и у существа нет товарной колонки, поэтому сумка его и
                // не видит.
                if (outIdx >= 0)
                    deliver_bag_home(self, rt, ctx, outIdx);
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
// the road takes REAL cargo. What to haul is decided by the HOME ITSELF —
// its own shelf and its own season's bill, read live at the moment of the
// deal (the cached ledger died 2026-09-30, ломтик E шаг 2): by the time the
// crew gets back the answer may be stale, and that is a trader's life.
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
// INPUTS (cloth ← fibre, bricks ← clay, …), then every other
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
            const ItemDef* d = item_def_at(commodity_item_index(i));
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


int haul_between(Inventory& from, Depot to, int defIdx,
                 int maxUnits, float capacityLeftKg) {
    if (maxUnits <= 0 || capacityLeftKg <= 0.0f) return 0;
    const ItemDef* def = item_def_at(defIdx);
    const float unitKg = def && def->weight > 0.0f ? def->weight : 1.0f;
    const int byWeight = int(capacityLeftKg / unitKg);
    const int n = std::min({maxUnits, byWeight, from.count_of(defIdx)});
    if (n <= 0) return 0;
    // Credit before debit (CANON S5): a hold with no free slot refuses, the
    // cargo stays where it was, and "units moved" is never said of goods
    // that evaporated between two bags.
    if (!to.inv.add_of(defIdx, n)) return 0;
    from.remove_of(defIdx, n);
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
    // контейнеру мира стоил бы вдвое, а закон от порядка не зависит.
    // Без броска (дверь зовут без RNG) рулетка честно вырождается в ПЕРВЫЙ
    // вес — у запечённого мира это ближайший сосед, потому что порталы
    // округи отсортированы по цене.
    float total = 0.0f;
    int pickId = -1;
    const MacroStore& stp = store_ctx(ctx);
    const auto offer_ = [&](std::uint16_t slot) {
        const int cx = slot_x(stp, slot, ctx.mapW);
        const int cy = slot_y(stp, slot, ctx.mapW);
        const float days = days_to_(cx, cy);
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
            pickId = int(stp.spawnId[slot].index);
            outX = float(cx);
            outY = float(cy);
        }
    };
    if (!baked || std::size_t(here) >= R) {
        // МИР БЕЗ ДОРОГ (граф округ не запечён — синтетическая фикстура,
        // молодой мир): соседство спрашивается у ГЕОМЕТРИИ. Урна — все
        // места мира, потому что другого понятия соседства здесь нет.
        for_each_place(stp, [&](std::uint16_t slot) {
            const int id = int(stp.spawnId[slot].index);
            if (id == currentId || id == prevId) return;
            if (!landmark_is_settlement(SquadType(stp.runtime[slot].squadType))
                || souls_flock(*ctx.mw.gs, stp, slot) <= 0)
                return;
            offer_(slot);
        });
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
        const std::uint16_t slot = place_slot_by_id(ctx, lmId);
        if (slot == kMacroNoSlot
            || !landmark_is_settlement(SquadType(stp.runtime[slot].squadType))
            || souls_flock(*ctx.mw.gs, stp, slot) <= 0)
            continue;
        offer_(slot);
    }
    return pickId;   // -1 = тупик: рейс кончается, крю идёт домой
}

int market_price_seen(int commodityIdx) {
    if (commodityIdx < 0 || commodityIdx >= kCommodityCount) return 0;
    // ЧУЖОЙ РЫНОК ОЦЕНИВАЕТСЯ АБСОЛЮТНОЙ СТОИМОСТЬЮ СТРОКИ (CANON S10,
    // вердикт владельца 2026-09-30: «у нас есть абсолютная стоимость /
    // локальная цена от спроса предложения контекста»). Ярус 2 знания о
    // цене — ведомость места и мировое среднее из неё — УНИЧТОЖЕН вместе с
    // колонками места (ломтик E шаг 2), и на его дальнюю половину встало то
    // же по смыслу число: «дальнее место выглядит обычным рынком». Разница
    // в том, что оно больше не считается сезонным проходом по всем местам,
    // а лежит строкой каталога — то есть его неоткуда рассинхронизировать.
    //
    // ЛОКАЛЬНОЙ ПОЛОВИНЫ ЯРУСА 2 БОЛЬШЕ НЕТ, и это названная дыра, а не
    // умолчание: пока эконом-эпик не построит цену от спроса/предложения,
    // спрашивающий не видит ни дефицита, ни завала у соседа. СВОЙ дом при
    // этом виден точно — его склад и счёт читаются живьём там, где решение
    // принимается, и ярусом это никогда не было.
    const ItemDef* d = item_def_at(commodity_item_index(commodityIdx));
    return d ? d->value : 0;
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

// (Параметр `const Landmark& at` снят вместе со строкой места, ломтик F:
// у него не было ни одного читателя в теле — цену «там» даёт прейскурант
// каталога, а не адрес рынка.)
long long trade_bid_value_(const MacroWorld& mw, int fromX, int fromY,
                           const int* mine,
                           const int* homePrice, const int* homeLack) {
    long long value = 0;
    long long purse = 0;
    int therePrice[kCommodityCount];
    for (int c = 0; c < kCommodityCount; ++c) {
        therePrice[c] = market_price_seen(c);
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
int leader_trade_power_(const MacroStore& st, MacroHandle self) {
    const CharacterSheet sh = sheet_of(st, self);
    return calculate_derived(sh.attributes, sh.skills).tradeDiscountPct;
}

// ...и ТОРГОВАЯ СИЛА МЕСТА — та же дверь над анкетой ландмарка (S25: у
// сделки две макросущности, и место — полноправная сторона, а не «лавка»).
int landmark_trade_power_(const MacroStore& st, std::uint16_t slot) {
    const CharacterSheet& sh = st.sheet[slot];
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
        // Ординал уже В РУКАХ: слот несёт `sl.def`, и мост товара читается
        // обратным концом (commodity_of_item) — строка каталога в тик не
        // заходит вовсе.
        if (commodity_of_item(int(sl.def)) >= 0) {
            const int have = store.count_of(int(sl.def));
            const int demand = season_demand_for(int(sl.def), needDebt,
                                                 population, site, &store);
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
            haul_between(store, *bag, int(sl.def), fit,
                         capKg - inventory_weight(*bag));
    }
    return planned;
}

// (ЗДЕСЬ ЖИЛ ai_caravan — 163 строки рейса «город → город со станциями».
// Снесён 2026-09-21 вместе с родом NPCType::Caravan: караван оказался
// СКВАДОМ, притворившимся видом существа, и его обоз был вписан в породу
// лидера 32 спинами вопреки закону «обоз = сумма спин состава» (squad.h).
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
void ai_vendor(MacroHandle self, MacroPos& p,
               ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || !ctx.mw.world) {
        ai_nomad(p, rt, pools, ctx);
        return;
    }
    MacroStore& st = store_ctx(ctx);
    Inventory* bag = &st.inventory[self.slot].inv;
    // Плечо дома — колонки его ТЕЛА (M-90 шаг 5): один резолв слота на такт.
    const std::uint16_t homeSlot = place_slot_by_id(ctx, rt.homeSettlementId);
    if (homeSlot == kMacroNoSlot) {
        ai_home_wanderer(p, rt, pools, ctx);
        return;
    }
    Inventory& homeInv = st.inventory[homeSlot].inv;

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
            if (const std::uint16_t m2 =
                    place_slot_by_id(ctx, rt.targetSettlementId);
                m2 != kMacroNoSlot
                && landmark_is_settlement(
                       SquadType(st.runtime[m2].squadType))) {
                rt.targetX = float(slot_x(st, m2, ctx.mapW));
                rt.targetY = float(slot_y(st, m2, ctx.mapW));
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
        const std::uint16_t market =
            place_slot_by_id(ctx, int(rt.errandObject));
        if (market == kMacroNoSlot
            || !landmark_is_settlement(SquadType(st.runtime[market].squadType))
            || market == homeSlot) {
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
        const Skills& homeSite = st.sheet[homeSlot].skills;
        plan_home_load_(homeInv, st.upkeep[homeSlot].needDebt,
                        souls_home(st, homeSlot), homeSite, rt.carryCap,
                        bag);
        if (inventory_weight(*bag) <= 0.0f
            && inventory_value(*bag) <= 0) {
            // Nothing to sell and nothing owed: wait out the morning.
            rt.stateTimer = std::int16_t(40 + rand_int(ctx, 40));
            return;
        }
        rt.targetSettlementId = int(st.spawnId[market].index);
        rt.targetX = float(slot_x(st, market, ctx.mapW));
        rt.targetY = float(slot_y(st, market, ctx.mapW));
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
        if (const std::uint16_t market =
                place_slot_by_id(ctx, rt.targetSettlementId);
            market != kMacroNoSlot
            && landmark_is_settlement(
                   SquadType(st.runtime[market].squadType))) {
            // ЧТО ВЕЗТИ ДОМОЙ судит ВЕДОМОСТЬ ДОМА, а не память крю
            // (CANON S10, ярус 2): дом сам выписал свои цены точным
            // складом и своим счётом.
            const CaravanDeal deal = trade_vendor_at_market(
                st, *bag, rt.carryCap, market, handle_at(st, homeSlot),
                leader_trade_power_(st, self),
                landmark_trade_power_(st, market),
                ctx.mw.econFacts, ctx.mw.econFactsUser);
            if (deal.movedTableValue > 0) {
                record_landmark_fact(st, *ctx.mw.gs, FactKind::Traded,
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
            // ЧТО ДОМУ НУЖНО И ПОЧЁМ — СЧИТАЕТСЯ ЖИВЬЁМ ПО САМОМУ ДОМУ.
            // Кэш-ведомость (ярус 2) уничтожена 2026-09-30; закон не
            // изменился — те же season_demand_for и stock_price, которыми
            // она и выписывалась. Оговорка «крю не видит склад дома живьём»
            // была фикцией уже тогда: нехватка и здесь, и в сборщике
            // считалась ВЫЧИТАНИЕМ живого склада дома из кэша.
            int homePrice[kCommodityCount] = {};
            int homeLack[kCommodityCount] = {};
            int cargo[kCommodityCount] = {};
            const Skills& homeHands = st.sheet[homeSlot].skills;
            long long homeValue = 0;
            for (int c = 0; c < kCommodityCount; ++c) {
                const int id = commodity_item_index(c);
                const ItemDef* d = item_def_at(id);
                const int base = d ? d->value : 0;
                const int have = homeInv.count_of(id);
                const int demand =
                    season_demand_for(id, st.upkeep[homeSlot].needDebt,
                                      souls_home(st, homeSlot), homeHands,
                                      &homeInv);
                homePrice[c] = base > 0 ? stock_price(base, have, demand)
                                        : 0;
                const int lack = demand - have;
                homeLack[c] = lack > 0 ? lack : 0;
                cargo[c] = bag->count_of(id);
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
            const int nextId =
                pick_next_station_(ctx, p, int(st.spawnId[market].index),
                                   rt.prevStationId, nextX, nextY);
            float bidGo = 0.0f;
            const std::uint16_t next = place_slot_by_id(ctx, nextId);
            if (next != kMacroNoSlot && next != homeSlot) {
                const int nextX_ = slot_x(st, next, ctx.mapW);
                const int nextY_ = slot_y(st, next, ctx.mapW);
                const long long gain =
                    trade_bid_value_(ctx.mw, int(p.x), int(p.y),
                                     cargo, homePrice, homeLack);
                // ДЛИТЕЛЬНОСТЬ — ВЕСЬ ОСТАТОК РЕЙСА: туда И оттуда домой.
                // Иначе «дальше» дешевело бы по построению, и крю уходило бы
                // от дома бесконечно — знаменатель обязан расти с отъездом.
                const float daysGo =
                    march_days_(ctx, int(p.x), int(p.y), nextX_, nextY_)
                    + march_days_(ctx, nextX_, nextY_, int(home.x),
                                  int(home.y));
                if (daysGo > 0.0f)
                    bidGo = (float(gain)
                             - route_fear_(ctx, int(p.x), int(p.y),
                                           nextX_, nextY_))
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
            if (goOn && next != kMacroNoSlot) {
                rt.prevStationId = int(st.spawnId[market].index);
                rt.errandObject = st.spawnId[next].index;
                rt.targetSettlementId = int(st.spawnId[next].index);
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
                    haul_between(*bag, depot_(homeSlot, ctx.mw),
                                 commodity_item_index(i), 1 << 30, 1e9f);
                }
                transfer_value_dense(*bag, depot_(homeSlot, ctx.mw),
                               inventory_value(*bag));
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
void ai_collector(MacroHandle self, MacroPos& p,
                  ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
                  const TickContext& ctx) {
    XY home;
    if (!home_pos(rt, ctx, home) || !ctx.mw.world) {
        ai_nomad(p, rt, pools, ctx);
        return;
    }
    MacroStore& st = store_ctx(ctx);
    Inventory* bag = &st.inventory[self.slot].inv;
    // Плечо дома и вассала — колонки их ТЕЛ (M-90 шаг 5).
    const std::uint16_t homeSlot = place_slot_by_id(ctx, rt.homeSettlementId);
    if (homeSlot == kMacroNoSlot) {
        ai_nomad(p, rt, pools, ctx);
        return;
    }
    const std::uint16_t vassal = place_slot_by_id(ctx, int(rt.errandObject));
    if (vassal == kMacroNoSlot || vassal == homeSlot) {
        ai_home_wanderer(p, rt, pools, ctx);
        return;
    }
    const int vassalX = slot_x(st, vassal, ctx.mapW);
    const int vassalY = slot_y(st, vassal, ctx.mapW);
    Inventory& homeInv = st.inventory[homeSlot].inv;
    Inventory& vassalInv = st.inventory[vassal].inv;

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // Дома и с пустыми руками — идём за долгом; дома с грузом — сдаём.
        if (torus_dist_sq(p.x, p.y, home.x, home.y,
                          float(ctx.mapW), float(ctx.mapH)) >= 4.0f) {
            rt.targetX = float(vassalX);
            rt.targetY = float(vassalY);
            rt.state = std::uint8_t(NS::Traveling);
            return;
        }
        if (!owes_tithe(*ctx.mw.gs, st, vassal)) {
            // Должник рассчитался (собрали или простили) — ждать нечего,
            // ротация завтра переторгует эту строку заново.
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 8));
            return;
        }
        rt.targetSettlementId = int(st.spawnId[vassal].index);
        rt.targetX = float(vassalX);
        rt.targetY = float(vassalY);
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
        // Долг живёт на ФЕОДАЛЬНОМ РЕБРЕ строки фракции сюзерена (v121).
        TitheEdge* edge = tithe_edge_of(*ctx.mw.gs, st, vassal);
        long long owed = edge ? edge->owedValue : 0;
        long long took = 0;
        // ── СНАЧАЛА ПО НУЖДЕ ДОМА, ОСТАТОК — ПО ПЛОТНОСТИ ───────────────
        // Вердикт владельца 2026-09-21, дословно: «грузит ПО НУЖДЕ ДОМА,
        // остаток — по value_dense_order. Иначе сборщик везёт домой
        // серебро, а меряем мы еду». Это ровно то, что измерилось
        // 2026-09-22, когда порядок был только по плотности: город получал
        // казну и продолжал голодать (food_city −99.8 %).
        // Чего дому не хватает — считается по самому дому той же дверью
        // (ведомость-кэш уничтожена 2026-09-30, ломтик E шаг 2; закон и
        // обе его функции — те же).
        if (owed > 0) {
            const Skills& homeHands = st.sheet[homeSlot].skills;
            const auto home_demand_of = [&](int cid) {
                return season_demand_for(cid, st.upkeep[homeSlot].needDebt,
                                         souls_home(st, homeSlot), homeHands,
                                         &homeInv);
            };
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
                const int cid = commodity_item_index(c);
                const ItemDef* cd = item_def_at(cid);
                const int cbase = cd ? cd->value : 0;
                const int chave = homeInv.count_of(cid);
                const int cdemand = home_demand_of(cid);
                const int lk = cdemand - chave;
                urgency[c] = lk > 0 && cbase > 0
                    ? (long long)lk * stock_price(cbase, chave, cdemand)
                    : 0;
            }
            for (int a = 1; a < kCommodityCount; ++a)
                for (int b = a; b > 0
                                && urgency[order[b]] > urgency[order[b - 1]];
                     --b)
                    std::swap(order[b], order[b - 1]);
            for (int oi = 0; oi < kCommodityCount && owed > 0; ++oi) {
                const int c = order[oi];
                const int id = commodity_item_index(c);
                const ItemDef* d = item_def_at(id);
                const int base = d ? d->value : 0;
                if (base <= 0) continue;
                const int lack =
                    home_demand_of(id) - homeInv.count_of(id);
                if (lack <= 0) continue;
                const long long affordable = owed / base;
                if (affordable <= 0) continue;
                const int want = int(std::min<long long>(lack, affordable));
                const int moved = haul_between(
                    vassalInv, *bag, id, want,
                    rt.carryCap - inventory_weight(*bag));
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
                vassalInv, Depot(*bag),
                int(std::min<long long>(owed, 1 << 30)));
            if (dense > 0) {
                owed -= dense;
                took += dense;
            }
        }
        if (took > 0 && edge) {
            edge->owedValue -= took;
            if (edge->owedValue < 0) edge->owedValue = 0;
            record_landmark_fact(st, *ctx.mw.gs, FactKind::Taxed,
                                 int(st.spawnId[vassal].index),
                                 int(p.x), int(p.y),
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
                haul_between(*bag, depot_(homeSlot, ctx.mw),
                             commodity_item_index(i), 1 << 30, 1e9f);
            transfer_value_dense(*bag, depot_(homeSlot, ctx.mw),
                                 inventory_value(*bag));
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
    const MacroStore& st = store_ctx(ctx);
    // Урна городов — ОДНА дверь обхода мест (landmark_iter.h): ось рода
    // тела отвечает «город ли это», и второго списка мест в мире нет.
    const auto is_other_city = [&](std::uint16_t slot, int notId) {
        return SquadType(st.runtime[slot].squadType) == SquadType::City
               && int(st.spawnId[slot].index) != notId;
    };

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // Pick another city (id != home).
            int candidates = 0;
            for_each_place(st, [&](std::uint16_t slot) {
                if (is_other_city(slot, rt.homeSettlementId)) ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                // Обход без досрочного выхода: взявший урну гасит флаг, и
                // остаток прохода молчит (у двери мест нет break — её
                // порядок есть закон приоритета клетки).
                bool taken = false;
                for_each_place(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(slot, rt.homeSettlementId))
                        return;
                    if (pick-- != 0) return;
                    rt.targetSettlementId = int(st.spawnId[slot].index);
                    rt.targetX = float(slot_x(st, slot, ctx.mapW));
                    rt.targetY = float(slot_y(st, slot, ctx.mapW));
                    rt.state  = std::uint8_t(NS::Traveling);
                    taken = true;
                });
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
            rt.targetSettlementId = 0;
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
    const MacroStore& st = store_ctx(ctx);
    const auto is_other_city = [&](std::uint16_t slot, int notId) {
        return SquadType(st.runtime[slot].squadType) == SquadType::City
               && int(st.spawnId[slot].index) != notId;
    };
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            int candidates = 0;
            for_each_place(st, [&](std::uint16_t slot) {
                if (is_other_city(slot, rt.targetSettlementId)) ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                bool taken = false;
                for_each_place(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(slot, rt.targetSettlementId))
                        return;
                    if (pick-- != 0) return;
                    rt.targetSettlementId = int(st.spawnId[slot].index);
                    rt.targetX = float(slot_x(st, slot, ctx.mapW));
                    rt.targetY = float(slot_y(st, slot, ctx.mapW));
                    rt.state  = std::uint8_t(NS::Traveling);
                    taken = true;
                });
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

MacroHandle nearest_magika_mage(MacroHandle self, const MacroPos& p,
                                const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return MacroHandle{};
    const MacroStore& st = store_ctx(ctx);
    const int magika = faction_index("magika");
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    float best = kMageHuntSightCells * kMageHuntSightCells + 1.0f;
    MacroHandle found{};
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                // Бакет несёт индекс в порядке (шаг 3): слот — ключ к
                // колонкам, хэндл — для дверей и долгой ссылки.
                const SquadWalkEntry& sw = g.order[*it];
                if (sw.slot == self.slot) continue;
                if (st.dead[sw.slot] != 0) continue;
                const auto& mcell = st.cell[sw.slot];
                const auto* oc = &mcell;
                const auto* ok = &st.kind[sw.slot];
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
                found = handle_at(st, sw.slot);
            }
        }
    }
    return found;
}

void ai_mage_hunt(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    if (ctx.squads && ctx.mw.world && ctx.mw.gs) {
        const MacroHandle prey = nearest_magika_mage(self, p, ctx);
        MacroStore& st = store_ctx(ctx);
        if (st.valid(prey)) {
            const auto& ecell = st.cell[prey.slot];
            const MacroPos ep{float(ecs::cell_x(ecell, ctx.mapW)),
                              float(ecs::cell_y(ecell, ctx.mapW))};
            if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
                // Игрок в теле ведьмы: встреча принадлежит форс-двери
                // игрока (main.cpp detect_forced_encounter), не тихому
                // резолву — тот же гард, что у threat step.
                if (prey.slot == ctx.playerFlagSlot
                    || prey.slot == ctx.playerSquadSlot) {
                    rt.visualSpeed = 0.0f;
                    return;
                }
                if (!ctx.allowAutoBattle) return;
                const AutoBattleOutcome o = resolve_auto_battle(
                    auto_battle_side_of(st, self),
                    auto_battle_side_of(st, prey),
                    Ambush::None, *ctx.rng);
                settle_auto_battle(ctx.mw, self, prey, o);
                rt.visualSpeed = 0.0f;
                if (!macro_dead(st, self)) {
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
MacroHandle nearest_weaker_squad(MacroHandle self, const MacroPos& p,
                                 float sightCells, const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return MacroHandle{};
    const MacroStore& st = store_ctx(ctx);
    const float myPower = squad_power(auto_battle_side_of(st, self));
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    float best = sightCells * sightCells + 1.0f;
    MacroHandle found{};
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const SquadWalkEntry& sw = g.order[*it];
                if (sw.slot == self.slot) continue;
                if (st.dead[sw.slot] != 0) continue;
                const auto& oc = st.cell[sw.slot];
                const float d = torus_dist_sq(
                    p.x, p.y,
                    float(ecs::cell_x(oc, ctx.mapW)),
                    float(ecs::cell_y(oc, ctx.mapW)),
                    float(ctx.mapW), float(ctx.mapH));
                if (d >= best) continue;
                if (squad_power(auto_battle_side_of(
                        st, handle_at(st, sw.slot))) >= myPower) {
                    continue;   // добыча — только слабее
                }
                best = d;
                found = handle_at(st, sw.slot);
            }
        }
    }
    return found;
}

void ai_lair_sorties(MacroHandle self, MacroPos& p,
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
    {
        const ecs::DesignCharacterTag& dc =
            store_ctx(ctx).designTag[self.slot];
        if (const DesignCharacterDef* row = design_character(dc.ordinal)) {
            if (row->agenda.radiusCells > 0) {
                radius = float(row->agenda.radiusCells);
            }
        }
    }
    const float fromLair = std::sqrt(torus_dist_sq(
        p.x, p.y, float(rt.lairX), float(rt.lairY),
        float(ctx.mapW), float(ctx.mapH)));

    if (ctx.squads && ctx.mw.world && ctx.mw.gs && fromLair < radius) {
        const MacroHandle prey =
            nearest_weaker_squad(self, p, radius, ctx);
        MacroStore& st = store_ctx(ctx);
        if (st.valid(prey)) {
            const auto& ecell = st.cell[prey.slot];
            const MacroPos ep{float(ecs::cell_x(ecell, ctx.mapW)),
                              float(ecs::cell_y(ecell, ctx.mapW))};
            if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
                if (prey.slot == ctx.playerFlagSlot
                    || prey.slot == ctx.playerSquadSlot) {
                    rt.visualSpeed = 0.0f;
                    return;
                }
                if (!ctx.allowAutoBattle) return;
                const AutoBattleOutcome o = resolve_auto_battle(
                    auto_battle_side_of(st, self),
                    auto_battle_side_of(st, prey),
                    Ambush::SideA, *ctx.rng);
                settle_auto_battle(ctx.mw, self, prey, o);
                rt.visualSpeed = 0.0f;
                if (!macro_dead(st, self)) {
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
// player. The rules are the owner's design (CANON S4/S13 (бывший macrosim.md)):
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

MacroHandle nearest_hostile_squad(MacroHandle self, const MacroPos& p,
                                  const ecs::NPCKind& kind,
                                  const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return MacroHandle{};
    const MacroStore& st = store_ctx(ctx);
    const char* myFaction = faction_id_for_index(kind.factionIdx);
    const int cx0 = int(p.x) / b.cellSize;
    const int cy0 = int(p.y) / b.cellSize;
    const float sight = squad_sight_cells(kind, &st.runtime[self.slot]);
    float best = sight * sight + 1.0f;
    MacroHandle found{};
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const SquadWalkEntry& sw = g.order[*it];
                if (sw.slot == self.slot) continue;
                const auto& oc = st.cell[sw.slot];
                const auto& ok = st.kind[sw.slot];
                const float d = torus_dist_sq(
                    p.x, p.y,
                    float(ecs::cell_x(oc, ctx.mapW)),
                    float(ecs::cell_y(oc, ctx.mapW)),
                    float(ctx.mapW),
                    float(ctx.mapH));
                if (d >= best) continue;
                if (!factions_hostile(ctx.mw.gs, myFaction,
                                      faction_id_for_index(ok.factionIdx))) {
                    continue;
                }
                best = d;
                found = handle_at(st, sw.slot);
            }
        }
    }
    return found;
}

// Returns true when the threat consumed this think (fled, pursued or
// fought); the role behaviour then waits for a calmer half hour.
bool squad_threat_step(MacroHandle self, MacroPos& p,
                       const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                       ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.world || !ctx.squads || !ctx.mw.gs) return false;

    const MacroHandle enemy = nearest_hostile_squad(self, p, kind, ctx);
    MacroStore& st = store_ctx(ctx);
    if (!st.valid(enemy)) {
        // Threat gone: a fleeing squad calms down and resumes its life.
        if (rt.state == std::uint8_t(NS::Fleeing)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = 0;
        }
        return false;
    }

    const auto& ecell = st.cell[enemy.slot];
    const MacroPos ep{float(ecs::cell_x(ecell, ctx.mapW)),
                      float(ecs::cell_y(ecell, ctx.mapW))};
    const float myPower = squad_power(auto_battle_side_of(st, self));
    const float theirPower = squad_power(auto_battle_side_of(st, enemy));

    // The geometric meeting: same macro cell = the fight happens, resolved
    // by the ONE law and settled through the ONE ledger. An ambush is a
    // pursuer catching a squad that never saw it coming.
    if (int(p.x) == int(ep.x) && int(p.y) == int(ep.y)) {
        // A player-controlled squad's meetings belong to the forced-encounter
        // door (Inc 6, main.cpp detect_forced_encounter): the squad stands ON
        // the meeting cell and the door opens the pre-battle screen — never
        // the silent auto-resolve. Both slots, because possession moves
        // the flag while the home squad stays home (player_entity.h).
        if (enemy.slot == ctx.playerFlagSlot
            || enemy.slot == ctx.playerSquadSlot) {
            rt.visualSpeed = 0.0f;
            return true;
        }
        if (!ctx.allowAutoBattle) return false;
        ecs::MacroNpcRuntime* ert = &st.runtime[enemy.slot];
        const bool ambush =
            rt.state == std::uint8_t(NS::Chasing) && ert
            && ert->state != std::uint8_t(NS::Chasing)
            && ert->state != std::uint8_t(NS::Fleeing);
        const AutoBattleOutcome o = resolve_auto_battle(
            auto_battle_side_of(st, self),
            auto_battle_side_of(st, enemy),
            ambush ? Ambush::SideA : Ambush::None, *ctx.rng);
        settle_auto_battle(ctx.mw, self, enemy, o);
        rt.visualSpeed = 0.0f;
        if (!macro_dead(st, self)) {
            rt.state = std::uint8_t(NS::Idle);
            rt.stateTimer = std::int16_t(3 + rand_int(ctx, 5));
        }
        // A beaten-but-alive enemy runs; distance is what prevents an
        // immediate rematch, and the winner's next think re-evaluates.
        if (st.valid(enemy) && !macro_dead(st, enemy)) {
            ert->state = std::uint8_t(NS::Fleeing);
        }
        return true;
    }

    const float bravery = bravery_of(&st.traits[self.slot]);
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
static std::uint32_t creatures_worth(const MacroStore& st, MacroHandle e) {
    std::uint32_t worth = 0;
    const Inventory& bag = st.inventory[e.slot].inv;
    for (int i = bag.creature_first(); i < kMaxInventorySlots; ++i) {
        const ItemRef& r = bag.slots[std::size_t(i)];
        const std::uint16_t kind =
            std::uint16_t(creature_of_world_row(r.def));
        if (!valid_npc_kind(kind)) continue;
        worth += std::uint32_t(
            std::max(0, npc_def(NPCType(kind)).hireGold))
            * std::uint32_t(r.count);
    }
    return worth;
}

void scent_squad_deposit(MacroHandle self, const MacroPos& p,
                         const ecs::NPCKind& kind, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.world) return;
    ScentField& sf = ctx.mw.gs->scent;
    const int f = int(kind.factionIdx);
    if (f < 0 || f >= sf.factions) return;   // kNoFaction не следит
    const MacroStore& st = store_ctx(ctx);
    const float power = squad_power(auto_battle_side_of(st, self));
    std::uint32_t worth = creatures_worth(st, self);
    worth += std::uint32_t(
        std::max(0, inventory_value(st.inventory[self.slot].inv)));
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
    const MacroStore& st = store_ctx(ctx);
    const auto put = [&](std::uint16_t slot) {
        if (slot >= kMacroEntityCap || st.alive[slot] == 0) return;
        const MacroHandle h = handle_at(st, slot);
        if (macro_dead(st, h)) return;
        const auto& c = st.cell[slot];
        const MacroPos p{float(ecs::cell_x(c, ctx.mapW)),
                         float(ecs::cell_y(c, ctx.mapW))};
        scent_squad_deposit(h, p, st.kind[slot], ctx);
    };
    put(ctx.playerFlagSlot);
    if (ctx.playerSquadSlot != ctx.playerFlagSlot) put(ctx.playerSquadSlot);
}

// (Крутилки охоты — kHuntBoldShift и kHuntScentFloor — в npc_ai.h: их
// читает тест и вертит дубль-прогон.)
bool scent_hunt_step(MacroHandle self, MacroPos& p,
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
        squad_power(auto_battle_side_of(store_ctx(ctx), self));
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
void ai_waypoints(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadOrders* orders = ctx.mw.world
        ? &store_ctx(ctx).orders[e.slot] : nullptr;
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
// Зовётся ТОЛЬКО из ветки SquadType::None — то есть для сквадов, у которых
// нет макро-источника поведения вовсе: генезисных одиночек и квестовой цели.
// Лестница честная и по убыванию authority: маршрут в приказе (он и есть
// приказ — вердикт владельца), строка стола анкет (МАКРО-таблица, законно),
// и только потом мобная строка лидера — та самая течь.
// С их сносом (порция Б-6, «всё через универсальную систему анкет») эта
// функция умирает целиком, а не переезжает.
AIBehaviour untyped_squad_behaviour(const MacroStore& st, MacroHandle h,
                                     const ecs::NPCKind& kind) {
    if (st.orders[h.slot].waypointCount > 0) return AIBehaviour::Waypoints;
    if (const DesignCharacterDef* row =
            design_character(st.designTag[h.slot].ordinal)) {
        return row->behaviour;
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
// The bite lands on the LORD's HP because the lord IS the squad — the creatures
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


void settle_exhaustion(MacroHandle e, const MacroPos& p,
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
        MacroStore& st = store_ctx(ctx);
        settle_leader_fraction(st, e, 0.0f);
        kill_fallen_squad_creatures(st, *ctx.mw.gs,
                                    ctx.mw.econFacts,
                                    ctx.mw.econFactsUser);
    }
}

// ОДНА ДВЕРЬ «ЧТО ДЕЛАЕТ ЭТОТ СКВАД» (владелец 2026-09-21: «сквад должен
// знать, кто он — тип задаёт поведение аи агента»). Прежде на этот вопрос
// отвечали ЧЕТЫРЕ словаря, и `dispatch` знал только один из них: поведение
// приходило ему АРГУМЕНТОМ, уже посчитанным из мобной строки, а тип сквада
// разбирался ВЕТКОЙ ВНУТРИ ai_gatherer — пятой машиной внутри первой.
// Теперь порядок обратный и единственный: сперва ТИП (макро-колонка), и лишь
// нетипизированный сквад падает на каталог тел.
void dispatch(MacroHandle e, MacroPos& p,
              const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    // ТЕЛО МЕСТА НЕ ДУМАЕТ ВООБЩЕ (ось рода, M-90 шаг 5): ни следа, ни
    // угрозы, ни охоты — его день идёт своим проходом (settle_landmark_day).
    // Без этого гейта squad_threat_step погнал бы ГОРОД преследовать врага.
    if (is_settlement_kind(SquadType(rt.squadType))) return;
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
        // НЕПОДВИЖНЫЕ РОДЫ ОСИ НЕ ДУМАЮТ, И ЭТО НЕ ЗАГЛУШКА (M-90 шаг 3а).
        // После слияния `SquadType` в эту ось сюда стало ВЫРАЗИМО
        // приехать городом: место есть неподвижный сквад, и лестница
        // поведения у него кончается на первом же вопросе — он не ходит.
        // Его день идёт своим проходом (`settle_landmark_day`), а не думкой
        // марша. Ветки выписаны поимённо, а не прикрыты `default`, ровно
        // чтобы следующий ПОДВИЖНЫЙ род компилятор назвал вслух.
        case SquadType::City:
        case SquadType::Village:
        case SquadType::Spire:
        case SquadType::Ruin:
        case SquadType::Lair:
        case SquadType::Shrine:
        case SquadType::Mine:
        case SquadType::Tower:
        case SquadType::Count: return;
        case SquadType::None:  break;   // ниже — течь, названная по имени
    }
    switch (untyped_squad_behaviour(store_ctx(ctx), e, kind)) {
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
CaravanDeal trade_caravan_at_station(MacroStore& st, Inventory& hold,
                                     float capacityKg,
                                     std::uint16_t marketSlot,
                                     int myTradePct, int theirTradePct,
                                     EconFactSink sink, void* user) {
    CaravanDeal out{};
    // Склад и счёт рынка — колонки его ТЕЛА (M-90 шаг 5).
    Inventory& ms = st.inventory[marketSlot].inv;
    // Рынок — МЕСТО (CANON S10): проданное ему падает в Depot и гасит его
    // долг СРАЗУ — город, купивший хлеб, хлеб уже проел.
    const Depot msd(ms, st.upkeep[marketSlot].needDebt, sink, user);
    // Анкета рынка — КОЛОНКА его тела (рождение места её и заполнило).
    const Skills& site = st.sheet[marketSlot].skills;
    for (int i = 0; i < kCommodityCount; ++i) {
        const int id = commodity_item_index(i);
        const ItemDef* def = item_def_at(id);
        const int base = def ? def->value : 0;
        if (base <= 0) continue;
        const int demand = season_demand_for(id, st.upkeep[marketSlot].needDebt,
                                             souls_home(st, marketSlot), site,
                                             &ms);
        // Спрос уже СЕЗОННЫЙ (остаток счёта + производный) — прежний
        // множитель горизонта умер вместе с календарём кривой.
        const int need = demand;
        const int have = ms.count_of(id);
        if (need > have) {
            // SELL into the shortage, up to the market's own seasonal need,
            // bounded by what its whole store can PAY (max_affordable_lot_).
            int n = std::min(hold.count_of(id), need - have);
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
CaravanDeal trade_vendor_at_market(MacroStore& st, Inventory& bag,
                                   float capacityKg,
                                   std::uint16_t marketSlot,
                                   MacroHandle home,
                                   int myTradePct, int theirTradePct,
                                   EconFactSink sink, void* user) {
    CaravanDeal out{};
    // Склад и счёт рынка — колонки его ТЕЛА (M-90 шаг 5).
    Inventory& ms = st.inventory[marketSlot].inv;
    // Рынок — МЕСТО (CANON S10): проданное гасит его долг сразу.
    const Depot msd(ms, st.upkeep[marketSlot].needDebt, sink, user);
    const Skills& site = st.sheet[marketSlot].skills;
    const auto base_value = [](int defIdx) {
        const ItemDef* d = item_def_at(defIdx);
        return d ? d->value : 0;
    };
    // SELL the whole load first — the coin below buys the home's lacks.
    for (int i = 0; i < kCommodityCount; ++i) {
        const int id = commodity_item_index(i);
        const int base = base_value(id);
        if (base <= 0) continue;
        int n = bag.count_of(id);
        if (n <= 0) continue;
        const int demand = season_demand_for(id, st.upkeep[marketSlot].needDebt,
                                             souls_home(st, marketSlot), site,
                                             &ms);
        const int have = ms.count_of(id);
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
    // ЦЕНА ДОМА СЧИТАЕТСЯ ЖИВЬЁМ, ТОЙ ЖЕ ДВЕРЬЮ, ЧТО И ЦЕНА РЫНКА.
    // Прежде здесь читался КЭШ — ведомость дома, выписанная на границе
    // сезона (ярус 2). Кэш уничтожен 2026-09-30 (ломтик E шаг 2) вместе с
    // колонками места, а закон остался тот же: `season_demand_for` +
    // `stock_price`, ровно те же две функции, которыми ведомость и
    // считалась. Второго диалекта цены не появилось — исчезла только копия.
    // Свой дом крю знает точно и знало всегда: его склад читается строкой
    // ниже, и горизонтом это никогда не было (горизонт — про ЧУЖИЕ рынки,
    // market_price_seen).
    if (st.valid(home)) {
        // Плечо дома — колонки его ТЕЛА (M-90 шаг 5).
        const std::uint16_t hSlot = home.slot;
        const Skills& homeHands = st.sheet[hSlot].skills;
        const Inventory& homeInv = st.inventory[hSlot].inv;
        struct Lot { int i; float gainPerKg; int homeCap; };
        Lot lots[std::size_t(kCommodityCount)];
        int lotCount = 0;
        for (int i = 0; i < kCommodityCount; ++i) {
            const int id = commodity_item_index(i);
            const ItemDef* def = item_def_at(id);
            const int base = def ? def->value : 0;
            if (base <= 0) continue;
            const int have = ms.count_of(id);
            if (have <= 0) continue;
            const int demand =
                season_demand_for(id, st.upkeep[marketSlot].needDebt,
                                  souls_home(st, marketSlot), site, &ms);
            const int buyHere = trade_buy_price(
                stock_price(base, have, demand), myTradePct, theirTradePct);
            // Чего это стоит ДОМА — тем же счётом и той же кривой.
            const int homeDemand =
                season_demand_for(id, st.upkeep[hSlot].needDebt,
                                  souls_home(st, hSlot),
                                  homeHands, &homeInv);
            const int worthHome =
                stock_price(base, homeInv.count_of(id), homeDemand);
            if (worthHome <= 0) continue;         // дома этой строке нет цены
            if (worthHome <= buyHere) continue;   // рейс не окупает закупку
            const float kg = def->weight > 0.0f ? def->weight : 1.0f;
            // ПОТОЛОК СТРОКИ — сезон домашней нужды (спрос уже сезонный),
            // но он больше НЕ ворота: у товара, который дома никто не ест,
            // потолок — только трюм и кошелёк.
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
            const int id = commodity_item_index(i);
            const ItemDef* def = item_def_at(id);
            const int base = def->value;
            const int demand =
                season_demand_for(id, st.upkeep[marketSlot].needDebt,
                                  souls_home(st, marketSlot), site, &ms);
            const int have = ms.count_of(id);
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
int provision_squad(Inventory& store, Inventory& bag, int soldiers,
                    float roundtripCells, float freeCarryKg) {
    if (soldiers <= 0) return 0;   // the leader is a subject — he needs nothing
    const int days =
        1 + int(std::ceil(roundtripCells / kSustainedMarchCellsPerDay));
    const int portion = soldiers * days;
    // The haul door already speaks credit-before-debit and respects the
    // carry the loaf must ride on.
    return haul_between(store, bag, hunger_item_index(), portion,
                        freeCarryKg);
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
//     «сколько ест состав». Вердикт владельца 2026-09-22: «оба снести — один
//     закон без исключений». СЛЕДСТВИЕ, НАЗВАННОЕ ВСЛУХ (§55): у навыка
//     Foraging не осталось НИ ОДНОГО механического читателя в мире — строка
//     в таблице навыков и бонус к ней живы, читателя нет. Его законное место
//     — добыча пищи В ПУТИ, а не скидка на счёт; до тех пор это известная
//     спящая колонка, а не забытая.
// Счёт теперь один: `upkeep_bill` (macro/upkeep_window.h).

int squad_season_window(MacroWorld& mw, int day) {
    if (!mw.gs || !mw.world) return 0;
    if (!season_boundary(day)) return 0;
    GameState& gs = *mw.gs;
    auto& reg = mw.world->reg;
    int deserted = 0;
    // Окно делит ОДИН пул дезертиров и один пул лута на всех — порядок суда
    // есть закон мира (squad_walk.h): по ординалу. Скрэтч локальный, как у
    // прочих дневных проходов.
    MacroStore& st = store_of(reg);
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(st, order, [](std::uint16_t) { return true; });
    for (const SquadWalkEntry& sw : order) {
        const std::uint16_t slot = sw.slot;
        auto& rt     = st.runtime[slot];
        auto& bag    = st.inventory[slot];
        auto& creatures = st.upkeep[slot];
        // СУД И СЧЁТ — ОДНА ДВЕРЬ НА ВЕСЬ МИР (macro/upkeep_window.h).
        // Артель судится тем же телом и тем же счётом, что контейнер места и
        // армия игрока: своего у неё здесь не осталось ничего.
        const UpkeepWindowOutcome out = upkeep_season_window(
            creatures, bag.inv, gs.deserterPool, gs.lootPoolValue,
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
        refresh_squad_carry(st, handle_at(st, slot));
    }
    return deserted;
}

int squad_bags_hygiene_daily(MacroWorld& mw) {
    if (!mw.gs || !mw.world) return 0;
    auto& reg = mw.world->reg;
    int melted = 0;
    // Порядок по ординалу (squad_walk.h): авто-скрап и гашение счёта трогают
    // цену дня через факты — одна очередь фактов на всех.
    MacroStore& st = store_of(reg);
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(st, order, [](std::uint16_t) { return true; });
    // Мешок носителя флажка решает ввод, не ИИ — слот из битов GameState
    // (1е кластер 5), тег-чтение умерло.
    const MacroHandle flagH = player_flag_handle(*mw.gs);
    const std::uint16_t flagSlot =
        st.valid(flagH) ? flagH.slot : kMacroNoSlot;
    for (const SquadWalkEntry& sw : order) {
        const std::uint16_t slot = sw.slot;
        auto& bag = st.inventory[slot];
        // Camp-life slot hygiene (CANON «Крафт/Скрап»: авто-скрап ИИ по
        // порогу >50% — «склад города ИЛИ МЕШОК СКВАДА»): the same daily
        // overflow law the settlement store runs. The gate is not a player
        // privilege but the seam of DECISION: this loop is the AI deciding
        // for its bag, and the flag holder's bag decisions come from input —
        // «автоматическое уничтожение вещей игрока строго запрещено».
        if (slot != flagSlot) melted += auto_scrap_overflow(bag.inv);
        // ── ПРИХОД ГАСИТ СЧЁТ ВЕСЬ СЕЗОН (CANON S10, v105) ───────────────
        // Страховочный дневной такт гашения — ровно тот же, что у места
        // (world_tick settle_landmark_day). Двери прихода у сквада разные
        // (добыл, купил, отнял), и городить у каждой свою уплату значило бы
        // писать закон пятый раз; вместо этого контейнер ест то, что приехало,
        // тем же вечером. «Добыча привезла — часть съелась» (владелец,
        // 2026-09-21), и это ТОТ ЖЕ econ_pay_debt, которым платит ландмарк.
        econ_pay_debt(bag.inv, st.upkeep[slot].needDebt, mw.econFacts,
                      mw.econFactsUser);
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

    MacroStore& stq = store_of(reg);
    // ПОРЯДОК ЗАКОНА (squad_walk.h) — он же ключ резолва ниже: ординально
    // отсортированные живые слоты. Собирается ОДИН раз на день, читают его
    // оба прохода переписи и `slot_of`; рождения и смерти идут ПОСЛЕ них.
    std::vector<SquadWalkEntry> crewOrder;
    collect_squads_by_ordinal(stq, crewOrder,
                              [](std::uint16_t) { return true; });
    // СЛОТ ТЕЛА ДОМА ПО ОРДИНАЛУ — ключ всех боковых таблиц дня (ломтик F:
    // строки места больше нет, а с ней и индекса в её векторе). Бинарный
    // поиск по порядку выше: дверь зовётся на КАЖДУЮ сущность дважды в день,
    // и скан капа сделал бы это O(сущности × кап) — та же худшая точка
    // переписи M-90, только дороже. Ось рода гейтится здесь: пространство
    // ординалов ОДНО (M-37).
    // «Дома нет» = kMacroNoSlot — ПОСЛЕДНЕЕ значение типа индекса (ЗАКОН
    // УЗКОГО ИНДЕКСА: слот 0 законен, нулём тут сказать нечего), и тот же
    // сентинел, каким этот проход уже называет «нет артели» (idleByHome,
    // claim_standing). Второго имени для «нет слота» здесь не заводится.
    const auto slot_of = [&](int id) -> std::uint16_t {
        if (id <= 0) return kMacroNoSlot;
        const MacroHandle h =
            macro_handle_by_spawn_id(stq, crewOrder, std::uint32_t(id));
        if (!stq.valid(h)) return kMacroNoSlot;
        return is_settlement_kind(SquadType(stq.runtime[h.slot].squadType))
            ? h.slot : kMacroNoSlot;
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
    std::vector<std::uint16_t> homeIdle;
    // exclude<Dead>: a dead crew at its home cell is NOT a crew coming home —
    // it is a corpse-row awaiting the drain (AI-2). Without the exclusion a
    // dead leader and his dead men dissolved into the landmark as living
    // souls.
    // Порядок по ординалу (squad_walk.h): idleByHome ниже раздаётся законом
    // «первая подходящая» (claim_standing) и растворяется в том же порядке —
    // «кто первым встал» обязан быть законом мира, не кишкой EnTT.
    std::vector<SquadWalkEntry> idleOrder;
    collect_squads_by_ordinal(
        stq, idleOrder,
        [&](std::uint16_t slot) { return stq.dead[slot] == 0; });
    for (const SquadWalkEntry& sw : idleOrder) {
        const std::uint16_t slot = sw.slot;
        const auto& kind = stq.kind[slot];
        const auto& rt   = stq.runtime[slot];
        const auto& cell = stq.cell[slot];
        // ТЕЛО МЕСТА — НЕ АРТЕЛЬ (ось рода, не строка существа): без этого
        // гейта город, носящий нулевую строку и числящий домом себя,
        // занимал крестьянскую строку СВОЕГО дома и получал поручение.
        if (is_settlement_kind(SquadType(rt.squadType))) continue;
        if (!is_crew(kind.type)) continue;
        if (rt.state != std::uint8_t(NS::Idle)) continue;
        const std::uint16_t homeSlot = slot_of(rt.homeSettlementId);
        if (homeSlot == kMacroNoSlot) continue;
        // «Дома» = радиус прибытия марша (at_target, ±2 клетки) — ОДИН
        // предикат с вендорской погрузкой: точное равенство клетке
        // оставляло финишировавшую у крыльца артель нерастворённой
        // навсегда (души не возвращались, пере-аукцион не наступал).
        if (torus_dist_sq(float(ecs::cell_x(cell, gs.mapW)),
                          float(ecs::cell_y(cell, gs.mapW)),
                          float(slot_x(stq, homeSlot, gs.mapW)),
                          float(slot_y(stq, homeSlot, gs.mapW)),
                          float(gs.mapW), float(gs.mapH)) >= 4.0f)
            continue;
        homeIdle.push_back(slot);
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
    // КЛЮЧ БОКОВЫХ ТАБЛИЦ ДНЯ — СЛОТ ТЕЛА ДОМА, поэтому их длина есть КАП
    // популяции (ломтик F: вектора мест, по размеру которого они жили,
    // больше нет). Дневной скрэтч, ~640 КиБ на все четыре; аллокация одна
    // на день и вне тика.
    std::vector<std::array<std::uint8_t, 8>> outCount(kMacroEntityCap);
    std::vector<int> afield(kMacroEntityCap, 0);
    // Души артелей, СТОЯЩИХ ДОМА, — часть базы пула труда: суд границы,
    // меривший пул одним населением, ужимал составы каждый сезон (души
    // стоящих выпадали из базы — поймано свидетелем resize).
    std::vector<int> standingSouls(kMacroEntityCap, 0);
    // Пара {слот дома, слот стоящей артели}; kMacroNoSlot во второй =
    // заявка уже разобрана (claim_standing).
    std::vector<std::pair<std::uint16_t, std::uint16_t>> idleByHome;
    std::sort(homeIdle.begin(), homeIdle.end());
    const auto is_home_idle = [&](std::uint16_t slot) {
        return std::binary_search(homeIdle.begin(), homeIdle.end(), slot);
    };
    // Тот же закон порядка (crewOrder выше): этот проход заполняет idleByHome.
    for (const SquadWalkEntry& sw : crewOrder) {
        const std::uint16_t slot = sw.slot;
        const auto& kind = stq.kind[slot];
        const auto& rt   = stq.runtime[slot];
        const std::uint16_t homeSlot = slot_of(rt.homeSettlementId);
        if (homeSlot == kMacroNoSlot) continue;
        // ТЕЛО МЕСТА — НЕ АРТЕЛЬ: без гейта весь инвентарь города шёл в
        // труд-гроссбух как «души в поле» его же строки.
        if (is_settlement_kind(SquadType(rt.squadType))) continue;
        const LandmarkDef& ld =
            landmark_def(SquadType(stq.runtime[homeSlot].squadType));
        bool standingHome = false;
        if (is_crew(kind.type)) {
            // Труд-гроссбух считает ЛЮДЕЙ: зверь области — спина и рот, но
            // не рука (world_row.h count_human_souls).
            const int souls = 1 + count_human_souls(stq.inventory[slot].inv);
            afield[homeSlot] += souls;
            if (is_home_idle(slot)) {
                standingHome = true;
                standingSouls[homeSlot] += souls;
                idleByHome.push_back({homeSlot, slot});
            }
        }
        if (standingHome) continue;   // its row stays OPEN for re-dispatch
        for (int i = 0; i < int(ld.crewCount); ++i) {
            if (std::uint16_t(ld.crews[i].npc) != kind.type) continue;
            // Экземпляр приписывается строке СВОЕГО ТИПА: у места строки
            // теперь различаются типом сквада, а не позицией в списке.
            const SquadType rowType = ld.crews[i].type;
            if (rowType != SquadType::None
                && std::uint8_t(rowType) != rt.squadType)
                continue;
            if (outCount[homeSlot][std::size_t(i)] < 255)
                ++outCount[homeSlot][std::size_t(i)];
            break;
        }
    }

    // Роспуск населенской артели: души и остатки — домой (существующие
    // двери), сущность умирает. Зовёт суд границы: «лишний сквад» = стоящая
    // дома артель, которой не досталось СТРОКИ (закрылся гейт, строку
    // держит полевая, пул ужался) — «просто распускает» (S19.2). Оба рычага
    // живы (владелец 2026-09-18): строки правят ЧИСЛОМ сквадов, пул — их
    // РАЗМЕРОМ (добор/ссадка в ветке стоящих ниже).
    const auto dissolve_population_crew = [&](std::uint16_t slot,
                                              std::uint16_t homeSlot) {
        {
            // Leftovers home: cargo by the haul door, coin by the wallet
            // door — a dissolved crew owns nothing (CANON S5, the loan law).
            auto& bag = stq.inventory[slot];
            for (int c = 0; c < kCommodityCount; ++c)
                haul_between(bag.inv, depot_(homeSlot, mw),
                             commodity_item_index(c), 1 << 30, 1e9f);
            transfer_value_dense(bag.inv, depot_(homeSlot, mw),
                                 inventory_value(bag.inv));
        }
        // ПЕРЕВОРОТ v122 — форма, которую владелец обещал этому месту
        // («буквально перенос между гарнизоном — весь гарнизон артели
        // отдаётся в город, и артель пустая удаляется»): души дома СТАЛИ
        // головами инвентаря места, и человек с лошадью едут ОДНОЙ дверью
        // creatures_push_slot. Паства (worked) НЕ меняется: вернувшаяся
        // душа и так была её частью — «в поле» лишь стало «дома». Лидер —
        // своя душа: он встаёт домой генерик-головой своего рода (его слот
        // store умирает, а душа из мира не испаряется).
        int souls = 1;
        {
            const auto& bag = stq.inventory[slot];
            // Обход области существ 1023 → first = старый порядок слотов
            // (старейший первым); источник не мутируется — слот умирает.
            for (int i = kMaxInventorySlots - 1;
                 i >= bag.inv.creature_first(); --i) {
                const ItemRef& sl = bag.inv.slots[std::size_t(i)];
                if (is_folk_kind(
                        std::uint16_t(creature_of_world_row(sl.def)))) {
                    souls += sl.count;
                }
                if (!creatures_push_slot(stq.inventory[homeSlot].inv, sl)) {
                    // Дому тесно (кап контейнера) — лишние честно уходят
                    // в пул, никто не испаряется.
                    creatures_push_slot(gs.deserterPool, sl);
                }
            }
        }
        {
            const NPCType leaderKind = NPCType(stq.kind[slot].type);
            if (is_folk_kind(std::uint16_t(leaderKind))
                && !creatures_push_stack(stq.inventory[homeSlot].inv,
                                         leaderKind,
                                         npc_def(leaderKind).baseLevel, 1)) {
                creatures_push_stack(gs.deserterPool, leaderKind,
                                     npc_def(leaderKind).baseLevel, 1);
            }
        }
        // 6.3: сквад ЕСТЬ слот store — смерть слота и есть вся смерть.
        store_death(stq, handle_at(stq, slot));
        return souls;
    };
    // ── Сезонная погрузка содержания (S19.2): та же арифметика нужд, что
    // у окна (upkeep_bill — вторых правд содержания не бывает);
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
    // счёт»: дом гасит долг уходящей артели ВПЕРЁД, из своих запасов, как
    // гасит счёт любого, кого кормит. Физика проверена: сезон харча души —
    // 32 кг при спине 154 кг, пятая часть.
    // Берётся РОВНО НЕДОСТАЮЩЕЕ по счёту, поэтому повторный вызов в тот же
    // день ничего не грузит и склад не сосётся дважды.
    const auto load_season_upkeep = [&](std::uint16_t homeSlot,
                                        std::uint16_t slot) {
        auto& bag = stq.inventory[slot];
        auto& creatures = stq.upkeep[slot];
        const int boardOrd = hunger_commodity_ordinal();
        const int owed = boardOrd >= 0 ? creatures.needDebt[boardOrd] : 0;
        const int haveBoard = bag.inv.count_of(hunger_item_index());
        if (owed > haveBoard) {
            haul_between(stq.inventory[homeSlot].inv, bag.inv,
                         hunger_item_index(), owed - haveBoard, 1e9f);
        }
        const std::int64_t haveCoin = inventory_value(bag.inv);
        if (creatures.wageDebt > haveCoin) {
            transfer_value_dense(stq.inventory[homeSlot].inv, bag.inv,
                                 int(creatures.wageDebt - haveCoin));
        }
        // Погасить тем, что только что легло в сумку: долг умирает в ту же
        // минуту, что и приход (одна дверь на весь мир).
        econ_pay_debt(bag.inv, creatures.needDebt, mw.econFacts,
                      mw.econFactsUser);
    };

    // 2) RAISE today's crews off the place's OWN registry row (owner
    //    2026-08-31, CANON S10): the crew pool is pop >> labourShift, split
    //    EVENLY across the rows whose gates are open today — a worksite gate
    //    walks the same find_worksite the working AI walks by (ore near home
    //    IS the presence of miners); solo rows ride alone (the tax courier).
    int raised = 0;
    for_each_place(stq, [&](const std::uint16_t sSlot) {
        const SquadType sKind = SquadType(stq.runtime[sSlot].squadType);
        const int sId = int(stq.spawnId[sSlot].index);
        const LandmarkDef& ld = landmark_def(sKind);
        if (ld.crewCount == 0 || souls_flock(gs, stq, sSlot) <= 0) return;
        Inventory& sInv = stq.inventory[sSlot].inv;
        static_assert(sizeof(LandmarkDef::crews) / sizeof(LandmarkCrewRow)
                          <= 8,
                      "outCount — восемь счётчиков на место: по строке");
        const XY home{float(slot_x(stq, sSlot, gs.mapW)),
                      float(slot_y(stq, sSlot, gs.mapW))};
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
        // Номер потока = первый номер ЗА строками контейнера: третий аргумент
        // hash3 в этой функции всюду означает СТРОКУ (0-7, static_assert
        // выше), и поток станции по построению не может столкнуться ни с
        // одной из них.
        Rng stationRoll(hash3(gs.worldSeed ^ std::uint32_t(day),
                              std::uint32_t(sId),
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
        const Skills& homeSite = stq.sheet[sSlot].skills;
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
            // артели не нужно. Строка, чей выход — СУЩЕСТВО (лошадь),
            // спиной не ограничена вовсе: существо идёт в область существ
            // контейнера, а не в сумку, и своего предела у ловли нет
            // (M-230, дыра названа у самой ловли выше).
            const NPCType crewKind = [&]() -> NPCType {
                const LandmarkDef& ldc = landmark_def(sKind);
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
                if (gd.creatureYield != NPCType::Count) {
                    // ЦЕЛЬ-СУЩЕСТВО ЦЕНИТСЯ СВОЕЙ СТРОКОЙ НАЙМА, И БОЛЬШЕ
                    // НИЧЕМ (M-230, вердикт владельца 2026-10-06 «сноси
                    // полностью»). Кривая дефицита над табуном снесена
                    // вместе с законом упряжки: её «нужда» была мерой «по
                    // лошадке на душу», то есть третьим написанием того же
                    // закона, а «склад» требовал отдельного счёта табуна по
                    // всем артелям дома. Цена плоская — значит насыщения
                    // цель не чувствует, и это НАЗВАННАЯ ДЫРА, а не
                    // замысел: обратная связь (ножом, рационом или ценой по
                    // выжившей колонке) строится отдельным нарядом.
                    const NpcTypeDef& yieldRow = npc_def(gd.creatureYield);
                    const int base = yieldRow.hireGold;
                    if (base <= 0) continue;
                    unitPrice = base;
                } else {
                    const int goalItem = gatherer_item_index(g);
                    const ItemDef* idef = item_def_at(goalItem);
                    const int base = idef ? idef->value : 0;
                    if (base <= 0) continue;
                    const int have = sInv.count_of(goalItem);
                    const int demand = season_demand_for(
                        goalItem, stq.upkeep[sSlot].needDebt,
                        souls_home(stq, sSlot), homeSite, &sInv);
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
                    // цену». Деревня знает лишь то, что строка чего-то
                    // СТОИТ САМА ПО СЕБЕ, — абсолютную стоимость каталога
                    // (CANON S10, вердикт 2026-09-30). Незнакомых рынков
                    // она по-прежнему не видит.
                    //
                    // ЗАМЕНА ПРЕЙСКУРАНТА СЮЗЕРЕНА, И ОНА ЖЕ УПРОЩЕНИЕ.
                    // Прежде здесь читалась ведомость сюзерена по
                    // феодальному ребру — единственное место экономики под
                    // грифом «не уверены». Ведомость уничтожена (ломтик E
                    // шаг 2), и на её место встало то же число без ребра,
                    // без кэша и без сомнения: цена труда не падает ниже
                    // АБСОЛЮТНОЙ СТОИМОСТИ строки. Дефект, ради которого
                    // рычаг заводили, закрыт тем же: деревня не бросает
                    // жилу оттого, что её собственная полка полна, — железо
                    // стоит железо, даже когда дома его девать некуда.
                    {
                        const int there = market_price_seen(
                            gatherer_commodity_index(g));
                        if (there > unitPrice) unitPrice = there;
                    }
                }
                // ОДНА ДВЕРЬ ПОИСКА РАБОЧЕГО МЕСТА на все роды: опись
                // округи уничтожена (ломтик E шаг 2), и аукцион спрашивает
                // ровно то же, что спросит поднятая им артель, — иначе он
                // сулил бы работу, которой исполнитель не найдёт.
                XY site;
                if (!find_worksite(gd, ctx, homePos, home, site)) continue;
                // СКОР = ВЫРАБОТКА В ДЕНЬ РЕЙСА. Одна величина на все
                // заявки — стоимость в день, — поэтому добыча и сбыт
                // наконец сравнимы в одной рулетке. Назначенного `1 +`
                // больше нет: делитель есть настоящая длина рейса.
                const ItemDef* gdef =
                    item_def_at(gatherer_item_index(g));
                const float unitKg =
                    gdef && gdef->weight > 0.0f ? gdef->weight : 1.0f;
                const float perSoul = gd.creatureYield != NPCType::Count
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
            const int firstId = pick_next_station_(ctx, homePos, sId, -1,
                                                   stX, stY);
            const std::uint16_t station = slot_of(firstId);
            if (station != kMacroNoSlot && station != sSlot) {
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
                    sInv, stq.upkeep[sSlot].needDebt, souls_home(stq, sSlot),
                    homeSite, carryPerSoul, nullptr);
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
                    const int id = commodity_item_index(c);
                    const ItemDef* d = item_def_at(id);
                    const int base = d ? d->value : 0;
                    if (base <= 0) continue;
                    const float kg = d->weight > 0.0f ? d->weight : 1.0f;
                    const auto fits = [&](long long want) -> long long {
                        const long long cap = (long long)(freeKg / kg);
                        return want < cap ? want : cap;
                    };
                    const int have = sInv.count_of(id);
                    // Спрос уже СЕЗОННЫЙ (остаток счёта + производный).
                    const int demand =
                        season_demand_for(id, stq.upkeep[sSlot].needDebt,
                                          souls_home(stq, sSlot), homeSite,
                                          &sInv);
                    const int homePrice =
                        stock_price(base, have, demand);
                    // ЦЕНА ТАМ — ЯРУС 2, ИЗ ТОЧКИ ДОМА (CANON S10). До
                    // 2026-09-22 здесь стояло `base − homePrice`, то есть
                    // «насколько дёшево моё излишнее добро У МЕНЯ ДОМА» —
                    // мера ГОТОВНОСТИ СБРОСИТЬ, а не выручки рейса
                    // (problems §55-II). Спред против прейскуранта партнёра
                    // — это и есть выручка, и знание на него законно.
                    const int therePrice = market_price_seen(c);
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
                    const int id = commodity_item_index(c);
                    const ItemDef* d = item_def_at(id);
                    const int base = d ? d->value : 0;
                    if (base <= 0) continue;
                    const int have = sInv.count_of(id);
                    const int demand =
                        season_demand_for(id, stq.upkeep[sSlot].needDebt,
                                          souls_home(stq, sSlot), homeSite,
                                          &sInv);
                    const int homePrice =
                        stock_price(base, have, demand);
                    // Тот же спред другим концом: везти домой стоит то, что
                    // ТАМ дешевле, чем дома. База заменена ценой партнёра по
                    // той же причине, что и в проходе продажи.
                    const int therePrice = market_price_seen(c);
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
                    const int id = commodity_item_index(c);
                    const ItemDef* d = item_def_at(id);
                    const int base = d ? d->value : 0;
                    if (base <= 0) continue;
                    const int have = sInv.count_of(id);
                    const int demand =
                        season_demand_for(id, stq.upkeep[sSlot].needDebt,
                                          souls_home(stq, sSlot), homeSite,
                                          &sInv);
                    if (demand <= have) continue;
                    needValue += (long long)(demand - have)
                                 * stock_price(base, have, demand);
                }
                if (value > 0) {
                    const XY citySite{
                        float(slot_x(stq, station, gs.mapW)),
                        float(slot_y(stq, station, gs.mapW))};
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
                            stq.spawnId[station].index, citySite, score};
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
                const Interest& it =
                    stq.interests[sSlot].slots[std::size_t(k)];
                if (it.stance == std::uint8_t(Stance::None)) break;
                if (it.stance != std::uint8_t(Stance::Vassal)) continue;
                const std::uint16_t vSlot = slot_of(it.object);
                if (vSlot == kMacroNoSlot || !owes_tithe(gs, stq, vSlot))
                    continue;
                if (bidCount >= int(sizeof(bids) / sizeof(bids[0]))) break;
                const XY site{float(slot_x(stq, vSlot, gs.mapW)),
                              float(slot_y(stq, vSlot, gs.mapW))};
                const float tripDays = road_days_(site);
                if (!(tripDays > 0.0f)) continue;
                const TitheEdge* e = tithe_edge_of(gs, stq, vSlot);
                const float score =
                    (float(e ? e->owedValue : 0) - fear_of(site)) / tripDays;
                if (score <= 0.0f) continue;
                bids[bidCount++] = GoalBid{std::uint8_t(SquadType::Collector),
                                           stq.spawnId[vSlot].index, site,
                                           score};
            }
        };

        // (ЗДЕСЬ СТОЯЛА ПАТРУЛЬНАЯ УРНА run_patrol_auction — 86 строк,
        // снесена 2026-09-21: её звала только строка с garrison=true, а
        // таких в реестре не осталось. Она же была единственным писателем
        // ErrandVerb::Patrol. Патруль вернётся своей строкой вместе со
        // своей механикой — и тогда его заявка встанет в ОБЩУЮ урну выше
        // одной размерностью со всеми, а не отдельным аукционом.)

        // Строка берёт только заявки своего типа; None = вся урна.
        const auto row_takes_ = [](const LandmarkCrewRow& cr,
                                   const GoalBid& b) -> bool {
            return cr.type == SquadType::None
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
                           std::uint32_t(sId), std::uint32_t(nonce)));
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
                    // по роду места. `None` = тип не объявлен, урна вся.
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
                    open = suzerain_of(stq, sSlot) > 0
                           && suzerain_of(stq, sSlot) != sId;
                    break;
                }
            }
            if (!open) continue;
            if (cr.solo) {
                if (outCount[sSlot][std::size_t(i)] == 0 && soloCount < 8)
                    solo[soloCount++] = i;
                continue;
            }
            // Уже в поле — не поднимаем заново: строка хочет `want`, в поле
            // стоит `outCount`, разница и есть сегодняшний наряд.
            int need = want[i] - int(outCount[sSlot][std::size_t(i)]);
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
            for (auto& [r2, s2] : idleByHome)
                if (r2 == sSlot && s2 != kMacroNoSlot)
                    load_season_upkeep(sSlot, s2);
        }
        // Заявка строки закрывается СТОЯЩЕЙ артелью первой — это и есть
        // пере-аукцион дня живой артели (S19.2: «рейс → дом → пере-аукцион
        // → новый рейс, домой вернулась — не исчезла»).
        const auto claim_standing =
            [&](std::uint16_t type) -> std::uint16_t {
            for (auto& [r2, s2] : idleByHome) {
                if (r2 != sSlot || s2 == kMacroNoSlot) continue;
                if (stq.kind[s2].type != type) continue;
                const std::uint16_t found = s2;
                s2 = kMacroNoSlot;
                return found;
            }
            return kMacroNoSlot;
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
            field_pool(gs, stq, sSlot, afield[sSlot], standingSouls[sSlot]);
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
            const std::uint16_t standing =
                claim_standing(std::uint16_t(ld.crews[i].npc));
            if (standing != kMacroNoSlot) {
                // ПОРУЧЕНИЕ НА СПИНУ (аукцион, CANON S10): пара {глагол,
                // объект} — рулетка этой строки уже решила; рефлекс
                // прерывает не спрашивая.
                auto& prt = stq.runtime[standing];
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
                // СНАРЯЖЕНИЕ СЧЁТОМ (v105): уходящая артель уносит
                // непогашенный харч и плату, поэтому граница застаёт её не
                // с пустой сумкой.
                load_season_upkeep(sSlot, standing);
                // ПРИВЕДЕНИЕ СОСТАВА (S19.2, 2026-09-18): на границе
                // стоящая артель дышит к пулу — добор из населения (дома,
                // сколько прокормит склад: окно этого же дня спишет сезон
                // с ПОЛНОГО состава), ссадка лишних обратно в население
                // (перенос, не баланс). В поле состав не трогается.
                if (boundary && perCrew > 0) {
                    {
                        auto& bg = stq.inventory[standing];
                        const int want = perCrew - 1;   // члены без лидера
                        int have = count_human_souls(bg.inv);
                        const int canFeed =
                            sInv.count_of(hunger_item_index())
                                / kDaysPerSeason;
                        int take = std::min(want - have,
                                            std::max(0, canFeed - have));
                        take = std::min(take, souls_home(stq, sSlot) - 1);
                        while (take-- > 0) {
                            // ГЕНЕРИК (CANON S4): массовый добор — стак,
                            // без ординала; имя душа зарабатывает историей
                            // (лидерство, найм в сюжет, вселение). Душа
                            // ПЕРЕЕЗЖАЕТ из дома (bleed_flock — v122: дома
                            // души головами) и надевает род строки крю;
                            // паства (worked) не меняется — «дома» стало
                            // «в поле».
                            SoldierRecord rec{};
                            rec.kind = std::uint16_t(ld.crews[i].npc);
                            rec.level = 1;
                            if (bleed_flock(sInv, 1) != 1) break;
                            if (!creatures_push(bg.inv, rec)) {
                                raise_flock_into_container(sInv, 1);
                                break;
                            }
                        }
                        // ССАДКА СУДИТ ЛЮДЕЙ: последняя человеческая
                        // душа сходит в население; табун артели суду
                        // состава не подсуден — лошадь не человек и в
                        // want не входит (дроссель ловли — в аукционе).
                        // Новейший людской слот = наименьший индекс области
                        // (старый обход slot_count-1 → 0 = first → 1023).
                        for (have = count_human_souls(bg.inv);
                             have > want; --have) {
                            int si = -1;
                            for (int k = bg.inv.creature_first();
                                 k < kMaxInventorySlots; ++k) {
                                if (is_folk_kind(std::uint16_t(
                                        creature_of_world_row(
                                            bg.inv.slots[std::size_t(k)]
                                                .def)))) {
                                    si = k;
                                    break;
                                }
                            }
                            SoldierRecord off{};
                            if (si < 0 || !creatures_take_at(bg.inv, si, off))
                                break;
                            // Домой — головой (v122); отказ контейнера
                            // честно возвращает душу в артель.
                            if (raise_flock_into_container(sInv, 1) != 1) {
                                creatures_push(bg.inv, off);
                                break;
                            }
                        }
                        // Приведённый состав — приведённый обоз (squad.h):
                        // добранная душа несёт свою спину, ссаженная уносит.
                        refresh_squad_carry(stq, handle_at(stq, standing));
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
            if (perCrew <= 0 || souls_home(stq, sSlot) < perCrew) continue;
            // СОЗДАНИЕ БЕЗ ПРЕДОПЛАТЫ СЕЗОНА (владелец 2026-09-19,
            // отменяет гейт 2026-09-17 «сезон содержания или не
            // поднимается»): «условие поднятия артели — это сколько ей
            // надо на сезон, а внутрь её загружать уже не обязательно;
            // худшее — если задержится, потеряет 1/8 по общему закону».
            // Сезонная нужда остаётся МЕРОЙ (upkeep_bill — её судит
            // окно), погрузка ниже — сколько склад даёт (haul_between
            // берёт что есть), недостача на окне = 1/8 контейнера в дезертиры
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
            spec.x = slot_x(stq, sSlot, gs.mapW);
            spec.y = slot_y(stq, sSlot, gs.mapW);
            spec.homeSettlementId = sId;
            for (int m = 1; m < perCrew; ++m) {
                // ГЕНЕРИК (CANON S4): члены артели — один стак, не
                // per-душевые ординалы (тот поток остаётся ИМЕНАМ:
                // лидерам и душам с историей — CANON S20.1 не про толпу).
                SoldierRecord rec{};
                rec.kind = std::uint16_t(spec.leaderType);
                rec.level = 1;
                if (!spec.members.push(rec)) break;
            }
            const MacroHandle ent = spawn_squad(
                gs, store_of(*mw.world), *mw.terrain, spec);
            if (stq.valid(ent)) {
                const std::uint16_t newSlot = ent.slot;
                // Души артели ВЗЯТЫ из дома (v122): лидер + члены сходят
                // головами (гейт souls_home >= perCrew выше гарантирует
                // достаточность); паства не меняется — они ушли В ПОЛЕ.
                bleed_flock(sInv, 1 + int(spec.members.size()));
                ++raised;
                auto& prt = stq.runtime[newSlot];
                prt.squadType = myType;
                prt.errandObject = myObject;
                // Сезонный груз содержания вместо провианта на рейс: еда —
                // баланс окна теперь, рейсовый ломоть умер у артелей
                // (остался у вылазок гарнизона — они не подсудны суду
                // состава).
                load_season_upkeep(sSlot, newSlot);
            }
        }
        for (int si = 0; si < soloCount; ++si) {
            // Живой одиночка (курьер дани) продолжает службу — его строка
            // закрыта им самим; новый — только на границе.
            if (claim_standing(std::uint16_t(ld.crews[solo[si]].npc))
                != kMacroNoSlot) {
                continue;
            }
            if (!boundary || souls_home(stq, sSlot) <= 0) continue;
            // ДУША КУРЬЕРА — ИЗ ТОГО ЖЕ ПУЛА (labour.h): соло-рождение
            // прежде шло мимо всякого счёта рук вовсе (npc_ai.cpp:5124 в
            // переписи девяти половин, §55) — «население <= 0» и всё.
            if (soloBudget <= 0) continue;
            SquadSpec spec{};
            spec.leaderType = ld.crews[solo[si]].npc;
            spec.x = slot_x(stq, sSlot, gs.mapW);
            spec.y = slot_y(stq, sSlot, gs.mapW);
            spec.homeSettlementId = sId;
            if (stq.valid(spawn_squad(gs, store_of(*mw.world),
                                      *mw.terrain, spec))) {
                bleed_flock(sInv, 1);   // душа курьера — из дома (v122)
                --soloBudget;
                ++raised;
            }
        }
        // ЛИШНИЙ СКВАД — «просто распускает» (S19.2, суд границы): стоящей
        // дома артели не досталось СТРОКИ (гейт закрылся, строку держит
        // полевая, пул ужался до меньшего числа сквадов) — души и остатки
        // домой. Вне границы неприкаянная артель просто стоит до суда.
        if (boundary) {
            for (auto& [r2, s2] : idleByHome) {
                if (r2 != sSlot || s2 == kMacroNoSlot) continue;
                dissolve_population_crew(s2, sSlot);
                s2 = kMacroNoSlot;
            }
        }
    });
    return raised;
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
        if (!ctx.mw.store) ctx.mw.store = &store_of(*mw.world);
        if (const ecs::MacroCell* pc =
                player_flag_cell(*mw.gs, *ctx.mw.store)) {
            ctx.playerX = float(ecs::cell_x(*pc, ctx.mapW));
            ctx.playerY = float(ecs::cell_y(*pc, ctx.mapW));
        }
        // Store свипа — адресом в конверте (выставлен выше): поведения
        // читают колонки, не спрашивая ctx реестра на каждом think (1е).
        // Слоты игрока — резолв ОДИН раз на свип из битов GameState
        // (1е кластер 5): распаковка двух полей, ноль сканов реестра.
        // Валидность спрашивается у store — фикстура без игрока несёт
        // сентинель, и оба слота честно остаются kMacroNoSlot.
        if (mw.gs && ctx.mw.store) {
            const MacroStore& st = *ctx.mw.store;
            const MacroHandle flag = player_flag_handle(*mw.gs);
            if (st.valid(flag)) ctx.playerFlagSlot = flag.slot;
            const MacroHandle home = player_squad_handle(*mw.gs);
            if (st.valid(home)) ctx.playerSquadSlot = home.slot;
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
// out of a dead creatures. Spawning here is safe: both drivers call this AFTER
// their sweep, где никто не держит ссылок на компоненты (грабля посадки 4:
// спавн реаллоцирует хранилище).
void settle_dead_squads(MacroWorld& mw) {
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    kill_fallen_squad_creatures(store_of(w), gs,
                                mw.econFacts, mw.econFactsUser);
    destroy_dead_macro_squads(store_of(w), gs, &gs.lootPoolValue);
}

} // namespace

void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks,
                       bool allowAutoBattle) {
    if (!mw.gs || !mw.world) return;   // no world, no thinking (fail-closed)
    GameState& gs = *mw.gs;
    ecs::World& w = *mw.world;
    MacroStore& st = store_of(w);

    build_squad_index(runtime.squadIndex, store_of(w), gs.mapW, gs.mapH);

    // Свежесть запечённой навигации — раз на свип, не в шаге (тор-закон
    // gigahrush2 «never re-bake per tick»: перепёк только на границах).
    if (mw.nav) nav_ensure(mw, *mw.nav);
    scent_ensure(gs.scent, gs.mapW, gs.mapH);

    TickContext ctx = make_tick_context(mw, runtime, allowAutoBattle);
    scent_player_deposit(ctx);   // игрок следит наравне со всеми (CANON S10)

    // Свип делит ОДИН RNG на всех — порядок обхода есть закон мира
    // (squad_walk.h): по ординалу, не по кишке хранилища. Игрока свип не
    // водит НИКОГДА — ни флажок, ни его родной сквад (слоты из ctx).
    collect_squads_by_ordinal(
        st, runtime.sweepOrder,
        [&](std::uint16_t slot) {
            return st.dead[slot] == 0 && slot != ctx.playerFlagSlot
                && slot != ctx.playerSquadSlot;
        });
    for (const SquadWalkEntry& sw : runtime.sweepOrder) {
        const std::uint16_t slot = sw.slot;
        const MacroHandle h = handle_at(st, slot);
        auto& cell = st.cell[slot];
        auto& kind = st.kind[slot];
        auto& rt   = st.runtime[slot];
        auto& hp   = st.pools[slot];

        // One think per call at most, as before: a caller that hands over a
        // huge jump does not get a burst of catch-up thinking, it gets one.
        rt.tickAccum += std::uint32_t(std::min<std::uint64_t>(ticks, kAiTicks));
        if (rt.tickAccum < kAiTicks) continue;
        rt.tickAccum -= kAiTicks;

        if (kind.type >= std::uint16_t(NPCType::Count)) continue;
        // A battle earlier in this very sweep may have killed this squad —
        // отбор жил на входе свипа, so re-check.
        if (st.dead[slot] != 0) continue;
        const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
        if (gate == ThinkGate::Dead) continue;
        refresh_overload_cost(rt, &st.inventory[slot]);
        // Decode → think in fractional scratch → encode (the scale split):
        // the STORE is one whole-cell number; the march's own float math
        // lives only on this think's stack.
        MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                   float(ecs::cell_y(cell, gs.mapW))};
        const float x0 = p.x, y0 = p.y;
        if (gate == ThinkGate::Think)
            dispatch(h, p, kind, rt, hp, ctx);
        settle_march_rhythm(h, p, rt, hp, p.x != x0 || p.y != y0, ctx);
        cell.idx = ecs::cell_index(int(p.x), int(p.y), gs.mapW);
    }

    // The END of every dead squad's story, once per tick (CANON S4): any
    // stragglers' survivors to the pool (idempotent for the already-drained;
    // it also catches a dead=1 creatures a save carried across the sweep
    // window), then the drained corpse-rows leave the map. Deferred to HERE
    // because the settle doors' callers still hold the entities mid-tick.
    settle_dead_squads(mw);
}

void tick_macro_npc_visuals(ecs::World& w, int mapW, int mapH, float dt) {
    if (mapW <= 0 || mapH <= 0 || dt <= 0.0f) return;

    // No player exclusion (подпосадка 4, owner: «универсально без игрокового
    // кода»): his squad and a possessed lord glide by the SAME law as every
    // sprite on the map — the walker moves the cell, this pass moves the eye.
    // ФЛИП 1в: чистый проход колонок store — глазу энтити не нужна вовсе,
    // это самый честный SoA-проход (только cell/visual/runtime/pools).
    MacroStore& st = store_of(w);
    for (std::uint16_t slot = 0; slot < std::uint16_t(kMacroEntityCap);
         ++slot) {
        if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
        const auto& c = st.cell[slot];
        const MacroPos p{float(ecs::cell_x(c, mapW)),
                         float(ecs::cell_y(c, mapW))};
        auto& v = st.visual[slot];
        const auto& rt = st.runtime[slot];
        const auto& hp = st.pools[slot];
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

    MacroStore& st = store_of(w);

    build_squad_index(runtime.squadIndex, store_of(w), gs.mapW, gs.mapH);

    // The same ONE assembly as the map-view driver. This used to be a paste
    // that had drifted (the deposits epitaph now lives on TickContext itself,
    // npc_ai.h); the two drivers may differ in HOW they walk the slots —
    // never in what world the squads think about (CANON.md S2).
    if (mw.nav) nav_ensure(mw, *mw.nav);
    scent_ensure(gs.scent, gs.mapW, gs.mapH);
    TickContext ctx = make_tick_context(mw, runtime, allowAutoBattle);
    scent_player_deposit(ctx);   // игрок следит наравне со всеми (CANON S10)

    // Тот же закон порядка, что у карт-драйвера (squad_walk.h): курсор —
    // позиция В ЭТОМ порядке. Лист собран на вызов; умерший внутри свипа
    // отсеивается проверкой Dead ниже, ровно как раньше. Игрока свип не
    // водит никогда — ни флажок, ни родной сквад (слоты из ctx).
    collect_squads_by_ordinal(
        st, runtime.sweepOrder,
        [&](std::uint16_t slot) {
            return st.dead[slot] == 0 && slot != ctx.playerFlagSlot
                && slot != ctx.playerSquadSlot;
        });

    while (runtime.pendingSweeps > 0
           && result.npcsProcessed < max_npc_ticks) {
        bool reachedEnd = true;

        for (std::size_t i = runtime.sweepCursor;
             i < runtime.sweepOrder.size(); ++i) {
            const std::uint16_t slot = runtime.sweepOrder[i].slot;
            const MacroHandle h = handle_at(st, slot);

            auto& cell = st.cell[slot];
            auto& kind = st.kind[slot];
            auto& rt   = st.runtime[slot];
            auto& hp   = st.pools[slot];
            if (kind.type < std::uint16_t(NPCType::Count)
                && st.dead[slot] == 0) {   // may have died this sweep
                const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
                if (gate != ThinkGate::Dead) {
                    refresh_overload_cost(rt, &st.inventory[slot]);
                    // Decode → fractional scratch → encode (the scale split).
                    MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                               float(ecs::cell_y(cell, gs.mapW))};
                    const float x0 = p.x, y0 = p.y;
                    if (gate == ThinkGate::Think) {
                        dispatch(h, p, kind, rt, hp, ctx);
                    }
                    settle_march_rhythm(h, p, rt, hp,
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

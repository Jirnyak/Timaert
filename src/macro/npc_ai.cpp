// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "core/stacks.h"           // kUnifiedCap — резерв скрэтчей порядка
#include "macro/upkeep_window.h"   // ОДИН суд границы на всякий контейнер
#include "macro/characters.h"  // стол анкет — ступень лестницы поведения
#include "macro/chronicle.h"
#include "macro/currency.h"
#include "macro/deal.h"   // deal/unit_mass_g/kNoCarryLimitG — один глагол обмена
#include "macro/economy.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/macro_stock.h"
#include "macro/entry_context.h"
#include "tables/faction.h"
#include "macro/labour.h"           // ОДИН пул рук места (CANON S4)
#include "macro/place_iter.h"    // for_each_place — обход мест по оси рода
#include "macro/place_registry.h"
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
#include "core/table_guard.h"  // страж порядка строк kAiModuleRows
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
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

// (МАССА ЕДИНИЦЫ СТРОКИ — `unit_mass_g@src/macro/deal.h`: уехала к двери
// `deal` вместе с клампом спины; провенанс килограмма существа стоит там.)

// Store свипа — из конверта (make_tick_context кладёт адрес в ctx.mw.store).
// Фолбэка через ctx реестра больше нет (M-150 ломоть A): store в конверте
// обязателен, звонящий проверяет ctx.mw.store.
inline MacroStore& store_mw(const MacroWorld& mw) {
    return *mw.store;
}
inline MacroStore& store_ctx(const TickContext& ctx) {
    return store_mw(ctx.mw);
}

// ── РЕЗОЛВ МЕСТА В ДУМКЕ: ОРДИНАЛ → СЛОТ (ломтик F) ─────────────────────
// Думка зовёт этот резолв по нескольку раз за think, поэтому он обязан быть
// дешевле скана капа (§6 граница O(N)) — и сегодня он такой САМ: резолв идёт
// бинарным поиском по справочнику идентичности стора
// (`macro_handle_by_spawn_id@src/macro/store.h`, M-271). Прежде дешевизну
// приходилось ПРИНОСИТЬ С СОБОЙ — порядком драйва отдельным параметром, — и
// у кого порядка не было (фикстура, дневная ротация с её `TickContext{}`),
// тот падал в скан: замер 64 дней назвал эту ветку самым частым сканом мира,
// 842 112 вызовов из 1 739 080 (48.4 %).
// Ось рода гейтится НЕ ЗДЕСЬ, а в самой двери `place_handle_by_ordinal`
// (`src/macro/squad.h`): пространство ординалов ОДНО (M-37), и «это место»
// отвечает только ось тела — одним написанием на весь мир. Ниже остался
// ПЕРЕХОДНИК «хэндл → слот», и он ОДИН на всех звонящих этого файла.
inline std::uint16_t place_slot_of(const MacroStore& st, int id) {
    if (id <= 0) return kMacroNoSlot;   // 0 = «никого» (ЗАКОН НУЛЯ-ОРДИНАЛА)
    const MacroHandle h = place_handle_by_ordinal(st, std::uint32_t(id));
    // «Дома нет» = kMacroNoSlot — ПОСЛЕДНЕЕ значение типа индекса (ЗАКОН
    // УЗКОГО ИНДЕКСА: слот 0 законен, нулём тут сказать нечего).
    return st.valid(h) ? h.slot : kMacroNoSlot;
}
inline std::uint16_t place_slot_by_id(const TickContext& ctx, int id) {
    return place_slot_of(store_ctx(ctx), id);
}

// УРНА ГОРОДОВ — ОДИН предикат на оба рейса (торговец и кочевник). Он стоял
// ДВУМЯ побайтово одинаковыми лямбдами в двух соседних поведениях, и это был
// второй ответ на один вопрос мира (DOD п.6), просто спрятанный в области
// видимости функции. Обход мест — одна дверь (`for_each_place`,
// `macro/place_iter.h`): ось рода ТЕЛА отвечает «город ли это», второго
// списка городов в мире нет. `notId` — ординал, который спрашивающий
// исключает (свой дом у торговца, текущая цель у кочевника).
inline bool is_other_city(const MacroStore& st, std::uint16_t slot,
                          int notId) {
    return SquadType(st.runtime[slot].squadType) == SquadType::City
           && int(st.spawnId[slot].index) != notId;
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

bool home_pos(MacroHandle self, const TickContext& ctx, XY& out) {
    // ДОМ = РОДИТЕЛЬ (M-245): ребро Parent реестра связей; колонка
    // `homeSettlementId` умерла. ОДИН эмитент ординалов (M-37).
    const std::uint16_t slot =
        place_slot_by_id(ctx, parent_of(store_ctx(ctx), self.slot));
    if (slot == kMacroNoSlot) return false;
    const MacroStore& st = store_ctx(ctx);
    out = {float(slot_x(st, slot, ctx.mapW)),
           float(slot_y(st, slot, ctx.mapW))};
    return true;
}

// ДОМ АРТЕЛИ — СЛОТ тела места; kMacroNoSlot = дома нет. (Спутник
// home_inventory умер с переводом сдачи груза на deal: сделка адресуется
// слотами, и склад по слоту берёт сама дверь.)
std::uint16_t home_place_slot(MacroHandle self, const TickContext& ctx) {
    return ctx.mw.gs
        ? place_slot_by_id(ctx, parent_of(store_ctx(ctx), self.slot))
        : kMacroNoSlot;
}

// Empty the gatherer's own bag of the catalog row `defIdx` into his home
// store — the shared arrival half of every honest work-loop (woodcutter,
// farmer). ОРДИНАЛ, А НЕ СТРОКА (ЗАКОН СЛОВАРЯ): строка цели переводится в
// ординал один раз рядом со своей таблицей (gatherer_item_index).
void deliver_bag_home(MacroHandle self, const TickContext& ctx, int defIdx) {
    if (!ctx.mw.store) return;
    MacroStore& st = store_ctx(ctx);
    const std::uint16_t homeSlot = home_place_slot(self, ctx);
    if (homeSlot == kMacroNoSlot) return;
    const int n = st.inventory[self.slot].inv.count_of(defIdx);
    if (n <= 0) return;
    // ОДНА сделка с пустым встречным лотом (ломоть 2 M-72): кредит-прежде-
    // дебета и «полный склад оставляет груз НА СПИНЕ» — законы самой двери;
    // приход гасит счёт дома СРАЗУ (CANON S10) — прежде этот путь шёл мимо
    // дверей и долг ждал дневной страховки.
    DealLine lot[1];
    lot[0] = DealLine{};
    lot[0].want = n;
    lot[0].def = std::uint16_t(defIdx);
    const DealFact fact = deal(st, self.slot, homeSlot, lot, {},
                               kNoCarryLimitG, kNoCarryLimitG,
                               ctx.mw.econFacts, ctx.mw.econFactsUser);
    // The arrival IS the gather flow: the pure econ steps announce their
    // own facts, but the agent work-loop lands its haul here — without
    // this fact every *_gathered column of the дубль-прогон reads zero
    // (measured: 4 years × 4 seeds of zeros, 2026-08-31). Количество —
    // ФАКТ сделки, не намерение.
    if (fact.settled && ctx.mw.econFacts) {
        EconFact f{};
        f.kind = EconFact::Kind::Gathered;
        f.commodity = commodity_of_item(defIdx);
        f.amount = fact.unitsAB;
        f.placeId = parent_of(st, self.slot);
        ctx.mw.econFacts(ctx.mw.econFactsUser, f);
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

bool at_goal(const MacroPos& p, const ecs::SquadGoal& g,
             const TickContext& ctx) {
    // Прибытие судится ЦЕЛЬЮ (M-1): адрес цели — клетка, близость меряется
    // к её точке тем же порогом, что и прежде (< 2 клеток) — сменился
    // НОСИТЕЛЬ адреса, закон посадки не менялся. Цели нет — прибытия нет.
    if (g.cell == ecs::kNoCell) return false;
    return torus_dist_sq(p.x, p.y, float(cell_x(g.cell, ctx.mapW)),
                         float(cell_y(g.cell, ctx.mapW)),
                         float(ctx.mapW), float(ctx.mapH)) < 4.0f;
}

// Точка марша из адреса цели: ноги (try_move) ходят ГЕОМЕТРИЕЙ, и пара
// координат законна ровно там (ЗАКОН АДРЕСА п.2 — в геометрии марша пара
// на месте, на хранении она была налогом).
XY goal_xy(const ecs::SquadGoal& g, const TickContext& ctx) {
    return {float(cell_x(g.cell, ctx.mapW)), float(cell_y(g.cell, ctx.mapW))};
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

void settle_exhaustion(MacroHandle e, const ecs::Pools& hp,
                       const TickContext& ctx);

// The weight of the ground under a body, from the one baked grid (defined
// below, beside the edge weight the greedy step reads). Declared here because
// the march rhythm needs it to price its hour, and the rhythm is settled
// before the stepping code is written.
float cell_weight(const TickContext& ctx, int x, int y);

// (No can_stand_at, and no nav_can_stand behind it. THE standing predicate
// died 2026-10-06 — «да уничтодить вторую стену она портит всё (ВЕСА БЫЛО
// ЕДИНОЕ РЕШЕНИЕ», владелец: a cell a walker could not stop on was a SECOND
// answer to the question the weight table already answered, and it held
// 4328 land cells outside every region of the baked navigation. Standing is
// now possible everywhere and lethality is a CONSEQUENCE of the two always-on
// processes — macro/recovery.h settle_pools_over_time.)

// ЧТО НЕСОМЫЙ ГРУЗ СТОИТ ЭТОМУ СКВАДУ В ЧАС — ЧИСЛО ОДНОЙ ДУМКИ, И ХРАНИЛИЩА
// У НЕГО НЕТ (вердикт владельца 2026-10-21, дословно: «убирай нафиг нам
// хранить скока перегруз ест сели просто достатчно хранить 2 значения вес
// который может нести сквад и вес который несёт»).
//
// ЗДЕСЬ СТОЯЛА `refresh_overload_cost`, ПИСАВШАЯ КОЛОНКУ `overloadCost`
// (`int16`), и последним её шагом был НЕМОЙ КЛАМП `std::clamp(cost, 0, 32767)`
// — M-302. Вся цепочка выше считала честно (`carry_capacity_g` отдаёт `int64`,
// цена есть точное `(g + 999) / 1000`), обрезал РОВНО последний шаг, на ширине
// хранилища. Замер назвал цену обрезания: на 3 сидах × 128 дней кламп
// СРАБОТАЛ 2 раза из 454 679 260 пересчётов, и в худшем попадании настоящая
// цена была 64 489 против потолка 32 767 — то есть перегруженный сквад
// заплатил за час ВДВОЕ меньше должного.
//
// ПОЧЕМУ СНОС, А НЕ ШИРИНА: колонка не переносила число через ВРЕМЯ, она
// переносила его через ДЕВЯТЬ СТРОК. Свип считал цену ровно один раз на думку
// прямо перед единственным читателем, и это ИЗМЕРЕНО, а не выведено:
// пересчётов 454 679 260, чтений 454 679 260 — ровно столько же. У числа, чей
// расчёт и чьё чтение стоят в одной думке, тотала нет; ему выдали колонку, у
// колонки оказалась ширина, а у ширины — обрезание. Теперь цена едет
// ПАРАМЕТРОМ, `int` от начала до конца, и обрезать её нечему по построению.
//
// Хранимых значений осталось ровно два, как и велено: спина (`carry_capacity_g`
// — своя колонка анкеты плюс граммы голов из сводки) и несомый вес
// (`inventory_mass_g` — тотал сводки, разряд А). Перегруз есть их разница, и
// третьего носителя у него больше нет.
inline int overload_cost_of(const ecs::MacroNpcRuntime& rt,
                            const ecs::NpcInventory* bag) {
    if (!bag) return 0;
    return overload_charge_from_capacity(carry_capacity_g(rt, bag->inv),
                                         bag->inv).cost;
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
        if (int(hp.sp) >= maxSp / 2) wake_from_rest(rt);
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
// WHERE a body stands no longer enters the question (2026-10-06): rest is owed
// to anything that stopped, ocean included, and the ocean stays lethal because
// its HOUR burns 20 SP against 13.75 of rest — the sign of the difference, not
// a predicate. The old «standing where no camp is possible is not rest» was the
// second half of the wall, and it died with the first.
//
// STOPPED is not the same question as "did not change cell this think", and
// getting that wrong is how the first cut of this law paid marchers to march:
// the pace is fractional (kMacroWalkCellsPerHour against a think's slice of
// the day), so a body on the road banks part-cells and stands still on maybe a
// quarter of its thinks. Reading those as rest handed a walking squad free
// stamina and no squad ever ran out again. A body has stopped when it is where
// it meant to be, or when it has DECIDED to stop — which is exactly the
// player's own gate, «маршрут пуст», said in the squads' words.
//
// `overloadCost` — ЦЕНА ГРУЗА ПАРАМЕТРОМ, А НЕ КОЛОНКОЙ (M-302): свип считает
// её дверью `overload_cost_of` выше в той же думке, ДО поведения, и передаёт
// сюда. МОМЕНТ РАСЧЁТА ОСТАВЛЕН ТОТ ЖЕ, что был у снесённой колонки, именно
// чтобы единственным изменением поведения осталось снятие обрезания, — и
// паритет это ДОКАЗАЛ атрибуцией: два сида из трёх побайтово идентичны (там
// кламп не срабатывал), а на третьем возврат ОДНОГО клампа складывает мир
// обратно байт в байт. `int` вместо `int16` делает прежнее немое обрезание на
// 32 767 невыразимым.
void settle_march_rhythm(MacroHandle e, const MacroPos& p,
                         ecs::MacroNpcRuntime& rt, ecs::Pools& hp,
                         bool moved, const TickContext& ctx,
                         int overloadCost) {
    const int maxSp = std::max<int>(1, hp.maxSp);

    // THE two rates of this body, in this cell, this hour — computed ONCE and
    // read by both halves below, so the decision and the settle can never
    // disagree about what standing here is worth.
    //
    // The hour is priced from the ground UNDER THE BODY plus the two terms
    // that were the dead per-step price's other half: the squad's own load
    // (`overloadCost`, priced from its bag by the sweep before the behaviour
    // ran — a parameter, never a column, M-302) and its training
    // (`travelRank`, a sheet cache the march itself read, so no sheet is
    // rebuilt per think).
    const float burnRate = burn_stamina_per_hour(
        cell_weight(ctx, int(p.x), int(p.y)), overloadCost,
        skill_mult_of(SkillId::Travel, int(rt.travelRank)));
    const bool campRepays =
        camp_repays_its_hour(burnRate, maxSp, int(rt.marathonRank));

    // The automaton's CAMP decision (npc_ai.h kCampBarDivisor): legs below an
    // eighth of the bar pitch camp — WHERE CAMP IS WORTH PITCHING. The
    // half-bar wake-up (prepare_) resumes the leg.
    //
    // `camp_repays_its_hour` is THE door of that question
    // (movement_cost.h) — one law, and the player's own driver asks the same
    // one. Why it is not the standing wall coming back, and why the demolition
    // would defeat its own purpose without it, is written there.
    //
    // ONE camp decision, and it used to be TWO: settle_exhaustion took the
    // same decision again for a body already in debt, each behind its own
    // copy of the standing predicate. A bar at or below an eighth already
    // includes every negative bar there is, so the second site was answering a
    // question this one had answered — it went with the predicate.
    if (campRepays
        && int(hp.sp) <= maxSp / kCampBarDivisor
        && rt.state != std::uint8_t(NPCState::Resting)) {
        rt.stateAfterRest = rt.state;   // пауза, не амнезия (components.h)
        rt.state = std::uint8_t(NPCState::Resting);
        rt.stateTimer = 0;
    }
    // ...and a body ALREADY camped where the ground turned against it is woken
    // on the spot, for the same reason: the rising tide of a flooded cell, a
    // re-centred frame, a bridge washed away. Sleeping through that is the
    // deadlock above with one extra step.
    if (!campRepays && rt.state == std::uint8_t(NPCState::Resting)) {
        wake_from_rest(rt);
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
                         || at_goal(p, store_ctx(ctx).goal[e.slot], ctx)
                         || (!moved && rt.moveBudget >= 1.0f);

    // ...and `!moved` on top, because a think that arrived still MARCHED: you
    // do not walk two cells and take a slice of rest in the same breath. Rest
    // begins on the first think after the legs stop.
    const bool resting = stopped && !moved;

    // THE two always-on processes, THE one door (recovery.h
    // settle_pools_over_time), asked UNCONDITIONALLY — which is the shape of
    // the 2026-10-06 law and not a widening of it. The burn runs for a body
    // that marched, a body that stood and a body asleep in camp alike; only
    // the REGEN half reads `resting`. Before this it was an `if` around the
    // whole call, and the price of a step lived in try_move below — two laws
    // of one quantity, the thing §5 п.14 is written about.
    const int bite = settle_pools_over_time(
        hp, kAiTickGameHours, int(rt.marathonRank), burnRate,
        /*regenerates=*/resting);

    // ...and a body the BITE killed is buried here. Gated on the bite itself,
    // not on `hp <= 0`: a squad cut down by an auto-battle earlier in this
    // very think already went through its own death door, and a second funeral
    // would settle its leader and kill its creatures twice.
    if (bite > 0) settle_exhaustion(e, hp, ctx);
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

// THE march, and since 2026-10-06 it takes NO BARS: a step has no price, so
// the walk has no business holding the block it used to debit. What it still
// owns is the geometry — budget, the baked gait, the greedy fallback and the
// entry stamp. Who pays for the hours is settle_march_rhythm, once per think.
void try_move(MacroPos& p, ecs::MacroNpcRuntime& rt,
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
        int bx = -1, by = -1;
        float bw = 1e30f;
        // Сосед шага — арифметика ИНДЕКСА (cell_step, ЗАКОН АДРЕСА): здесь
        // стояло четыре wrapi — аппаратное деление на каждом шаге марша
        // каждого сквада.
        const std::uint32_t hereIdx = cell_of(ix, iy, ctx.mapW);

        // ЗАПЕЧЁННАЯ ПОХОДКА (CANON S7, 2026-09-02): округи + порталы +
        // граф — три чтения, ни волны, ни поиска. Жадный шаг остаётся миру
        // без запечённого слоя.
        // Гейт «стоит на суше» снят 2026-10-06 вместе со стеной: после единой
        // заливки у КАЖДОЙ клетки тора есть округа, поэтому походке больше
        // нечего не знать — она отвечает и из воды.
        if (!flying && ctx.mw.nav && ctx.mw.nav->baked()) {
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
            // The straight step is the TIE-BREAKER candidate, and since
            // 2026-10-06 it is nothing but that: it used to be filtered by the
            // standing predicate, and so did every neighbour below, and the
            // predicate's three exceptions (dry feet, flight, a fleeing body's
            // ford) were three answers to one question. All four died
            // together. Water is now simply the dearest row of the price list,
            // so a coast is walked AROUND because it is expensive, and a river
            // IS forded when going round costs more — which was Session 21's
            // own design before the wall overrode it.
            const auto walker_w = [&](int nx2, int ny2) {
                if (flying) return 1.0f;   // воздух — дорога летуна
                return edge_weight(ctx, ix, iy, nx2, ny2);
            };
            bx = straight.nx;
            by = straight.ny;
            bw = walker_w(bx, by);
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
                    const float w = walker_w(nx, ny);
                    if (w < bw) { bx = nx; by = ny; bw = w; }
                }
            }
        }
        if (bx < 0) break;   // no step progresses: the leg ends here

        // Entry-side stamp: the signed step of THIS cell change, torus-folded
        // (stepping east off the map's edge is still +1, not -(w-1)).
        int dx = bx - ix;
        if (dx > 1) dx = -1; else if (dx < -1) dx = 1;
        int dy = by - iy;
        if (dy > 1) dy = -1; else if (dy < -1) dy = 1;

        rt.entryDir = pack_entry_dir(dx, dy);
        rt.entryTicks = 0;

        ix = bx; iy = by;
        rt.moveBudget -= 1.0f;

        // NOTHING IS CHARGED HERE, AND THAT IS THE LAW (2026-10-06). A step
        // has no price: what the body pays is the HOUR it spends on ground,
        // settled once per think by settle_march_rhythm above. Three things
        // stood on this spot and all three went together — the per-step
        // charge, the legs' REFUSAL of a step they could not pay for, and the
        // `sp < 0` break that ended a think mid-stride. Each was a second
        // writer of the same bar, and the refusal was a THIRD wall: a body
        // could be stopped by price as surely as by water.
        //
        // What stops a body now is the world, not a gate: heavy ground is
        // slower (terrain_speed_mult feeds `perThink`), debt bites HP
        // quadratically, and a bar below an eighth pitches camp by DECISION.
        // «Pressing on is priced, never gated» (CANON S14.1) finally holds on
        // the macro march too.

        // Never hop OVER the player's cell in a multi-cell think: the forced
        // encounter (Inc 6) is geometric, so the squad stops ON the meeting
        // cell where the door can see it.
        if (ix == playerCellX && iy == playerCellY) break;
    }

    p.x = float(ix);
    p.y = float(iy);
    // Скорость картинки — РАССТОЯНИЕ, пройденное этой думкой, за её период
    // (одна дверь, `set_visual_speed@src/macro/squad.h`). Число то же, что
    // здесь считалось своей парой строк до M-289; дверь общая с маршем
    // игрока, потому что вопрос один.
    set_visual_speed(rt, std::sqrt((p.x - oldX) * (p.x - oldX)
                                   + (p.y - oldY) * (p.y - oldY)),
                     kAiPeriodSeconds);

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

void ai_home_wanderer(MacroHandle self, MacroPos& p,
                      ecs::MacroNpcRuntime& rt,
                      [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    XY home;
    if (!home_pos(self, ctx, home)) return;
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            float dsq = torus_dist_sq(p.x, p.y, home.x, home.y,
                                      float(ctx.mapW), float(ctx.mapH));
            if (dsq > 400.0f) {
                ecs::goal_set(g, cell_of(int(home.x), int(home.y), ctx.mapW),
                              0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Returning);
            } else {
                XY t = pick_random_nearby(home.x, home.y, 12, ctx);
                ecs::goal_set(g, cell_of(int(t.x), int(t.y), ctx.mapW),
                              0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 20));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
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
// crew DECIDED on this row — решение кэшируется ЦЕЛЬЮ, `what = (Cell,
// строка ниже)`. (Здесь стояло «аукцион выдал строку поручением
// `rt.errandObject`» — аукцион казнён, колонка поручения кодовых сайтов не
// имеет; строку цели читает `artel_goal_def`.)

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
    // ПАСТБИЩЕ (CANON S10 «ЛОШАДЬ — ЮНИТ»): стоящий загон с поголовьем, и
    // только он — точное зеркало HomeField. Бутстрап «а без него — лучшая
    // огораживаемая клетка» снесён 2026-10-12 (M-258) вместе с полем дикого
    // табуна: число стада есть число ФИЧИ, и брать его с клетки без фичи
    // нечего. Загоны ставит генерация (M-79).
    HomePasture,
};

struct GathererDef {
    ResourceFieldId row;        // what leaves the world
    // (КОЛОНКИ `commodity` И `creatureYield` УМЕРЛИ 2026-10-23, ломоть 1
    // M-72: словарь «поле→предмет» 3 → 1. Выход цели ВЫВОДИТСЯ из колонок
    // реестра полей и таблицы фич — см. gatherer_item_index ниже; второй
    // носитель того же ответа здесь был бы вторым словарём, DOD п.6.)
    Worksite        worksite;
    // СКОЛЬКО ОДИН РАБОТНИК БЕРЁТ ЗА ДЕНЬ — темп ЭТОГО источника. Колонка
    // появилась с ПИЩЕЙ (владелец, 2026-09-18: «просто сделать, что охота не
    // такая выгодная по добыче, как поле»): один и тот же товар честно
    // приходит из разных источников с разной отдачей, и это ЧИСЛО, а не
    // ветка. Якорь мира — kGatherPerWorkerDay (S10, «добытчик кормит 32»).
    int perWorkerDay;
};

constexpr GathererDef kGathererDefs[] = {
    // ПИЩА из двух источников: пашня — полный якорь, охота — четверть его
    // (зверь бегает, а колос стоит; число — крутилка дубль-прогона). Именно
    // это кормит хутор на скале, где пашни нет, а зверь есть.
    {ResourceFieldId::Wheat,  Worksite::HomeField,     kGatherPerWorkerDay},
    {ResourceFieldId::Fauna,  Worksite::HomeFauna,     kGatherPerWorkerDay / 4},
    // ЛЁН — ТА ЖЕ ЗЕМЛЯ, СВОЯ ПАРЦЕЛЛА (владелец, 2026-09-20: «на пашне уже
    // пшеница-пища, значит нужна фича льняное поле»). Ряд тот же арабельный:
    // клетка под льном — это клетка, не занятая хлебом, и конкуренция за
    // землю выходит сама, без второго поля и без нового ресурса. Четверть
    // якоря — тем же числом охота слабее пашни (крутилка дубль-прогона).
    // ЧТО с парцеллы берут — говорит ФИЧА (FT_FlaxField → fibre), не цель.
    {ResourceFieldId::Wheat,  Worksite::HomeFlaxField, kGatherPerWorkerDay / 4},
    // ШАХТЁР — ОДНА ЦЕЛЬ НА ВСЕ ЖИЛЫ (вердикт 2026-10-23: артель не
    // профессия, добыча универсальна): что добудется, решает фича шахты,
    // на которую артель пришла, — её строка, не строка цели.
    {ResourceFieldId::Vein,   Worksite::Deposit,       kGatherPerWorkerDay},
    {ResourceFieldId::Trees,  Worksite::ForestCell,    kGatherPerWorkerDay},
    // ЛОШАДЬ-ЮНИТ: темп 1 — поимка стоит работнику ДЕНЬ (цикл = целый бар
    // по закону цены труда S14.1), против 32 колосьев той же ценой; выход
    // не товар, а существо — умолчание ряда Livestock (реестр полей).
    {ResourceFieldId::Livestock, Worksite::HomePasture, 1},
};
constexpr int kGathererGoalCount =
    int(sizeof(kGathererDefs) / sizeof(kGathererDefs[0]));

// ФИЧА, КОТОРУЮ ПОДРАЗУМЕВАЕТ WORKSITE ЦЕЛИ — мост от «где работа» к «что
// кладут в руки»: оверрайд выхода читается СО СТРОКИ ФИЧИ (features.h), не
// дублируется здесь. FT_None = worksite фичу не называет, выход — умолчание
// ряда реестра.
constexpr FeatureType worksite_feature(Worksite ws) {
    return ws == Worksite::HomeField     ? FT_Field
         : ws == Worksite::HomeFlaxField ? FT_FlaxField
         : ws == Worksite::HomePasture   ? FT_Pasture
                                         : FT_None;
}

// ВЫХОД ЦЕЛИ СУЩЕСТВОМ — умолчание её ряда, если фича worksite не
// переопределила выход предметом (предмет фичи старше существа ряда —
// та же лестница, что в field_harvest).
inline NPCType gatherer_creature_yield(const GathererDef& gd) {
    if (feature_def(worksite_feature(gd.worksite)).yieldItem)
        return NPCType::Count;
    return resource_field_def(gd.row).yieldCreature;
}

// ОРДИНАЛ ВЫХОДА СТРОКИ ЦЕЛИ — РЕЗОЛВ ОДИН РАЗ, РЯДОМ СО СВОЕЙ ТАБЛИЦЕЙ
// (ЗАКОН СЛОВАРЯ И ОРДИНАЛА п.1: строка живёт на границе, внутри мира — число;
// образец — `r.needIdx` в econ_day.cpp). -1 у строки, чей выход СУЩЕСТВО:
// у него нет каталожной строки товара, он встаёт душой в контейнер. Предмет
// называют КОЛОНКИ реестра полей и таблицы фич (ломоть 1 M-72, словарь 3 → 1);
// резолв ленивый, потому что каталог виден только своей единице трансляции.
int gatherer_item_index(int goal) {
    static const std::array<int, std::size_t(kGathererGoalCount)> kOut = [] {
        std::array<int, std::size_t(kGathererGoalCount)> m{};
        for (int g = 0; g < kGathererGoalCount; ++g) {
            const GathererDef& gd = kGathererDefs[g];
            const char* fid =
                feature_def(worksite_feature(gd.worksite)).yieldItem;
            const ResourceFieldDef& row = resource_field_def(gd.row);
            const char* id = fid ? fid
                : (row.yieldCreature == NPCType::Count ? row.yieldItem
                                                       : nullptr);
            m[std::size_t(g)] = id ? item_index(id) : -1;
        }
        return m;
    }();
    return (goal >= 0 && goal < kGathererGoalCount)
        ? kOut[std::size_t(goal)] : -1;
}

// (gatherer_commodity_index УМЕР 2026-10-23: прейскурант урны берёт товар
// от РАЗРЕШЁННОГО предмета места — commodity_of_item(goalItem), — потому что
// у цели-жилы статического товара нет, его называет фича рабочего места.)

// ПРЕДМЕТНЫЕ ВЫХОДЫ ЦЕЛИ-ЖИЛЫ — строки фич шахт (род решает фича рабочего
// места). Нужен учёту груза и выгрузке домой: у цели со статическим выходом
// ординал один, у жилы он — вопрос клетки, но МНОЖЕСТВО возможных выходов
// названо данными (kFeatureDefs, numberRow == Vein), не веткой.
const std::vector<int>& vein_yield_ordinals() {
    static const std::vector<int> kOut = [] {
        std::vector<int> m;
        for (std::size_t t = 0; t < std::size_t(FT_Count); ++t) {
            const FeatureDef& def = kFeatureDefs[t];
            if (def.numberRow != ResourceFieldId::Vein || !def.yieldItem)
                continue;
            const int idx = item_index(def.yieldItem);
            if (idx >= 0) m.push_back(idx);
        }
        return m;
    }();
    return kOut;
}

// СТРОКА ЦЕЛИ ДОБЫЧИ — ИЗ КОЛОНКИ ЦЕЛИ, НЕ ИЗ ПОРУЧЕНИЯ (ломоть 4 M-72):
// писатель поручений — аукцион — казнён, «что добыть» решает сам модуль
// Artel (вердикт 2026-10-23 №1) и кэширует решение ТЕГОМ ЦЕЛИ: род Cell,
// ординал = строка kGathererDefs. nullptr = цель не о добыче (решения ещё
// не было) — no goal = no conjured work, the fail-closed rule.
const GathererDef* artel_goal_def(const ecs::SquadGoal& g) {
    if (ecs::goal_kind(g.what) != ecs::GoalKind::Cell) return nullptr;
    const std::uint32_t row = ecs::goal_ordinal(g.what);
    if (row >= std::uint32_t(kGathererGoalCount)) return nullptr;
    return &kGathererDefs[row];
}

// Пастбище артели: стоящая FT_Pasture с живым поголовьем — и ТОЛЬКО она.
// Точное зеркало find_home_field: нет парцеллы — нет работы, fail-closed.
//
// БУТСТРАП-ВЕТКА СНЕСЕНА 2026-10-12 (M-258), И ЭТО НЕ УПРОЩЕНИЕ, А СНЯТИЕ
// ЛЖИ. Она искала «лучшую огораживаемую клетку» и отправляла артель брать
// стадо с клетки, на которой НЕТ ЗАГОНА, — то есть с дикого табуна, стоявшего
// по вердикту владельца «бредом» (своё поле на весь мир под невидимых
// лошадей). Число стада теперь есть число ФИЧИ, и на клетке без фичи его нет
// по построению: ветка вернула бы цель, с которой нечего взять.
// ДЫРА НАЗВАНА И ОТКРЫТА (§1): загоны ставит ГЕНЕРАЦИЯ — «сложная весовая
// контекстная» (владелец 2026-10-12, наряд M-79), — и до неё эта цель не
// находит работы ни на одном сиде. Заглушки не ставим.
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
            // Род фичи тут НЕ спрашивается: «моё ли это число» отвечает сам
            // ряд своей дверью (`resource_field_read` → носитель стада читает
            // колонку `numberRow` фичи), поэтому загон льна или шпиль дадут
            // здесь ноль, не зная друг о друге.
            const int stock =
                resource_field_read(ctx.mw, ResourceFieldId::Livestock, cx, cy);
            if (stock <= 0) continue;   // нет загона или выеден начисто
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

} // namespace

// ПОДЪЁМ ИЗ ЛАГЕРЯ — ОДНА ДВЕРЬ, И ЭТО НЕ СТИЛЬ (DOD п.6; сведено 2026-10-12,
// M-254). ЛАГЕРЬ — ПАУЗА, НЕ АМНЕЗИЯ (`stateAfterRest@src/ecs/components.h`):
// подъём возвращает ПРЕРВАННУЮ ногу — Returning остаётся Returning. Старая
// побудка в Idle посреди дороги домой отдавала артель правилу «Idle вне дома =
// продолжить рейс к рынку», и рейс не завершался никогда.
//
// ПОЧЕМУ ДВЕРЬ, А НЕ ТРИ СТРОКИ НА МЕСТЕ: причин встать ДВЕ и они разные —
// SP добрал половину бара, и земля перестала окупать лагерь. Пока тройка
// строк стояла в обоих местах списком, это были ДВА ОТВЕТА на один вопрос
// мира («как тело просыпается»), из которых правка одного молча расходится с
// другим. Условий по-прежнему два; ПОДЪЁМ — один.
void wake_from_rest(ecs::MacroNpcRuntime& rt) {
    rt.state = rt.stateAfterRest;
    rt.stateAfterRest = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
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

// РАБОЧЕЕ МЕСТО ЖИЛЫ — ШАХТА, НЕ ГОЛАЯ ГЕОЛОГИЯ (вердикт 2026-10-23:
// «добыча … существует по шахтам»): артель ищет клетку с фичей, чьё число
// есть залежь (`FeatureDef::numberRow == Vein`), и живой залежью в `worked`.
// Шахты ставит генерация у поселений; выработанная (залежь 0) рабочим местом
// не является — и честно отпадает тем же предикатом.
bool find_home_deposit(const TickContext& ctx, ResourceFieldId row,
                       const XY& home, XY& out) {
    (void)row;   // ряд один — жила; подпись держит форму вызова worksite
    if (!ctx.mw.features || !ctx.mw.gs || ctx.mapW <= 0) return false;
    const FeatureLayer& fl = *ctx.mw.features;
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
        // Шахта с живой залежью — предикат колонкой строки фичи, не родом;
        // залежь спрашивается ДВЕРЬЮ ряда (worked сырьём читает только она).
        if (feature_def(fl.at(n)).numberRow != ResourceFieldId::Vein) return;
        if (resource_field_read(ctx.mw, ResourceFieldId::Vein, x, y) == 0)
            return;   // выработана — не рабочее место
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

// ТОЖЕ ОПРЕДЕЛЕНЫ НИЖЕ, А СПРАШИВАЮТСЯ ЗДЕСЬ — ВИДОМ И ПУЛЬТОМ рейса (та же
// идиома, что у `march_is_stuck_` строкой выше): законы дороги и погрузки
// живут рядом со своей торговой секцией, а дверь к ним обязана стоять ДО
// классов, которые её проносят модулю. Переносить определения вверх значило
// бы разорвать секцию с её шрамами.
float march_days_(const TickContext& ctx, int ax, int ay, int bx, int by);
float route_fear_(const TickContext& ctx, int ax, int ay, int bx, int by);
long long plan_home_load_(MacroStore& st, std::uint16_t homeSlot,
                          std::uint16_t traderSlot, const Skills& site,
                          std::int64_t capG, EconFactSink sink, void* user);
// ФОЛБЭКИ — ТОЖЕ ГЛАГОЛЫ ПУЛЬТА: сквад без дома и сквад без цели ходят
// чужими модулями, и право позвать их даёт пульт, а не прямой вызов из тела.
void ai_nomad(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx);


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

// ═════ ФОРМА АИ-МОДУЛЯ — ОБРАЗЕЦ (ломоть 4 M-72) ═════════════════════════
// Вердикт владельца 2026-10-22 №1 (CANON «AI-МОДУЛИ»): «модуль получает
// const-КОНТЕКСТ (чтение мира родами пакета) и ПУЛЬТ (глаголы-двери); мира
// в руках у модуля нет, запись мимо пульта несобираема». Ограничение ставит
// ПОДПИСЬ (прецедент S29): телу модуля передаются ровно три вещи — СВОЯ
// СТРОКА (мутабельные колонки самого сквада), ВИД (const-чтение мира) и
// ПУЛЬТ (глаголы). TickContext лежит в виде и пульте ПРИВАТНО: дотянуться
// до MacroWorld из тела модуля не из чего — стена стоит подписью, не ревью.
// Форму СКОПИРОВАЛИ `caravan_think` и `collector_think` (рейс сбыта и
// сборщик дани) — у трёх модулей ОДИН вид и ОДИН пульт, и всякая новая
// нужда приходит в них const-методом либо глаголом. Остался модуль места
// (ломоть 5); переходники-пятёрки умрут одним флипом подписи kAiModuleRows,
// когда мигрирует последний модуль.

// СВОЯ СТРОКА: колонки самого сквада. Себя модуль пишет без пульта — право
// на МИР и на чужое даёт только глагол.
struct AiSelf {
    MacroHandle h;
    MacroPos& pos;
    ecs::MacroNpcRuntime& rt;
    ecs::Pools& pools;
    ecs::SquadGoal& goal;
    Inventory& bag;   // свой контейнер: руки, спина, души
};

// ВИД: const-чтение мира. Новая нужда модуля = новый const-метод, никогда
// прямой MacroWorld в теле.
class AiView {
public:
    explicit AiView(const TickContext& ctx) : ctx_(ctx) {}
    int map_w() const { return ctx_.mapW; }
    bool home(MacroHandle self, XY& out) const {
        return home_pos(self, ctx_, out);
    }
    std::uint16_t home_slot(MacroHandle self) const {
        return home_place_slot(self, ctx_);
    }
    bool worksite(const GathererDef& def, const MacroPos& p,
                  const XY& home, XY& out) const {
        return find_worksite(def, ctx_, p, home, out);
    }
    int field_read(ResourceFieldId row, int x, int y) const {
        return resource_field_read(ctx_.mw, row, x, y);
    }
    bool at(const MacroPos& p, const ecs::SquadGoal& g) const {
        return at_goal(p, g, ctx_);
    }
    XY goal_pos(const ecs::SquadGoal& g) const { return goal_xy(g, ctx_); }
    float home_dist_sq(const MacroPos& p, const XY& home) const {
        return torus_dist_sq(p.x, p.y, home.x, home.y,
                             float(ctx_.mapW), float(ctx_.mapH));
    }
    // ЦЕННОСТЬ СТРОКИ ДОМА — закон цены казнённого аукциона ротации,
    // переживший его форму: непогашенная нужда сама дорожает дефицитом
    // (stock_price × season_demand_for). Ею скорится решение «что добыть».
    long long need_price(std::uint16_t placeSlot, int itemIdx) const {
        if (!ctx_.mw.store || placeSlot == kMacroNoSlot || itemIdx < 0)
            return 0;
        const MacroStore& st = *ctx_.mw.store;
        const ItemDef* d = item_def_at(itemIdx);
        if (!d || d->value <= 0) return 0;
        const Inventory& homeInv = st.inventory[placeSlot].inv;
        const int demand = season_demand_for(
            itemIdx, st.upkeep[placeSlot].needDebt,
            souls_home(st, placeSlot), st.sheet[placeSlot].skills, &homeInv);
        return stock_price(d->value, homeInv.count_of(itemIdx), demand);
    }
    // ЧЕГО СТРОКЕ ДОМА НЕ ХВАТАЕТ — та же дверь `season_demand_for`, другой
    // вопрос: цена говорит «почём», нехватка — «сколько ещё надо». Вход
    // считается ДВАЖДЫ, и это осознанно: закон выведенной величины прямо
    // предпочитает перевывод хранению тотала («устаревший тотал врёт МОЛЧА, а
    // перевывод всего лишь тратит такты», DOD п.9) — третьей колонки с
    // кэшем спроса здесь не заводится.
    int need_lack(std::uint16_t placeSlot, int itemIdx) const {
        if (!ctx_.mw.store || placeSlot == kMacroNoSlot || itemIdx < 0)
            return 0;
        const MacroStore& st = *ctx_.mw.store;
        const Inventory& homeInv = st.inventory[placeSlot].inv;
        const int lack = season_demand_for(
                             itemIdx, st.upkeep[placeSlot].needDebt,
                             souls_home(st, placeSlot),
                             st.sheet[placeSlot].skills, &homeInv)
                         - homeInv.count_of(itemIdx);
        return lack > 0 ? lack : 0;
    }
    // ── МЕСТО: ОДИН ПЕРЕХОДНИК ОРДИНАЛА И ТРИ ЕГО КОЛОНКИ ────────────────
    std::uint16_t place_slot(int id) const { return place_slot_by_id(ctx_, id); }
    std::uint32_t place_id(std::uint16_t slot) const {
        return store_ctx(ctx_).spawnId[slot].index;
    }
    std::uint32_t place_cell(std::uint16_t slot) const {
        return store_ctx(ctx_).cell[slot].idx;
    }
    bool is_settlement(std::uint16_t slot) const {
        return place_is_settlement(
            SquadType(store_ctx(ctx_).runtime[slot].squadType));
    }
    XY place_xy(std::uint16_t slot) const {
        const MacroStore& st = store_ctx(ctx_);
        return {float(slot_x(st, slot, ctx_.mapW)),
                float(slot_y(st, slot, ctx_.mapW))};
    }
    // МЕСТО ЦЕЛИ — ОДНА ДВЕРЬ НА ТРИ ЧИТАТЕЛЯ РЕЙСА (прежде этот тернарник
    // стоял ТРЕМЯ написаниями в одном модуле: «не дома», погрузка и сделка на
    // рынке — DOD п.6). kMacroNoSlot = цель местом не названа либо место
    // умерло, и это ЗНАЧЕНИЕ, а не ошибка: рейсу тогда некуда ехать.
    std::uint16_t goal_place(const ecs::SquadGoal& g) const {
        return ecs::goal_kind(g.what) == ecs::GoalKind::Place
            ? place_slot_by_id(ctx_, int(ecs::goal_ordinal(g.what)))
            : kMacroNoSlot;
    }
    // Долг вассала на ребре Suzerain (M-245) — есть ли что взыскивать.
    bool owes_tithe(std::uint16_t placeSlot) const {
        return sm::owes_tithe(store_ctx(ctx_), placeSlot);
    }
    // ДНИ ПУТИ и СТРАХ МАРШРУТА — две меры решения «дальше или домой», обе
    // над одной дверью навигации (nav_path_cost), обе чистое чтение.
    float march_days(int ax, int ay, int bx, int by) const {
        return march_days_(ctx_, ax, ay, bx, by);
    }
    float route_fear(int ax, int ay, int bx, int by) const {
        return route_fear_(ctx_, ax, ay, bx, by);
    }
private:
    const TickContext& ctx_;
};

// ПУЛЬТ: глаголы-двери — всё, чем модуль касается мира и соседей.
class AiPult {
public:
    explicit AiPult(const TickContext& ctx) : ctx_(ctx) {}
    // Транзакция «поле → контейнер» — одна дверь (ломоть 1 M-72).
    int harvest(ResourceFieldId row, int x, int y, int hands,
                std::uint32_t squadSlot) {
        MacroWorld mw = ctx_.mw;  // the envelope, whole — never a
                                  // partial re-pick of its layers
        return field_harvest(mw, row, x, y, hands, squadSlot);
    }
    // Сдача каталожной строки домой — одна сделка deal (ломоть 2 M-72).
    void deliver_home(MacroHandle self, int defIdx) {
        deliver_bag_home(self, ctx_, defIdx);
    }
    // Ноги: шаг к точке — march читает цену земли, пишет только себя.
    void march(MacroPos& p, ecs::MacroNpcRuntime& rt, float x, float y) {
        try_move(p, rt, x, y, ctx_);
    }
    int rand(int n) { return rand_int(ctx_, n); }
    // БРОСОК В [0,1), А «КИДАТЬ НЕЧЕМ» — ЗНАЧЕНИЕ 1.0, НЕ ВЕТКА: рулетка без
    // кости честно ОТКАЗЫВАЕТ дальней заявке (roll × total < bid неверно при
    // roll = 1), а не берёт её даром — то же вырождение, каким дверь станции
    // отдаёт урну первому весу. Квант 2^20 — ширина струи, по которой
    // кидает и она; `rand_int` разыменовывает RNG, поэтому проверка здесь
    // не вкус, а условие существования вызова.
    float roll01() {
        return ctx_.rng ? float(rand_int(ctx_, 1 << 20)) / float(1 << 20)
                        : 1.0f;
    }
    XY rand_nearby(const XY& around, int radius) {
        return pick_random_nearby(around.x, around.y, radius, ctx_);
    }
    // Летопись: выработанная жила — факт мира (FactKind::Drained).
    void drained_fact(MacroHandle self, int x, int y, ResourceFieldId row) {
        record_place_fact(store_ctx(ctx_), *ctx_.mw.gs, FactKind::Drained,
                             parent_of(store_ctx(ctx_), self.slot),
                             x, y, int(row) + 1);
    }
    // ── ЧУЖИМ МОДУЛЕМ — ТОЖЕ ГЛАГОЛОМ ───────────────────────────────────
    // Сквад без дома ходит бродягой, сквад без цели — вокруг дома. Это не
    // «фолбэк на всякий случай», а два законных ответа мира: дома нет —
    // значит некуда возить, цели нет — значит нечего решать.
    void nomad(AiSelf& s) { ai_nomad(s.h, s.pos, s.rt, s.pools, ctx_); }
    void home_wander(AiSelf& s) {
        ai_home_wanderer(s.h, s.pos, s.rt, s.pools, ctx_);
    }
    // ПОГРУЗКА ДОМА — одна дверь плана и исполнения (`plan_home_load_`):
    // «что решили» и «что повезли» одно число и один коммит.
    void load_home(AiSelf& s, std::uint16_t homeSlot) {
        MacroStore& st = store_ctx(ctx_);
        plan_home_load_(st, homeSlot, s.h.slot, st.sheet[homeSlot].skills,
                        carry_capacity_g(s.rt, s.bag),
                        ctx_.mw.econFacts, ctx_.mw.econFactsUser);
    }
    // СДЕЛКА НА РЫНКЕ — дверь торговли (S10/S25): обе ноги атомарны, сила
    // торга у каждой стороны своя и берётся ЕЁ анкетой.
    CaravanDeal trade_at(AiSelf& s, std::uint16_t market,
                         std::uint16_t homeSlot) {
        MacroStore& st = store_ctx(ctx_);
        return trade_vendor_at_market(
            st, s.h.slot, carry_capacity_g(s.rt, s.bag), market,
            handle_at(st, homeSlot),
            trade_power_of(sheet_of(st, s.h)),
            trade_power_of(st.sheet[market]),
            ctx_.mw.econFacts, ctx_.mw.econFactsUser);
    }
    // Летопись: состоявшийся обмен — факт мира (FactKind::Traded).
    void traded_fact(AiSelf& s, int x, int y, int value, int marketId) {
        MacroStore& st = store_ctx(ctx_);
        record_place_fact(st, *ctx_.mw.gs, FactKind::Traded,
                             parent_of(st, s.h.slot), x, y, value, marketId);
    }
    // СЛЕДУЮЩАЯ СТАНЦИЯ — рулетка по весу `1/(1 + дни)` среди жилых мест
    // соседних округ: глагол, потому что КИДАЕТ КОСТЬ (дневная струя RNG).
    // Пара выходных координат двери здесь не берётся: решение называет
    // МЕСТО, а его клетку спрашивают у колонок места (вид), и прежние два
    // мёртвых локала умерли вместе с этим вопросом.
    int next_station(const MacroPos& p, int currentId, int prevId) {
        float outX = 0.0f, outY = 0.0f;
        return pick_next_station_(ctx_, p, currentId, prevId, outX, outY);
    }
    // ВЫЛИВ СУМКИ ДОМОЙ — ОДНА сделка лотом по всему словарю плюс остаток
    // стоимости той же дверью цены (ломоть 2 M-72). Один глагол на оба рейса
    // (сбыт и дань): вопрос у них один — «всё, что привёз, на склад дома».
    void dump_bag_home(AiSelf& s, std::uint16_t homeSlot) {
        MacroStore& st = store_ctx(ctx_);
        DealLine dump[kCommodityCount];
        for (int i = 0; i < kCommodityCount; ++i) {
            dump[i] = DealLine{};
            dump[i].want = 1 << 30;
            dump[i].def = std::uint16_t(commodity_item_index(i));
        }
        deal(st, s.h.slot, homeSlot, dump, {},
             kNoCarryLimitG, kNoCarryLimitG,
             ctx_.mw.econFacts, ctx_.mw.econFactsUser);
        transfer_value_dense(s.bag, depot_(homeSlot, ctx_.mw),
                             inventory_value(s.bag));
    }
    // ── ВЗЫСКАНИЕ ДАНИ — ОДИН ГЛАГОЛ, ПОТОМУ ЧТО ОДНА ТОЧКА ──────────────
    // ВЗЫСКАНИЕ И ПОГАШЕНИЕ — В ОДНОЙ ТОЧКЕ: сколько увёз, столько и
    // списал, поэтому шва между «взято» и «зачтено» физически нет.
    // Долг живёт на РЕБРЕ Suzerain самого вассала (M-245).
    void collect_tithe(AiSelf& s, std::uint16_t vassal,
                       std::uint16_t homeSlot) {
        MacroStore& st = store_ctx(ctx_);
        Inventory& homeInv = st.inventory[homeSlot].inv;
        auto& rt = s.rt;
        Inventory* bag = &s.bag;
        Interest* edge = tithe_of(st, vassal);
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
                // Взыскание — строка сделки (ломоть 2 M-72): спина сборщика
                // клампит дверью, факт честный — дюпа «add без снятия» нет.
                DealLine draw[1];
                draw[0] = DealLine{};
                draw[0].want = want;
                draw[0].def = std::uint16_t(id);
                deal(st, vassal, s.h.slot, draw, {},
                     carry_capacity_g(rt, *bag) - inventory_mass_g(*bag),
                     kNoCarryLimitG, ctx_.mw.econFacts, ctx_.mw.econFactsUser);
                const int moved = draw[0].done;
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
                st.inventory[vassal].inv, Depot(*bag),
                int(std::min<long long>(owed, 1 << 30)));
            if (dense > 0) {
                owed -= dense;
                took += dense;
            }
        }
        if (took > 0 && edge) {
            edge->owedValue -= took;
            if (edge->owedValue < 0) edge->owedValue = 0;
            record_place_fact(st, *ctx_.mw.gs, FactKind::Taxed,
                                 int(st.spawnId[vassal].index),
                                 int(s.pos.x), int(s.pos.y),
                                 int(std::min<long long>(took, 1 << 30)),
                                 parent_of(st, s.h.slot));
        }
    }
private:
    const TickContext& ctx_;
};

// ═════ МОДУЛЬ Artel — добыча (образец формы) ═════════════════════════════
// «не понял откуда ты берёшь профессии артели если артель это ии модуль
// который просто решает что добыть по потреонстям деревни и окружению а не
// профессия» (владелец, вердикт 2026-10-23 №1). Решение модуля кэшируется
// ЦЕЛЬЮ: cell = рабочее место, what = (Cell, строка kGathererDefs),
// amount = ОСТАТОК циклов работы (первый писатель и читатель колонки —
// вердикт №6), onArrival = Working. Поручений (errandObject) у добычи
// больше нет — их писатель-аукцион казнён этим же ломтём.
void artel_think(AiSelf& s, const AiView& view, AiPult& pult) {
    XY home;
    if (!view.home(s.h, home)) return;
    ecs::SquadGoal& g = s.goal;
    const GathererDef* def = artel_goal_def(g);
    // Выход строки цели ОРДИНАЛОМ, один резолв на таблицу (ЗАКОН СЛОВАРЯ):
    // -1 = выход существо ЛИБО цель-жила (её предмет решает фича рабочего
    // места — поштучно знает транзакция, множество — vein_yield_ordinals).
    const int outIdx =
        def ? gatherer_item_index(int(def - kGathererDefs)) : -1;
    // «Спина полна СВОИМ выходом» — одним предикатом на статическую цель и
    // цель-жилу: гружёный металл в сумке и места меньше одной единицы.
    const auto back_full_of_yield = [&]() {
        const std::int64_t freeG =
            carry_capacity_g(s.rt, s.bag) - inventory_mass_g(s.bag);
        const auto full = [&](int idx) {
            return idx >= 0 && s.bag.count_of(idx) > 0
                && freeG < unit_mass_g(item_def_at(idx));
        };
        if (outIdx >= 0) return full(outIdx);
        for (const int m : vein_yield_ordinals()) {
            if (full(m)) return true;
        }
        return false;
    };
    // Домой: решение (what) НЕ стирается — кэш предиката переживает рейс,
    // сдача дома и предикат полной спины читают его же.
    const auto go_home = [&]() {
        ecs::goal_set(g, cell_of(int(home.x), int(home.y), view.map_w()),
                      g.what, 0, std::uint8_t(NS::Idle));
        s.rt.state = std::uint8_t(NS::Returning);
    };

    if (s.rt.state == std::uint8_t(NS::Idle)) {
        --s.rt.stateTimer;
        if (s.rt.stateTimer <= 0) {
            // ПОЛНАЯ СПИНА ИДЁТ ДОМОЙ (S10 «возвращается, кладёт на склад»):
            // отдых посреди дорогого маршрута будит артель в Idle, а Idle
            // слал её к жиле — даже с грузом, которому на жиле нечего взять.
            // Вечный челнок «шахта→привал→шахта» держал 536 серебра в одной
            // сумке 48 дней при пустом складе (измерено, сид 7). Выход-
            // СУЩЕСТВО едет душой, не сумкой — у него «полной спины» нет.
            if (def && gatherer_creature_yield(*def) == NPCType::Count
                && back_full_of_yield()) {
                go_home();
                return;
            }
            // РЕШЕНИЕ «ЧТО ДОБЫТЬ» — каждый выход заново, по сегодняшней
            // нужде дома и живым рабочим местам округи (вердикт 2026-10-23
            // №1); рулетка по скору — артели диверсифицируются без
            // координации (закон казнённого аукциона, переживший его форму).
            struct Candidate { int row; XY site; int score; };
            Candidate cand[kGathererGoalCount];
            int candCount = 0;
            int total = 0;
            const std::uint16_t homeSlot = view.home_slot(s.h);
            for (int r = 0; r < kGathererGoalCount; ++r) {
                XY site;
                if (!view.worksite(kGathererDefs[r], s.pos, home, site))
                    continue;
                const long long need =
                    view.need_price(homeSlot, gatherer_item_index(r));
                // Выход-существо и жила скорятся МИНИМУМОМ, а не нулём:
                // кандидат с живым рабочим местом из рулетки не выбывает.
                // Верх — страж ширины рулетки (int), не коридор цены: сама
                // цена дома не ограничена (вердикт 2026-09-18).
                const int score =
                    int(std::clamp<long long>(need, 1, 1 << 20));
                cand[candCount] = {r, site, score};
                ++candCount;
                total += score;
            }
            if (candCount > 0) {
                const int draw = pult.rand(total);
                int pick = candCount - 1;
                int acc = 0;
                for (int c = 0; c < candCount; ++c) {
                    acc += cand[c].score;
                    if (draw < acc) { pick = c; break; }
                }
                const int row = cand[pick].row;
                // БЮДЖЕТ ЦИКЛОВ — ОСТАТОК amount: сколько циклов спина
                // вместит при сегодняшних руках. Считается у цели со
                // статическим выходом; у жилы и существа — 0, «не считаем»
                // (значение, не отсутствие).
                std::uint16_t cycles = 0;
                const int pickOut = gatherer_item_index(row);
                if (pickOut >= 0) {
                    const std::int64_t unitG =
                        unit_mass_g(item_def_at(pickOut));
                    const int workers =
                        production_hands(count_human_souls(s.bag));
                    const std::int64_t freeG =
                        carry_capacity_g(s.rt, s.bag)
                        - inventory_mass_g(s.bag);
                    const std::int64_t perCycleG = std::max<std::int64_t>(
                        1, std::int64_t(workers) * unitG);
                    cycles = std::uint16_t(std::clamp<std::int64_t>(
                        freeG / perCycleG, 1, 0xFFFF));
                }
                ecs::goal_set(g,
                              cell_of(int(cand[pick].site.x),
                                      int(cand[pick].site.y), view.map_w()),
                              ecs::goal_what(ecs::GoalKind::Cell,
                                             std::uint32_t(row)),
                              cycles, std::uint8_t(NS::Working));
                s.rt.state = std::uint8_t(NS::Traveling);
            } else if (view.home_dist_sq(s.pos, home) > 400.0f) {
                // No work and far afield: come home first — the exact
                // HomeWanderer rule every gatherer degrades to.
                go_home();
            } else {
                const XY t = pult.rand_nearby(home, 12);
                ecs::goal_set(g, cell_of(int(t.x), int(t.y), view.map_w()),
                              g.what, 0, std::uint8_t(NS::Idle));
                s.rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    if (s.rt.state == std::uint8_t(NS::Traveling)) {
        if (view.at(s.pos, g)) {
            ecs::goal_arrive(g, s.rt);  // → Working: цель несла «чем стать»
            s.rt.stateTimer = std::int16_t(8 + pult.rand(8));
            return;
        }
        const float ox = s.pos.x, oy = s.pos.y;
        const XY t = view.goal_pos(g);
        pult.march(s.pos, s.rt, t.x, t.y);
        // A leg that cannot advance gives the run up (the vendor's own
        // law): the reach wave keeps this rare, but a concave shore can
        // still wedge a greedy march — better home tonight than frozen at
        // the bank forever (measured 2026-08-31).
        if (march_is_stuck_(s.pos, ox, oy, s.rt, s.pools)) go_home();
        return;
    }
    if (s.rt.state == std::uint8_t(NS::Working)) {
        --s.rt.stateTimer;
        if (s.rt.stateTimer <= 0) {
            // Решение потерялось (битый тег цели) — честно домой: новое
            // решение примет Idle, работы из воздуха нет (fail-closed).
            if (!def) { go_home(); return; }
            ecs::Pools& pools = s.pools;
            // WORK COSTS SP through the one stamina law (CANON S14.1): цена
            // цикла — бар / темп СВОЕГО источника (пашня 32 в день, охота
            // 8 — зверь честно вчетверо дороже за единицу; рука берёт один
            // объект всегда, вердикт 2026-09-16). Пустой бар — домой
            // отдыхать, не работа в ноль.
            const int cycleCost =
                sp_price(int(pools.maxSp), def->perWorkerDay);
            if (int(pools.sp) < cycleCost) { go_home(); return; }
            // Рабочее место — АДРЕС ЦЕЛИ: прибытие его не гасит (адрес есть
            // кэш предиката), работа читает его клеткой. (Здесь артель
            // когда-то строила шахту над голой жилой — вырезано 2026-09-22,
            // вердикт: фичи ставит генерация мира; артель только добывает.)
            const int tx = cell_x(g.cell, view.map_w());
            const int ty = cell_y(g.cell, view.map_w());
            // Every soul in the squad works: headcount multiplies the
            // cycle's yield at the squad's one fixed SP price (owner: «SP
            // тратится столько же, добывают кратно больше»). Руки — ЛЮДИ:
            // лошадь в контейнере — спина и рот, не рука.
            const int workers = production_hands(count_human_souls(s.bag));
            const int got =
                pult.harvest(def->row, tx, ty, workers, s.h.slot);
            const bool tookSomething = got > 0;
            if (got > 0) {
                // The cycle is PAID the moment it produced — through the
                // same fractional carry the march charges: work and walking
                // drain one purse, which is the whole law.
                pools.spCarry -= float(cycleCost);
                settle_sp_carry(pools);
                // A DEPOSIT worked down to nothing is a fact of the world
                // (FactKind::Drained); forest and wheat regrow by their own
                // law, so their emptied cells write nothing. Факт судится
                // по ОСТАТКУ после транзакции.
                if (def->worksite == Worksite::Deposit
                    && view.field_read(def->row, tx, ty) == 0) {
                    pult.drained_fact(s.h, tx, ty, def->row);
                }
            }
            // THE HAUL IS EMERGENT (owner, 2026-09-16): a worker takes one
            // object, and the crew keeps taking while its BACKS have room,
            // its BAR has another act in it — и пока жив ОСТАТОК ЦИКЛОВ
            // цели (четвёртая причина уйти, вердикт №6: completed выводится
            // из остатка, а не хранится). Земля, отдавшая ноль, гонит домой
            // третьей причиной — иначе артель на выеденной клетке работала
            // бы вечно, не платя ничем.
            const bool backsFull =
                gatherer_creature_yield(*def) == NPCType::Count
                && back_full_of_yield();
            const bool barSpent = int(pools.sp) < cycleCost;
            const bool budgetSpent = tookSomething && !ecs::goal_spend(g);
            if (tookSomething && !backsFull && !barSpent && !budgetSpent) {
                // Still standing at the worksite, still able: work again.
                s.rt.stateTimer = 1;
                return;
            }
            go_home();
        }
        return;
    }
    if (s.rt.state == std::uint8_t(NS::Wandering)
        || s.rt.state == std::uint8_t(NS::Returning)) {
        if (view.at(s.pos, g)) {
            // Home with the haul: everything gathered lands in the HOME
            // store — the same universal inventory the market sells from.
            // Дом принимает ВСЯКИЙ добычный выход, не только выход текущего
            // решения: решение могло смениться между рейсами, а груз в
            // спине — факт, не намерение. Пойманное СУЩЕСТВО остаётся в
            // инвентаре артели (M-230): у него нет товарной колонки.
            if (s.rt.state == std::uint8_t(NS::Returning)) {
                for (int r = 0; r < kGathererGoalCount; ++r) {
                    const int m = gatherer_item_index(r);
                    if (m >= 0) pult.deliver_home(s.h, m);
                }
                for (const int m : vein_yield_ordinals())
                    pult.deliver_home(s.h, m);
            }
            ecs::goal_arrive(g, s.rt);   // → Idle
            s.rt.stateTimer = std::int16_t(6 + pult.rand(12));
            return;
        }
        const XY t = view.goal_pos(g);
        pult.march(s.pos, s.rt, t.x, t.y);
    }
}

// Переходник пятёрки — умрёт флипом подписи kAiModuleRows, когда мигрирует
// последний модуль: собирает СВОЮ СТРОКУ, ВИД и ПУЛЬТ из TickContext.
void ai_artel(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv};
    const AiView view(ctx);
    AiPult pult(ctx);
    artel_think(self, view, pult);
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

// (Объявление `ai_nomad` переехало к пульту выше: фолбэк стал ГЛАГОЛОМ, и
//  дверь к нему обязана стоять до класса, который её проносит модулю.)

// (caravan_buy_order — белый список закупки «нужды дома → входы рецептов» —
// СНЕСЁН 2026-09-18 по вердикту владельца: купец берёт то, что выгоднее по
// цене относительно стоимости, и список решал бы за него, что бывает товаром.)


// (`haul_between` УМЕР ЗДЕСЬ в ломоть 2 M-72 — его законы целиком у двери
// `deal@src/macro/deal.h`: кламп «влезает столько целых единиц» стал клампом
// спины, кредит-прежде-дебета — исполнением на копиях, гашение долга места —
// хвостом коммита. Его ЛАТЕНТНЫЙ ДЮП — возврат `remove_of` выбрасывался, и
// строка существа дала бы add без снятия — у двери невыразим по построению.)

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
//  · ДЕТЕРМИНИЗМ ВЕНДОРСКОГО КАНАЛА — аукцион заявок (казнён ломтём 4
//    M-72) звал дверь без RNG, поэтому 98 % торговли мира
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
    const std::size_t R = nv ? nv->regionPlaceId.size() : 0;
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
            if (!place_is_settlement(SquadType(stp.runtime[slot].squadType))
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
        const int lmId = int(nv->regionPlaceId[to]);
        if (lmId < 0 || lmId == currentId || lmId == prevId) continue;
        const std::uint16_t slot = place_slot_by_id(ctx, lmId);
        if (slot == kMacroNoSlot
            || !place_is_settlement(SquadType(stp.runtime[slot].squadType))
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

// (Параметр `const Place& at` снят вместе со строкой места, ломтик F:
// у него не было ни одного читателя в теле — цену «там» даёт прейскурант
// каталога, а не адрес рынка.)
// (`mw`, `fromX`, `fromY` снят ы 2026-10-09 — ТОТ ЖЕ класс, что снятый выше
// `const Place& at`: цену спрашивают у `market_price_seen(c)`, которая
// отвечает абсолютной стоимостью строки ВСЕМ И ВСЕГДА, то есть «где я»
// этой ставке сегодня не известно вовсе. Это НАЗВАННАЯ ДЫРА M-191
// (ближняя половина яруса 2 знания о цене), и теперь она видна в подписи,
// а не спрятана за параметром, который делает вид, что место учтено.)
long long trade_bid_value_(const int* mine,
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

// (ДВЕ ПРИВАТНЫЕ КОПИИ ТОРГОВОЙ СИЛЫ УМЕРЛИ 2026-10-18. Здесь стояли
//  `leader_trade_power_` и `place_trade_power_`, и их возвращающая строка
//  СОВПАДАЛА ПОБАЙТОВО — обе считали `calculate_derived(...).tradeDiscountPct`,
//  то есть ЧЕТВЁРТОЕ и ПЯТОЕ написание одного выражения при живой двери
//  `trade_power_of@src/macro/anketa.h`. Шапка той двери предупреждает ровно об
//  этом дословно: «Эта же строка была написана в трёх местах, и ТРЕТЬЕ
//  написание врало (поймано 2026-09-21)».
//
//  Различались они ровно формой адреса — хэндл против сырого слота, — и это
//  НЕ два вопроса: у сделки две макросущности (CANON S25), но сила у них
//  одна и та же величина одной анкеты. После смерти дуализма `sheet_of`
//  (M-246а) даже разница «дверь против колонки» исчезла: лист теперь ОДИН.
//  Оба звонящих ниже спрашивают ту одну дверь напрямую.)

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
// Стороны — СЛОТЫ стора (deal.h, ломоть 2 M-72): `traderSlot ==
// kMacroNoSlot` значит «только посчитать» (так дверь спрашивает аукцион);
// иначе план стейджится лотом и исполняется ОДНОЙ сделкой в конце —
// «что решили» и «что повезли» теперь не только одно число, но и один
// коммит.
// (Умолчания `sink = nullptr, user = nullptr` сняты вместе с переездом
//  вызова в пульт: звонящий у двери один, и канал фактов он передаёт всегда —
//  умолчание было разрешением МОЛЧА потерять факт сделки.)
long long plan_home_load_(MacroStore& st, std::uint16_t homeSlot,
                          std::uint16_t traderSlot, const Skills& site,
                          std::int64_t capG, EconFactSink sink, void* user) {
    Inventory& store = st.inventory[homeSlot].inv;
    const std::int32_t* needDebt = st.upkeep[homeSlot].needDebt;
    const int population = souls_home(st, homeSlot);
    Inventory* bag = traderSlot == kMacroNoSlot
        ? nullptr : &st.inventory[traderSlot].inv;
    long long planned = 0;
    bool done[kMaxInventorySlots] = {};
    // Лот плана: по строке на выбранный СТАК (пер-слот, как и выбор).
    DealLine lot[kMaxInventorySlots];
    int lines = 0;
    std::int64_t used = bag ? inventory_mass_g(*bag) : 0;
    const std::int64_t budgetG = capG - used;   // спина этого рейса
    while (used < capG) {
        // ЗАКОН ПЛОТНОСТИ — ЧУЖАЯ ДВЕРЬ, И ЗДЕСЬ ОН БОЛЬШЕ НЕ ВЫПИСАН (M-298,
        // вердикт владельца 2026-10-20 «слить в одну дверь сейчас»). Этот
        // цикл держал ВТОРУЮ копию `densest_value_slot` — тот же перекрёстный
        // `v·bestW > bestV·w`, та же вырождающаяся нулевая масса, — и
        // комментарий выше ЧЕСТНО называл дверь, которую копия обходила.
        // Разошлись они ровно в одном: копия пускала в гонку плотности и
        // СУЩЕСТВО (у строки существа `item_def_at` отдаёт nullptr, то есть
        // массу 0, то есть победу всегда), а дверь отсекает его `payable_row`
        // ещё до взвешивания (M-292). Исход от этого не менялся — ниже стоял
        // второй отказ `if (!d) continue`, — но каждая душа склада стоила
        // лишнего прохода по тысяче слотов.
        const DensestSlot best = densest_value_slot(store, done);
        if (best.slot < 0) break;
        done[std::size_t(best.slot)] = true;
        const ItemRef& sl = store.slots[std::size_t(best.slot)];
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
        // ВЕТКИ «А ЕСЛИ МАССА НОЛЬ» БОЛЬШЕ НЕТ, И ЭТО НЕ ПОТЕРЯ: нулевую массу
        // несло здесь ТОЛЬКО существо, а его отсекает отказ `if (!d)` строкой
        // выше, то есть `bestW > 0.0f` была истинна на каждом проходе мира.
        // Страж колонки сделал её невыразимой и для будущей строки.
        // Масса едет из двери плотности УЖЕ ЦЕЛОЙ В ГРАММАХ (`DensestSlot::
        // massG`), и делить её больше не нужно — здесь стояло обратное
        // превращение в килограммы, последнее на этом пути.
        const std::int64_t byWeight = (capG - used) / best.massG;
        if (std::int64_t(fit) > byWeight) fit = int(byWeight);
        if (fit <= 0) continue;
        planned += (long long)fit * best.value;
        used += std::int64_t(fit) * best.massG;
        if (bag) {
            lot[lines] = DealLine{};
            lot[lines].want = fit;
            lot[lines].def = sl.def;
            lot[lines].slot = std::uint16_t(best.slot);
            ++lines;
        }
    }
    if (bag && lines > 0)
        deal(st, homeSlot, traderSlot,
             std::span<DealLine>(lot, std::size_t(lines)), {},
             budgetG, kNoCarryLimitG, sink, user);
    return planned;
}

// (ЗДЕСЬ ЖИЛ ai_caravan — 163 строки рейса «город → город со станциями».
// Снесён 2026-09-21 вместе с родом NPCType::Caravan: караван оказался
// СКВАДОМ, притворившимся видом существа, и его обоз был вписан в породу
// лидера 32 спинами вопреки закону «обоз = сумма спин состава» (squad.h).
// Торговый канал мира держат рейсы сбыта артелей — ai_vendor, одна машина
// рейса на любые два места. Караван вернётся сквадом: люди плюс лошади.)

// ═════ МОДУЛЬ Caravan — РЕЙС К РЫНКУ (форма образца) ════════════════════
// РЕЙС К РЫНКУ — одна машина для ЛЮБЫХ двух мест (владелец 2026-09-19:
// «добавим сквад горожан, которые идут в деревню закупаться»). Рынок берётся
// ИЗ ЦЕЛИ — род `GoalKind::Place`, ординал рынка в `goal.what`, — а не из
// феодального ребра: деревня едет к сюзерену, горожане — в деревню домена,
// и это ОДИН рейс, а не два ИИ.
//
// ПОРУЧЕНИЕ (`rt.errandObject`) ЗДЕСЬ УМЕРЛО (ломоть 4 M-72), и это был
// ВТОРОЙ НОСИТЕЛЬ ОДНОЙ ВЕЛИЧИНЫ (DOD п.6): из трёх мест, спрашивавших «где
// мой рынок», ДВА уже читали цель, а третье — колонку, и единственный её
// писатель стоял строкой рядом с `goal_set`, дублируя тот же ординал. Теперь
// носитель один, и ответ у трёх читателей общий (`AiView::goal_place`).
// `rt.prevStationId` ОСТАЛСЯ: «откуда пришёл» — другая величина, ею рулетка
// станции исключает только что обслуженный рынок.
// ПОЧЕМУ ЭТО НЕ УДОБСТВО, А НЕОБХОДИМОСТЬ, циферью (замер 2026-09-19):
// продать еду ГОЛОДНОМУ городу невозможно арифметически — при сезонной
// гиперболе первая буханка у пустой полки стоит база × сезонная нужда / 2
// (≈185 000 у города на 1200 душ), и кошелька на неё нет ни у кого. Зонд
// показал ловушку ликвидности прямо: 4035 артелей в рейсах сбыта — 45
// сделок в день на весь мир, деревни с 3.6 млн хлеба, города выедены в
// ноль. Сделка обязана происходить НА ДЕШЁВОМ КОНЦЕ: покупатель едет туда,
// где товар изобилен (цена 1-2), и его кошелька хватает на горы. Один
// закон цены, ни одного нового — просто рейс в правильную сторону.
// Рейс: погрузить дешёвое дома, дойти до рынка ЦЕЛИ, продать, купить
// домашние нехватки, вернуться. Ротация поднимает и судит крю как любую.
void caravan_think(AiSelf& s, const AiView& view, AiPult& pult) {
    XY home;
    if (!view.home(s.h, home)) { pult.nomad(s); return; }
    ecs::SquadGoal& g = s.goal;
    // Плечо дома — ребро Parent (M-245): один резолв слота на такт. Ветки
    // «а если дома нет» здесь больше нет, и это не упрощение: `view.home`
    // выше спрашивает ТОТ ЖЕ слот ТОГО ЖЕ ребра и уже отказал бы — ветка
    // была недостижима с рождения (образец Artel её и не имеет).
    const std::uint16_t homeSlot = view.home_slot(s.h);
    auto& rt = s.rt;

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
        if (view.home_dist_sq(s.pos, home) >= 4.0f) {
            const std::uint16_t m2 = view.goal_place(g);
            if (m2 != kMacroNoSlot && view.is_settlement(m2)) {
                ecs::goal_set(g, view.place_cell(m2), g.what, 0,
                              std::uint8_t(NS::Working));
                rt.state = std::uint8_t(NS::Traveling);
            } else {
                ecs::goal_set(g,
                              cell_of(int(home.x), int(home.y), view.map_w()),
                              0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Returning);
            }
            return;
        }
        // РЫНОК — ИЗ ЦЕЛИ (её и поставило решение прошлого выезда: деревне —
        // сюзерен, городу — деревня домена). Прежний жёсткий сюзерен был
        // хардкодом «рынок бывает только городом»: с ним горожанам было
        // некуда ехать.
        const std::uint16_t market = view.goal_place(g);
        if (market == kMacroNoSlot || !view.is_settlement(market)
            || market == homeSlot) {
            pult.home_wander(s);
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
        pult.load_home(s, homeSlot);
        if (inventory_mass_g(s.bag) <= 0
            && inventory_value(s.bag) <= 0) {
            // Nothing to sell and nothing owed: wait out the morning.
            rt.stateTimer = std::int16_t(40 + pult.rand(40));
            return;
        }
        ecs::goal_set(g, view.place_cell(market),
                      ecs::goal_what(ecs::GoalKind::Place,
                                     view.place_id(market)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (view.at(s.pos, g)) {
            ecs::goal_arrive(g, rt);   // → Working
            rt.stateTimer = std::int16_t(4 + pult.rand(4));
            return;
        }
        const float ox = s.pos.x, oy = s.pos.y;
        const XY t = view.goal_pos(g);
        pult.march(s.pos, rt, t.x, t.y);
        if (march_is_stuck_(s.pos, ox, oy, rt, s.pools)) {
            ecs::goal_set(g, cell_of(int(home.x), int(home.y), view.map_w()),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        if (const std::uint16_t market = view.goal_place(g);
            market != kMacroNoSlot && view.is_settlement(market)) {
            // ЧТО ВЕЗТИ ДОМОЙ судит ВЕДОМОСТЬ ДОМА, а не память крю
            // (CANON S10, ярус 2): дом сам выписал свои цены точным
            // складом и своим счётом.
            const CaravanDeal leg = pult.trade_at(s, market, homeSlot);
            if (leg.movedTableValue > 0) {
                pult.traded_fact(s, cell_x(g.cell, view.map_w()),
                                 cell_y(g.cell, view.map_w()),
                                 leg.movedTableValue,
                                 int(ecs::goal_ordinal(g.what)));
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
            long long homeValue = 0;
            for (int c = 0; c < kCommodityCount; ++c) {
                const int id = commodity_item_index(c);
                // ЦЕНА И НЕХВАТКА СТРОКИ ДОМА — ДВЕ ДВЕРИ ВИДА, обе над
                // одним законом (`season_demand_for` + `stock_price`), и
                // ту же цену ими спрашивает решение артели «что добыть».
                homePrice[c] = int(view.need_price(homeSlot, id));
                homeLack[c] = view.need_lack(homeSlot, id);
                cargo[c] = s.bag.count_of(id);
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
                view.march_days(int(s.pos.x), int(s.pos.y), int(home.x),
                                int(home.y));
            const float bidHome =
                daysHome > 0.0f ? float(homeValue) / daysHome : 0.0f;
            // КАНДИДАТ — СОСЕД ПО МЕМБРАНЕ, ИЗ ТОЧКИ СТОЯНИЯ (S7,
            // диффузия), и НЕ тот, откуда пришли: рынок, который только что
            // обслужили, шанса не получает.
            const int nextId = pult.next_station(
                s.pos, int(view.place_id(market)), rt.prevStationId);
            float bidGo = 0.0f;
            const std::uint16_t next = view.place_slot(nextId);
            if (next != kMacroNoSlot && next != homeSlot) {
                const XY at = view.place_xy(next);
                const int nextX_ = int(at.x);
                const int nextY_ = int(at.y);
                const long long gain =
                    trade_bid_value_(cargo, homePrice, homeLack);
                // ДЛИТЕЛЬНОСТЬ — ВЕСЬ ОСТАТОК РЕЙСА: туда И оттуда домой.
                // Иначе «дальше» дешевело бы по построению, и крю уходило бы
                // от дома бесконечно — знаменатель обязан расти с отъездом.
                const float daysGo =
                    view.march_days(int(s.pos.x), int(s.pos.y), nextX_,
                                    nextY_)
                    + view.march_days(nextX_, nextY_, int(home.x),
                                      int(home.y));
                if (daysGo > 0.0f)
                    bidGo = (float(gain)
                             - view.route_fear(int(s.pos.x), int(s.pos.y),
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
                const float roll = pult.roll01();
                goOn = roll * total < bidGo;
            }
            if (goOn && next != kMacroNoSlot) {
                rt.prevStationId = int(view.place_id(market));
                // Рынок следующего плеча НАЗЫВАЕТ ЦЕЛЬ, и больше ничто:
                // здесь стояла строка-дубль `rt.errandObject = …` с тем же
                // ординалом (ломоть 4 M-72).
                ecs::goal_set(g, view.place_cell(next),
                              ecs::goal_what(ecs::GoalKind::Place,
                                             view.place_id(next)),
                              0, std::uint8_t(NS::Working));
                rt.state = std::uint8_t(NS::Traveling);
                return;
            }
        }
        }
        rt.prevStationId = 0;   // ноль = «никто» (ЗАКОН СЛОВАРЯ п.6)
        ecs::goal_set(g, cell_of(int(home.x), int(home.y), view.map_w()),
                      0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (view.at(s.pos, g)) {
            if (rt.state == std::uint8_t(NS::Returning)) {
                // Home: purchases and earnings land on the home store; the
                // rotation dissolves the crew at dawn. Вылив сумки — ОДНА
                // сделка лотом по всему словарю (ломоть 2 M-72), остаток
                // стоимости — той же дверью цены.
                pult.dump_bag_home(s, homeSlot);
            }
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(10 + pult.rand(15));
            return;
        }
        const XY t = view.goal_pos(g);
        pult.march(s.pos, rt, t.x, t.y);
    }
}

// Переходник пятёрки — тот же, что у образца: собирает СВОЮ СТРОКУ, ВИД и
// ПУЛЬТ. Фолбэк «мира нет» ведёт бродягой — ровно куда вёл прежний отказ
// `!ctx.mw.store` в теле (а `gs` здесь добавлен не ради формы: летопись
// сделки разыменовывала его без проверки).
void ai_vendor(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
               ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) {
        ai_nomad(e, p, rt, pools, ctx);
        return;
    }
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv};
    const AiView view(ctx);
    AiPult pult(ctx);
    caravan_think(self, view, pult);
}

// The SUZERAIN place a town owes — a place's Stance::Suzerain row in its
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
//
// ═════ МОДУЛЬ Collector — форма образца ═════════════════════════════════
// ДОЛЖНИК — ИЗ ЦЕЛИ (род Place), как рынок у рейса сбыта: поручение
// (`rt.errandObject`) здесь умерло тем же ломтём и по той же причине — цель
// УЖЕ несла ординал вассала в обеих ветках Idle, то есть колонка была
// вторым носителем одной величины (DOD п.6). Взыскание целиком — ОДИН
// глагол пульта (`collect_tithe`): «сколько увёз, столько и списал» есть
// одна транзакция мира, и разрезать её телу модуля нечем.
void collector_think(AiSelf& s, const AiView& view, AiPult& pult) {
    XY home;
    if (!view.home(s.h, home)) { pult.nomad(s); return; }
    ecs::SquadGoal& g = s.goal;
    // Плечо дома — ребро Parent (M-245); вассала — колонки его ТЕЛА.
    // (Ветка «дома нет» снята как недостижимая: `view.home` выше спрашивает
    // тот же слот того же ребра.)
    const std::uint16_t homeSlot = view.home_slot(s.h);
    const std::uint16_t vassal = view.goal_place(g);
    auto& rt = s.rt;
    if (vassal == kMacroNoSlot || vassal == homeSlot) {
        pult.home_wander(s);
        return;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        // Дома и с пустыми руками — идём за долгом; дома с грузом — сдаём.
        if (view.home_dist_sq(s.pos, home) >= 4.0f) {
            ecs::goal_set(g, view.place_cell(vassal),
                          ecs::goal_what(ecs::GoalKind::Place,
                                         view.place_id(vassal)),
                          0, std::uint8_t(NS::Working));
            rt.state = std::uint8_t(NS::Traveling);
            return;
        }
        if (!view.owes_tithe(vassal)) {
            // Должник рассчитался (собрали или простили) — ждать нечего,
            // ротация завтра переторгует эту строку заново.
            rt.stateTimer = std::int16_t(8 + pult.rand(8));
            return;
        }
        ecs::goal_set(g, view.place_cell(vassal),
                      ecs::goal_what(ecs::GoalKind::Place,
                                     view.place_id(vassal)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (view.at(s.pos, g)) {
            ecs::goal_arrive(g, rt);   // → Working
            rt.stateTimer = std::int16_t(2 + pult.rand(3));
            return;
        }
        // НОГИ. Без этой строки машина СТОИТ, вечно числясь «в пути»:
        // ровно так и было при первой сборке 2026-09-22 — гистограмма
        // состояний показала Traveling 799 563 при Working РОВНО НОЛЬ, и
        // канал дани был пуст, хотя заявки и рождение работали.
        const XY t = view.goal_pos(g);
        pult.march(s.pos, rt, t.x, t.y);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer > 0) return;
        pult.collect_tithe(s, vassal, homeSlot);
        ecs::goal_set(g, cell_of(int(home.x), int(home.y), view.map_w()),
                      0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)
        || rt.state == std::uint8_t(NS::Returning)) {
        if (view.at(s.pos, g)) {
            // Вылив дани домой — ОДНА сделка (ломоть 2 M-72).
            pult.dump_bag_home(s, homeSlot);
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(10 + pult.rand(15));
            return;
        }
        const XY t = view.goal_pos(g);
        pult.march(s.pos, rt, t.x, t.y);
        return;
    }
    rt.state = std::uint8_t(NS::Idle);
    rt.stateTimer = std::int16_t(4);
}

// Переходник пятёрки — тот же, что у образца и у рейса сбыта.
void ai_collector(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) {
        ai_nomad(e, p, rt, pools, ctx);
        return;
    }
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv};
    const AiView view(ctx);
    AiPult pult(ctx);
    collector_think(self, view, pult);
}

void ai_trader(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
               [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    XY home;
    if (!home_pos(self, ctx, home)) return;
    const MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    const int myHomeId = parent_of(st, self.slot);

    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            // Pick another city (id != home).
            int candidates = 0;
            for_each_place(st, [&](std::uint16_t slot) {
                if (is_other_city(st, slot, myHomeId)) ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                // Обход без досрочного выхода: взявший урну гасит флаг, и
                // остаток прохода молчит (у двери мест нет break — её
                // порядок есть закон приоритета клетки).
                bool taken = false;
                for_each_place(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(st, slot, myHomeId))
                        return;
                    if (pick-- != 0) return;
                    ecs::goal_set(g, st.cell[slot].idx,
                                  ecs::goal_what(ecs::GoalKind::Place,
                                                 st.spawnId[slot].index),
                                  0, std::uint8_t(NS::Working));
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
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Working
            rt.stateTimer = std::int16_t(15 + rand_int(ctx, 20));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            ecs::goal_set(g, cell_of(int(home.x), int(home.y), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Returning)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(20 + rand_int(ctx, 30));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
    }
}

void ai_nomad(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
              [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    const MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    // «Не тот город, где был» — прежний ординал несёт сама цель (род Place).
    const int lastPlace = ecs::goal_kind(g.what) == ecs::GoalKind::Place
                              ? int(ecs::goal_ordinal(g.what))
                              : 0;
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            int candidates = 0;
            for_each_place(st, [&](std::uint16_t slot) {
                if (is_other_city(st, slot, lastPlace))
                    ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                bool taken = false;
                for_each_place(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(st, slot, lastPlace))
                        return;
                    if (pick-- != 0) return;
                    ecs::goal_set(g, st.cell[slot].idx,
                                  ecs::goal_what(ecs::GoalKind::Place,
                                                 st.spawnId[slot].index),
                                  0, std::uint8_t(NS::Idle));
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
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
    }
}

void ai_aggressive(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
                   [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
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
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 20, ctx);
            ecs::goal_set(g, cell_of(int(t.x), int(t.y), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 15));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
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
    // Ординал НА СБОРКЕ (M-268): здесь стоял `faction_index("magika")` —
    // линейный скан реестра по строке на КАЖДУЮ думку охотника. Имя задано
    // авторски, извне приехать не может, поэтому оно и не доживает до тика.
    constexpr int magika = faction_ordinal("magika");
    static_assert(magika >= 0, "реестр обязан нести строку магов");
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
                  [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    if (ctx.squads && ctx.mw.store && ctx.mw.gs) {
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
                // ИЗЪЯТИЕ ПО КЛЕТКЕ, А НЕ ПО РОДУ СЦЕНЫ (M-226): клетка
                // встречи в аренде у субмира — значит её ведёт он, и решать
                // этот бой наверху значило бы завести второго писателя.
                // Встреча не пропадает: она ОТЛАГАЕТСЯ до конца аренды.
                if (ctx.mw.gs
                    && cell_is_leased(*ctx.mw.gs, st.cell[self.slot].idx)) return;
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
            // Добыча ДВИЖЕТСЯ — род цели «сквад», адрес пересчитывается
            // каждую думку (кэш предиката, случай четвёртый).
            ecs::goal_set(store_ctx(ctx).goal[self.slot],
                          st.cell[prey.slot].idx,
                          ecs::goal_what(ecs::GoalKind::Squad,
                                         st.spawnId[prey.slot].index),
                          0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Chasing);
            try_move(p, rt, ep.x, ep.y, ctx);
            return;
        }
    }
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
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
            ecs::goal_set(g,
                          cell_of(rand_int(ctx, ctx.mapW),
                                  rand_int(ctx, ctx.mapH), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(8 + rand_int(ctx, 15));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
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
                     ecs::MacroNpcRuntime& rt, [[maybe_unused]] ecs::Pools& pools,
                     const TickContext& ctx) {
    // Логово самозалечивается: тело без дома объявляет домом место, где
    // проснулось (спавн-дверь анкеты пишет честную клетку раньше).
    if (rt.lairCell == ecs::kNoCell) {
        rt.lairCell = cell_of(int(p.x), int(p.y), ctx.mapW);
    }
    const float lairX = float(cell_x(rt.lairCell, ctx.mapW));
    const float lairY = float(cell_y(rt.lairCell, ctx.mapW));
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
        p.x, p.y, lairX, lairY,
        float(ctx.mapW), float(ctx.mapH)));

    if (ctx.squads && ctx.mw.store && ctx.mw.gs && fromLair < radius) {
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
                if (ctx.mw.gs
                    && cell_is_leased(*ctx.mw.gs, st.cell[self.slot].idx)) return;
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
            // Добыча ДВИЖЕТСЯ — род цели «сквад», пересчёт каждую думку.
            ecs::goal_set(store_ctx(ctx).goal[self.slot],
                          st.cell[prey.slot].idx,
                          ecs::goal_what(ecs::GoalKind::Squad,
                                         st.spawnId[prey.slot].index),
                          0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Chasing);
            try_move(p, rt, ep.x, ep.y, ctx);
            return;
        }
    }

    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    // Улетел за радиус или добычи нет и стоит не дома — домой.
    if (fromLair >= radius
        || (rt.state != std::uint8_t(NS::Wandering)
            && (int(p.x) != int(lairX) || int(p.y) != int(lairY)))) {
        ecs::goal_set(g, rt.lairCell, 0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        try_move(p, rt, lairX, lairY, ctx);
        return;
    }
    // Дома и сыт: кружи по округе логова.
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(lairX, lairY, int(radius), ctx);
            ecs::goal_set(g, cell_of(int(t.x), int(t.y), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 20));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
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

void ai_teleporter(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
                   [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
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
            ecs::goal_set(g, cell_of(int(t.x), int(t.y), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(12 + rand_int(ctx, 20));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
    }
}

void ai_wanderer(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
                 [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    if (rt.state == std::uint8_t(NS::Idle)) {
        --rt.stateTimer;
        if (rt.stateTimer <= 0) {
            XY t = pick_random_nearby(p.x, p.y, 25, ctx);
            ecs::goal_set(g, cell_of(int(t.x), int(t.y), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            rt.stateTimer = std::int16_t(10 + rand_int(ctx, 15));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
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
    // ВОПРОС ВРАЖДЕБНОСТИ — ОРДИНАЛАМИ, И ЭТО САМОЕ ГОРЯЧЕЕ МЕСТО МИРА
    // (M-268). Здесь стоял строковый путь: ординал фракции превращался в
    // ИМЯ, а дверь враждебности сканом реестра искала по этому имени ординал
    // ОБРАТНО — число → текст → число, плюс strcmp, ради одного байта
    // матрицы, адрес которого был известен с самого начала. Замер трёх миров:
    // 578 648…614 391 звонков и 2 263 088…2 447 875 строковых вопросов ЗА
    // ИГРОВОЙ ДЕНЬ. Теперь это два чтения массива (ЗАКОН СЛОВАРЯ: внутри
    // симуляции живут только числа).
    const FactionState& fx = ctx.mw.gs->factions;
    const FactionSlot myFaction = FactionSlot(kind.factionIdx);
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
                const auto& ok = st.kind[sw.slot];
                // Враждебность СПЕРВА: гейт и расстояние суть КОНЪЮНКЦИЯ,
                // порядок ответа не меняет (мир вышел побитово тем же), а
                // дешёвый вопрос вперёд дорогого снимает ~12 торовых
                // расстояний со звонка до единиц.
                if (!factions_hostile(fx, myFaction,
                                      FactionSlot(ok.factionIdx))) {
                    continue;
                }
                const auto& oc = st.cell[sw.slot];
                const float d = torus_dist_sq(
                    p.x, p.y,
                    float(ecs::cell_x(oc, ctx.mapW)),
                    float(ecs::cell_y(oc, ctx.mapW)),
                    float(ctx.mapW),
                    float(ctx.mapH));
                if (d >= best) continue;
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
                       [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.squads || !ctx.mw.gs) return false;

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
        if (ctx.mw.gs
            && cell_is_leased(*ctx.mw.gs, st.cell[self.slot].idx)) return false;
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
        // Рефлекс пишет ТУ ЖЕ колонку цели, что и роль, — бегство и есть
        // перерешение цели (кэш предиката), второго носителя «куда» нет.
        // Свёртку даёт МАСКА cell_of (ЗАКОН АДРЕСА п.3), марш берёт точку
        // из самой цели — рукописного wrapf здесь больше нет.
        ecs::goal_set(st.goal[self.slot],
                      cell_of(int(p.x + dx / len * 8.0f),
                              int(p.y + dy / len * 8.0f), ctx.mapW),
                      0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Fleeing);
        const XY t = goal_xy(st.goal[self.slot], ctx);
        try_move(p, rt, t.x, t.y, ctx);
        return true;
    }

    // Fighters close in when the law says they win: the same data column
    // that decides subworld combat stance decides who is a fighter at all.
    const bool fighter = combatant_behaviour(kNpcTypeDefs[kind.type].ai);
    if (fighter && myPower > theirPower * kPursueMargin) {
        rt.state = std::uint8_t(NS::Chasing);
        ecs::goal_set(st.goal[self.slot], st.cell[enemy.slot].idx,
                      ecs::goal_what(ecs::GoalKind::Squad,
                                     st.spawnId[enemy.slot].index),
                      0, std::uint8_t(NS::Idle));
        try_move(p, rt, ep.x, ep.y, ctx);
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

// (`creatures_worth` УМЕРЛА ЗДЕСЬ 2026-10-20, M-292. Она складывала цену
// душ по строкам найма, и существовала ровно потому, что дверь стоимости
// была полуслепа: запах богатства считал `creatures_worth + inventory_value`
// — ДВЕ половины одного вопроса, сложенные вручную, то есть второй ответ на
// «сколько стоит этот контейнер» (DOD п.6). Теперь обе половины даёт
// `container_worth@src/macro/currency.h` одним проходом, и закон уровня у
// них тот же, что у найма: эта дверь читала `hireGold` БЕЗ
// `soldier_level_factor`, то есть ветеран пахнул как новобранец.)

void scent_squad_deposit(MacroHandle self, const MacroPos& p,
                         const ecs::NPCKind& kind, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.store) return;
    ScentField& sf = ctx.mw.gs->scent;
    const int f = int(kind.factionIdx);
    if (f < 0 || f >= sf.factions) return;   // kNoFaction не следит
    const MacroStore& st = store_ctx(ctx);
    const float power = squad_power(auto_battle_side_of(st, self));
    // ОБЕ ПОЛОВИНЫ КОНТЕЙНЕРА ОДНОЙ ДВЕРЬЮ (M-292): души и имущество суть
    // один вопрос «сколько тут добра», и складывать их вручную значило бы
    // держать второй ответ (здесь стояла `creatures_worth`).
    const std::uint32_t worth = std::uint32_t(
        std::max(0, container_worth(st.inventory[self.slot].inv)));
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
    if (!ctx.mw.store) return;
    const MacroStore& st = store_ctx(ctx);
    const auto put = [&](std::uint16_t slot) {
        if (slot >= kUnifiedCap || st.alive[slot] == 0) return;
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
                     [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.store) return false;
    if (!combatant_behaviour(kNpcTypeDefs[kind.type].ai)) return false;
    const ScentField& sf = ctx.mw.gs->scent;
    if (sf.factions <= 0 || sf.w != ctx.mapW || sf.h != ctx.mapH)
        return false;
    const int f = int(kind.factionIdx);
    if (f < 0 || f >= kFactionCount) return false;
    // ЖИВАЯ МАТРИЦА, И ЗАПЕЧЁННОЙ МАСКИ БОЛЬШЕ НЕТ (M-268, вердикт владельца
    // 2026-10-21: «отношения фракций однозначно динамические … ради полировки
    // минимум систем и чистоты лучше снести»). Прежде здесь читался
    // `ctx.factionHostileMask[f]` — ЕДИНСТВЕННЫЙ читатель запечённой копии, —
    // и копия эта была ловушкой под будущую дипломатию: перепекалась раз в
    // драйв, а отношения вправе двинуться в любой момент. Биты она не
    // покупала ничего: строка отношений `rows[f].rel` есть РОВНО одна
    // кеш-линия (64 Б), а `f` на всю думку постоянна, значит все чтения
    // канала попадают в неё же — та же цена, что у бит-теста, но ответ
    // всегда свежий.
    const FactionState& fx = ctx.mw.gs->factions;

    // ВОПРОС ЗАДАЁТСЯ РАЗ НА ДУМКУ, А НЕ РАЗ НА КЛЕТКУ: враждебность канала
    // `b` от координат не зависит, а `sniff` ниже бежит по девяти клеткам —
    // значит наивная форма спрашивала бы ОДНО И ТО ЖЕ 9 × F = 171 раз вместо
    // F = 19 (замерено: +10 % к aiMsDay). Это НЕ возврат снесённой маски:
    // та жила колонкой `TickContext` на весь драйв и запекалась для ВСЕХ
    // фракций, то есть была копией, умеющей устареть. Здесь — инвариант
    // цикла, снятый с ЖИВОЙ матрицы внутри одной думки; устареть ему негде,
    // потому что матрица внутри думки не меняется по построению.
    std::uint64_t hostile = 0;
    for (int b = 0; b < sf.factions; ++b)
        if (factions_hostile(fx, f, b)) hostile |= 1ull << b;
    // «Есть ли у меня враги вообще» — ранний выход ДО `squad_power`, и он
    // здесь не роскошь: сила сквада суммирует весь ростер. Семантику он не
    // меняет (без врагов все каналы дали бы нули и подъём всё равно не нашёл
    // бы соседа) — он экономит именно этот обход.
    if (hostile == 0ull) return false;

    const float myPower =
        squad_power(auto_battle_side_of(store_ctx(ctx), self));
    if (myPower <= 0.0f) return false;
    const std::uint32_t fearCap =
        (std::uint32_t(myPower) >> kScentQuantShift) << kHuntBoldShift;

    // Сумма враждебных каналов клетки — по инварианту, снятому выше с ЖИВОЙ
    // матрицы отношений.
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
    try_move(p, rt, float(bx), float(by), ctx);
    return true;
}

namespace {

// Follow the waypoint route in the squad's orders (Session 15, Inc 7): walk
// to the current waypoint, arrive, take the next, loop. A squad ordered onto
// a route with no route wanders — a degraded order is a visible NPC, not a
// frozen one.
void ai_waypoints(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadOrders* orders = ctx.mw.store
        ? &store_ctx(ctx).orders[e.slot] : nullptr;
    if (!orders || orders->waypointCount == 0) {
        ai_wanderer(e, p, rt, pools, ctx);
        return;
    }
    const int i = orders->currentWaypoint % orders->waypointCount;
    ecs::SquadGoal& g = store_ctx(ctx).goal[e.slot];
    ecs::goal_set(g,
                  cell_of(int(orders->waypoints[std::size_t(i * 2)]),
                          int(orders->waypoints[std::size_t(i * 2 + 1)]),
                          ctx.mapW),
                  0, 0, std::uint8_t(NS::Idle));
    if (at_goal(p, g, ctx)) {
        orders->currentWaypoint =
            std::uint8_t((i + 1) % orders->waypointCount);
        ecs::goal_arrive(g, rt);   // → Idle
        rt.stateTimer = std::int16_t(2 + rand_int(ctx, 4));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle) && rt.stateTimer > 0) {
        --rt.stateTimer;
        return;
    }
    rt.state = std::uint8_t(NS::Traveling);
    const XY t = goal_xy(g, ctx);
    try_move(p, rt, t.x, t.y, ctx);
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
// Зовётся ТОЛЬКО из модуля None-строки kAiModuleRows — для сквадов, у которых
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
// отдыхаешь». Since 2026-10-06 the first half of that sentence is wider than
// movement: the burn runs every hour, so a body can exhaust itself STANDING,
// and in the open sea that is exactly what kills it. The second half is
// untouched — stopping on cheap ground still repays more than it burns.
//
// WHAT IS LEFT HERE IS THE FUNERAL, AND NOTHING ELSE. The bite itself moved
// into the one door that moves bars with time (recovery.h
// settle_pools_over_time), because price and recovery belong in one place
// (ЗАКОН СПОСОБНОСТИ п.4) and because a bite billed per CALL would have made
// the sea's depth depend on which driver was walking in it. The camp decision
// moved into settle_march_rhythm's single `kCampBarDivisor` branch, which
// already covers every negative bar there is. Both were duplicates behind
// their own copy of the dead standing predicate.
//
// The bite lands on the LORD's HP because the lord IS the squad — a creature
// is a row inside him, macro damage lands on the avatar. An hour the bar
// cannot pay therefore kills, through the same tracked-death door an
// auto-battle uses; the dead lord's men settle by the standing rule.
void settle_exhaustion(MacroHandle e, const ecs::Pools& hp,
                       const TickContext& ctx) {
    if (hp.hp > 0) return;
    if (!ctx.mw.store || !ctx.mw.gs) return;
    MacroStore& st = store_ctx(ctx);
    settle_leader_fraction(st, e, 0.0f);
    kill_fallen_squad_creatures(st, *ctx.mw.gs,
                                ctx.mw.econFacts,
                                ctx.mw.econFactsUser);
}

// ПОВЕДЕНИЕ НЕТИПИЗИРОВАННОГО СКВАДА — МОДУЛЬ None-СТРОКИ kAiModuleRows
// (ломоть 4 M-72): лестница untyped_squad_behaviour и её switch по
// AIBehaviour живут ВНУТРИ модуля; снаружи диспетч знает одну ось SquadType.
// ПОЧЕМУ У ДЕСЯТИ `ai_*` НИЖЕ СТОИТ `[[maybe_unused]] ecs::Pools& pools`, А
// НЕ СНЯТЫЙ ПАРАМЕТР (2026-10-09, вместе со сносом `-Wno-unused-parameter`).
// Это СЕМЕЙСТВО С ЕДИНОЙ ФОРМОЙ ВЫЗОВА: один `switch` по `AIBehaviour`, и
// каждая ветвь зовёт свою функцию ОДНОЙ И ТОЙ ЖЕ пятёркой аргументов.
// Снять параметр у тех десяти, кто пулы не читает, значит сделать подписи
// семейства рваными — ветвь начнёт знать, какой именно вид её callee, то
// есть ровно то знание, ради избавления от которого диспетчер и написан.
// Поэтому глушитель здесь ЛОКАЛЬНЫЙ и НАЗВАННЫЙ, а глобального одеяла
// (`-Wno-unused-parameter` на весь проект) больше нет: новый мёртвый
// параметр в любом другом месте дерева теперь красит сборку.
// Уйдёт это вместе с модулем: эпик ИИ переписывает здесь всё (M-235/M-242),
// и чинить форму семейства ДО его переписки значит работать на снос (§5 п.14).
void ai_untyped(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                ecs::Pools& pools, const TickContext& ctx) {
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    switch (untyped_squad_behaviour(store_ctx(ctx), e, kind)) {
        // ПОСЛЕДНИЕ ДВЕ МАКРО-РОЛИ, ЕЩЁ ЖИВУЩИЕ В КАТАЛОГЕ ТЕЛ (CANON S2):
        // `Gatherer` на строке Peasant и `Trader` на строке Merchant. Они
        // здесь не потому, что нетипизированный сквад должен добывать, а
        // потому что их несут СТРОКИ, и снести значение раньше строки нельзя.
        // Нетипизированный крестьянин — ДОМОСЕД, и теперь это сказано
        // прямо: прежде он проваливался в добытчика до gatherer_def_of (нет
        // поручения → home_wanderer), но поручений больше нет — «что
        // добыть» решает МОДУЛЬ Artel по своей цели (ломоть 4 M-72), а
        // думать добычей нетипизированному не с чего. Обе роли уйдут вместе
        // со своими строками (порция Б-6).
        case AIBehaviour::Gatherer:  ai_home_wanderer(e, p, rt, pools, ctx); break;
        // `TaxRun` ОСТАЛСЯ БЕЗ МАКРО-СМЫСЛА 2026-09-22 (сборщик стал ТИПОМ
        // сквада). Значение ещё несёт строка TaxCollector каталога тел,
        // поэтому ветка обязана существовать — но ведёт она туда же, куда
        // ведёт всякая роль без занятия. Умрёт вместе со строкой (Б-6).
        case AIBehaviour::TaxRun:       ai_wanderer  (e, p, rt, pools, ctx); break;
        case AIBehaviour::Trader:       ai_trader    (e, p, rt, pools, ctx); break;
        case AIBehaviour::Aggressive:   ai_aggressive(e, p, rt, pools, ctx); break;
        case AIBehaviour::Teleporter:   ai_teleporter(e, p, rt, pools, ctx); break;
        case AIBehaviour::Wanderer:     ai_wanderer  (e, p, rt, pools, ctx); break;
        // Prey. Running is not its own errand: the threat step above already
        // makes ANY row run from what it cannot beat (squad_power), so what
        // this column says about an untroubled day is "it roams" — the same
        // walk a wanderer takes. The difference between a fox and a rabbit is
        // what happens when something appears, and that is decided above.
        case AIBehaviour::Flee:         ai_wanderer  (e, p, rt, pools, ctx); break;
        case AIBehaviour::Waypoints:    ai_waypoints (e, p, rt, pools, ctx); break;
        case AIBehaviour::MageHunt:     ai_mage_hunt (e, p, rt, pools, ctx); break;
        case AIBehaviour::LairSorties:  ai_lair_sorties(e, p, rt, pools, ctx); break;
        case AIBehaviour::Count:        break;
    }
}

// РОД → МОДУЛЬ: диспетч есть ТАБЛИЦА по ординалу SquadType, не ветвление
// (ломоть 4 M-72; образец формы — kGenKindRows@src/sub/gens/dispatch.cpp).
// nullptr = «у рода думки нет»: место не ходит, его день идёт своим проходом
// (settle_place_day) — это ДАННЫЕ строки, а не гейт по архетипу (шапка
// squad_type.h: архетип законен КЛЮЧОМ таблиц и ЗАПРЕЩЁН ГЕЙТОМ в проходе
// симуляции — прежний гейт M-90 шага 5 умер в эту колонку). Модуль места
// придёт ломтём 5: заполнится строка, диспетч не изменится.
using AiModuleFn = void (*)(MacroHandle, MacroPos&, ecs::MacroNpcRuntime&,
                            ecs::Pools&, const TickContext&);
struct AiModuleRow {
    SquadType type;      // своя ось колонкой — под страж порядка строк
    AiModuleFn module;   // nullptr = род не думает
};
constexpr AiModuleRow kAiModuleRows[] = {
    { SquadType::None,      ai_untyped   },
    // Столица и дракон СТОЯТ В ТАБЛИЦЕ С ПЕРВОГО ДНЯ СВОЕЙ ОСИ, и оба с
    // `nullptr`: модуль места придёт ломтём 5, модуль дракона — ломтём 6.
    // Строка с `nullptr` есть ЧЕСТНОЕ «род не думает», а не пропуск: без
    // неё ассерт полноты ниже не собрался бы, и это ровно та стена, ради
    // которой диспетч стал таблицей.
    { SquadType::Capital,   nullptr      },
    { SquadType::City,      nullptr      },
    { SquadType::Village,   nullptr      },
    { SquadType::Spire,     nullptr      },
    { SquadType::Ruin,      nullptr      },
    { SquadType::Lair,      nullptr      },
    { SquadType::Shrine,    nullptr      },
    { SquadType::Mine,      nullptr      },
    { SquadType::Tower,     nullptr      },
    { SquadType::Artel,     ai_artel     },
    { SquadType::Caravan,   ai_vendor    },
    { SquadType::Collector, ai_collector },
    { SquadType::Dragon,    nullptr      },
};
static_assert(std::size(kAiModuleRows) == std::size_t(SquadType::Count),
              "kAiModuleRows must cover every SquadType: a new row of the "
              "axis names its module (or nullptr) here, never a branch");
static_assert(sm::rows_in_enum_order(kAiModuleRows, &AiModuleRow::type),
              "kAiModuleRows row order must mirror SquadType");

// ОДНА ДВЕРЬ «ЧТО ДЕЛАЕТ ЭТОТ СКВАД» (владелец 2026-09-21: «сквад должен
// знать, кто он — тип задаёт поведение аи агента»). Ответ — ОДНА строка
// kAiModuleRows: прежние два switch (по типу и по AIBehaviour) умерли в
// таблицу и в модуль None-строки (ломоть 4 M-72).
void dispatch(MacroHandle e, MacroPos& p,
              const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    const std::size_t row = std::size_t(rt.squadType);
    const AiModuleFn module =
        row < std::size(kAiModuleRows) ? kAiModuleRows[row].module : nullptr;
    // БЕЗ МОДУЛЯ ТЕЛО НЕ ДУМАЕТ ВООБЩЕ: ни следа, ни угрозы, ни охоты — его
    // день идёт своим проходом; без этого squad_threat_step погнал бы ГОРОД
    // преследовать врага.
    if (module == nullptr) return;
    // Каждый думающий сквад следит — писатель полей следов один (CANON S10).
    scent_squad_deposit(e, p, kind, ctx);
    if (squad_threat_step(e, p, kind, rt, pools, ctx)) return;
    // Визуального врага нет — может, есть запах: охота съедает think, роль
    // ждёт получаса потише (макроцель в rt не тронута — пауза, не амнезия).
    if (scent_hunt_step(e, p, kind, rt, pools, ctx)) return;
    module(e, p, rt, pools, ctx);
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
// ЦЕНА ГРАНИЦЫ — ТА ЖЕ, ЧТО ЦЕНА СПИСАНИЯ (фикс разрыва наценки, ломоть 2
// M-72): прежде лот резался по голому `stock_price`, а списывался по
// `trade_sell_price` с маржой — рынку выставляли счёт больше, чем мерили
// его кошелёк, и transfer довозил НЕ ВСЁ (недоплаченная поставка). Теперь
// граница и списание считают ОДНИМ числом — маржа входит в поиск.
int max_affordable_lot_(int base, int have, int demand, bool selling,
                        int wallet, int cap, int myPct, int theirPct) {
    if (cap <= 0 || wallet <= 0) return 0;
    int lo = 0, hi = cap;
    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        const int shelf = selling ? have + mid : have - mid;
        const int unit0 = stock_price(base, shelf, demand);
        const int unit = selling
            ? trade_sell_price(unit0, myPct, theirPct)
            : trade_buy_price(unit0, myPct, theirPct);
        const long long cost = (long long)mid * unit;
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
CaravanDeal trade_caravan_at_station(MacroStore& st,
                                     std::uint16_t traderSlot,
                                     std::int64_t capacityG,
                                     std::uint16_t marketSlot,
                                     int myTradePct, int theirTradePct,
                                     EconFactSink sink, void* user) {
    CaravanDeal out{};
    // Склад и счёт рынка — колонки его ТЕЛА (M-90 шаг 5); гашение долга
    // рынка приходом (CANON S10) теперь внутри двери deal.
    Inventory& hold = st.inventory[traderSlot].inv;
    Inventory& ms = st.inventory[marketSlot].inv;
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
            // bounded by what its whole store can PAY at the SAME margined
            // price the debit uses (max_affordable_lot_ — фикс разрыва).
            int n = std::min(hold.count_of(id), need - have);
            if (n <= 0) continue;
            n = max_affordable_lot_(base, have, demand, /*selling=*/true,
                                    inventory_value(ms), n,
                                    myTradePct, theirTradePct);
            if (n <= 0) continue;
            // The ONE trade-price law (economy.h): the seller's charisma
            // and trade skill claw back part of the house margin — a
            // caravan out-trades a peasant because its ROW rolls a better
            // sheet, never because the code knows who it is (owner,
            // 2026-08-30).
            const int price = trade_sell_price(
                stock_price(base, have + n, demand),
                myTradePct, theirTradePct);
            // ТОВАР И ОПЛАТА — ОДНА СДЕЛКА (ломоть 2 M-72): exact-лоты,
            // недоплаченная поставка невыразима — не покрыл рынок счёт,
            // не уехал и товар.
            DealLine goods[1];
            goods[0] = DealLine{};
            goods[0].want = n;
            goods[0].def = std::uint16_t(id);
            goods[0].exact = 1;
            DealLine pay[64];
            const StagedValue sv = stage_value_lines(ms, n * price, pay);
            if (sv.value < n * price) continue;
            const DealFact f =
                deal(st, traderSlot, marketSlot, goods,
                     std::span<DealLine>(pay, std::size_t(sv.lines)),
                     kNoCarryLimitG, kNoCarryLimitG, sink, user);
            if (!f.settled) continue;
            out.soldValue += sv.value;
            out.movedTableValue += base * n;
        } else if (have > need) {
            // BUY the surplus above the market's own seasonal need — never
            // its living stock; the lot is bounded by the caravan's purse
            // through the same exact door (max_affordable_lot_).
            const std::int64_t freeG = capacityG - inventory_mass_g(hold);
            int n = freeG <= 0
                        ? 0
                        : int(std::min<std::int64_t>(
                              have - need,
                              freeG / std::int64_t(item_mass_g(*def))));
            n = max_affordable_lot_(base, have, demand, /*selling=*/false,
                                    inventory_value(hold), n,
                                    myTradePct, theirTradePct);
            if (n <= 0) continue;
            const int cost =
                n * trade_buy_price(stock_price(base, have - n, demand),
                                    myTradePct, theirTradePct);
            DealLine goods[1];
            goods[0] = DealLine{};
            goods[0].want = n;
            goods[0].def = std::uint16_t(id);
            goods[0].exact = 1;
            DealLine pay[64];
            const StagedValue sv = stage_value_lines(hold, cost, pay);
            if (sv.value < cost) continue;
            const DealFact f =
                deal(st, traderSlot, marketSlot,
                     std::span<DealLine>(pay, std::size_t(sv.lines)), goods,
                     kNoCarryLimitG, freeG, sink, user);
            if (!f.settled) continue;
            out.boughtValue += sv.value;
            out.movedTableValue += base * n;
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
CaravanDeal trade_vendor_at_market(MacroStore& st,
                                   std::uint16_t traderSlot,
                                   std::int64_t capacityG,
                                   std::uint16_t marketSlot,
                                   MacroHandle home,
                                   int myTradePct, int theirTradePct,
                                   EconFactSink sink, void* user) {
    CaravanDeal out{};
    // Склад и счёт рынка — колонки его ТЕЛА (M-90 шаг 5); гашение долга
    // рынка приходом (CANON S10) теперь внутри двери deal.
    Inventory& bag = st.inventory[traderSlot].inv;
    Inventory& ms = st.inventory[marketSlot].inv;
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
        // station: the lot pays the post-trade shelf AT THE MARGINED PRICE
        // (фикс разрыва наценки), so a famine ceiling price never blanks a
        // sale the market can genuinely afford.
        n = max_affordable_lot_(base, have, demand, /*selling=*/true,
                                inventory_value(ms), n,
                                myTradePct, theirTradePct);
        if (n <= 0) continue;
        // Same ONE trade-price law as the station: a village hand haggles
        // with a peasant's charisma, a caravan with a trader's — the sheet
        // is the whole difference.
        const int price = trade_sell_price(
            stock_price(base, have + n, demand), myTradePct, theirTradePct);
        // ТОВАР И ОПЛАТА — ОДНА СДЕЛКА: недоплаченная поставка невыразима.
        DealLine goods[1];
        goods[0] = DealLine{};
        goods[0].want = n;
        goods[0].def = std::uint16_t(id);
        goods[0].exact = 1;
        DealLine pay[64];
        const StagedValue sv = stage_value_lines(ms, n * price, pay);
        if (sv.value < n * price) continue;
        const DealFact f =
            deal(st, traderSlot, marketSlot, goods,
                 std::span<DealLine>(pay, std::size_t(sv.lines)),
                 kNoCarryLimitG, kNoCarryLimitG, sink, user);
        if (!f.settled) continue;
        out.soldValue += sv.value;
        out.movedTableValue += base * n;
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
        // ВЫГОДА НА МАССУ — ДРОБЬЮ, А НЕ ФЛОАТОМ: держим ЧИСЛИТЕЛЬ (монеты) и
        // ЗНАМЕНАТЕЛЬ (граммы) порознь, а порядок решает перекрёстное
        // умножение ниже — тот же приём, что у `value_dense_order` и у
        // `densest_value_slot`. Делить было не нужно никогда: сортировке
        // нужен ПОРЯДОК, а не само отношение.
        struct Lot { int i; int gain; std::int64_t massG; int homeCap; };
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
            // ПОТОЛОК СТРОКИ — сезон домашней нужды (спрос уже сезонный),
            // но он больше НЕ ворота: у товара, который дома никто не ест,
            // потолок — только трюм и кошелёк.
            const int seasonCap = homeDemand > 0 ? homeDemand : (1 << 20);
            lots[std::size_t(lotCount++)] =
                Lot{i, worthHome - buyHere,
                    std::int64_t(item_mass_g(*def)), seasonCap};
        }
        // Выгоднейшее — первым: трюм и кошелёк конечны, и купец грузит их
        // тем, что несёт больше всего ценности на единицу массы.
        std::sort(lots, lots + lotCount, [](const Lot& a, const Lot& b) {
            return std::int64_t(a.gain) * b.massG
                 > std::int64_t(b.gain) * a.massG;
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
            const std::int64_t freeG = capacityG - inventory_mass_g(bag);
            int n = freeG <= 0
                        ? 0
                        : int(std::min<std::int64_t>(
                              {have, lots[li].homeCap,
                               freeG / std::int64_t(item_mass_g(*def))}));
            // Точная граница по кошельку (max_affordable_lot_): срез лота
            // по цене ДО среза недокупал — оставшаяся полка дешевле, и
            // честная точка выше. Цена границы — С МАРЖОЙ, та же, что ниже.
            n = max_affordable_lot_(base, have, demand, /*selling=*/false,
                                    inventory_value(bag), n,
                                    myTradePct, theirTradePct);
            if (n <= 0) continue;
            const int cost =
                n * trade_buy_price(stock_price(base, have - n, demand),
                                    myTradePct, theirTradePct);
            DealLine goods[1];
            goods[0] = DealLine{};
            goods[0].want = n;
            goods[0].def = std::uint16_t(id);
            goods[0].exact = 1;
            DealLine pay[64];
            const StagedValue sv = stage_value_lines(bag, cost, pay);
            if (sv.value < cost) continue;
            const DealFact f =
                deal(st, traderSlot, marketSlot,
                     std::span<DealLine>(pay, std::size_t(sv.lines)), goods,
                     kNoCarryLimitG, freeG, sink, user);
            if (!f.settled) continue;
            out.boughtValue += sv.value;
            out.movedTableValue += base * n;
        }
    }
    return out;
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
//   · ФУРАЖИР. Скидка ведущего по рангу фуражирования была вторым ответом на
//     «сколько ест состав». Вердикт владельца 2026-09-22: «оба снести — один
//     закон без исключений». СЛЕДСТВИЕ, НАЗВАННОЕ ТОГДА ВСЛУХ: у навыка не
//     осталось ни одного механического читателя, и строка была объявлена
//     «известной спящей, а не забытой». ЭТО ОКАЗАЛОСЬ НЕВЕРНЫМ ИМЕНЕМ, и
//     2026-10-12 строка снесена целиком (M-144): лист персонажа рисовал её
//     процент игроку общим циклом, то есть колонка не спала, а ОБЕЩАЛА. Место
//     в замысле сохранено — добыча пищи В ПУТИ, колонкой строки предмета,
//     апдейтом после демо, — но до тех пор строки в листе нет вовсе.
// Счёт теперь один: `upkeep_bill` (macro/upkeep_window.h).

// КЛЕЙМО АДРЕСА НА ФАКТАХ ОДНОЙ ДВЕРИ СОДЕРЖАНИЯ. Дверь судит ОДИН
// контейнер и адресов не знает; окно — единственный, кто знает, чей это
// склад. Та же форма, что у дневного релея (world_tick.cpp): по канону S9
// ушедший ресурс принадлежит ДОМУ судимого, а не «миру».
namespace {
struct UpkeepFactRelay {
    EconFactSink sink = nullptr;
    void* user = nullptr;
    std::int32_t homeId = 0;
};
void relay_upkeep_fact_(void* user, const EconFact& fact) {
    const auto* r = static_cast<const UpkeepFactRelay*>(user);
    EconFact stamped = fact;
    stamped.placeId = r->homeId;
    r->sink(r->user, stamped);
}
}   // namespace

// ВХОД — `gs` И `st`, И БОЛЬШЕ НИЧЕГО. Здесь стоял конверт `MacroWorld`, из
// которого окно брало ОДНУ вещь — `store_of(reg)`, то есть тот самый store,
// который звонящий уже держит в руках, — и платило за это зависимостью от
// entt-реестра (ЗАКОН ГЛАДКОЙ ПАМЯТИ п.2: entt легаси под снос везде).
//
// ЦЕНА КОНВЕРТА БЫЛА НЕ СТИЛЕВОЙ: дневной проход звал окно ИЗ-ПОД гейта
// `macro && macro->world && macro->terrain`, то есть мир без загруженного
// ТЕРРАИНА не ел и не платил ВООБЩЕ. Содержание — не рендер и не поле
// угрозы; у него с терраином нет общего вопроса, и выключать его вместе с
// ним значит ровно то, что запрещает ЗАКОН ДВУХ ТЕМПОВ п.5: глобальный
// флаг, гасящий систему макромира.
int squad_season_window(GameState& gs, MacroStore& st, int day,
                        EconFactSink sink, void* user) {
    if (!season_boundary(day)) return 0;
    int deserted = 0;
    // Окно делит ОДИН пул дезертиров и один пул лута на всех — порядок суда
    // есть закон мира (squad_walk.h): по ординалу. Скрэтч локальный, как у
    // прочих дневных проходов.
    std::vector<SquadWalkEntry> order;
    // МЁРТВЫЙ НЕ ПОДСУДИМЫЙ, И ЭТОГО ФИЛЬТРА ЗДЕСЬ НЕ БЫЛО (M-233, попутно).
    // `collect_squads_by_ordinal` отбирает по ОДНОМУ условию — `alive[slot]
    // == 0`, — а `dead` не читает; два соседних прохода по тем же слотам
    // (`kill_fallen_squad_creatures@src/macro/squad.h`, свип мёртвых записей)
    // его читают. Павший сквад выставлял себе новый сезонный счёт и платил по
    // нему со своего склада: покойник ел, пока его не свиповали.
    collect_squads_by_ordinal(
        st, order, [&](std::uint16_t slot) { return st.dead[slot] == 0; });
    for (const SquadWalkEntry& sw : order) {
        const std::uint16_t slot = sw.slot;
        auto& bag    = st.inventory[slot];
        auto& creatures = st.upkeep[slot];
        // СУД И СЧЁТ — ОДНА ДВЕРЬ НА ВЕСЬ МИР (macro/upkeep_window.h).
        // Артель судится тем же телом и тем же счётом, что контейнер места и
        // армия игрока: своего у неё здесь не осталось ничего.
        //
        // ПАСТВА СНИМАЕТСЯ ПАРОЙ, И ДО ЭТОЙ СТРОКИ ОНА НЕ СНИМАЛАСЬ ВОВСЕ.
        // Дверь уносит души двумя исходами — голод из мира, уход в пул, — и
        // оба выводят душу из паствы её дома («умерла в бою, ушла в пул
        // дезертиров, — число обязано упасть», leave_home_flock). Ни один из
        // них паству не трогал: замер `balance_run 12345,1 × 48` на границе
        // сезона дал 51 322 и 51 544 голодные смерти при Σworked городов и
        // деревень, ВЫРОСШЕЙ на 1 827 и 1 840 (рождения дня) — то есть не
        // списанной ни на одну душу. Мир кормил, растил, поднимал сквады и
        // облагал данью людей, которых у него нет.
        const int folkBefore = count_human_souls(bag.inv);
        // АДРЕС ФАКТА — ДОМ СУДИМОГО, И ШТАМПУЕТ ЕГО РЕЛЕЙ. Дверь про адреса
        // не знает (и не должна: она видит один контейнер), а звонящий —
        // единственный, кто знает, чей это склад. Тот же приём, которым
        // дневной проход клеймит факты своего места. Клеймятся ВСЕ факты
        // двери, включая `Consumed` её гашения: до релея они уезжали в
        // ведомость безадресными.
        UpkeepFactRelay relay{sink, user, parent_of(st, slot)};
        const EconFactSink us = sink ? &relay_upkeep_fact_ : nullptr;
        void* uu = sink ? static_cast<void*>(&relay) : nullptr;
        const UpkeepWindowOutcome out = upkeep_season_window(
            creatures, bag.inv, st.wellbeing[slot], us, uu);
        flock_left_home(gs, st, slot, folkBefore);
        deserted += out.walked;
        // (ВЕДОМОСТЬ СКЛАДА ДУШ переехала В САМУ ДВЕРЬ, M-233: событие
        // рождается там, где взыскание, и прямой суд над дверью теперь даёт
        // ведомости оба факта. Здесь оставался только их АДРЕС — он ушёл в
        // релей выше. Два исхода — два факта, и они НЕ альтернатива
        // (M-232): прежде здесь стоял тернарник по ярлыку `byWage`, один
        // факт на две причины, и при равных долях он называл полный провал
        // «не кормлен». Строка `SoulsDesertedUnfed` умерла вместе с ним.)
        // ── ЛЕТОПИСЬ ГОЛОДА — ЗДЕСЬ, ПОТОМУ ЧТО СУД ЗДЕСЬ ────────────────
        // Переехала из `tick_settlements_`/`tick_villages_` вместе с судом:
        // раньше её писали ДВА близнеца, каждый под своим гейтом рода, и
        // артель, корован и сборщик не попадали в хронику НИКОГДА — хотя
        // голодали по тому же закону. «Сквад голодал» есть ОДНО событие
        // мира, и говорит о нём одна строка на всех.
        if (out.starved > 0) {
            const int px = ecs::cell_x(st.cell[slot], gs.mapW);
            const int py = ecs::cell_y(st.cell[slot], gs.mapW);
            record_place_fact(st, gs, FactKind::Starved,
                                 int(st.spawnId[slot].index), px, py,
                                 out.starved);
            // ВЫМЕР — это «был состав и не стало», и судится он ПОСЛЕ
            // взыскания: до него спрашивать нечего, после — некого.
            if (creatures_empty(bag.inv)) {
                record_place_fact(st, gs, FactKind::Died,
                                     int(st.spawnId[slot].index), px, py,
                                     folkBefore);
            }
        }
        // (ЗДЕСЬ СТОЯЛ `refresh_squad_carry` — догон обоза после ухода души.
        //  Снят: спину уносит СВОДКА, которую правит сама запись слота.)
    }
    return deserted;
}

int squad_bags_hygiene_daily(MacroWorld& mw) {
    if (!mw.gs || !mw.store) return 0;
    int melted = 0;
    // Порядок по ординалу (squad_walk.h): авто-скрап и гашение счёта трогают
    // цену дня через факты — одна очередь фактов на всех.
    MacroStore& st = *mw.store;
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
        // (world_tick settle_place_day). Двери прихода у сквада разные
        // (добыл, купил, отнял), и городить у каждой свою уплату значило бы
        // писать закон пятый раз; вместо этого контейнер ест то, что приехало,
        // тем же вечером. «Добыча привезла — часть съелась» (владелец,
        // 2026-09-21), и это ТОТ ЖЕ econ_pay_debt, которым платит ландмарк.
        econ_pay_debt(bag.inv, st.upkeep[slot].needDebt, mw.econFacts,
                      mw.econFactsUser);
    }
    return melted;
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
    runtime.sweepOrder.reserve(kUnifiedCap);
    runtime.squadIndex.order.reserve(kUnifiedCap);
}

// ONE assembly of the AI think's view (canon-audit H2: this block used to
// exist twice, line for line, one copy per driver, and the copies drifted —
// the budgeted twin once shipped without `deposits`, so every miner in the
// world stopped digging while the player was underground). The layer envelope
// arrives assembled by its owner; this adds only the drive-state.
static TickContext make_tick_context(MacroWorld& mw,
                                     MacroNpcAiRuntime& runtime) {
    TickContext ctx{};
    ctx.mw      = mw;
    ctx.mapW    = mw.gs->mapW;
    ctx.mapH    = mw.gs->mapH;
    ctx.rng     = &runtime.jitter;
    // The player is a squad: WHERE he is = his flag holder's cell, the same
    // one-number truth every squad keeps (подпосадка 4).
    if (mw.store) {
        if (const ecs::MacroCell* pc =
                player_flag_cell(*mw.gs, *mw.store)) {
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
    // ЗДЕСЬ ЗАПЕКАЛАСЬ МАСКА ВРАЖДЕБНОСТИ, И ОНА СНЕСЕНА (M-268, вердикт
    // владельца 2026-10-21). Запекалка была ПОСЛЕДНИМ жильцом строк в
    // макро-думке — 342 табличных id на драйв = 175 104 скана реестра за
    // игровой день, — а запечённые биты были копией ДИНАМИЧЕСКОЙ матрицы при
    // ровно одном читателе (охота по следу). Оба вопроса — «враг ли он» и
    // «есть ли враги вообще» — отвечает теперь сама матрица дверью
    // `factions_hostile@src/macro/factions.h`: строка отношений есть одна
    // кеш-линия, то есть биты не покупали и скорости.
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
    MacroStore& st = *mw.store;
    kill_fallen_squad_creatures(st, gs, mw.econFacts, mw.econFactsUser);
    destroy_dead_macro_squads(st, gs);
}

} // namespace

void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks) {
    if (!mw.gs || !mw.store) return;   // no store, no thinking (fail-closed)
    GameState& gs = *mw.gs;
    MacroStore& st = *mw.store;

    build_squad_index(runtime.squadIndex, st, gs.mapW, gs.mapH);

    // Свежесть запечённой навигации — раз на свип, не в шаге (тор-закон
    // gigahrush2 «never re-bake per tick»: перепёк только на границах).
    if (mw.nav) nav_ensure(mw, *mw.nav);
    scent_ensure(gs.scent, gs.mapW, gs.mapH);

    TickContext ctx = make_tick_context(mw, runtime);
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
        // ТРАТА КВАНТА — ДВЕРЬ «ВСЁ ИЛИ НИЧЕГО» (M-218, 2026-10-12). Здесь
        // стояли гейт и голое `-=` двумя строками, то есть `try_spend`,
        // написанный руками над БЕЗЗНАКОВЫМ счётчиком: разъехались бы они —
        // и копилка завернулась бы в 4 млрд тиков, то есть сквад перестал бы
        // думать молча и навсегда. Дверь делает расхождение невыразимым.
        if (!try_spend(rt.tickAccum, kAiTicks)) continue;

        if (kind.type >= std::uint16_t(NPCType::Count)) continue;
        // A battle earlier in this very sweep may have killed this squad —
        // отбор жил на входе свипа, so re-check.
        if (st.dead[slot] != 0) continue;
        const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
        if (gate == ThinkGate::Dead) continue;
        // Цена груза — ДО поведения, тем же моментом, каким её брала
        // снесённая колонка (M-302): час, который оплатит ритм ниже, пройден
        // с ТОЙ сумкой, какая была на входе думки, а не с той, какую сквад
        // набрал на рынке внутри неё.
        const int overloadCost = overload_cost_of(rt, &st.inventory[slot]);
        // Decode → think in fractional scratch → encode (the scale split):
        // the STORE is one whole-cell number; the march's own float math
        // lives only on this think's stack.
        MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                   float(ecs::cell_y(cell, gs.mapW))};
        const float x0 = p.x, y0 = p.y;
        if (gate == ThinkGate::Think)
            dispatch(h, p, kind, rt, hp, ctx);
        settle_march_rhythm(h, p, rt, hp, p.x != x0 || p.y != y0, ctx,
                            overloadCost);
        const std::uint32_t newIdx = ecs::cell_index(int(p.x), int(p.y),
                                                     gs.mapW);
        // ДОЛЯ КЛЕТКИ ПОСЛЕ ХОДА (M-244): клетка сменилась — сквад стоит у
        // края входа по румбу последнего шага (ходок уже проштамповал
        // entryDir); прыжок/телепорт штампует kEntryDirNone, и dx=dy=0
        // честно дают центр. Клетка не сменилась — доля не трогается.
        if (newIdx != cell.idx) {
            int sdx = 0, sdy = 0;
            unpack_entry_dir(rt.entryDir, sdx, sdy);
            st.sub[slot] = ecs::cell_sub_after_step(sdx, sdy);
        }
        cell.idx = newIdx;
    }

    // The END of every dead squad's story, once per tick (CANON S4): any
    // stragglers' survivors to the pool (idempotent for the already-drained;
    // it also catches a dead=1 creatures a save carried across the sweep
    // window), then the drained corpse-rows leave the map. Deferred to HERE
    // because the settle doors' callers still hold the entities mid-tick.
    settle_dead_squads(mw);
}

// (tick_macro_npc_visuals жил здесь и умер с M-244a: глайд карты — чистое
// ПРЕДСТАВЛЕНИЕ, и его интегратор переехал в кэш глаз оверлея —
// tick_macro_eyes@src/ui/macro_overlay.cpp; мир несёт клетку и целую долю
// CellSub, float-колонки вида в store больше нет.)

MacroNpcAiSliceResult tick_macro_npc_ai_budgeted(
    MacroWorld& mw,
    MacroNpcAiRuntime& runtime, std::uint64_t ticks, int max_npc_ticks) {
    MacroNpcAiSliceResult result{};
    if (max_npc_ticks <= 0) return result;
    if (!mw.gs || !mw.store) return result;  // no store, no thinking
    GameState& gs = *mw.gs;

    constexpr int kMaxQueuedSweeps = 4;
    if (ticks > 0) {
        runtime.sweepAccum +=
            std::uint32_t(std::min<std::uint64_t>(ticks, kAiTicks * kMaxQueuedSweeps));
        // Та же дверь, тот же закон (M-218): условие цикла И ЕСТЬ трата
        // кванта, а не спутник вычитания рядом с ним.
        while (try_spend(runtime.sweepAccum, kAiTicks)) {
            if (runtime.pendingSweeps < kMaxQueuedSweeps) {
                ++runtime.pendingSweeps;
            } else {
                result.backlog = true;
            }
        }
    }
    if (runtime.pendingSweeps <= 0) return result;

    MacroStore& st = *mw.store;

    build_squad_index(runtime.squadIndex, st, gs.mapW, gs.mapH);

    // The same ONE assembly as the map-view driver. This used to be a paste
    // that had drifted (the deposits epitaph now lives on TickContext itself,
    // npc_ai.h); the two drivers may differ in HOW they walk the slots —
    // never in what world the squads think about (CANON.md S2).
    if (mw.nav) nav_ensure(mw, *mw.nav);
    scent_ensure(gs.scent, gs.mapW, gs.mapH);
    TickContext ctx = make_tick_context(mw, runtime);
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
                    // Цена груза — ДО поведения, тот же момент (M-302).
                    const int overloadCost =
                        overload_cost_of(rt, &st.inventory[slot]);
                    // Decode → fractional scratch → encode (the scale split).
                    MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                               float(ecs::cell_y(cell, gs.mapW))};
                    const float x0 = p.x, y0 = p.y;
                    if (gate == ThinkGate::Think) {
                        dispatch(h, p, kind, rt, hp, ctx);
                    }
                    settle_march_rhythm(h, p, rt, hp,
                                        p.x != x0 || p.y != y0, ctx,
                                        overloadCost);
                    const std::uint32_t newIdx =
                        ecs::cell_index(int(p.x), int(p.y), gs.mapW);
                    // Та же дверь доли, что у полного свипа выше (M-244).
                    if (newIdx != cell.idx) {
                        int sdx = 0, sdy = 0;
                        unpack_entry_dir(rt.entryDir, sdx, sdy);
                        st.sub[slot] = ecs::cell_sub_after_step(sdx, sdy);
                    }
                    cell.idx = newIdx;
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

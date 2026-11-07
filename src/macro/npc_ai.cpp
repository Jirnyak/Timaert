// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include <numbers>                 // sqrt2 — евклидова дорога картинки
#include "core/caps.h"             // kUnifiedCap — резерв скрэтчей порядка
#include "macro/upkeep_window.h"   // ОДИН суд границы на всякий контейнер
#include "macro/characters.h"  // стол анкет — ступень лестницы поведения
#include "macro/chronicle.h"
#include "macro/currency.h"
#include "macro/deal.h"   // deal/unit_mass_g/kNoCarryLimitG — один глагол обмена
#include "macro/economy.h"
#include "macro/deposit_layer.h"
#include "macro/econ_day.h"
#include "macro/macro_stock.h"
#include "bus/entry_context.h"
#include "tables/faction.h"
#include "macro/labour.h"           // ОДИН пул рук места (CANON S4)
#include "macro/rooted_iter.h"    // for_each_rooted — обход мест по оси рода
#include "tables/squad_type.h"   // SquadType/rooted_is_settlement — ось рода
#include "tables/actions.h"   // kLabourActions — строки добычи, одни на оба мира
#include "tables/movement_cost.h"
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
#include "bus/store.h"
#include "macro/threat_field.h"     // поле угрозы: скор патруля, страх артелей
#include "macro/travel.h"
#include "ecs/components.h"
#include "core/subtract.h"   // drain — кванты марша уходят дверью
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
// (`macro_handle_by_spawn_id@src/bus/store.h`, M-271). Прежде дешевизну
// приходилось ПРИНОСИТЬ С СОБОЙ — порядком драйва отдельным параметром, — и
// у кого порядка не было (фикстура, дневная ротация с её `TickContext{}`),
// тот падал в скан: замер 64 дней назвал эту ветку самым частым сканом мира,
// 842 112 вызовов из 1 739 080 (48.4 %).
// Ось рода гейтится НЕ ЗДЕСЬ, а в самой двери `rooted_handle_by_ordinal`
// (`src/macro/squad.h`): пространство ординалов ОДНО (M-37), и «это место»
// отвечает только ось тела — одним написанием на весь мир. Ниже остался
// ПЕРЕХОДНИК «хэндл → слот», и он ОДИН на всех звонящих этого файла.
inline std::uint16_t rooted_slot_of(const MacroStore& st, int id) {
    if (id <= 0) return kMacroNoSlot;   // 0 = «никого» (ЗАКОН НУЛЯ-ОРДИНАЛА)
    const MacroHandle h = rooted_handle_by_ordinal(st, std::uint32_t(id));
    // «Дома нет» = kMacroNoSlot — ПОСЛЕДНЕЕ значение типа индекса (ЗАКОН
    // УЗКОГО ИНДЕКСА: слот 0 законен, нулём тут сказать нечего).
    return st.valid(h) ? h.slot : kMacroNoSlot;
}
inline std::uint16_t rooted_slot_by_id(const TickContext& ctx, int id) {
    return rooted_slot_of(store_ctx(ctx), id);
}

// УРНА ГОРОДОВ — ОДИН предикат на оба рейса (торговец и кочевник). Он стоял
// ДВУМЯ побайтово одинаковыми лямбдами в двух соседних поведениях, и это был
// второй ответ на один вопрос мира (DOD п.6), просто спрятанный в области
// видимости функции. Обход мест — одна дверь (`for_each_rooted`,
// `macro/rooted_iter.h`): ось рода ТЕЛА отвечает «город ли это», второго
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
        rooted_slot_by_id(ctx, parent_of(store_ctx(ctx), self.slot));
    if (slot == kMacroNoSlot) return false;
    const MacroStore& st = store_ctx(ctx);
    out = {float(slot_x(st, slot, ctx.mapW)),
           float(slot_y(st, slot, ctx.mapW))};
    return true;
}

// ДОМ АРТЕЛИ — СЛОТ тела места; kMacroNoSlot = дома нет. (Спутник
// home_inventory умер с переводом сдачи груза на deal: сделка адресуется
// слотами, и склад по слоту берёт сама дверь.)
std::uint16_t home_rooted_slot(MacroHandle self, const TickContext& ctx) {
    return ctx.mw.gs
        ? rooted_slot_by_id(ctx, parent_of(store_ctx(ctx), self.slot))
        : kMacroNoSlot;
}

// Empty the gatherer's own bag of the catalog row `defIdx` into his home
// store — the shared arrival half of every honest work-loop (woodcutter,
// farmer). ОРДИНАЛ, А НЕ СТРОКА (ЗАКОН СЛОВАРЯ): строка цели переводится в
// ординал один раз рядом со своей таблицей (gatherer_item_index).
void deliver_bag_home(MacroHandle self, const TickContext& ctx, int defIdx) {
    if (!ctx.mw.store) return;
    MacroStore& st = store_ctx(ctx);
    const std::uint16_t homeSlot = home_rooted_slot(self, ctx);
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
    // (ДОКЛАД `Gathered` СТОЯЛ ЗДЕСЬ — приход И ЕСТЬ поток добычи, и он один
    //  из девяти, умерших 2026-11-03 вместе со вторым фактным путём мира:
    //  некролог — `src/macro/econ_day.h`. Число не потеряно как СОБЫТИЕ —
    //  дверь сделки отдаёт его (`DealFact::unitsAB`); потерян СУММАТОР по
    //  миру, читателем которого был один прибор.)
    (void)deal(st, self.slot, homeSlot, lot, {},
               kNoCarryLimitG, kNoCarryLimitG);
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
// before the stepping code is written. Берёт САМУ вес-сетку, не TickContext:
// это всё, что вес читает (M-412 — тем же входом его зовёт и марш игрока).
float cell_weight(const PathCostData* pc, int x, int y);

// РЕФЛЕКС УГРОЗЫ — ОБЪЯВЛЕН ЗДЕСЬ, ПОТОМУ ЧТО ЕГО ЗОВУТ МОДУЛИ, А НЕ ДИСПЕТЧ
// (первая половина M-307, ломоть 5а M-72). Преамбула рефлексов переехала из
// `dispatch` в головы четырёх думающих модулей, и три из них написаны ВЫШЕ
// определения этого тела — та же причина, по которой выше объявлены ритм и
// вес клетки. Два других рефлекса (`scent_squad_deposit`, `scent_hunt_step`)
// объявления здесь не просят: они публичны и приезжают из `npc_ai.h`.
bool squad_threat_step(MacroHandle self, MacroPos& p,
                       const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                       ecs::Pools& pools, const TickContext& ctx);

// (No can_stand_at, and no nav_can_stand behind it. THE standing predicate
// died 2026-10-06 — «да уничтодить вторую стену она портит всё (ВЕСА БЫЛО
// ЕДИНОЕ РЕШЕНИЕ», владелец: a cell a walker could not stop on was a SECOND
// answer to the question the weight table already answered, and it held
// 4328 land cells outside every region of the baked navigation. Standing is
// now possible everywhere and lethality is a CONSEQUENCE of the two always-on
// processes — macro/recovery.h settle_pools_over_time.)

// ВО СКОЛЬКО РАЗ НЕСОМЫЙ ГРУЗ ДОРОЖИТ ЭТОМУ СКВАДУ ЧАС ПУТИ — ЧИСЛО ОДНОЙ
// ДУМКИ, И ХРАНИЛИЩА У НЕГО НЕТ (вердикт владельца 2026-10-21, дословно:
// «убирай нафиг нам хранить скока перегруз ест сели просто достатчно хранить
// 2 значения вес который может нести сквад и вес который несёт»).
//
// ВО СКОЛЬКО РАЗ, А НЕ СКОЛЬКО SP — вердикт владельца 2026-10-24: перегруз
// есть МНОЖИТЕЛЬ жжения (вывод — `overload_burden_from_capacity@src/macro/
// travel.h`). Прежняя абсолютная цена в SP входила в жжение слагаемым и
// потому жгла стоящего, у которого жжения нет: именно этим мир стирал свои
// места (M-310, мест 2275 → 90). Множителю это невыразимо.
//
// А ЕЩЁ РАНЬШЕ ЗДЕСЬ СТОЯЛА `refresh_overload_cost`, ПИСАВШАЯ КОЛОНКУ `overloadCost`
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
inline float overload_burden_of(const ecs::MacroNpcRuntime& rt,
                                const ecs::NpcInventory* bag) {
    if (!bag) return 1.0f;   // нет сумки — нечего нести, множитель нейтрален
    return overload_burden_from_capacity(carry_capacity_g(rt, bag->inv),
                                         bag->inv);
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
// to anything that stopped, ocean included. Until 2026-10-24 the ocean stayed
// lethal to a STANDING body too, by the sign of 20 SP of burn against 13.75 of
// rest; the owner's inversion took the burn out of standing altogether, so what
// the open sea now costs is paid ВПЛАВЬ and nowhere else. The old «standing
// where no camp is possible is not rest» was the second half of the wall, and
// it died with the first.
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
// `overloadBurden` — МНОЖИТЕЛЬ ГРУЗА ПАРАМЕТРОМ, А НЕ КОЛОНКОЙ (M-302 +
// M-310): свип считает его дверью `overload_burden_of` выше в той же думке, ДО
// поведения, и передаёт сюда. МОМЕНТ РАСЧЁТА ОСТАВЛЕН ТОТ ЖЕ, что был у
// снесённой колонки `overloadCost`, — менялись с тех пор только тип числа
// (`int16` → `int` → `float`) и его РОЛЬ в формуле (слагаемое → множитель).
// `captured` — АРЕНДОВАН ЛИ СТАРТ ДУМКИ (ЗАХВАТ, ломоть 2 M-419): свип
// сэмплит его тем же моментом, что землю часа, и передаёт параметром — как
// грунт и груз. Захваченный для ритма ВСТАЛ (макро-ног в окне нет: реген —
// фон, жжения нет — «жжение только при движении»), а доезд доли в центр для
// него не бежит: доля — писателя fold.
void settle_march_rhythm(MacroHandle e, const MacroPos& p,
                         ecs::MacroNpcRuntime& rt, ecs::Pools& hp,
                         bool moved, bool captured, const TickContext& ctx,
                         float overloadBurden, float groundWeight) {
    const int maxSp = std::max<int>(1, hp.maxSp);

    // ЧТО ЧАС ПУТИ СТОИТ ЭТОМУ ТЕЛУ НА ЭТОЙ ЗЕМЛЕ. Все три входа цены —
    // ПАРАМЕТРЫ, снятые ДО поведения, и это одно высказывание, а не три:
    // грунт (`groundWeight`), собственный груз сквада (`overloadBurden`,
    // оценённый по его сумке свипом) и выучка (`travelRank`, кэш листа,
    // который читал сам марш, так что лист в думке не пересобирается).
    //
    // ГРУНТ ПРИХОДИТ ПАРАМЕТРОМ, И ЭТО ПОЧИНКА БАГА, А НЕ СТИЛЬ (M-395,
    // вердикт владельца 2026-11-04: «клетка старта»). Здесь стояло своё
    // чтение `cell_weight(ctx, int(p.x), int(p.y))`, а `p` на входе в ритм
    // есть позиция ПОСЛЕ думки — то есть час оплачивался по клетке, в которой
    // думка КОНЧИЛАСЬ, тогда как темп той же думки (`perThink`) считался по
    // клетке, в которой она НАЧАЛАСЬ. Думка покрывает до трёх клеток, а цена
    // клетки идёт дорога 0.25 · луг 0.70 · чаща 2.39 · гора 2.77 · вода 7.83
    // SP, значит думка «из воды на дорогу» платила по дороге — промах до ×31.
    // Второго сэмпла в коде не осталось: свип сэмплит землю ОДИН раз, и у
    // скорости с ценой теперь одна и та же земля под ногами.
    //
    // СТОЯЩЕЕ ТЕЛО ЭТОГО НЕ ПЛАТИТ, и гейт стоит не здесь, а в двери
    // (`settle_pools_over_time` берёт `resting` и решает сама) — поэтому
    // ставку можно считать безусловно, одной строкой, не спрашивая заранее,
    // пойдёт ли тело.
    const float burnRate = burn_stamina_per_hour(
        groundWeight, overloadBurden,
        skill_mult_of(SkillId::Travel, int(rt.travelRank)));

    // The automaton's CAMP decision (npc_ai.h kCampBarDivisor): legs below an
    // eighth of the bar pitch camp. The half-bar wake-up (prepare_) resumes
    // the leg.
    //
    // БЕЗ ВОПРОСА «А ОКУПИТСЯ ЛИ ЗДЕСЬ ПРИВАЛ» (снос 2026-10-24): привал
    // окупается ВЕЗДЕ по построению — у стоящего тела жжения нет, а реген
    // пассивен, значит планка в лагере только растёт и порог пробуждения в
    // половину планки достигается всегда. Эпитафия предиката и четырёх его
    // ветвей — у `rest_stamina_per_hour@src/tables/movement_cost.h`.
    //
    // ONE camp decision, and it used to be TWO: settle_exhaustion took the
    // same decision again for a body already in debt, each behind its own
    // copy of the standing predicate. A bar at or below an eighth already
    // includes every negative bar there is, so the second site was answering a
    // question this one had answered — it went with the predicate.
    if (int(hp.sp) <= maxSp / kCampBarDivisor
        && rt.state != std::uint8_t(NPCState::Resting)) {
        rt.stateAfterPause = rt.state;   // пауза, не амнезия (components.h)
        rt.state = std::uint8_t(NPCState::Resting);
        // ГЕЙТ ЗАНЯТОСТИ ПРИВАЛ НЕ ТРОГАЕТ (шаг E ломтя stateTimer M-369):
        // здесь стояло обнуление — ровно та форма, которой M-313 купил себе
        // закон (лагерь, зануляя чужой долг, учетверял крафт места). Часы
        // тают временем и в лагере — как у тела («re-deriving would cancel
        // a swing mid-recovery», sub/spawn.cpp).
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
    const bool stopped = captured
                         || rt.state == std::uint8_t(NPCState::Resting)
                         || at_goal(p, store_ctx(ctx).goal[e.slot], ctx)
                         || (!moved
                             && (rt.marchFlags & ecs::kMarchRefused) != 0);

    // ...and `!moved` on top, because a think that arrived still MARCHED: you
    // do not walk two cells and take a slice of rest in the same breath. Rest
    // begins on the first think after the legs stop.
    const bool resting = stopped && !moved;

    // ВСТАВШЕЕ ТЕЛО ДОЕЗЖАЕТ В ЦЕНТР СВОЕЙ КЛЕТКИ (M-375, «у нас вообще
    // должно быть завершено стояние между клетками»): думка, чья машина
    // марша не звалась (цель — своя клетка, ожидание, работа на месте),
    // иначе навсегда оставила бы долю на краю входа. Темп — базовый шаг
    // думки (та же величина, что производит марш); это НЕ марш — resting
    // уже вычислен, жжения доезд не несёт.
    // …НО НЕ В АРЕНДЕ: доля захваченного — писателя fold (ЗАХВАТ, ломоть 2
    // M-419); доезд сверху был бы вторым писателем той же величины.
    if (resting && !captured) {
        ecs::cell_sub_walk_home(
            rt.sub,
            std::uint32_t(std::lround(kMacroWalkCellsPerHour
                                      * kAiTickGameHours
                                      * float(2 * ecs::kCellSubEdge))));
    }

    // THE two processes, THE one door (recovery.h settle_pools_over_time),
    // asked UNCONDITIONALLY — which is the shape of the law and not a widening
    // of it. Since the 2026-10-24 inversion the REGEN half runs for a body
    // that marched, a body that stood and a body asleep in camp alike, and
    // what `resting` now gates is the BURN (plus HP/MP, where «марш не лечит»
    // still stands). Before all this it was an `if` around the whole call, and
    // the price of a step lived in try_move below — two laws of one quantity,
    // the thing §5 п.14 is written about.
    const int bite = settle_pools_over_time(
        hp, kAiTickGameHours, int(rt.marathonRank), burnRate,
        /*resting=*/resting);

    // ...and a body the BITE killed is buried here. Gated on the bite itself,
    // not on `hp <= 0`: a squad cut down by an auto-battle earlier in this
    // very think already went through its own death door, and a second funeral
    // would settle its avatar and kill its creatures twice.
    if (bite > 0) settle_exhaustion(e, hp, ctx);
}

// Weight of a macro cell from the baked grid the player's A* walks
// (mw.pathCost); a missing grid reads as a featureless free-road world.
float cell_weight(const PathCostData* pc, int x, int y) {
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
float edge_weight(const PathCostData* pc, int fx, int fy, int tx, int ty) {
    float w = cell_weight(pc, tx, ty);
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

// ТОНКИЙ ЗВОНЯЩИЙ ДУМКИ (M-412а/б): ядро закона марша экспортировано
// (`try_move@src/macro/npc_ai.h`) и зовётся обоими ходоками — думкой НПЦ
// отсюда и маршем игрока из `step_macro_walk@src/ui/macro_overlay.cpp`.
// Здесь остался только состав БЮДЖЕТА этого звонящего: клетки за думку —
// the SAME march the player walks (kMacroWalkCellsPerHour per game hour),
// paced by the avatar's own sheet (moveMult: spd × athletics) and by the
// ground underfoot (terrain_speed_mult of the CURRENT cell — one sample
// per think). Base numbers: 3 cells per think on a road, ~1 in open water.
// ЛЕТУН (v93): воздух — его стихия; рельеф не платится (вес — единица),
// внутри ядра любая клетка держит — ОДИН цикл марша, меняется стихия.
void try_move(MacroPos& p, ecs::MacroNpcRuntime& rt,
              float tx, float ty, const TickContext& ctx) {
    const bool flying = rt.flying != 0;
    const float perThink = march_cells_per_think(
        rt, flying ? 1.0f : cell_weight(ctx.mw.pathCost, int(p.x), int(p.y)));
    const std::uint32_t quanta = std::uint32_t(
        std::lround(perThink * float(2 * ecs::kCellSubEdge)));
    MarchGround g;
    g.pathCost = ctx.mw.pathCost;
    g.nav = ctx.mw.nav;
    g.side = ctx.mapW;
    g.playerCellX = wrap_axis(int(ctx.playerX), ctx.mapW);
    g.playerCellY = wrap_axis(int(ctx.playerY), ctx.mapH);
    // Центр аренды — ядру (изъятие ног, ломоть 2 M-419). Тернар обязателен:
    // `ctx.mw.gs` нуллим по контракту (те же `if (ctx.mw.gs && …)` у судов
    // автобоя ниже).
    g.leaseCenter = ctx.mw.gs ? ctx.mw.gs->leasedWindow : kNoWindowLease;
    const float walked = sm::try_move(p, rt, tx, ty, quanta, g);
    // Скорость картинки — евклидова ДОРОГА думки за её период, одна дверь
    // (`set_visual_speed@src/macro/squad.h`). Пишется и нулём: стоящее тело
    // (заминка боя, отказ ног) показывает ноль, а не прошлый темп.
    set_visual_speed(rt, walked, kAiPeriodSeconds);

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
        if (!squad_recovering(rt)) {
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
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 20)) * kAiTicks));
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
// The rate is the ROW's spRatePerDay (`kLabourActions@src/tables/actions.h`,
// anchored to kGatherPerWorkerDay) — the same row the subworld's axe charges
// by: one law of labour, one row for both worlds (M-369). No worksite or no
// wired layer = the home wander, fail closed — nothing is conjured.
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
    // СТРОКА АКТА — ССЫЛКА ОРДИНАЛОМ в единую таблицу действий (парадигма
    // трёх контент-систем, CANON 1г: связи между системами — ординалом).
    // Темп источника и цена цикла — ЕЁ колонка spRatePerDay (бар/темп,
    // sp_price); прежняя колонка perWorkerDay умерла в неё ломтём «Добыча»
    // M-369 — это та самая строка, которой платит топор субмира: один закон
    // труда, одна строка на оба мира. Эта спецтаблица — половина рода при
    // типе: ЧТО покидает мир и ГДЕ работать, чего L0 не выразит (реестр
    // полей — слой macro).
    std::uint8_t    action;
};

constexpr GathererDef kGathererDefs[] = {
    // ПИЩА из двух источников: пашня — полный якорь, охота — четверть его
    // (зверь бегает, а колос стоит; темпы — колонки строк актов). Именно
    // это кормит хутор на скале, где пашни нет, а зверь есть.
    {ResourceFieldId::Wheat,  Worksite::HomeField,     action_slot("reap_wheat")},
    {ResourceFieldId::Fauna,  Worksite::HomeFauna,     action_slot("hunt_fauna")},
    // ЛЁН — ТА ЖЕ ЗЕМЛЯ, СВОЯ ПАРЦЕЛЛА (владелец, 2026-09-20: «на пашне уже
    // пшеница-пища, значит нужна фича льняное поле»). Ряд тот же арабельный:
    // клетка под льном — это клетка, не занятая хлебом, и конкуренция за
    // землю выходит сама, без второго поля и без нового ресурса.
    // ЧТО с парцеллы берут — говорит ФИЧА (FT_FlaxField → fibre), не цель.
    {ResourceFieldId::Wheat,  Worksite::HomeFlaxField, action_slot("reap_flax")},
    // ШАХТЁР — ОДНА ЦЕЛЬ НА ВСЕ ЖИЛЫ (вердикт 2026-10-23: артель не
    // профессия, добыча универсальна): что добудется, решает фича шахты,
    // на которую артель пришла, — её строка, не строка цели.
    {ResourceFieldId::Vein,   Worksite::Deposit,       action_slot("mine_vein")},
    {ResourceFieldId::Trees,  Worksite::ForestCell,    action_slot("fell_tree")},
    // ЛОШАДЬ-ЮНИТ: поимка стоит работнику ДЕНЬ (темп 1 её строки = целый
    // бар по закону цены труда S14.1) против 32 колосьев той же ценой;
    // выход не товар, а существо — умолчание ряда Livestock (реестр полей).
    {ResourceFieldId::Livestock, Worksite::HomePasture,
     action_slot("catch_livestock")},
};
constexpr int kGathererGoalCount =
    int(sizeof(kGathererDefs) / sizeof(kGathererDefs[0]));

// Спецтаблица добычи обязана ссылаться ТОЛЬКО на трудовой отрезок: цена
// цикла ниже делит бар на spRatePerDay строки, и нетемповая строка (0)
// молча отдавала бы весь бар за акт (sp_price(bar, 0) = bar).
constexpr bool gatherer_rows_point_at_labour() {
    for (const GathererDef& d : kGathererDefs) {
        const ActionDef& a = action_def(int(d.action));
        if (archetype_of(a.type) != ActionArchetype::Labour) return false;
        if (a.spRatePerDay <= 0) return false;
    }
    return true;
}
static_assert(gatherer_rows_point_at_labour(),
              "каждая цель добытчика — строка Gather с темповой ценой: "
              "ссылка ординалом мимо трудового отрезка несобираема");

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
// M-254). ЛАГЕРЬ — ПАУЗА, НЕ АМНЕЗИЯ (`stateAfterPause@src/ecs/components.h`):
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
    rt.state = rt.stateAfterPause;
    rt.stateAfterPause = std::uint8_t(NPCState::Idle);
    // Гейт занятости подъём НЕ трогает (шаг E ломтя stateTimer M-369):
    // прежнее «часы прерванного состояния не переносятся» было законом
    // СЛУЧАЯ — у единого гейта долг акта переживает привал, как у тела.
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
// есть залежь (`FeatureDef::numberRow == Vein`), и живой залежью в `featureNumber`.
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
        // залежь спрашивается ДВЕРЬЮ ряда (число фичи сырьём читает только она).
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
                          std::int64_t capG);
// ФОЛБЭКИ — ТОЖЕ ГЛАГОЛЫ ПУЛЬТА: сквад без дома и сквад без цели ходят
// чужими модулями, и право позвать их даёт пульт, а не прямой вызов из тела.
void ai_nomad(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx);


// Приёмник-МЕСТО (CANON S10): склад + счёт — дверь прихода гасит долг СРАЗУ
// тем, что упало. Сумки в Depot не заворачиваются (неявная конверсия из
// Inventory&, долга нет).
inline Depot depot_(std::uint16_t slot, const MacroWorld& mw) {
    MacroStore& st = store_mw(mw);
    return Depot(st.inventory[slot].inv, st.upkeep[slot].needDebt);
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
    // СВОЯ анкета (ЗАКОН АНКЕТЫ п.2: «анкета источник») — эффективное
    // рековери акта считается из НЕЁ той же дверью, что у руки внизу.
    const CharacterSheet& sheet;
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
        return home_rooted_slot(self, ctx_);
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
    long long need_price(std::uint16_t rootedSlot, int itemIdx) const {
        if (!ctx_.mw.store || rootedSlot == kMacroNoSlot || itemIdx < 0)
            return 0;
        const MacroStore& st = *ctx_.mw.store;
        const ItemDef* d = item_def_at(itemIdx);
        if (!d || d->value <= 0) return 0;
        const Inventory& homeInv = st.inventory[rootedSlot].inv;
        const int demand = season_demand_for(
            itemIdx, st.upkeep[rootedSlot].needDebt,
            souls_home(st, rootedSlot), st.sheet[rootedSlot].skills, &homeInv);
        return stock_price(d->value, homeInv.count_of(itemIdx), demand);
    }
    // ЧЕГО СТРОКЕ ДОМА НЕ ХВАТАЕТ — та же дверь `season_demand_for`, другой
    // вопрос: цена говорит «почём», нехватка — «сколько ещё надо». Вход
    // считается ДВАЖДЫ, и это осознанно: закон выведенной величины прямо
    // предпочитает перевывод хранению тотала («устаревший тотал врёт МОЛЧА, а
    // перевывод всего лишь тратит такты», DOD п.9) — третьей колонки с
    // кэшем спроса здесь не заводится.
    int need_lack(std::uint16_t rootedSlot, int itemIdx) const {
        if (!ctx_.mw.store || rootedSlot == kMacroNoSlot || itemIdx < 0)
            return 0;
        const MacroStore& st = *ctx_.mw.store;
        const Inventory& homeInv = st.inventory[rootedSlot].inv;
        const int lack = season_demand_for(
                             itemIdx, st.upkeep[rootedSlot].needDebt,
                             souls_home(st, rootedSlot),
                             st.sheet[rootedSlot].skills, &homeInv)
                         - homeInv.count_of(itemIdx);
        return lack > 0 ? lack : 0;
    }
    // ── МЕСТО: ОДИН ПЕРЕХОДНИК ОРДИНАЛА И ТРИ ЕГО КОЛОНКИ ────────────────
    std::uint16_t rooted_slot(int id) const { return rooted_slot_by_id(ctx_, id); }
    std::uint32_t rooted_id(std::uint16_t slot) const {
        return store_ctx(ctx_).spawnId[slot].index;
    }
    std::uint32_t rooted_cell(std::uint16_t slot) const {
        return store_ctx(ctx_).cell[slot].idx;
    }
    bool is_settlement(std::uint16_t slot) const {
        return rooted_is_settlement(
            SquadType(store_ctx(ctx_).runtime[slot].squadType));
    }
    XY rooted_xy(std::uint16_t slot) const {
        const MacroStore& st = store_ctx(ctx_);
        return {float(slot_x(st, slot, ctx_.mapW)),
                float(slot_y(st, slot, ctx_.mapW))};
    }
    // МЕСТО ЦЕЛИ — ОДНА ДВЕРЬ НА ТРИ ЧИТАТЕЛЯ РЕЙСА (прежде этот тернарник
    // стоял ТРЕМЯ написаниями в одном модуле: «не дома», погрузка и сделка на
    // рынке — DOD п.6). kMacroNoSlot = цель местом не названа либо место
    // умерло, и это ЗНАЧЕНИЕ, а не ошибка: рейсу тогда некуда ехать.
    std::uint16_t goal_rooted(const ecs::SquadGoal& g) const {
        return ecs::goal_kind(g.what) == ecs::GoalKind::Rooted
            ? rooted_slot_by_id(ctx_, int(ecs::goal_ordinal(g.what)))
            : kMacroNoSlot;
    }
    // Долг вассала на ребре Suzerain (M-245) — есть ли что взыскивать.
    bool owes_tithe(std::uint16_t rootedSlot) const {
        return sm::owes_tithe(store_ctx(ctx_), rootedSlot);
    }
    // МОЙ ДОЛЖНИК — ПЕРВЫЙ ВАССАЛ С НЕПОГАШЕННЫМ ДОЛГОМ, И ВОПРОС ЭТОТ
    // ЗАДАЁТСЯ СВОИМ РЕЕСТРОМ СВЯЗЕЙ, А НЕ ПОИСКОМ ПО МИРУ. Зеркало
    // `Stance::Vassal` пишет та же дверь, что ставит `Suzerain`
    // (`set_relation@src/bus/store.h`) — роль несимметрична по природе
    // (ФРЕЙМ род 6), поэтому «кому я сюзерен» лежит колонкой МОЕЙ строки, и
    // обходить мир незачем. Обход плотный: занятые записи идут подряд от
    // нуля, первая `Stance::None` закрывает проход — тот же стоп, которым
    // ходит `tithe_of@src/bus/store.h`.
    // kMacroNoSlot = должника нет, и это ЗНАЧЕНИЕ: сборщику некого доить.
    std::uint16_t owing_vassal(std::uint16_t suzerainSlot) const {
        if (suzerainSlot == kMacroNoSlot) return kMacroNoSlot;
        const MacroStore& st = store_ctx(ctx_);
        const Interests& in = st.interests[suzerainSlot];
        for (int i = 0; i < kMaxInterests; ++i) {
            const Interest& it = in.slots[i];
            if (it.stance == std::uint8_t(Stance::None)) break;
            if (it.stance != std::uint8_t(Stance::Vassal)) continue;
            // Ординал → слот одной дверью резолва (M-271); мёртвый вассал
            // отвечает kMacroNoSlot и просто пропускается.
            const std::uint16_t slot = rooted_slot_by_id(ctx_, it.object);
            if (slot != kMacroNoSlot && sm::owes_tithe(st, slot)) return slot;
        }
        return kMacroNoSlot;
    }
    // ЕСТЬ ЛИ У МЕНЯ ЖИВОЙ РЕБЁНОК ЭТОГО ТИПА — гейт дубля перед рождением,
    // И ОТВЕЧАЮТ НА НЕГО СВОИ РЁБРА, А НЕ СКАН МИРА. Вопрос о СВОИХ детях
    // есть вопрос о СВОЕЙ строке: `set_relation@src/bus/store.h` ставит
    // родителю зеркало `Stance::Child` той же записью, которой ставит
    // ребёнку `Parent`, — значит дети перечислены колонкой МОЕЙ строки, как
    // вассалы у `owing_vassal` выше. Тип на ребре не лежит и не должен:
    // ребро говорит «кто», а «что он делает» есть колонка ЕГО строки
    // (`runtime.squadType`, ЗАКОН АНКЕТЫ п.1) — поэтому форма двухшаговая,
    // ровно та же, которой `owing_vassal` спрашивает долг.
    //
    // ЗДЕСЬ СТОЯЛ ПРОХОД КАПОМ, И СНЕСЁН ОН ЗА ФОРМУ, А НЕ ЗА СКОРОСТЬ
    // (DOD п.6; ЗАКОН КЛЕТОЧНОГО КАРКАСА п.3 — «скан всех записей … тот же
    // класс дефекта, что скан жил у геологии, только на энтити»): два метода
    // ОДНОГО класса, стоящие в сорока строках друг от друга, спрашивали «кто
    // со мной связан» ДВУМЯ разными способами. Структура старше чисел
    // (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ), и это здесь единственный довод.
    //
    // ЧИСЛА НАЗВАНЫ ОБА, ВКЛЮЧАЯ ТО, КОТОРОЕ ВЫИГРЫША НЕ ПОКАЗАЛО. Зонд в
    // самой точке считал ОБЕ формы одним прогоном (72 дня, сид 7): вызовов
    // **99 439**, итераций КАПОМ **400 930 850**, итераций РЁБРАМИ за ТОТ ЖЕ
    // ответ **653 692** — **613×**, в установившемся режиме ≈ 39.9 млн
    // итераций в сутки. А СТЕННОГО ВЫИГРЫША НЕТ: замер назад-к-назад на
    // простое дал `aiMsDay` **2205 / 2215** мс капом против **2237 / 2366**
    // мс рёбрами, то есть разницу ВНУТРИ пола шума этой колонки. Значит скан
    // стоил около процента прохода думки — ровно поэтому его никто и не
    // заметил, — и ускорением эта правка НЕ является. Что она покупает
    // кроме формы: цена ответа перестала расти с КАПОМ, то есть подъём
    // `kUnifiedCap` (ЗАКОН КАПА — наше число, его можно назвать заново)
    // больше не удваивает здесь работу.
    //
    // Обход плотный и ограничен `kMaxInterests`: занятые записи идут подряд
    // от нуля, первая `Stance::None` закрывает проход (`interest_clear`
    // затыкает дыру последней записью — плотность есть часть формы).
    //
    // ПОВЕДЕНИЕ НЕ СДВИНУЛОСЬ, И ЭТО ПАРИТЕТ, А НЕ НАДЕЖДА: 2 сида × 128
    // дней — `places_*.tsv` ПОБАЙТОВО идентичны, из 134 колонок
    // `world_*.tsv` разошлась ОДНА, `aiMsDay` (стенное время прибора).
    //
    // РЁБРА НЕ ГНИЮТ, И ЭТО ТОЖЕ ЗАМЕР, А НЕ НАДЕЖДА (256 дней, сид 7):
    // рёбер `Child` у мест **≈ 2488** (по одному на место), УСТАРЕВШИХ
    // (ординал мёртв или слот прибран) — **0**, максимум занятых записей у
    // места **60–61 ПЛОСКО** из 128, упёршихся в кап — **0**. Переезд
    // ребёнка к новому дому снимает старое ребро той же дверью
    // (`set_relation` чистит зеркало), а ординалы монотонны, поэтому мёртвый
    // ребёнок честно не резолвится и просто пропускается.
    //
    // МЕСТО РЕБЁНКОМ СЕБЕ НЕ ЯВЛЯЕТСЯ — ВЕРДИКТ ВЛАДЕЛЬЦА 2026-10-25.
    // Self-ребро `Parent` у места значит «мой дом — я сам», а не «я свой
    // ребёнок», и зеркала `Child` оно не имеет по построению (`set_relation`:
    // «self-ребро Parent зеркала не имеет»). Прежний скан капом отвечал на
    // вопрос места о СВОЁМ типе «да» — через это самое self-ребро, — и это
    // был АРТЕФАКТ ФОРМЫ, а не закон: ответ никем не спрашивался (деревня
    // спрашивает `Artel`, город — `Collector` и `Caravan`), поэтому смена
    // формы его и сняла.
    //
    // ЧИТАТЕЛИ — МОДУЛИ ТИПОВ (ломоть 5а M-72): `village_think` спрашивает про
    // `Artel`, `city_think` — про `Collector` и `Caravan`. Это гейт ДУБЛЯ
    // перед рождением («у меня уже есть артель, второй не нужно»), и он же
    // делает ненужной колонку «число рождённых»: выводимое не хранится
    // (ЗАКОН АНКЕТЫ п.2, шрам трёх носителей). Свидетель гейта —
    // `tests/rooted_module_day_test.cpp`: пятые сутки, душ с избытком, артель
    // по-прежнему ОДНА.
    bool has_child(MacroHandle self, SquadType t) const {
        const MacroStore& st = store_ctx(ctx_);
        if (!st.valid(self)) return false;
        const Interests& in = st.interests[self.slot];
        for (int i = 0; i < kMaxInterests; ++i) {
            const Interest& it = in.slots[i];
            if (it.stance == std::uint8_t(Stance::None)) break;
            if (it.stance != std::uint8_t(Stance::Child)) continue;
            // Ординал → слот ОДНОЙ дверью резолва (M-271); дети суть ХОДОКИ,
            // поэтому дверь нужна общая, а не местная. Мёртвый ребёнок не
            // резолвится, прибранный — отсеивается байтом `dead`, как и у
            // прежнего прохода.
            const MacroHandle ch =
                macro_handle_by_spawn_id(st, std::uint32_t(it.object));
            if (!st.valid(ch) || st.dead[ch.slot] != 0) continue;
            if (st.runtime[ch.slot].squadType == std::uint8_t(t)) return true;
        }
        return false;
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

// (Здесь жил `kCraftThinksPerDay` = kTicksPerDay/kAiTicks — день, пересчи-
// танный в ДУМКИ под прежний носитель `stateTimer`. Гейт занятости ходит
// МИРОВЫМИ ТИКАМИ, и пересчёт умер вместе с единицей: день есть
// `kTicksPerDay`, своего имени у него здесь больше нет.)

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
        record_rooted_fact(store_ctx(ctx_), *ctx_.mw.gs, FactKind::Drained,
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
                        carry_capacity_g(s.rt, s.bag));
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
            trade_power_of(st.sheet[market]));
    }
    // Летопись: состоявшийся обмен — факт мира (FactKind::Traded).
    void traded_fact(AiSelf& s, int x, int y, int value, int marketId) {
        MacroStore& st = store_ctx(ctx_);
        record_rooted_fact(st, *ctx_.mw.gs, FactKind::Traded,
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
             kNoCarryLimitG, kNoCarryLimitG);
        // ВЫГРУЗКА ИЗ спины В СКЛАД: предела нет, и это ВЫСКАЗЫВАНИЕ, а не
        // пропуск (M-308) — у склада места спины нет вовсе, потому что обоз
        // есть свойство анкеты, которая идёт, а место не идёт никуда
        // (`kNoCarryLimitG@src/macro/deal.h`).
        transfer_value_dense(s.bag, depot_(homeSlot, ctx_.mw),
                             inventory_value(s.bag), kNoCarryLimitG);
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
                     kNoCarryLimitG);
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
            // ЗАГРУЗКА В спину: предел — ОСТАТОК ЁМКОСТИ, тот же, что у
            // ветки нужды выше (M-308). До этого наряда здесь не было
            // предела вовсе, и две половины ОДНОЙ загрузки спрашивали спину
            // по-разному — вассал, гасящий остаток по плотности, грузил
            // сборщика без всякого предела.
            const int dense = transfer_value_dense(
                st.inventory[vassal].inv, Depot(*bag),
                int(std::min<long long>(owed, 1 << 30)),
                carry_capacity_g(rt, *bag) - inventory_mass_g(*bag));
            if (dense > 0) {
                owed -= dense;
                took += dense;
            }
        }
        if (took > 0 && edge) {
            edge->owedValue -= took;
            if (edge->owedValue < 0) edge->owedValue = 0;
            record_rooted_fact(st, *ctx_.mw.gs, FactKind::Taxed,
                                 int(st.spawnId[vassal].index),
                                 int(s.pos.x), int(s.pos.y),
                                 int(std::min<long long>(took, 1 << 30)),
                                 parent_of(st, s.h.slot));
        }
    }
    // ── ПРОИЗВОДСТВЕННЫЙ АКТ — ОДИН ГЛАГОЛ НА ДНЕВНОЙ ВЫПУСК ─────────────
    // «Произвёл» есть РАБОТА, и решает её модуль, а ИСПОЛНЯЕТ дверь: сама
    // лестница рецептов (`econ_produce_day@src/macro/econ_day.h`) не
    // переписана и не удвоена — глагол лишь собирает ей входы ИЗ СВОЕЙ
    // СТРОКИ и закрывает акт восстановлением.
    //
    // ВСЕ ВХОДЫ — СВОИ КОЛОНКИ, МИРА В НИХ НЕТ НИ ОДНОГО: склад — свой
    // контейнер, счёт — свой `needDebt`, руки — скилы СВОЕЙ анкеты (ЗАКОН
    // АНКЕТЫ п.2: «анкета источник»), верстаки — своя паства на квоту
    // `kCreaturesPerCityWorker`, семья монет — своя колонка фракции
    // (`faction_or_freefolk` сводит вольных к имперской; -1 глушит минтовый
    // рецепт fail-closed). Ноль душ держит НОЛЬ верстаков: `max(1, …)` без
    // этого гейта печатал призрачного работника пустому складу.
    //
    // И ГЛАГОЛ САМ СТАВИТ ВОССТАНОВЛЕНИЕ — ОДНА ДВЕРЬ ЗАРЯДА (ЗАКОН
    // СПОСОБНОСТИ п.4): период производства назначает не модуль, а эта
    // точка, поэтому второй акт РАНЬШЕ КОНЦА ЭФФЕКТИВНОГО ДНЯ невыразим.
    // (Здесь стояло «произвёл дважды за СУТКИ невыразимо» — арифметика
    // этого не держит: резвый лист законно укорачивает день, Spd 64 + ранг
    // Armsmaster 27 дают делитель 2; у анкеты МЕСТА делитель 1.00, так что
    // сегодня период и есть календарные сутки — якорь на этом живом числе
    // держит rooted_module_day_test.) Носитель —
    // гейт занятости `recoverySteps@src/bus/squad_row.h`, тот же, что у
    // всех актов сквада. БАЗА — ИЗ ИСПОЛНЕННЫХ CRAFT-СТРОК (их
    // `recoveryTicks`, «рабочий день тиками» — M-326), пустой день пробует
    // через kTicksPerDay; ЭФФЕКТИВНОЕ считает та же дверь
    // `recovery_steps@src/bus/anketa.h`, что у руки внизу (CANON «БАЗОВОЕ И
    // ЭФФЕКТИВНОЕ»: Spd и генерик домена труда только укорачивают день —
    // второй двери темпа не существует).
    //
    // ЗВОНЯЩИЕ — `village_think` и `city_think` (ломоть 5а M-72), и они
    // ЕДИНСТВЕННЫЕ: прежний вызов `econ_produce_day` из
    // `tick_squads_day_@src/macro/world_tick.cpp` УМЕР тем же шагом, которым
    // появились модули, потому что два пути к одному акту мира держать
    // запрещено (§5 п.14). Лестница рецептов при этом НЕ переехала и НЕ
    // удвоилась — она осталась там, где живут рецепты, и зовётся ДВЕРЬЮ.
    // Замер приёмки: `bricks_produced` 0 → 46 073, `minted` 0 → 17 792.
    // Возвращает единиц выпущено (ответ двери, не свой счёт).
    int craft(AiSelf& s) {
        MacroStore& st = store_ctx(ctx_);
        const std::uint16_t slot = s.h.slot;
        const int heads = count_human_souls(s.bag);
        std::uint32_t dayBase = 0;
        const int made = econ_produce_day(
            s.bag, st.upkeep[slot].needDebt, st.sheet[slot].skills,
            heads > 0 ? std::max(1, heads / kCreaturesPerCityWorker) : 0,
            heads,
            faction_or_freefolk(std::int16_t(st.kind[slot].factionIdx)),
            &dayBase);
        const CharacterSheet& sheet = st.sheet[slot];
        charge_squad_recovery(
            s.rt, std::uint32_t(recovery_steps(
                      dayBase > 0 ? dayBase : std::uint32_t(kTicksPerDay),
                      sheet.attributes, sheet.skills,
                      tempo_generic(ActionArchetype::Labour))));
        return made;
    }
    // ── РОЖДЕНИЕ ПАРЫ РОДИТЕЛЬ↔РОЖДЁННЫЙ — ОДИН ГЛАГОЛ ──────────────────
    // «Родил» есть ПАРА, а не узел (вердикт владельца 2026-10-24 №3):
    // родитель называет СЕБЯ в данных рождаемого (`parentId`), и ребро
    // `Parent`/`Child` ставит сама дверь рождения. Отдельной колонки «число
    // рождённых» нет — выводимое не хранится (ЗАКОН АНКЕТЫ п.2, шрам трёх
    // носителей); сколько детей есть, отвечает `AiView::has_child`.
    //
    // ОТКАЗ — В ТОЧКЕ РОЖДЕНИЯ И ВСЛУХ (§5 п.3): душ дома меньше, чем просит
    // крю, — пары не возникает ВОВСЕ, и возврат это говорит невалидным
    // хэндлом. Fail-closed на чтении соврал бы потише: сквад без своих душ
    // был бы рождён из воздуха.
    //
    // РОЖДЕНИЕ ПЛАТИТ ДУШАМИ РОДИТЕЛЯ, И ПЛАТИТ ПО ФАКТУ УСПЕХА
    // (`bleed_flock@src/bus/world_row.h` — дверь drain, берёт сколько
    // есть и возвращает факт): лидер и члены СХОДЯТ головами из своего
    // контейнера. Это и есть предел рождения по построению — потолка числа
    // детей колонкой не заводится (ЗАКОН КЛАМПА: предел наступает обратной
    // связью, и убыль паствы её даёт). Паству (число фичи клетки)
    // рождение НЕ трогает: души ушли В ПОЛЕ, а не умерли.
    //
    // ФРАКЦИЯ НЕ ЗАДАЁТСЯ НАРОЧНО: дефолт спеки (-1) означает «решает
    // земля», и рождённый у дома наследует дом через родителя — второго
    // носителя «чей он» рядом не ставится.
    // Тип рождённому ставится ПОСЛЕ успеха: спека типа не несёт вовсе.
    //
    // ЗВОНЯЩИЕ — ТЕ ЖЕ ДВА МОДУЛЯ, ЧТО У `craft` (ломоть 5а M-72), и рождение
    // рабочих сквадов вернулось в мир НЕ АУКЦИОНОМ, А РЕШЕНИЕМ РОДИТЕЛЯ:
    // деревня поднимает `Artel`, город — `Caravan` и (только при вассале-
    // должнике) `Collector`. Урны, заявок и строк крю по-прежнему нет —
    // аукцион ротации казнён ломтём 4 и не воскрешён. Замер приёмки,
    // 70 дней × 2 сида: `artel` 2313/2129, `caravan` 66/66, `collector`
    // 66/65 — по ОДНОМУ на родителя, то есть гейт дубля держит.
    // ── ПЕРЕПИСАТЬ ФИЧУ ПОД СОБОЙ — ГЛАГОЛ, А НЕ ПРАВКА СЛОЯ ────────────
    // Вердикт владельца 2026-10-25, дословно: «можно сделать через модуль —
    // если сквад умирает он в своём модуле пишет что переписыает фичу под
    // собой её тип в фичу руина … он это должен мочь сделать (это идёт через
    // систему работа/способность)»; и о границе: «это не какотой общий закон
    // а чисто внутри каждого модуля своё — например город переписывает фичу
    // под собой в руины / а допустим будут потом сквады места без фичи под
    // собой (так можно кто сказал чт онет — всё агностично) и он в модуле не
    // будет ничего пистаь».
    //
    // ПОЭТОМУ ЗДЕСЬ ГЛАГОЛ, А НЕ ЗАКОН: пульт умеет переписать фичу, а ЧТО
    // писать и писать ли вообще — дело модуля рода. Слой фич модуль не
    // видит и видеть не должен (DOD-инкапсуляция пульта), дверь одна —
    // `feature_put@src/macro/features.h`, та же, которой фичи ставит
    // генерация, и вода у неё решает ровно у ПОСТАНОВКИ.
    //
    // ВОЗВРАТ — ФАКТ, А НЕ НАМЕРЕНИЕ: `feature_put` отдаёт, ЧТО встало
    // (вода может отказать), и `FT_None` значит «не встало ничего». Модуль,
    // которому ответ не нужен, его просто не читает.
    FeatureType reshape_feature(MacroHandle self, FeatureType want) {
        if (!ctx_.mw.features || !ctx_.mw.terrain || !ctx_.mw.store)
            return FT_None;
        const MacroStore& st = store_ctx(ctx_);
        if (!st.valid(self)) return FT_None;
        return feature_put(*ctx_.mw.features, *ctx_.mw.terrain,
                           st.cell[self.slot].idx, want);
    }
    MacroHandle spawn(AiSelf& s, SquadType bornType, int crewSouls) {
        if (crewSouls <= 0 || !ctx_.mw.gs || !ctx_.mw.terrain)
            return MacroHandle{};
        MacroStore& st = store_ctx(ctx_);
        if (count_human_souls(s.bag) < crewSouls) return MacroHandle{};
        const std::uint16_t slot = s.h.slot;
        SquadSpec spec{};
        spec.avatarType = NPCType::Peasant;
        spec.x = slot_x(st, slot, ctx_.mapW);
        spec.y = slot_y(st, slot, ctx_.mapW);
        spec.parentId = int(st.spawnId[slot].index);
        for (int m = 1; m < crewSouls; ++m) {
            // ГЕНЕРИК (CANON S4): члены — ОДИН стак, не пер-душевые
            // ординалы (`SquadSpecMembers::push` сливает равные записи);
            // поток имён остаётся лидерам и душам с историей.
            SoldierRecord rec{};
            rec.kind = std::uint16_t(spec.avatarType);
            rec.level = 1;
            if (!spec.members.push(rec)) break;
        }
        const MacroHandle born =
            spawn_squad(*ctx_.mw.gs, st, *ctx_.mw.terrain, spec);
        if (!st.valid(born)) return MacroHandle{};
        st.runtime[born.slot].squadType = std::uint8_t(bornType);
        bleed_flock(s.bag, 1 + spec.members.size());
        return born;
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
        if (!squad_recovering(s.rt)) {
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
            // АКТ МГНОВЕНЕН (ЗАКОН СПОСОБНОСТИ п.1): прежний счётчик «8 +
            // rand(8) думок ДО цикла» был castTime-манерой, убитой ещё у
            // спеллов, — первый цикл случится первой же свободной думкой,
            // а длительность работы повиснет ПОСЛЕ него базой СТРОКИ.
            ecs::goal_arrive(g, s.rt);  // → Working: цель несла «чем стать»
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
        if (!squad_recovering(s.rt)) {
            // Решение потерялось (битый тег цели) — честно домой: новое
            // решение примет Idle, работы из воздуха нет (fail-closed).
            if (!def) { go_home(); return; }
            ecs::Pools& pools = s.pools;
            // WORK COSTS SP through the one stamina law (CANON S14.1): цена
            // цикла — бар / темп СТРОКИ своего акта (пашня 32 в день, охота
            // 8 — зверь честно вчетверо дороже за единицу; рука берёт один
            // объект всегда, вердикт 2026-09-16). Та же строка ценит топор
            // субмира (M-369). Пустой бар — домой отдыхать, не работа в ноль.
            const ActionDef& act = action_def(int(def->action));
            const int cycleCost = sp_price(int(pools.maxSp),
                                           act.spRatePerDay);
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
                // АКТ СЛУЧИЛСЯ — ДОЛГ ПОВИС («сделал → вишу», ломоть
                // stateTimer M-369): база — `recoveryTicks` СТРОКИ акта
                // (жатва/рубка/жила — замах, поимка лошади — рабочий ДЕНЬ
                // 8192), эффективное — та же дверь `recovery_steps`, что у
                // руки внизу. Темп циклов задаёт СТРОКА, а не авторская
                // пауза модуля.
                charge_squad_recovery(
                    s.rt, std::uint32_t(recovery_steps(
                              act.recoveryTicks, s.sheet.attributes,
                              s.sheet.skills, action_tempo_generic(act))));
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
                // Стоит у места и способен: следующий цикл запейсит долг
                // акта выше — своей строкой, не второй паузой.
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
            charge_squad_recovery(s.rt,
                std::uint32_t((6 + pult.rand(12)) * kAiTicks));
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
    // РЕФЛЕКСЫ — ВЫБОР МОДУЛЯ, А НЕ ПЕРЕХВАТ ДИСПЕТЧА (первая половина
    // M-307): преамбула приехала сюда дословно и тем же порядком, каким
    // стояла в `dispatch`. Повтор у четырёх модулей — НОРМА (DOD п.5).
    // СТОИТ ПОСЛЕ ГЕЙТА МИРА, И ЭТО НЕ СМЕНА ПОРЯДКА: все три рефлекса
    // сами начинаются с `!ctx.mw.store || !ctx.mw.gs` и на пустом мире были
    // пустышками, а `store_ctx` ниже разыменовывает store без проверки —
    // то есть «рефлексы раньше гейта» было бы ровно UB вместо тех же нулей.
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    // Каждый думающий сквад следит — писатель полей следов один (CANON S10).
    scent_squad_deposit(e, p, kind, ctx);
    if (squad_threat_step(e, p, kind, rt, pools, ctx)) return;
    // Визуального врага нет — может, есть запах: охота съедает think, роль
    // ждёт. Прежде здесь стояло «макроцель в rt не тронута — пауза, не
    // амнезия»: это было ВЕРНО и перестало быть, когда охота научилась
    // писать своё «куда» в колонку цели (хвост ломтя 3). Рефлекс запаха
    // теперь ведёт себя как визуальный — забирает цель на время погони и
    // оставляет роль решать заново, — и двух законов у реакции больше нет.
    if (scent_hunt_step(e, p, kind, rt, pools, ctx)) return;
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
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
// beside the legal one, so the hold neither grew with the avatar's back nor
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
// beyond water with no bridge) reads as: position pinned while the march
// itself said REFUSED and the bar is fresh. The rider gives the run up
// instead of pacing the surf forever. (The whole-budget check died with the
// float bank: refusal is the march's own flag now.)
bool march_is_stuck_(const MacroPos& p, float oldX, float oldY,
                     const ecs::MacroNpcRuntime& rt,
                     const ecs::Pools& pools) {
    return p.x == oldX && p.y == oldY
           && (rt.marchFlags & ecs::kMarchRefused) != 0
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
    const std::size_t R = nv ? nv->regionRootedId.size() : 0;
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
        for_each_rooted(stp, [&](std::uint16_t slot) {
            const int id = int(stp.spawnId[slot].index);
            if (id == currentId || id == prevId) return;
            // ВЫВОД ДЕЛАЕТ МОДУЛЬ ХОДОКА, ЧИТАЯ ЧУЖОЕ ЧИСЛО ПОЛЯ: «подо мной
            // фича города, сверху стоит сквад города — значит это его
            // популяция, и мне туда есть смысл идти». `souls_flock` снесена
            // (M-320): она объявляла этот вывод МИРОВЫМ, а он принадлежит
            // модулю читающего.
            if (!rooted_is_settlement(SquadType(stp.runtime[slot].squadType))
                || feature_number_read(*ctx.mw.gs,
                               ecs::cell_x(stp.cell[slot], ctx.mapW),
                               ecs::cell_y(stp.cell[slot], ctx.mapW)) <= 0)
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
        const int lmId = int(nv->regionRootedId[to]);
        if (lmId < 0 || lmId == currentId || lmId == prevId) continue;
        const std::uint16_t slot = rooted_slot_by_id(ctx, lmId);
        if (slot == kMacroNoSlot
            || !rooted_is_settlement(SquadType(stp.runtime[slot].squadType))
            || feature_number_read(*ctx.mw.gs,
                           ecs::cell_x(stp.cell[slot], ctx.mapW),
                           ecs::cell_y(stp.cell[slot], ctx.mapW)) <= 0)
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
//  `trade_power_of@src/bus/anketa.h`. Шапка той двери предупреждает ровно об
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
long long plan_home_load_(MacroStore& st, std::uint16_t homeSlot,
                          std::uint16_t traderSlot, const Skills& site,
                          std::int64_t capG) {
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
             budgetG, kNoCarryLimitG);
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
// ИЗ ЦЕЛИ — род `GoalKind::Rooted`, ординал рынка в `goal.what`, — а не из
// феодального ребра: деревня едет к сюзерену, горожане — в деревню домена,
// и это ОДИН рейс, а не два ИИ.
//
// ПОРУЧЕНИЕ (`rt.errandObject`) ЗДЕСЬ УМЕРЛО (ломоть 4 M-72), и это был
// ВТОРОЙ НОСИТЕЛЬ ОДНОЙ ВЕЛИЧИНЫ (DOD п.6): из трёх мест, спрашивавших «где
// мой рынок», ДВА уже читали цель, а третье — колонку, и единственный её
// писатель стоял строкой рядом с `goal_set`, дублируя тот же ординал. Теперь
// носитель один, и ответ у трёх читателей общий (`AiView::goal_rooted`).
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
        if (squad_recovering(rt)) return;
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
            const std::uint16_t m2 = view.goal_rooted(g);
            if (m2 != kMacroNoSlot && view.is_settlement(m2)) {
                ecs::goal_set(g, view.rooted_cell(m2), g.what, 0,
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
        const std::uint16_t market = view.goal_rooted(g);
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
            charge_squad_recovery(rt,
                std::uint32_t((40 + pult.rand(40)) * kAiTicks));
            return;
        }
        ecs::goal_set(g, view.rooted_cell(market),
                      ecs::goal_what(ecs::GoalKind::Rooted,
                                     view.rooted_id(market)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (view.at(s.pos, g)) {
            // АКТ МГНОВЕНЕН: сделка случится первой же свободной думкой,
            // её длительность повиснет ПОСЛЕ как долг (инверсия castTime).
            ecs::goal_arrive(g, rt);   // → Working
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
        if (squad_recovering(rt)) return;
        if (const std::uint16_t market = view.goal_rooted(g);
            market != kMacroNoSlot && view.is_settlement(market)) {
            // ЧТО ВЕЗТИ ДОМОЙ судит ВЕДОМОСТЬ ДОМА, а не память крю
            // (CANON S10, ярус 2): дом сам выписал свои цены точным
            // складом и своим счётом.
            const CaravanDeal leg = pult.trade_at(s, market, homeSlot);
            if (leg.movedTableValue > 0) {
                // СДЕЛКА СОСТОЯЛАСЬ — ДОЛГ ПОВИС («сделал → вишу»): тики
                // авторские, своей строки у сделки пока нет (придёт с
                // ИИ-модулями M-307). Пустой визит акта не делал — долга
                // нет, развилка рейса решается той же думкой.
                charge_squad_recovery(rt,
                    std::uint32_t((4 + pult.rand(4)) * kAiTicks));
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
                s.pos, int(view.rooted_id(market)), rt.prevStationId);
            float bidGo = 0.0f;
            const std::uint16_t next = view.rooted_slot(nextId);
            if (next != kMacroNoSlot && next != homeSlot) {
                const XY at = view.rooted_xy(next);
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
                rt.prevStationId = int(view.rooted_id(market));
                // Рынок следующего плеча НАЗЫВАЕТ ЦЕЛЬ, и больше ничто:
                // здесь стояла строка-дубль `rt.errandObject = …` с тем же
                // ординалом (ломоть 4 M-72).
                ecs::goal_set(g, view.rooted_cell(next),
                              ecs::goal_what(ecs::GoalKind::Rooted,
                                             view.rooted_id(next)),
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
            charge_squad_recovery(rt,
                std::uint32_t((10 + pult.rand(15)) * kAiTicks));
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
    // Преамбула рефлексов — та же и в том же порядке (M-307, см. ai_artel).
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    scent_squad_deposit(e, p, kind, ctx);
    if (squad_threat_step(e, p, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, p, kind, rt, pools, ctx)) return;
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
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
// ДОЛЖНИК — ИЗ ЦЕЛИ (род Rooted), как рынок у рейса сбыта: поручение
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
    auto& rt = s.rt;

    // ДОРОГА ДОМОЙ ДОЛЖНИКА НЕ СПРАШИВАЕТ, И ЭТА СТУПЕНЬ СТОИТ ВЫШЕ ГЕЙТА
    // НАМЕРЕННО. Взыскание заканчивается тем, что цель перенацеливается на
    // ДОМ, а `what` обнуляется — должник в ней больше не нужен. Но гейт
    // ниже читает должника ИЗ ЦЕЛИ, и пока эта ступень стояла после него,
    // гружёный сборщик на первой же думке обратного плеча получал
    // «должника нет» и уходил в `home_wander` — а ветка `Returning` с
    // `dump_bag_home` была НЕДОСТИЖИМА.
    //
    // ЧЕМ ЭТО БЫЛО В МИРЕ: долг ребра списывался, товар уезжал со склада
    // вассала — и НАВСЕГДА оставался в спине бродящего вокруг дома
    // сборщика. Дань не доезжала НИ РАЗУ; дом беднел, вассал платил,
    // материя зависала в кармане. Замер этого прибора: долг 200 → 0, у
    // вассала 40 → 0, дома 0, В СПИНЕ 40, сквад жив и бродит на (14,2).
    //
    // ОТКУДА ВЗЯЛОСЬ: ломоть 4 M-72 увёл должника из колонки
    // `rt.errandObject` в колонку ЦЕЛИ — верно по форме (второй носитель
    // одной величины умер), но цель несёт ещё и «куда иду», и обратное
    // плечо затирало ею же идентичность поручения. Свидетеля у рода
    // `Collector` не было ни одного, поэтому дефект приехал зелёным.
    //
    // ПОЧЕМУ ИМЕННО ТАК, А НЕ «СОХРАНИТЬ ДОЛЖНИКА В `what` НА ОБРАТНОМ
    // ПЛЕЧЕ»: `what` отвечает на вопрос «к чему я иду», и на обратном плече
    // честный ответ — ДОМ. Вписать туда должника значило бы заставить
    // колонку врать ради удобства гейта. Какое плечо идёт, знает `state`, и
    // спрашивать надо его.
    if (rt.state == std::uint8_t(NS::Returning)) {
        if (view.at(s.pos, g)) {
            // Вылив дани домой — ОДНА сделка (ломоть 2 M-72).
            pult.dump_bag_home(s, homeSlot);
            ecs::goal_arrive(g, rt);   // → Idle
            charge_squad_recovery(rt,
                std::uint32_t((10 + pult.rand(15)) * kAiTicks));
            return;
        }
        const XY t = view.goal_pos(g);
        pult.march(s.pos, rt, t.x, t.y);
        return;
    }

    // ДОЛЖНИКА СБОРЩИК НАХОДИТ САМ, И ЭТО ПОЧИНКА МЁРТВОГО ПИСАТЕЛЯ, А НЕ
    // НОВАЯ МЕХАНИКА (ломоть 5а M-72). Цель сборщика ставил АУКЦИОН, а он
    // казнён ломтём 4 — то есть у колонки цели этого рода не осталось
    // писателя ни одного, и всякий рождённый сборщик падал в `home_wander`
    // НАВСЕГДА: род был жив, канал дани пуст. Спрашивать «кто мне должен»
    // есть вопрос о СВОИХ рёбрах (`AiView::owing_vassal` — обход своих
    // `interests` по `Stance::Vassal`, не скан мира), поэтому решение
    // принадлежит МОДУЛЮ, а не внешнему распределителю.
    //
    // ФОРМА ЦЕЛИ — ТА ЖЕ, ЧТО У ДВУХ ВЕТОК Idle НИЖЕ (род Rooted, ординал
    // вассала, `onArrival = Working`): второго способа назвать должника не
    // заводится, иначе у одной величины станет два носителя (DOD п.6).
    std::uint16_t vassal = view.goal_rooted(g);
    if (vassal == kMacroNoSlot || vassal == homeSlot) {
        const std::uint16_t owing = view.owing_vassal(homeSlot);
        if (owing == kMacroNoSlot || owing == homeSlot) {
            // Дом никому не сюзерен (или все рассчитались) — ждать нечего.
            pult.home_wander(s);
            return;
        }
        ecs::goal_set(g, view.rooted_cell(owing),
                      ecs::goal_what(ecs::GoalKind::Rooted,
                                     view.rooted_id(owing)),
                      0, std::uint8_t(NS::Working));
        vassal = owing;
    }

    if (rt.state == std::uint8_t(NS::Idle)) {
        if (squad_recovering(rt)) return;
        // Дома и с пустыми руками — идём за долгом; дома с грузом — сдаём.
        if (view.home_dist_sq(s.pos, home) >= 4.0f) {
            ecs::goal_set(g, view.rooted_cell(vassal),
                          ecs::goal_what(ecs::GoalKind::Rooted,
                                         view.rooted_id(vassal)),
                          0, std::uint8_t(NS::Working));
            rt.state = std::uint8_t(NS::Traveling);
            return;
        }
        if (!view.owes_tithe(vassal)) {
            // Должник рассчитался (собрали или простили) — ждать нечего,
            // ротация завтра переторгует эту строку заново.
            charge_squad_recovery(rt,
                std::uint32_t((8 + pult.rand(8)) * kAiTicks));
            return;
        }
        ecs::goal_set(g, view.rooted_cell(vassal),
                      ecs::goal_what(ecs::GoalKind::Rooted,
                                     view.rooted_id(vassal)),
                      0, std::uint8_t(NS::Working));
        rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (view.at(s.pos, g)) {
            // АКТ МГНОВЕНЕН: взыскание случится первой же свободной думкой,
            // его длительность повиснет ПОСЛЕ как долг (инверсия castTime).
            ecs::goal_arrive(g, rt);   // → Working
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
        if (squad_recovering(rt)) return;
        pult.collect_tithe(s, vassal, homeSlot);
        // ВЗЫСКАНИЕ СЛУЧИЛОСЬ — ДОЛГ ПОВИС («сделал → вишу»): тики
        // авторские, своей строки у взыскания пока нет (придёт с M-307).
        charge_squad_recovery(rt,
            std::uint32_t((2 + pult.rand(3)) * kAiTicks));
        ecs::goal_set(g, cell_of(int(home.x), int(home.y), view.map_w()),
                      0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    // Блуждание с живым должником: та же пара «дошёл — вылил», что у плеча
    // домой выше. `Returning` здесь больше не упоминается — он разобран ДО
    // гейта должника, иначе до него не доходит ход.
    if (rt.state == std::uint8_t(NS::Wandering)) {
        if (view.at(s.pos, g)) {
            pult.dump_bag_home(s, homeSlot);
            ecs::goal_arrive(g, rt);   // → Idle
            charge_squad_recovery(rt,
                std::uint32_t((10 + pult.rand(15)) * kAiTicks));
            return;
        }
        const XY t = view.goal_pos(g);
        pult.march(s.pos, rt, t.x, t.y);
        return;
    }
    rt.state = std::uint8_t(NS::Idle);
    charge_squad_recovery(rt, std::uint32_t(4 * kAiTicks));
}

// Переходник пятёрки — тот же, что у образца и у рейса сбыта.
void ai_collector(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) {
        ai_nomad(e, p, rt, pools, ctx);
        return;
    }
    // Преамбула рефлексов — та же и в том же порядке (M-307, см. ai_artel).
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    scent_squad_deposit(e, p, kind, ctx);
    if (squad_threat_step(e, p, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, p, kind, rt, pools, ctx)) return;
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    collector_think(self, view, pult);
}

// ═════ МОДУЛИ Village / City / Capital — ТЕ ЖЕ ОБЫЧНЫЕ МОДУЛИ СКВАДОВ ════
// Вердикт владельца 2026-10-24, дословно: «нет это снова фигня какаято - у
// нас место это же АРХЕТИП сквадов и всё а система сквадов едины у них ест
// ьвсе системы - архетип эоо как ТЕБЯ видят другие а не какой ты (город
// деревню и столицу видят как места наприме корвоаны - что за бред». Поэтому
// ниже — тот же `AiSelf` + const-`AiView` + `AiPult`, что у трёх образцов
// выше, та же ось ступеней `NPCState` и тот же гейт занятости
// `recoverySteps`. Архетип в этих телах не упомянут ни разу, и законов
// «места» среди них нет ни одного.
//
// ЧЕГО В НИХ НЕТ — И ЭТО ИХ ВНУТРЕННЕЕ ДЕЛО, А НЕ СВОЙСТВО РОДА: ни `march`,
// ни преамбулы рефлексов. Ногами эти два модуля сегодня не пользуются;
// рефлексы (след, порог угрозы, охота по запаху) стоят в головах ЧЕТЫРЁХ
// ходоков и сюда не скопированы. Прежде они стояли в `dispatch`, и ровно
// поэтому строки Village/City/Capital нельзя было заполнить: ветка бегства
// `squad_threat_step` предиката бойца не имеет вовсе, у места
// `traits.count == 0` ⇒ `bravery_of` = 1.5, — то есть город, увидевший в
// шести клетках что-то в полтора раза сильнее, ПОСТАВИЛ БЫ ЦЕЛЬ В ВОСЬМИ
// КЛЕТКАХ И УШЁЛ, оставив свою фичу позади. Страж, не пускавший рефлексы к
// неподвижному сквадy, был САМ `nullptr` в строке диспетча — заполнение
// строк его снимало, поэтому преамбула уехала в головы ходоков ПЕРВОЙ
// (первая половина M-307, ломоть 5а M-72).
//
// РАЗМЕР КРЮ — ОДНА СМЕНА РУК СВОЕГО ЖЕ ВЕРСТАКА. Число НЕ новое:
// `kCreaturesPerCityWorker@src/macro/econ_day.h` — квота станка, одна рука на
// столько душ, — и взято оно затем, что говорит о мире ровно нужное: подняв
// крю, место платит за него РОВНО ОДНИМ своим работником, а крю приносит в
// поле `production_hands` от восьми душ, то есть девять рук. Второй константы
// «сколько душ в артели» рядом не заводится (ЗАКОН КОНСТАНТ).
//
// И ЭТО БАЛАНС, А НЕ АВТОРСКОЕ РЕШЕНИЕ (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ): прежний
// аукцион ротации выводил размер иначе — `пул / число живых строк`, где пул
// был `паства >> labourShift` (деревня 1/2, город 1/8), — и крю его жили
// РОВНО ОДИН день, распускаясь на рассвете. Здесь крю живёт, пока жив, значит
// калибровка у числа другая по построению; двигать его свободно.
inline constexpr int kCrewSouls = kCreaturesPerCityWorker;

// ДУШИ СВОБОДНЫ ТОЛЬКО ТЕ, БЕЗ КОТОРЫХ МЕСТО ОСТАЁТСЯ СТОЯТЬ, — отсюда
// строгое «больше», а не «не меньше». Это не потолок численности (ЗАКОН
// КЛАМПА): предел рождения даёт сама плата душами (`bleed_flock` в `spawn`),
// а здесь сказано лишь то, что последнюю душу из дома не выписывают — иначе
// место с ровно восемью головами ушло бы в крю ЦЕЛИКОМ и встало бы с нулём
// рук у своих же верстаков, то есть перестало бы производить тем самым
// решением, которым собиралось производить больше.
bool has_spare_souls_(const AiSelf& s) {
    return count_human_souls(s.bag) > kCrewSouls;
}

// ДНЕВНОЙ ГЕЙТ — ОДИН НА ОБА МОДУЛЯ ПО ФОРМЕ, НО СВОЙ У КАЖДОГО ПО ТЕЛУ
// (DOD п.5: похожий код в двух модулях есть НОРМА). Период назначает не
// модуль, а глагол (`AiPult::craft` заряжает гейт занятости сам — ЗАКОН
// СПОСОБНОСТИ п.4), поэтому здесь только вопрос «занят ли»: слив кванта
// живёт в свипе, и ход, на котором гейт дотаял до нуля, И ЕСТЬ новый день.
// Решения рождения стоят за этим же гейтом нарочно: `AiView::has_child` —
// холодный проход капом, и его шапка требует дневного каденса прямым
// текстом.

void village_think(AiSelf& s, const AiView& view, AiPult& pult) {
    if (squad_recovering(s.rt)) return;
    pult.craft(s);

    // АРТЕЛЬ — ПРИ СВОБОДНЫХ ДУШАХ И ЖИВОЙ НУЖДЕ. Нужда спрашивается ТОЙ ЖЕ
    // дверью, которой артель выбирает, ЧТО добыть (`season_demand_for` под
    // `AiView::need_lack`), и вопросы у них разные, а не два ответа на один:
    // деревня спрашивает «надо ли кого-то поднимать вообще», артель —
    // «куда идти сегодня». Своего списка добычи деревня не держит: строки
    // читаются из того же каталога `kGathererDefs`, по которому ходит артель.
    if (!has_spare_souls_(s)) return;
    bool lacksSomething = false;
    for (int r = 0; r < kGathererGoalCount && !lacksSomething; ++r) {
        const int out = gatherer_item_index(r);
        if (out >= 0 && view.need_lack(s.h.slot, out) > 0)
            lacksSomething = true;
    }
    if (!lacksSomething) return;
    // ГЕЙТ ДУБЛЯ — ПОСЛЕДНИМ, И ЭТО ПОРЯДОК ЦЕНЫ, А НЕ ВКУСА: проход капом
    // платится только за решение, дошедшее до рождения.
    if (view.has_child(s.h, SquadType::Artel)) return;
    pult.spawn(s, SquadType::Artel, kCrewSouls);
}

void city_think(AiSelf& s, const AiView& view, AiPult& pult) {
    if (squad_recovering(s.rt)) return;
    pult.craft(s);

    // СБОРЩИК — ТОЛЬКО ПРИ ВАССАЛЕ-ДОЛЖНИКЕ, И ВОПРОС ЭТОТ ЗАДАЁТСЯ СВОИМИ
    // РЁБРАМИ, А НЕ ПОИСКОМ ПО МИРУ (`AiView::owing_vassal` — обход своих
    // `interests` по `Stance::Vassal`). Должника нет — сборщику некого доить,
    // и рождать его значило бы послать сквад в `home_wander` навсегда: ровно
    // так он и жил, пока его цель ставил казнённый аукцион.
    if (has_spare_souls_(s)
        && view.owing_vassal(s.h.slot) != kMacroNoSlot
        && !view.has_child(s.h, SquadType::Collector)) {
        pult.spawn(s, SquadType::Collector, kCrewSouls);
    }
    // КОРОВАН — БЕЗ ГЕЙТА НУЖДЫ, И ЭТО НЕ ПРОПУСК: рейс сбыта решает, что
    // везти и что брать, НА МЕСТЕ и своей же анкетой (`caravan_think`:
    // погрузка по полке дома, сделка по цене рынка). Спрашивать нужду здесь
    // значило бы решить за него дважды — вторым носителем одного решения.
    // Свободные души читаются ЗАНОВО: сборщик выше мог только что увести
    // свои восемь, и вчерашний ответ о них уже неверен.
    if (has_spare_souls_(s) && !view.has_child(s.h, SquadType::Caravan))
        pult.spawn(s, SquadType::Caravan, kCrewSouls);
}

// Переходники пятёрки — те же, что у трёх образцов, МИНУС преамбула
// рефлексов (причина названа в шапке модулей выше).
void ai_village(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
                ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    village_think(self, view, pult);
}

void ai_city(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
             ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    AiSelf self{e, p, rt, pools, store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    city_think(self, view, pult);
}

void ai_trader(MacroHandle self, MacroPos& p, ecs::MacroNpcRuntime& rt,
               [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    XY home;
    if (!home_pos(self, ctx, home)) return;
    const MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    const int myHomeId = parent_of(st, self.slot);

    if (rt.state == std::uint8_t(NS::Idle)) {
        if (!squad_recovering(rt)) {
            // Pick another city (id != home).
            int candidates = 0;
            for_each_rooted(st, [&](std::uint16_t slot) {
                if (is_other_city(st, slot, myHomeId)) ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                // Обход без досрочного выхода: взявший урну гасит флаг, и
                // остаток прохода молчит (у двери мест нет break — её
                // порядок есть закон приоритета клетки).
                bool taken = false;
                for_each_rooted(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(st, slot, myHomeId))
                        return;
                    if (pick-- != 0) return;
                    ecs::goal_set(g, st.cell[slot].idx,
                                  ecs::goal_what(ecs::GoalKind::Rooted,
                                                 st.spawnId[slot].index),
                                  0, std::uint8_t(NS::Working));
                    rt.state  = std::uint8_t(NS::Traveling);
                    taken = true;
                });
            } else {
                charge_squad_recovery(rt, std::uint32_t(20 * kAiTicks));
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Working
            charge_squad_recovery(rt,
                std::uint32_t((15 + rand_int(ctx, 20)) * kAiTicks));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        if (!squad_recovering(rt)) {
            ecs::goal_set(g, cell_of(int(home.x), int(home.y), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Returning)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            charge_squad_recovery(rt,
                std::uint32_t((20 + rand_int(ctx, 30)) * kAiTicks));
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
    // «Не тот город, где был» — прежний ординал несёт сама цель (род Rooted).
    const int lastRooted = ecs::goal_kind(g.what) == ecs::GoalKind::Rooted
                              ? int(ecs::goal_ordinal(g.what))
                              : 0;
    if (rt.state == std::uint8_t(NS::Idle)) {
        if (!squad_recovering(rt)) {
            int candidates = 0;
            for_each_rooted(st, [&](std::uint16_t slot) {
                if (is_other_city(st, slot, lastRooted))
                    ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                bool taken = false;
                for_each_rooted(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(st, slot, lastRooted))
                        return;
                    if (pick-- != 0) return;
                    ecs::goal_set(g, st.cell[slot].idx,
                                  ecs::goal_what(ecs::GoalKind::Rooted,
                                                 st.spawnId[slot].index),
                                  0, std::uint8_t(NS::Idle));
                    rt.state  = std::uint8_t(NS::Traveling);
                    taken = true;
                });
            } else {
                charge_squad_recovery(rt, std::uint32_t(10 * kAiTicks));
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Traveling)) {
        if (at_goal(p, g, ctx)) {
            ecs::goal_arrive(g, rt);   // → Idle
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 15)) * kAiTicks));
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
        charge_squad_recovery(rt,
            std::uint32_t((5 + rand_int(ctx, 10)) * kAiTicks));
        return;
    }
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    if (rt.state == std::uint8_t(NS::Idle)) {
        if (!squad_recovering(rt)) {
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
            charge_squad_recovery(rt,
                std::uint32_t((8 + rand_int(ctx, 15)) * kAiTicks));
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
                    charge_squad_recovery(rt,
                        std::uint32_t((3 + rand_int(ctx, 5)) * kAiTicks));
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
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        if (!squad_recovering(rt)) {
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
            charge_squad_recovery(rt,
                std::uint32_t((8 + rand_int(ctx, 15)) * kAiTicks));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
    }
}

// ── МОДУЛЬ РОДА Dragon — вылеты из логова (ломоть 6 M-72) ────────────────
// Дом — КЛЕТКА (rt.lairCell, гора — не ландмарк). В вылете бьёт любой сквад
// СЛАБЕЕ себя (слово владельца) — той же дверью встречи, что threat step;
// наевшись или улетев за поводок — домой. Игрокова встреча — форс-двери
// игрока, как у всех. До 2026-11-02 это была модель AIBehaviour::LairSorties
// под родом None (лестница стола анкет) — второй ответ на «как он действует»
// рядом с осью рода; теперь думку раздаёт ОДНА таблица kAiModuleRows.
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

// Поводок — ВНУТРИ модуля (вердикт владельца 2026-10-10, дословно
// «5) внутри модуля»; список НЕ-ЕДИНОГО называет его прямо): не колонка
// сквада и не агенда строки — у агенды радиуса был второй носитель того же
// числа. Значение — БАЛАНС: прежняя авторская агенда драконов стояла 10.
constexpr float kDragonLeashCells = 10.0f;

void ai_dragon(MacroHandle self, MacroPos& p,
               ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    // Преамбула рефлексов — та же и в том же порядке, что у четырёх
    // думающих модулей (M-307, см. ai_artel). Повтор — НОРМА (DOD п.5).
    const ecs::NPCKind& kind = store_ctx(ctx).kind[self.slot];
    scent_squad_deposit(self, p, kind, ctx);
    if (squad_threat_step(self, p, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(self, p, kind, rt, pools, ctx)) return;
    // Логово самозалечивается: тело без дома объявляет домом место, где
    // проснулось (спавн-дверь анкеты пишет честную клетку раньше).
    if (rt.lairCell == ecs::kNoCell) {
        rt.lairCell = cell_of(int(p.x), int(p.y), ctx.mapW);
    }
    const float lairX = float(cell_x(rt.lairCell, ctx.mapW));
    const float lairY = float(cell_y(rt.lairCell, ctx.mapW));
    const float radius = kDragonLeashCells;
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
                    charge_squad_recovery(rt,
                        std::uint32_t((5 + rand_int(ctx, 8)) * kAiTicks));
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
        if (!squad_recovering(rt)) {
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
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 20)) * kAiTicks));
            return;
        }
        const XY t = goal_xy(g, ctx);
        try_move(p, rt, t.x, t.y, ctx);
        return;
    }
    rt.state = std::uint8_t(NS::Idle);
    charge_squad_recovery(rt,
        std::uint32_t((3 + rand_int(ctx, 5)) * kAiTicks));
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
    // ПРЫЖОК — АКТ, и занятость ему ставит ТОТ ЖЕ гейт, что всем актам
    // сквада. Здесь жил `teleportCooldown` — второй счётчик того же
    // субъекта со своим ручным сливом, буквально «своя таблица
    // восстановления у отдельного рода действий» (ЗАКОН СПОСОБНОСТИ п.6);
    // прежняя пара «кулдаун 50 думок + пауза Idle 10» слилась в один заряд.
    // ПРЫЖОК ИЗ АРЕНДЫ ОТЛАГАЕТСЯ (ЗАХВАТ, ломоть 2 M-419): в окне позицию
    // ведёт субмир — прыжок макро был бы вторым писателем (тело осталось бы
    // в сцене, запись уехала). Прыжок В окно законен: вошедший захватывается
    // следующим слепком. Форма гейта — та же, что у судов автобоя
    // (`ctx.mw.gs && cell_is_leased(…)`); он стоит ПЕРВЫМ, чтобы отложенный
    // прыжок не сжигал ролл.
    const bool jumpLeased = ctx.mw.gs
        && cell_is_leased(*ctx.mw.gs,
                          cell_of(int(p.x), int(p.y), ctx.mapW));
    if (!jumpLeased && !squad_recovering(rt) && rand_f01(ctx) < 0.005f) {
        XY t = pick_random_nearby(p.x, p.y, 40, ctx);
        p.x = t.x; p.y = t.y;
        // A jump has no entry edge — the sentinel degrades placement to the
        // whole-cell scatter instead of inventing a side.
        rt.entryDir = kEntryDirNone;
        rt.entryTicks = 0;
        charge_squad_recovery(rt, std::uint32_t(50 * kAiTicks));
        rt.state = std::uint8_t(NS::Idle);
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        if (!squad_recovering(rt)) {
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
            charge_squad_recovery(rt,
                std::uint32_t((12 + rand_int(ctx, 20)) * kAiTicks));
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
        if (!squad_recovering(rt)) {
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
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 15)) * kAiTicks));
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
// Phase 6: the ROLE layer arrived — the avatar's Scouting rank widens the
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
            charge_squad_recovery(rt,
                std::uint32_t((3 + rand_int(ctx, 5)) * kAiTicks));
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
    // КУДА Я ИДУ — ЭТО КОЛОНКА ЦЕЛИ, И У ОХОТЫ ТОЖЕ (хвост ломтя 3 M-72).
    // Здесь стоял голый `try_move`: сквад ШЁЛ, а «куда» жило только в
    // аргументе функции и умирало с кадром стека. Это второй, безымянный
    // носитель того же вопроса — ровно то, что ломоть 3 запретил, сведя
    // «куда» в одну колонку рода 2. Цена была не теоретической: рефлекс
    // запаха невидим пакету и сейву — сквад, поднятый из файла посреди
    // погони, забывал, за чем шёл, и решал заново.
    //
    // РЯДОМ ПОРОГ УГРОЗЫ ПИСАЛ ЕЁ ВСЕГДА (и бегство, и погоня), значит на
    // один вопрос «куда ведёт рефлекс» в одном проходе стояло ДВА
    // правописания — у соседних ступеней одной думки.
    //
    // `what` = 0 ЧЕСТНО: охота идёт на ГРАДИЕНТ поля, а не к названной
    // сущности — цели как предмета у неё нет, есть клетка. Этим она
    // отличается от погони выше, которая кладёт в `what` ординал врага.
    //
    // СЛЕДСТВИЕ, НАЗВАННОЕ ВСЛУХ: запись затирает рабочую цель сквада, как
    // её уже затирало бегство, — артель после погони РЕШАЕТ ЗАНОВО, и
    // остаток циклов `amount` обнуляется. Это та же цена, что платит
    // визуальный рефлекс; второй набор правил для запаха не заводится.
    ecs::goal_set(store_ctx(ctx).goal[self.slot],
                  cell_of(bx, by, ctx.mapW), 0, 0,
                  std::uint8_t(NS::Idle));
    rt.state = std::uint8_t(NS::Chasing);
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
        charge_squad_recovery(rt,
            std::uint32_t((2 + rand_int(ctx, 4)) * kAiTicks));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle) && squad_recovering(rt)) return;
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
    // (`|| !ctx.mw.gs` снят вместе с параметром `GameState&` у двери ниже:
    //  предусловие переживало свою причину — ни одна строка этого тела мира
    //  больше не спрашивает. Условие осталось ровно одно и настоящее: без
    //  стора `store_ctx` невыразим.)
    if (!ctx.mw.store) return;
    MacroStore& st = store_ctx(ctx);
    settle_avatar_fraction(st, e, 0.0f);
    kill_fallen_squad_creatures(st);
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
    // Преамбула рефлексов — та же и в том же порядке (M-307, см. ai_artel).
    // Гейта мира у этого модуля нет и не было: его зовёт тот же свип, что
    // уже проверил store и gs на входе.
    scent_squad_deposit(e, p, kind, ctx);
    if (squad_threat_step(e, p, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, p, kind, rt, pools, ctx)) return;
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
        // (`LairSorties` УМЕР 2026-11-02, ломоть 6 M-72: вылеты из логова —
        //  МОДУЛЬ рода Dragon (ai_dragon), лестница None их не раздаёт.)
        case AIBehaviour::Count:        break;
    }
}

// РОД → МОДУЛЬ: диспетч есть ТАБЛИЦА по ординалу SquadType, не ветвление
// (ломоть 4 M-72; образец формы — kGenKindRows@src/sub/gens/dispatch.cpp).
// nullptr = «у рода думки нет»: его день идёт своим проходом
// (settle_rooted_day) — это ДАННЫЕ строки, а не гейт по архетипу (шапка
// squad_type.h: архетип законен КЛЮЧОМ таблиц и ЗАПРЕЩЁН ГЕЙТОМ в проходе
// симуляции — прежний гейт M-90 шага 5 умер в эту колонку).
using AiModuleFn = void (*)(MacroHandle, MacroPos&, ecs::MacroNpcRuntime&,
                            ecs::Pools&, const TickContext&);

// ── ПОСЛЕДНЕЕ СЛОВО УМИРАЮЩЕГО — ВТОРАЯ КОЛОНКА ТОЙ ЖЕ СТРОКИ ───────────
// Вердикт владельца 2026-10-25: «это не какотой общий закон а чисто внутри
// каждого модуля своё». Значит это НЕ закон свипа и НЕ ветка по роду, а
// ДАННЫЕ строки: `nullptr` = «этот род на своей смерти не пишет ничего», и
// читать из него «у места нет X» запрещено ровно так же, как у колонки
// думки (ЗАКОН ИМЕНИ).
//
// ПОДПИСЬ УЖЕ, ЧЕМ У ДУМКИ, И ЭТО НЕ ЭКОНОМИЯ, А ПРАВДА О МОМЕНТЕ: у
// умирающего нет ни хода, ни кванта думки, ни позы — у него есть СЕБЯ и
// ПУЛЬТ. Передать ему `MacroPos&` значило бы обещать марш трупу.
//
// ЗАЧЕМ ВООБЩЕ: замер 128 дней (сид 7) нашёл, что из обхода мест ушло 35
// строк, и ФИЧА ОСТАЛАСЬ ЖИВА 35 раз из 35 — то есть мир оставлял за собой
// фичу деревни, на которой никто не живёт. Фича есть РЕСУРС клетки (ЗАКОН
// ПОЛЯ п.3), и соврать ею дороже, чем потерять строку.
using AiDeathFn = void (*)(MacroHandle, AiPult&);

struct AiModuleRow {
    SquadType type;      // своя ось колонкой — под страж порядка строк
    AiModuleFn module;   // nullptr = род не думает
    AiDeathFn death;     // nullptr = род на своей смерти не пишет ничего
};
// ГОРОД И СТОЛИЦА ОСТАВЛЯЮТ ПО СЕБЕ РУИНУ (вердикт владельца: «например
// город переписывает фичу под собой в руины»). Руина — ЖИВАЯ строка оси и
// фичи, то есть мир честно говорит «здесь ЖИЛИ», а не «здесь город».
void city_death(MacroHandle self, AiPult& pult) {
    pult.reshape_feature(self, FT_Ruin);
}

// ДЕРЕВНЯ СНИМАЕТ ФИЧУ ВОВСЕ — ПОЛЕ ЗАРОСЛО (вердикт владельца 2026-10-25,
// строка смерти деревни). `FT_None` здесь ЗНАЧЕНИЕ, а не отказ: у снятия
// вода не спрашивается вовсе (`feature_put` судит воду только у постановки),
// поэтому деревня исчезает с карты фич на любой земле одинаково. Руины она
// по себе не оставляет, и это не копия городской строки, а ДРУГОЙ ответ:
// деревня есть пашня с домами, и без рук её затягивает, а не рушит.
void village_death(MacroHandle self, AiPult& pult) {
    pult.reshape_feature(self, FT_None);
}

constexpr AiModuleRow kAiModuleRows[] = {
    { SquadType::None,      ai_untyped,   nullptr       },
    // СТОЛИЦЕ ДАНА ФУНКЦИЯ ГОРОДА — ОДНА ФУНКЦИЯ ДВУМ СТРОКАМ ЗАКОННА,
    // ПОТОМУ ЧТО ЭТО ДАННЫЕ: различие столицы ещё не названо системой
    // (её строка `kSquadKinds` тоже носит городские числа), и выдумывать ей
    // второе поведение раньше вердикта владельца значило бы назначить
    // различие кодом (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ). Придёт различие —
    // поменяется ЭТА СТРОКА, и больше ничего.
    { SquadType::Capital,   ai_city,      city_death    },
    { SquadType::City,      ai_city,      city_death    },
    { SquadType::Village,   ai_village,   village_death },
    { SquadType::Spire,     nullptr,      nullptr       },
    { SquadType::Ruin,      nullptr,      nullptr       },
    { SquadType::Lair,      nullptr,      nullptr       },
    { SquadType::Shrine,    nullptr,      nullptr       },
    { SquadType::Mine,      nullptr,      nullptr       },
    { SquadType::Tower,     nullptr,      nullptr       },
    { SquadType::Artel,     ai_artel,     nullptr       },
    { SquadType::Caravan,   ai_vendor,    nullptr       },
    { SquadType::Collector, ai_collector, nullptr       },
    // Дракон — ПРИБОР ЭМЕРДЖЕНТНОСТИ эпика (владелец: «будет ли он есть
    // крестьян и тд эмерджентно»): модуль не знает ни деревень, ни артелей —
    // добычу называет сила (nearest_weaker_squad), смерть — авто-бой.
    { SquadType::Dragon,    ai_dragon,    nullptr       },
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
//
// И ДИСПЕТЧ ЗОВЁТ МОДУЛЬ ПО КОЛОНКЕ, И БОЛЬШЕ НИЧЕГО (первая половина M-307,
// ломоть 5а M-72). Здесь стояла ПРЕАМБУЛА РЕФЛЕКСОВ — след, угроза, охота, —
// и она была второй системой РЯДОМ с модулем: диспетч решал за всякий род
// сразу, что тот сперва понюхает и испугается, а модуль получал уже
// израсходованную думку. Тексты рефлексов не изменились ни на букву —
// изменилось, ЧЬИ они: преамбула уехала дословно и тем же порядком в головы
// четырёх думающих модулей (`ai_untyped`, `ai_artel`, `ai_vendor`,
// `ai_collector`), и повтор между ними есть НОРМА (DOD п.5), а не долг.
//
// ПОЧЕМУ ЭТО БЫЛО НЕОБХОДИМО СДЕЛАТЬ ПЕРВЫМ, ЧИСЛОМ И АДРЕСОМ: страж, не
// пускавший рефлексы к неподвижному сквадy, был САМ `nullptr` в строке
// диспетча, и заполнение строк Village/City/Capital его снимало. Ветка
// БЕГСТВА `squad_threat_step` предиката бойца не имеет вовсе, у места
// `traits.count == 0` ⇒ `bravery_of` = 1.5, а `moveMult` по умолчанию
// 1.0f (`src/ecs/components.h`) и гейта неподвижности у `try_move` нет, —
// то есть город, увидевший в 6 клетках что-то в 1.5× сильнее, ПОСТАВИЛ БЫ
// ЦЕЛЬ В 8 КЛЕТКАХ И УШЁЛ, оставив свою фичу позади. Две другие ветки
// (погоня, охота по запаху) гаснут на `combatant_behaviour` сами.
//
// `kind` ИЗ ПОДПИСИ УШЁЛ ВМЕСТЕ С ПРЕАМБУЛОЙ: каталог тел спрашивали ровно
// рефлексы, а ось, по которой диспетч выбирает модуль, — `rt.squadType`.
void dispatch(MacroHandle e, MacroPos& p, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    const std::size_t row = std::size_t(rt.squadType);
    const AiModuleFn module =
        row < std::size(kAiModuleRows) ? kAiModuleRows[row].module : nullptr;
    // БЕЗ МОДУЛЯ ТЕЛО НЕ ДУМАЕТ ВООБЩЕ — его день идёт своим проходом; это
    // по-прежнему правда, и правдой осталась ЦЕЛИКОМ: строка `nullptr` есть
    // честное «род не думает».
    if (module == nullptr) return;
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
        // Цена границы — ТА ЖЕ композиция, что цена списания ниже
        // (`lot_unit_price@src/macro/economy.h`, M-415/Б3): полка после
        // сделки + маржа одной строкой, разрыв наценки невыразим.
        const int unit =
            lot_unit_price(base, have, demand, mid, selling, myPct, theirPct);
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
                                     int myTradePct, int theirTradePct) {
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
            // 2026-08-30). Композиция — одной дверью lot_unit_price (Б3).
            const int price = lot_unit_price(base, have, demand, n,
                                             /*selling=*/true,
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
            if (!deal_covered(sv.value, n * price)) continue;
            const DealFact f =
                deal(st, traderSlot, marketSlot, goods,
                     std::span<DealLine>(pay, std::size_t(sv.lines)),
                     kNoCarryLimitG, kNoCarryLimitG);
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
            const int cost = n * lot_unit_price(base, have, demand, n,
                                                /*selling=*/false,
                                                myTradePct, theirTradePct);
            DealLine goods[1];
            goods[0] = DealLine{};
            goods[0].want = n;
            goods[0].def = std::uint16_t(id);
            goods[0].exact = 1;
            DealLine pay[64];
            const StagedValue sv = stage_value_lines(hold, cost, pay);
            if (!deal_covered(sv.value, cost)) continue;
            const DealFact f =
                deal(st, traderSlot, marketSlot,
                     std::span<DealLine>(pay, std::size_t(sv.lines)), goods,
                     kNoCarryLimitG, freeG);
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
                                   int myTradePct, int theirTradePct) {
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
        // is the whole difference. Композиция — одной дверью (Б3).
        const int price = lot_unit_price(base, have, demand, n,
                                         /*selling=*/true,
                                         myTradePct, theirTradePct);
        // ТОВАР И ОПЛАТА — ОДНА СДЕЛКА: недоплаченная поставка невыразима.
        DealLine goods[1];
        goods[0] = DealLine{};
        goods[0].want = n;
        goods[0].def = std::uint16_t(id);
        goods[0].exact = 1;
        DealLine pay[64];
        const StagedValue sv = stage_value_lines(ms, n * price, pay);
        if (!deal_covered(sv.value, n * price)) continue;
        const DealFact f =
            deal(st, traderSlot, marketSlot, goods,
                 std::span<DealLine>(pay, std::size_t(sv.lines)),
                 kNoCarryLimitG, kNoCarryLimitG);
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
            const int buyHere =
                lot_unit_price(base, have, demand, /*n=*/0,
                               /*selling=*/false, myTradePct, theirTradePct);
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
            const int cost = n * lot_unit_price(base, have, demand, n,
                                                /*selling=*/false,
                                                myTradePct, theirTradePct);
            DealLine goods[1];
            goods[0] = DealLine{};
            goods[0].want = n;
            goods[0].def = std::uint16_t(id);
            goods[0].exact = 1;
            DealLine pay[64];
            const StagedValue sv = stage_value_lines(bag, cost, pay);
            if (!deal_covered(sv.value, cost)) continue;
            const DealFact f =
                deal(st, traderSlot, marketSlot,
                     std::span<DealLine>(pay, std::size_t(sv.lines)), goods,
                     kNoCarryLimitG, freeG);
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
int squad_season_window(GameState& gs, MacroStore& st, int day) {
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
        // сезона дал 51 322 и 51 544 голодные смерти при Σ чисел фич городов и
        // деревень, ВЫРОСШЕЙ на 1 827 и 1 840 (рождения дня) — то есть не
        // списанной ни на одну душу. Мир кормил, растил, поднимал сквады и
        // облагал данью людей, которых у него нет.
        const int folkBefore = count_human_souls(bag.inv);
        const UpkeepWindowOutcome out = upkeep_season_window(
            creatures, bag.inv, st.wellbeing[slot]);
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
            record_rooted_fact(st, gs, FactKind::Starved,
                                 int(st.spawnId[slot].index), px, py,
                                 out.starved);
            // ВЫМЕР — это «был состав и не стало», и судится он ПОСЛЕ
            // взыскания: до него спрашивать нечего, после — некого.
            if (creatures_empty(bag.inv)) {
                record_rooted_fact(st, gs, FactKind::Died,
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
        // (world_tick settle_rooted_day). Двери прихода у сквада разные
        // (добыл, купил, отнял), и городить у каждой свою уплату значило бы
        // писать закон пятый раз; вместо этого контейнер ест то, что приехало,
        // тем же вечером. «Добыча привезла — часть съелась» (владелец,
        // 2026-09-21), и это ТОТ ЖЕ econ_pay_debt, которым платит ландмарк.
        econ_pay_debt(bag.inv, st.upkeep[slot].needDebt);
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
    // Аллокация на сборке мира, не в тике (DOD п.4): скрэтч закона порядка
    // (squad_walk.h) греется до капа один раз, свипы дальше zero-alloc.
    // (Второй скрэтч `sweepOrder` умер с M-376 — лист на драйв один.)
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
// ТРАМПЛИН СТРОКИ СМЕРТИ. Это НЕ клеймовщик вроде снесённых релеев факта —
// у тех не было работы, кроме штампа в колонку без читателя; у этого работа
// есть и она одна: перевести «слот уходит» в «спроси строку его рода». Форма
// вынуждена указателем на функцию — свип живёт слоем НИЖЕ таблицы модулей
// (`squad.h` не смеет её включить), и это ровно та дверь, которой слой
// отдаёт момент наверх, не узнавая, что с ним сделают.
struct SweptDeathRelay {
    AiPult* pult = nullptr;
    const MacroStore* st = nullptr;
};
void relay_swept_death_(MacroHandle h, void* user) {
    const auto* r = static_cast<const SweptDeathRelay*>(user);
    if (!r->st->valid(h)) return;
    const std::size_t row = std::size_t(r->st->runtime[h.slot].squadType);
    const AiDeathFn fn =
        row < std::size(kAiModuleRows) ? kAiModuleRows[row].death : nullptr;
    if (fn) fn(h, *r->pult);
}

void settle_dead_squads(MacroWorld& mw) {
    GameState& gs = *mw.gs;
    MacroStore& st = *mw.store;
    kill_fallen_squad_creatures(st);
    // КОНТЕКСТ СОБИРАЕТСЯ ЗДЕСЬ, И ОН МИНИМАЛЬНЫЙ ПО ЧЕСТНОЙ ПРИЧИНЕ: у
    // свипа мёртвых нет ни кванта думки, ни позиции игрока, ни индекса
    // сквадов — и глаголу `reshape_feature` ничего из этого не нужно. Пустые
    // поля суть «этого в момент смерти не существует», а не забытые входы.
    TickContext ctx{};
    ctx.mw   = mw;
    ctx.mapW = gs.mapW;
    ctx.mapH = gs.mapH;
    AiPult pult(ctx);
    SweptDeathRelay relay{&pult, &st};
    destroy_dead_macro_squads(st, gs, &relay_swept_death_, &relay);
}

} // namespace

// ОДИН тик мира — закон ритма целиком (M-379): единообразный перебор всех
// сквадов, фаза каждого копится ЧЕСТНЫМИ ТИКАМИ (+1), созревшая — думает.
// Квант kAiTicks и персональные фазы рождения — ШИРИНА ИСПОЛНИТЕЛЯ, не закон
// мира (владелец, 2026-11-02: размазка «сделана только ради оптимизации,
// другого смысла там нет»); закон мира — перебор каждый тик, и он исполняется
// здесь буквально.
static void tick_macro_npc_ai_one(MacroWorld& mw, MacroNpcAiRuntime& runtime) {
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
    // (squad_walk.h): по ординалу, не по кишке хранилища. Лист на драйв
    // ОДИН — тот самый, что собрал build_squad_index выше (M-376: второй
    // скан капа умер; содержимое и порядок те же — живые по ординалу).
    // Игрока свип не водит НИКОГДА — ни флажок, ни его родной сквад
    // (слоты из ctx, пропуск в теле).
    for (const SquadWalkEntry& sw : runtime.squadIndex.order) {
        const std::uint16_t slot = sw.slot;
        if (slot == ctx.playerFlagSlot || slot == ctx.playerSquadSlot)
            continue;
        const MacroHandle h = handle_at(st, slot);
        auto& cell = st.cell[slot];
        auto& kind = st.kind[slot];
        auto& rt   = st.runtime[slot];
        auto& hp   = st.pools[slot];

        // Приток — честный тик (M-379). Здесь стоял срез min(ticks, kAiTicks)
        // с законом «одна думка на вызов»: фаза копила ВЫЗОВЫ, не тики, и мир
        // зависел от дробления — пачка 32 схлопывала все фазы в ноль.
        ++rt.tickAccum;
        // ТРАТА КВАНТА — ДВЕРЬ «ВСЁ ИЛИ НИЧЕГО» (M-218, 2026-10-12). Здесь
        // стояли гейт и голое `-=` двумя строками, то есть `try_spend`,
        // написанный руками над БЕЗЗНАКОВЫМ счётчиком: разъехались бы они —
        // и копилка завернулась бы в 4 млрд тиков, то есть сквад перестал бы
        // думать молча и навсегда. Дверь делает расхождение невыразимым.
        if (!try_spend(rt.tickAccum, kAiTicks)) continue;
        ++runtime.thinksDone;   // метр-свидетель: квант оплачен — думка была

        // ГЕЙТ ЗАНЯТОСТИ ТАЕТ ВРЕМЕНЕМ (ломоть stateTimer M-369; аналог
        // `tick_body_recovery@src/sub/ability.h`): оплаченный квант думки =
        // kAiTicks мировых тиков прошло для ЭТОГО сквада — в походе, в
        // привале и в бою одинаково. Прежний носитель декрементировался 18
        // строками ВНУТРИ веток состояний, то есть часы шли только в
        // «своём» состоянии и стояли во всех прочих. Слив — дверью
        // (`squad_recovery_elapsed@src/bus/squad_row.h`, M-412д): второй
        // звонящий — часы флажка, свип игрока не водит.
        ecs::squad_recovery_elapsed(rt, std::uint32_t(kAiTicks));

        if (kind.type >= std::uint16_t(NPCType::Count)) continue;
        // A battle earlier in this very sweep may have killed this squad —
        // отбор жил на входе свипа, so re-check.
        if (st.dead[slot] != 0) continue;
        const ThinkGate gate = prepare_macro_npc_tick(rt, hp);
        if (gate == ThinkGate::Dead) continue;
        // Множитель груза — ДО поведения, тем же моментом, каким цену брала
        // снесённая колонка (M-302): час, который оплатит ритм ниже, пройден
        // с ТОЙ сумкой, какая была на входе думки, а не с той, какую сквад
        // набрал на рынке внутри неё.
        const float overloadBurden =
            overload_burden_of(rt, &st.inventory[slot]);
        // Decode → think in fractional scratch → encode (the scale split):
        // the STORE is one whole-cell number; the march's own float math
        // lives only on this think's stack.
        MacroPos p{float(ecs::cell_x(cell, gs.mapW)),
                   float(ecs::cell_y(cell, gs.mapW))};
        const float x0 = p.x, y0 = p.y;
        // ЗЕМЛЯ ЭТОГО ЧАСА — ОДИН СЭМПЛ, И СНЯТ ОН ТАМ ЖЕ, ГДЕ ЕГО БЕРЁТ ТЕМП
        // (M-395): в КЛЕТКЕ СТАРТА думки, до поведения, тем же моментом, каким
        // снята сумка выше. Час мира считается по ОДНОЙ клетке — и скорость, и
        // цена обязаны говорить об одной и той же земле, иначе думка «из воды
        // на дорогу» платит по дороге (промах до ×31). Адрес назван СТАРТОМ, а
        // не `p`, чтобы следующая правка порядка не сменила смысл молча.
        const float groundWeight = cell_weight(ctx.mw.pathCost, int(x0), int(y0));
        // АРЕНДОВАН ЛИ СТАРТ ДУМКИ (ЗАХВАТ, ломоть 2 M-419) — один сэмпл,
        // тем же моментом, что земля часа строкой выше: вошедший в окно
        // СРЕДИ думки честно оплатил свой марш и станет захваченным только
        // со следующей.
        const bool captured = cell_is_leased(gs, cell.idx);
        rt.marchFlags = 0;   // флаги марша живут одну думку
        if (gate == ThinkGate::Think)
            dispatch(h, p, rt, hp, ctx);
        // «moved» включает внутриклеточный ход: доля проехала — тело шло,
        // даже если клетка (позиция мира) не сменилась.
        settle_march_rhythm(h, p, rt, hp,
                            p.x != x0 || p.y != y0
                                || (rt.marchFlags & ecs::kMarchAdvanced) != 0,
                            captured, ctx, overloadBurden, groundWeight);
        // Марш свою долю ведёт САМ (квантовый ход, вердикт 2026-11-02);
        // драйверу остался только ПРЫЖОК: телепорт штампует kEntryDirNone,
        // и такая смена клетки честно ставит центр. Запись — ЕДИНСТВЕННОЙ
        // дверью settle_walk_cell (M-412г), той же, что у шага игрока.
        settle_walk_cell(cell, rt,
                         ecs::cell_index(int(p.x), int(p.y), gs.mapW));
    }

    // The END of every dead squad's story, once per tick (CANON S4): any
    // stragglers' survivors to the pool (idempotent for the already-drained;
    // it also catches a dead=1 creatures a save carried across the sweep
    // window), then the drained corpse-rows leave the map. Deferred to HERE
    // because the settle doors' callers still hold the entities mid-tick.
    settle_dead_squads(mw);
}

// ЕДИНСТВЕННЫЙ драйвер макро-думок (M-379): N тиков = N итераций шага выше,
// байтово равных N вызовам по 1, — эволюция мира есть функция ТИКОВ по
// построению, и дробление вызовов (кадр, пачка теста, фон субмира по
// делителю, прыжок addtime) в состоянии невыразимо. Бюджетный собрат
// tick_macro_npc_ai_budgeted умер этим же нарядом: его машинерия
// (sweepAccum/pendingSweeps/sweepCursor, бюджет думок на шаг) была размазкой
// ради оптимизации (вердикт владельца 2026-11-02) и вторым законом ритма —
// ряд SKELETON «два драйвера — один закон каданса» закрыт.
void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks) {
    if (!mw.gs || !mw.store) return;   // no store, no thinking (fail-closed)
    for (std::uint64_t i = 0; i < ticks; ++i)
        tick_macro_npc_ai_one(mw, runtime);
}

// ── ОДИН ЗАКОН МАРША — ЯДРО (M-412) ────────────────────────────────────
// THE march, and since 2026-10-06 it takes NO BARS: a step has no price, so
// the walk has no business holding the block it used to debit. What it owns
// is the geometry — the baked gait, the greedy fallback, the entry stamp and
// the quantum ride of the cell sub-coordinate. Who pays for the hours is
// settle_march_rhythm, once per think; the BUDGET of quanta is the caller's
// law (M-412б). Экспортировано из анонимного неймспейса (M-412а): до того
// марш игрока (`step_macro_walk@src/ui/macro_overlay.cpp`) был второй
// реализацией этой же оплаты — из ui ядро было физически невызываемо.
float try_move(MacroPos& p, ecs::MacroNpcRuntime& rt,
               float tx, float ty, std::uint32_t quanta,
               const MarchGround& g, MarchRoute* route) {
    // (Гейт ног заминки боя — `if (rt.halted != 0)`, M-319A → M-418 — умер
    //  шагом 0 ЗАХВАТА (M-419): в окне 3×3 тело держит сам бой сцены, вне
    //  окна эмитентов у стопера не было. Некролог — у `kMarchRefused` в
    //  bus/squad_row.h.)
    int ix = int(p.x), iy = int(p.y);
    const int itx = int(tx), ity = int(ty);
    // ЛЕТУН (v93): воздух — его стихия, как вода у паруса. Море ему не
    // «недостижимо посуху», рельеф не платится, любая клетка держит — ОДИН
    // цикл марша, меняется только стихия (образец кораблей).
    const bool flying = rt.flying != 0;
    const float sidef = float(g.side);

    // КВАНТОВЫЙ ХОД (вердикт владельца 2026-11-02: «никаких флоатов всё
    // чисто инт», «дошол до 127 … и меняет клетку с другой стороны»).
    // Производство думки/кадра — целые кванты доли; банка между думками
    // умерла вместе с float-бюджетом: прогресс марша И ЕСТЬ доля rt.sub, а
    // кванты, которым некуда идти, сгорают (kMarchRefused скажет ритму).
    rt.marchFlags = 0;
    float walked = 0.0f;   // евклидова дорога — только для картинки

    // ИЗЪЯТИЕ НОГ ПО АРЕНДЕ (ЗАХВАТ, ломоть 2 M-419): в девяти клетках окна
    // позицию ведёт СУБМИР, fold сворачивает её на границе тика — макро-ноги
    // здесь отказывают ЦЕЛИКОМ: ни клетки, ни доли (доля тоже fold'а).
    // БЕЗ ФЛАГОВ: kMarchRefused был бы ложью «мир не пускает» —
    // march_is_stuck_ перезаписал бы живую цель на дом, а цели захваченных
    // ЖИВУТ (думка идёт, M-313). Ритму «тело стоит» скажет параметр
    // captured свипа, не флаг.
    if (cell_in_lease_frame(g.leaseCenter, cell_of(ix, iy, g.side), g.side))
        return 0.0f;

    while (quanta > 0) {
        int bx = -1, by = -1;
        int dx = 0, dy = 0;
        if (route != nullptr) {
            // «ШАГ ЗАДАН» (M-412в): маршрут A* игрока исполняется БУКВАЛЬНО
            // — какие клетки выбрал поиск, те ноги и оплачивают; походка
            // ядра спит. Румб — знак тор-свёрнутой разницы (torus_fold);
            // вейпоинт своей же клетки — пропуск, не смена.
            if (route->idx >= route->count) break;
            const PathPoint& nxt = route->pts[route->idx];
            const int fdx = torus_fold(nxt.x - ix, g.side);
            const int fdy = torus_fold(nxt.y - iy, g.side);
            dx = fdx > 0 ? 1 : (fdx < 0 ? -1 : 0);
            dy = fdy > 0 ? 1 : (fdy < 0 ? -1 : 0);
            if (dx == 0 && dy == 0) {   // вейпоинт — своя же клетка
                ++route->idx;
                continue;
            }
            bx = nxt.x;
            by = nxt.y;
        } else {
            if (ix == itx && iy == ity) break;
            // Greedy steering (Session 21, owner's choice over per-squad
            // A*): among the eight neighbours, keep those strictly CLOSER
            // to the target and step onto the cheapest by the one weight
            // grid. The straight torus step wins ties, so a featureless
            // world walks exactly the line the old flat step walked — and a
            // coast is walked AROUND (land is 2-5×, water 10×), while a
            // river with no cheap way through is finally forded at its
            // honest price. O(8) per cell: 16384 squads can afford it where
            // a pathfind each would starve the frame.
            float bw = 1e30f;
            // Сосед шага — арифметика ИНДЕКСА (cell_step, ЗАКОН АДРЕСА).
            const std::uint32_t hereIdx = cell_of(ix, iy, g.side);

            // ЗАПЕЧЁННАЯ ПОХОДКА (CANON S7, 2026-09-02): округи + порталы +
            // граф — три чтения, ни волны, ни поиска. Жадный шаг остаётся
            // миру без запечённого слоя. Гейт «стоит на суше» снят
            // 2026-10-06 вместе со стеной: после единой заливки у КАЖДОЙ
            // клетки тора есть округа — походка отвечает и из воды.
            if (!flying && g.nav && g.nav->baked()) {
                int fdx = 0, fdy = 0;
                if (nav_step(*g.nav, ix, iy, itx, ity, fdx, fdy)) {
                    const std::uint32_t n =
                        cell_step(hereIdx, fdx, fdy, g.side);
                    bx = cell_x(n, g.side);
                    by = cell_y(n, g.side);
                }
            }

            if (bx < 0) {
                // Жадный шаг — закон дальних маршей вне округ (Session 21).
                const Step straight =
                    torus_step_toward(ix, iy, itx, ity, g.side, g.side);
                // The straight step is the TIE-BREAKER candidate, and since
                // 2026-10-06 it is nothing but that: the standing predicate
                // and its three exceptions (dry feet, flight, a fleeing
                // body's ford) died together. Water is now simply the
                // dearest row of the price list — a coast is walked AROUND
                // because it is expensive, and a river IS forded when going
                // round costs more.
                const auto walker_w = [&](int nx2, int ny2) {
                    if (flying) return 1.0f;   // воздух — дорога летуна
                    return edge_weight(g.pathCost, ix, iy, nx2, ny2);
                };
                bx = straight.nx;
                by = straight.ny;
                bw = walker_w(bx, by);
                const float dHere =
                    torus_dist_sq(float(ix), float(iy),
                                  float(itx), float(ity), sidef, sidef);
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) continue;
                        const std::uint32_t n =
                            cell_step(hereIdx, ox, oy, g.side);
                        const int nx = cell_x(n, g.side);
                        const int ny = cell_y(n, g.side);
                        if (nx == bx && ny == by) continue;
                        const float d =
                            torus_dist_sq(float(nx), float(ny),
                                          float(itx), float(ity),
                                          sidef, sidef);
                        if (d >= dHere) continue;  // only steps that progress
                        const float w = walker_w(nx, ny);
                        if (w < bw) { bx = nx; by = ny; bw = w; }
                    }
                }
            }
            if (bx < 0) {
                // Шага, приближающего к цели, нет — кванты сгорают, а ритм
                // марша узнаёт «ноги отказали» флагом.
                rt.marchFlags |= ecs::kMarchRefused;
                break;
            }

            // Entry-side stamp: the signed step of THIS cell change,
            // torus-folded (stepping east off the map's edge is still +1,
            // not -(w-1)) — та же дверь torus_fold, что у маршрута.
            const int fdx = torus_fold(bx - ix, g.side);
            const int fdy = torus_fold(by - iy, g.side);
            dx = fdx > 0 ? 1 : (fdx < 0 ? -1 : 0);
            dy = fdy > 0 ? 1 : (fdy < 0 ? -1 : 0);
        }

        // ЦЕНА ПЕРЕСЕЧЕНИЯ — доехать долей до края по каждой активной оси;
        // оси едут ВМЕСТЕ, поэтому диагональ стоит те же 2×127 кванта от
        // края до края, что и прямой ход, — прежний закон «клетка = 1.0
        // бюджета в любом румбе», теперь целыми числами.
        const int costX =
            dx != 0 ? ecs::kCellSubEdge - dx * int(rt.sub.x) : 0;
        const int costY =
            dy != 0 ? ecs::kCellSubEdge - dy * int(rt.sub.y) : 0;
        const std::uint32_t cost = std::uint32_t(costX > costY ? costX : costY);

        // Кванты уходят ДВЕРЬЮ drain (ЗАКОН ТРЁХ ДВЕРЕЙ): взять сколько
        // есть, факт честен — хватило на край или нет, решает возврат.
        const std::uint32_t spent = drain(quanta, cost);
        rt.marchFlags |= ecs::kMarchAdvanced;
        // Дорога картинки ЕВКЛИДОВА: диагональный квант ведёт обе оси и
        // стоит √2 дистанции — глаз, квотированный без этого, отставал на
        // диагоналях (замер глайд-смоука: maxGap 3.54 клетки).
        walked += float(spent) / float(2 * ecs::kCellSubEdge)
                  * ((dx != 0 && dy != 0) ? std::numbers::sqrt2_v<float>
                                          : 1.0f);
        if (spent < cost) {
            // Края не достигли: доля едет ВНУТРИ текущей клетки (ось у
            // своего края стоит клампом), позиция мира не меняется —
            // «сквад локально хранит свою промежуточную позицию».
            if (dx != 0) {
                const int nx = int(rt.sub.x) + dx * int(spent);
                rt.sub.x = std::int8_t(std::clamp(
                    nx, -ecs::kCellSubEdge, ecs::kCellSubEdge));
            }
            if (dy != 0) {
                const int ny = int(rt.sub.y) + dy * int(spent);
                rt.sub.y = std::int8_t(std::clamp(
                    ny, -ecs::kCellSubEdge, ecs::kCellSubEdge));
            }
            break;
        }

        // ПЕРЕСЕЧЕНИЕ КРАЯ: клетка меняется, активные оси входят с
        // противоположного края, пассивная ось долю СОХРАНЯЕТ.
        rt.sub = ecs::cell_sub_after_step(dx, dy, rt.sub);
        rt.entryDir = pack_entry_dir(dx, dy);
        rt.entryTicks = 0;

        ix = bx; iy = by;
        if (route != nullptr) ++route->idx;

        // NOTHING IS CHARGED HERE, AND THAT IS THE LAW (2026-10-06). A step
        // has no price: what the body pays is the HOUR it spends on ground,
        // settled once per think by settle_march_rhythm (НПЦ) или ходом
        // макро-ветки главного цикла (игрок). «Pressing on is priced, never
        // gated» (CANON S14.1) holds on the macro march.

        // Never hop OVER the player's cell in a multi-cell think: the forced
        // encounter (Inc 6) is geometric, so the squad stops ON the meeting
        // cell where the door can see it. (Для марша САМОГО игрока гейт
        // спит: g.playerCellX = −1 вне тора.)
        if (ix == g.playerCellX && iy == g.playerCellY) break;

        // ШАГ В АРЕНДУ — ПОСЛЕДНИЙ ШАГ ДУМКИ (ЗАХВАТ, ломоть 2 M-419):
        // вошедший в окно встаёт на ПЕРВОЙ арендованной клетке и
        // захватывается следующим слепком — пролёт насквозь за одну думку
        // был бы сквадом в окне, которого слепок не воплотил.
        if (cell_in_lease_frame(g.leaseCenter,
                                cell_of(ix, iy, g.side), g.side)) break;
    }

    // ПРИБЫЛ — ДОЕЗЖАЕТ В ЦЕНТР (M-375, вторая половина хода): пересечение
    // оставило тело на краю клетки-цели, остаток квантов доводит его
    // внутрь. Это не марш (флагов и жжения не добавляет — тело уже дома).
    // Для маршрута «цель» — его исчерпание: тот же доезд закрывает и
    // прежнюю idle-ветку игрока «маршрута нет, а доля не дома» (M-375/Б5).
    const bool goalDone = route != nullptr
        ? route->idx >= route->count
        : (ix == itx && iy == ity);
    if (goalDone && quanta > 0u) {
        walked += float(ecs::cell_sub_walk_home(rt.sub, quanta))
                  / float(2 * ecs::kCellSubEdge);
    }

    p.x = float(ix);
    p.y = float(iy);
    return walked;
}


// (tick_macro_npc_visuals жил здесь и умер с M-244a: глайд карты — чистое
// ПРЕДСТАВЛЕНИЕ, и его интегратор переехал в кэш глаз оверлея —
// tick_macro_eyes@src/ui/macro_overlay.cpp; мир несёт клетку и целую долю
// CellSub, float-колонки вида в store больше нет.)

} // namespace sm

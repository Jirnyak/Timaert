// Macroworld NPC AI — full behaviour set, faithful port of `npc-ai.ts`.
#include "macro/npc_ai.h"
#include "macro/squad_ai/module_window.h"  // окно модуля: своя строка, вид, пульт
#include "macro/squad_ai/module_entries.h"  // точки входа модулей для kAiModuleRows
#include "core/caps.h"             // kUnifiedCap — резерв скрэтчей порядка
#include "macro/upkeep_window.h"   // ОДИН суд границы на всякий контейнер
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
#include "macro/squad_act.h"   // акт сквада: цена и восстановление одной дверью
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace sm {

namespace {

// TS uses Math.random() inside behaviours. The C++ port uses App-owned
// MacroNpcAiRuntime RNG so world resets do not inherit hidden module state.
// Файл-локальна: модули бросают кость дверью пульта `AiPult::rand`.
int rand_int(const TickContext& ctx, int range) {
    if (range <= 0) return 0;
    return int(ctx.rng->next_u32() % std::uint32_t(range));
}

// (МАССА ЕДИНИЦЫ СТРОКИ — `unit_mass_g@src/macro/deal.h`: уехала к двери
// `deal` вместе с клампом спины; провенанс килограмма существа стоит там.)

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

}  // namespace

// УРНА ГОРОДОВ — ОДИН предикат на оба рейса (торговец и кочевник). Он стоял
// ДВУМЯ побайтово одинаковыми лямбдами в двух соседних поведениях, и это был
// второй ответ на один вопрос мира (DOD п.6), просто спрятанный в области
// видимости функции. Обход мест — одна дверь (`for_each_rooted`,
// `macro/rooted_iter.h`): ось рода ТЕЛА отвечает «город ли это», второго
// списка городов в мире нет. `notId` — ординал, который спрашивающий
// исключает (свой дом у торговца, текущая цель у кочевника).
bool is_other_city(const MacroStore& st, std::uint16_t slot,
                   int notId) {
    return SquadType(st.runtime[slot].squadType) == SquadType::City
           && int(st.spawnId[slot].index) != notId;
}

// (Здесь жили `slot_x` / `slot_y` — клетка слота парой координат «для
// геометрии марша и округи». Геометрия идёт дверями над ИНДЕКСОМ
// (`cell_delta` / `cell_dist` / `cell_toward@src/core/torus.h`, M-462), и
// клетка слота читается колонкой как есть: `st.cell[slot].idx`.)

// ── Helpers shared by all behaviours ──────────────────────────

// (Здесь жил `struct XY { float x, y; }` — клетка парой флоатов, носитель
// дома, рабочего места и точки прогулки. Клетка есть ОДНО число
// (ЗАКОН АДРЕСА п.2), и «нет клетки» — сентинел `ecs::kNoCell`, а не
// `bool` рядом с выходным параметром: M-462.)

// (`fold_d` — торова складка разности координат — умерла вместе со сканом жил:
// обход бокса от дома строит смещения сам, складывать нечего.)

// (КОРАБЛИ, ПОРТЫ И МОРСКОЙ ХОД ВЫРЕЗАНЫ 2026-09-22, сессия 14, вердикт
// владельца: «никакого транспорта корабли механики и всё пока вырезаем
// полностью из кода потому что не дошли до них». Здесь жили ships_at /
// ships_add над слоем разработки и цена корпуса kShipWoodUnits.)

// (`stamp_feature_if_bare` ВЫРЕЗАНА 2026-09-22: фичи ставит генерация мира,
// а не сквады — артель больше не строит ничего.)

namespace {

// Случайная клетка в боксе ±range вокруг `c` — шаг ИНДЕКСА, заворот делает
// маска `cell_step`. Разброс симметричен: [-range, +range] по каждой оси
// (флоатная предшественница брала [-range, range-1] — на клетку смещённо).
// Файл-локальна, как и клетка дома ниже: модули зовут двери окна
// `AiPult::rand_nearby` и `AiView::home`.
std::uint32_t cell_nearby(std::uint32_t c, int range, const TickContext& ctx) {
    const int dx = rand_int(ctx, range * 2 + 1) - range;
    const int dy = rand_int(ctx, range * 2 + 1) - range;
    return cell_step(c, dx, dy, ctx.mapW);
}

// КЛЕТКА ДОМА; `ecs::kNoCell` = дома нет.
std::uint32_t home_cell(MacroHandle self, const TickContext& ctx) {
    // ДОМ = РОДИТЕЛЬ (M-245): ребро Parent реестра связей; колонка
    // `homeSettlementId` умерла. ОДИН эмитент ординалов (M-37).
    const std::uint16_t slot =
        rooted_slot_by_id(ctx, parent_of(store_ctx(ctx), self.slot));
    if (slot == kMacroNoSlot) return ecs::kNoCell;
    return store_ctx(ctx).cell[slot].idx;
}

// ДОМ АРТЕЛИ — СЛОТ тела места; kMacroNoSlot = дома нет. (Спутник
// home_inventory умер с переводом сдачи груза на deal: сделка адресуется
// слотами, и склад по слоту берёт сама дверь.)
std::uint16_t home_rooted_slot(MacroHandle self, const TickContext& ctx) {
    return ctx.mw.gs
        ? rooted_slot_by_id(ctx, parent_of(store_ctx(ctx), self.slot))
        : kMacroNoSlot;
}

// The RIPEST home field, not the nearest (owner increment 2026-08-31) —
// fields live within the home's ±3 box (the worldgen stamp and the plough
// both land there). The old nearest-pick was measured to waste the whole
// ring: crews hammered the one closest parcel bare while every other field
// stood ripe and unvisited, so the village's grain flow was pinned to a
// single cell's regrowth however much land it ploughed. Standing stock
// picks the parcel now; distance only breaks ties. A box with nothing
// standing offers no work — the crew is honestly not raised that day.
// Ответ — клетка; `ecs::kNoCell` = работы в округе нет.
std::uint32_t find_home_field(const TickContext& ctx, std::uint32_t at,
                              std::uint32_t home,
                              FeatureType parcel = FT_Field) {
    if (!ctx.mw.gs) return ecs::kNoCell;
    std::uint32_t found = ecs::kNoCell;
    int bestStock = 0;
    int best = std::numeric_limits<int>::max();
    // Бокс — арифметика ИНДЕКСА через cell_step (ЗАКОН АДРЕСА).
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(home, dx, dy, ctx.mapW);
            if (ctx.mw.gs->features.at(n) != parcel) continue;
            const int stock =
                resource_field_read(ctx.mw, ResourceFieldId::Wheat, n);
            if (stock <= 0) continue;   // eaten bare — nothing to reap here
            const int d = cell_dist(at, n, ctx.mapW);
            if (stock > bestStock || (stock == bestStock && d < best)) {
                bestStock = stock;
                best = d;
                found = n;
            }
        }
    }
    return found;
}

// ГДЕ ОХОТИТЬСЯ: лучшая по поголовью клетка своей округи. Зверь — природа на
// КАЖДОЙ клетке (биом задаёт вместимость), поэтому это не поиск редкости, а
// выбор лучшей из ближних — тот же бокс и тот же вид ответа, что у пашни.
std::uint32_t find_home_fauna(const TickContext& ctx, std::uint32_t at,
                              std::uint32_t home) {
    std::uint32_t found = ecs::kNoCell;
    int bestStock = 0;
    int best = std::numeric_limits<int>::max();
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(home, dx, dy, ctx.mapW);
            const int stock =
                resource_field_read(ctx.mw, ResourceFieldId::Fauna,
                                    n);
            if (stock <= 0) continue;   // выбито — здесь не охотятся
            const int d = cell_dist(at, n, ctx.mapW);
            if (stock > bestStock || (stock == bestStock && d < best)) {
                bestStock = stock;
                best = d;
                found = n;
            }
        }
    }
    return found;
}

bool at_goal(std::uint32_t at, const ecs::SquadGoal& g) {
    // ПРИБЫТИЕ СУДИТСЯ КЛЕТКОЙ, И БОЛЬШЕ НИЧЕМ (M-461, вердикт владельца
    // 2026-11-12: «система перемещения должна проверять по индексу агностично
    // целому»). Сквад прибыл, когда его клетка РАВНА клетке цели — тот же
    // вопрос, которым мир судит встречу, бой и торг; рода цели предикат не
    // читает. Цели нет — прибытия нет.
    //
    // ЧТО ЗДЕСЬ СТОЯЛО: порог «ближе двух клеток» по дробной точке (`< 4.0f`
    // на квадрате расстояния — то есть окрестность 3×3 вокруг цели) и ветка
    // по роду `Squad`, которая уже тогда была равенством клеток. Порог был
    // вторым ответом на вопрос, на который ядро марша отвечает само: оно
    // перестаёт шагать ровно при равенстве клеток. Хуже — число ядра утекло в
    // модули: шесть модульных «дома ли я» обязаны были повторять его, а
    // охотник называл адрес «две клетки по румбу» только затем, чтобы
    // перешагнуть порог. «Встать не доходя» — частность модуля, и выражает он
    // её АДРЕСОМ, который называет, либо своим вопросом к индексам.
    return g.cell != ecs::kNoCell && at == g.cell;
}

// (Здесь жила `goal_xy` — «точка марша из адреса цели», пара флоатов для
// ног. Ноги берут адрес цели как он лежит в колонке — индексом: M-462.)

// (Здесь жила обёртка `settle_sp_carry(Pools&)` — ею артель сама списывала SP
// за цикл через дробный перенос марша. Единственный звонящий ушёл в дверь
// акта (`charge_squad_act@src/macro/squad_act.h`), ряд M-486.)

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
float cell_weight(const PathCostData* pc, std::uint32_t cell);

}  // namespace

// ПОГОНЯ КОНЧИЛАСЬ — ЕЁ ПРЕДИКАТ УМИРАЕТ ВМЕСТЕ С НЕЙ (M-468; вердикт
// владельца 2026-11-12 «да давай» на простую форму: кто отпускает погоню,
// оставляет колонку цели ПУСТОЙ, как её уже оставляет бегство). Погоня
// пишет в цель «кого ловлю» — ординал добычи, по нему ноги каждую думку
// пересчитывают адрес. `goal_drop@src/bus/squad_row.h` бросает один адрес и
// бережёт «что я искал» для следующего решения — это верно для рынка
// корована после стычки и неверно для добычи: погоня кончилась, а её ординал
// оставался лежать в той же колонке, которой роль называет свой рынок или
// должника. Пустая цель выразима общей дверью записи: адреса нет, «чего
// ищу» — ноль; отдельной двери под это не заводится.
void chase_release(ecs::SquadGoal& g) {
    ecs::goal_set(g, ecs::kNoCell, 0u, 0, std::uint8_t(NPCState::Idle));
}

namespace {

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
// «ШЁЛ ЛИ» — ВОПРОС К САМОМУ ХОДУ, А НЕ К КЛЕТКЕ И НЕ К ЦЕЛИ (M-465). Он не
// равен «сменил ли клетку в эту думку», и первая редакция закона на этом
// обожглась: темп был дробным, тело на дороге копило долю клетки и на
// четверти думок «стояло» — приняв это за отдых, закон платил идущим за
// марш, и ни один сквад больше не выдыхался. Лечили тогда предикатом
// «остановился = там, где хотел быть, либо РЕШИЛ встать». С квантовым ходом
// (2026-11-02) копить нечего: всякий оплаченный квант ядро марша метит
// флагом `kMarchAdvanced`, и «флага нет» есть точное «не шёл» — предикат
// остановки умер, а вместе с ним и чтение цели, состояния и аренды ритмом.
//
// `overloadBurden` — МНОЖИТЕЛЬ ГРУЗА ПАРАМЕТРОМ, А НЕ КОЛОНКОЙ (M-302 +
// M-310): свип считает его дверью `overload_burden_of` выше в той же думке, ДО
// поведения, и передаёт сюда. МОМЕНТ РАСЧЁТА ОСТАВЛЕН ТОТ ЖЕ, что был у
// снесённой колонки `overloadCost`, — менялись с тех пор только тип числа
// (`int16` → `int` → `float`) и его РОЛЬ в формуле (слагаемое → множитель).
// `captured` — АРЕНДОВАН ЛИ СТАРТ ДУМКИ (ЗАХВАТ, ломоть 2 M-419): свип
// сэмплит его тем же моментом, что землю часа, и передаёт параметром — как
// грунт и груз. Нужен он ритму для ОДНОГО: доезд доли в центр у
// захваченного не бежит — доля писателя fold. Жжения у него нет по общему
// закону (макро-ног в окне нет — флаг хода не встаёт), отдельной ветки
// «захваченный встал» для этого не требуется.
void settle_march_rhythm(MacroHandle e,
                         ecs::MacroNpcRuntime& rt, ecs::Pools& hp,
                         bool captured, const TickContext& ctx,
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

    // ОТДЫХ ЕСТЬ ДУМКА, В КОТОРУЮ НОГИ НЕ ПРОЕХАЛИ НИ КВАНТА — И БОЛЬШЕ
    // НИЧЕГО (M-465, вердикт владельца 2026-11-12: «не двигается не жжот это
    // правмльный замысел типа чтобы сжигание включало именно движение как
    // универсальная система»; и раньше, 2026-08-31: «если встал —
    // безусловно, агностично, сразу реген»). Ответ даёт сам ход: ядро марша
    // ставит `kMarchAdvanced` на первом же оплаченном кванте, и другого
    // вопроса ритм не задаёт — ни о цели, ни о состоянии, ни об аренде.
    //
    // ЧТО ЗДЕСЬ СТОЯЛО: двухступенчатый предикат — сперва «есть ли причина
    // стоять» (`stopped` = в аренде, в лагере или в клетке цели), потом «и не
    // сдвинулся». У сквада, которому модуль велел ВСТАТЬ (`goal_drop` стирает
    // адрес), причины не находилось: он стоял, жёг SP как на марше и не
    // лечился. Зонд 2026-11-12, 20 думок стоя на одной земле: без адреса SP
    // 70 → 92, HP 80 → 80; с целью «стой тут» SP 70 → 95, HP 80 → 100.
    //
    // ПОЧЕМУ ПРЕДИКАТ БЫЛ НУЖЕН И ПЕРЕСТАЛ: при дробном банке хода сквад
    // копил долю клетки и ШЁЛ, не меняя клетки, — «не сдвинулся» не значило
    // «стоит». С квантовым ходом (2026-11-02) всякий проеханный квант
    // оставляет флаг, и «флага нет» есть точное «стоит». В лагере, в аренде,
    // в клетке цели и у стоящего места ответ прежний — они не шагают;
    // ПРЫЖОК ходом не является (ноги не ехали), и марша он больше не платит.
    const bool resting = (rt.marchFlags & ecs::kMarchAdvanced) == 0;

    // ВСТАВШЕЕ ТЕЛО ДОЕЗЖАЕТ В ЦЕНТР СВОЕЙ КЛЕТКИ (M-375, «у нас вообще
    // должно быть завершено стояние между клетками»): думка, чья машина
    // марша не звалась (цель — своя клетка, ожидание, работа на месте),
    // иначе навсегда оставила бы долю на краю входа. Темп — базовый шаг
    // думки (та же величина, что производит марш); это НЕ марш — resting
    // уже вычислен, жжения доезд не несёт. Вставший без адреса доезжает так
    // же (M-465): «встал» больше не требует причины.
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
float cell_weight(const PathCostData* pc, std::uint32_t cell) {
    if (!pc || !pc->live() || cell >= pc->costGrid.size()) return 1.0f;
    return pc->costGrid[cell];
}

// The EDGE weight of one greedy step — the cell half from the baked grid
// plus the uphill climb half (movement_cost.h: the law's two halves; the
// squad walks the same slopes the player and both A*s pay for). Обе клетки —
// индексы: сетка весов лежит над тем же тором, её индекс и есть индекс мира.
//
// ВЕС КЛЕТКИ УМНОЖАЕТСЯ НА ДЛИНУ ШАГА (M-463): диагональный сосед дальше в
// `kDiagonalStepLen@src/core/torus.h` — ровно тем же составом ребро считает
// запекание навигации (`edge_cost_of` в nav_field.cpp), так что жадный шаг и
// походка оценивают один шаг одинаково. Летуну вес клетки — единица.
float edge_weight(const PathCostData* pc, std::uint32_t from,
                  std::uint32_t to, int side, bool flying) {
    const CellDelta d = cell_delta(from, to, side);
    const float len = (d.dx != 0 && d.dy != 0) ? kDiagonalStepLen : 1.0f;
    if (flying) return len;
    float w = cell_weight(pc, to) * len;
    if (pc && pc->live() && pc->height16.size() == pc->costGrid.size()
        && from < pc->costGrid.size() && to < pc->costGrid.size()) {
        w += pc->climb(from, to);
    }
    return w;
}

// ── НОГИ СВИПА — ОДНА ДВЕРЬ ДВИЖЕНИЯ ВСЕХ СКВАДОВ (M-2 / M-307) ──────────
// «ДВИЖЕНИЕ — НЕ СИСТЕМА, А СОГЛАШЕНИЕ». Модуль пишет ЦЕЛЬ в свою строку и
// больше ничего; везёт к ней эта дверь, и зовёт её ОДИН звонящий — свип,
// сразу после думки, для всякого сквада одинаково. Она не знает ни рода, ни
// состояния, ни модуля: единственный её вопрос — в пути ли сквад к своей
// цели (`SquadGoal::underway`). Здесь же живёт ПРИБЫТИЕ
// (`goal_arrive@src/bus/squad_row.h` — стать тем, что несла цель). Третьей
// части наряда — застревания — нет: у него в мире нет предмета (некролог у
// `kMarchArrived@src/bus/squad_row.h`).
//
// ЧТО ЗДЕСЬ СТОЯЛО. Тонкий звонящий думки с подписью (поза, рантайм, точка,
// контекст), и звали его 17 мест: каждая машина состояний несла ноги сама и
// сама спрашивала «дошёл ли». Сборщик однажды собрался без строки ног и
// СТОЯЛ, числясь «в пути» (Traveling 799 563 при Working ноль, 2026-09-22);
// охота по запаху и дракон шагали сами, мимо общего прибытия. Теперь шагнуть
// из модуля нечем.
//
// К НАЗВАННОМУ СКВАДУ НОГИ ВЕЗУТ ТУДА, ГДЕ ОН СЕЙЧАС (M-307, вердикт
// владельца 2026-10-24: «сама механика преследования когда сквад уже
// преследует должна быть единая да агностичная»). Цель, названная ОРДИНАЛОМ
// сквада, держит адрес кэшем предиката, и ноги пересчитывают его каждую
// думку — для всякого названного сквада одинаково (M-459): добыча погони
// ушла — адрес идёт за ней; рынок стоит — пересчёт даёт ту же клетку.
// Спрашивать, «стоит ли он», ногам незачем. Названный умер или ушёл из мира
// — ординал не резолвится, адрес остаётся последним известным: сквад дойдёт
// туда, где видел его в последний раз, и прибудет. Отдельной ветки «цель
// пропала» нет.
//
// СОСТАВ БЮДЖЕТА — закон этого звонящего (M-412а/б; ядро марша
// экспортировано, `try_move@src/macro/npc_ai.h`, и его же зовёт марш игрока
// из `step_macro_walk@src/ui/macro_overlay.cpp`): клетки за думку — the
// SAME march the player walks (kMacroWalkCellsPerHour per game hour), paced
// by the avatar's own sheet (moveMult: spd × athletics) and by the ground
// underfoot (terrain_speed_mult of the CURRENT cell — one sample per think).
// Base numbers: 0.75 клетки за думку на ровной земле (8 клеток в час). ЛЕТУН (v93):
// воздух — его стихия; рельеф не платится (вес — единица), внутри ядра любая
// клетка держит — ОДИН цикл марша, меняется стихия.
//
// Exhaustion is settled by the sweep right after (it owns the entity +
// Health this function never sees): rest on land, the debt's bite on water.
void march_goal(MacroHandle e, ecs::MacroNpcRuntime& rt,
                const TickContext& ctx) {
    MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = st.goal[e.slot];
    if (g.underway == 0) return;

    if (ecs::goal_kind(g.what) == ecs::GoalKind::Squad) {
        const MacroHandle named =
            macro_handle_by_spawn_id(st, ecs::goal_ordinal(g.what));
        if (st.valid(named) && st.dead[named.slot] == 0)
            g.cell = st.cell[named.slot].idx;
    }

    // Ноги идут ПРЯМО ПО КОЛОНКЕ КЛЕТКИ (M-462): черновика позиции на время
    // думки нет, ядро марша пишет клетку само.
    ecs::MacroCell& cell = st.cell[e.slot];
    if (!at_goal(cell.idx, g)) {
        const bool flying = rt.flying != 0;
        const float perThink = march_cells_per_think(
            rt, flying ? 1.0f : cell_weight(ctx.mw.pathCost, cell.idx));
        const std::uint32_t quanta = std::uint32_t(
            std::lround(perThink * float(2 * ecs::kCellSubEdge)));
        MarchGround ground;
        ground.pathCost = ctx.mw.pathCost;
        ground.nav = ctx.mw.nav;
        ground.side = ctx.mapW;
        ground.playerCell = ctx.playerCell;
        // Центр аренды — ядру (изъятие ног, ломоть 2 M-419). Тернар
        // обязателен: адрес сессии в конверте нуллим по контракту (фикстура
        // без субмира), и нулевой адрес читается как «аренды нет».
        ground.leaseCenter =
            ctx.mw.session ? ctx.mw.session->leasedWindow : kNoWindowLease;
        sm::try_move(cell, rt, g.cell, quanta, ground);
    }
    if (at_goal(cell.idx, g)) ecs::goal_arrive(g, rt);
}

// Ближайшее дерево в пределах поиска; `ecs::kNoCell` = рядом леса нет.
// Радиус — 30 клеток ПУТИ (та же величина, что несла флоатная
// предшественница квадратом 30² + 1): три на три корзины сетки покрывают его
// целиком. «Ближе» судит дверь пути над индексами — рукописная складка
// «если больше полупериода, вычесть» здесь умерла вместе с парой флоатов.
std::uint32_t find_nearest_tree_grid(const MacroWorld& mw, const TreeGrid& g,
                                     std::uint32_t at, int side) {
    if (!g.trees || g.trees->empty()) return ecs::kNoCell;
    constexpr int kTreeSearchCells = 30;
    const CellBuckets& b = g.grid;
    const int cx0 = cell_x(at, side) / b.cellSize;
    const int cy0 = cell_y(at, side) / b.cellSize;
    int best = kTreeSearchCells * kStepStraight + 1;
    std::uint32_t found = ecs::kNoCell;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            int gx = wrapi(cx0 + ox, b.cols);
            int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const auto& t = (*g.trees)[*it];
                const std::uint32_t tc = cell_of(int(t.x), int(t.y), side);
                const int d = cell_dist(at, tc, side);
                // СЕТКА — ТОЧКИ ГЕНЕРАЦИИ, ЛЕС — ЖИВОЕ ПОЛЕ: клетка, срубленная
                // до нуля, местом работы не является (как выеденная пашня и
                // выработанная жила), иначе лесоруб вечно ходит к пню.
                if (d < best
                    && resource_field_read(mw, ResourceFieldId::Trees, tc) > 0) {
                    best = d;
                    found = tc;
                }
            }
        }
    }
    return found;
}

}  // namespace

// ── Behaviours ────────────────────────────────────────────────

// ПОВОДОК ПРОГУЛКИ: бродящий дальше стольких клеток пути от дома сперва
// возвращается. Число балансное; прежде стояло литералом `400.0f` (квадрат
// двадцати) в двух местах — у бродяги и у артели без работы.
constexpr int kWanderLeashCells = 20;

void ai_home_wanderer(MacroHandle self,
                      ecs::MacroNpcRuntime& rt,
                      [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    const std::uint32_t home = home_cell(self, ctx);
    if (home == ecs::kNoCell) return;
    MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = st.goal[self.slot];

    if (rt.state == std::uint8_t(NS::Idle)) {
        // Дошёл — постоял: пауза после прогулки, прибытие назвали ноги.
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 20)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            if (!cell_within(st.cell[self.slot].idx, home, kWanderLeashCells,
                             ctx.mapW)) {
                ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Returning);
            } else {
                ecs::goal_set(g, cell_nearby(home, 12, ctx),
                              0, 0, std::uint8_t(NS::Idle));
                rt.state = std::uint8_t(NS::Wandering);
            }
        }
        return;
    }
    // В пути (Wandering/Returning) модулю решать нечего: к цели везут ноги
    // свипа, они же сделают его Idle по прибытии.
}

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
// (`kSustainedMarchCellsPerDay` СНЕСЕНА 2026-11-08, вердикт владельца M-6/M-436:
//  это был ОДИН темп на всех ходоков мира — второй ответ на вопрос, на который
//  ноги уже отвечали анкетой. Её вывод (24 часа, половина календаря на отдых)
//  переехал ДОСЛОВНО в `march_cells_per_day@src/macro/npc_ai.h`, которая читает
//  тот же `moveMult`, что и `march_cells_per_think` рядом с ней.)

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
std::uint32_t find_home_deposit(const TickContext& ctx, std::uint32_t home) {
    if (!ctx.mw.gs || ctx.mapW <= 0) return ecs::kNoCell;
    const FeatureLayer& fl = ctx.mw.gs->features;
    NavWorld* nv = ctx.mw.nav;
    const bool navReady = nv && nav_ensure(ctx.mw, *nv);
    const std::uint16_t myRegion =
        navReady ? nav_region_at(*nv, home) : kNavNoRegion;
    std::uint32_t bestDry = ~0u;
    int bestWay = std::numeric_limits<int>::max();
    std::uint32_t dryAt = ecs::kNoCell;
    std::uint32_t nearAt = ecs::kNoCell;
    // THE ERRAND IS A NEIGHBOURHOOD QUESTION, so it walks a neighbourhood: the
    // hand's own box (kNavHandReach). «Где ближайшая жила моей округи»
    // уезжало отсюда в ОПИСЬ МЕСТА, а опись уничтожена 2026-09-30 (ломтик E
    // шаг 2, наряд M-191) — значит эта дверь снова ЕДИНСТВЕННАЯ, и её бокс
    // есть весь горизонт артели. Дыра названа в CANON S10 «КАРТА ОКРУГИ». A field
    // is indexed by the torus, so the box IS the loop: 33×33 reads, no
    // candidate discarded (problems.md §52 is the same lesson, one door over).
    const auto consider = [&](int dx, int dy) {
        const std::uint32_t n = cell_step(home, dx, dy, ctx.mapW);
        // Шахта с живой залежью — предикат колонкой строки фичи, не родом;
        // залежь спрашивается ДВЕРЬЮ ряда (число фичи сырьём читает только она).
        if (feature_def(fl.at(n)).numberRow != ResourceFieldId::Vein) return;
        if (resource_field_read(ctx.mw, ResourceFieldId::Vein, n) == 0)
            return;   // выработана — не рабочее место
        if (!navReady) {
            const int way = cell_dist(home, n, ctx.mapW);
            if (way < bestWay) {
                bestWay = way;
                nearAt = n;
            }
            return;
        }
        const std::uint16_t reg = nav_region_at(*nv, n);
        if (reg == myRegion) {
            const std::uint32_t d = nv->distHome[n];
            if (d < bestDry) {
                bestDry = d;
                dryAt = n;
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
    return navReady ? dryAt : nearAt;
}

// Ближайшее дерево для сквада в клетке `at` — только в округе навигации дома:
// дерево за водой — работа соседа. `ecs::kNoCell` = леса в округе нет.
std::uint32_t find_home_tree(const TickContext& ctx, std::uint32_t at,
                             std::uint32_t home) {
    if (!ctx.mw.treeGrid) return ecs::kNoCell;
    const std::uint32_t tree =
        find_nearest_tree_grid(ctx.mw, *ctx.mw.treeGrid, at, ctx.mapW);
    if (tree == ecs::kNoCell) return ecs::kNoCell;
    if (NavWorld* nv = ctx.mw.nav; nv && nv->baked()) {
        const std::uint16_t rh = nav_region_at(*nv, home);
        const std::uint16_t rw = nav_region_at(*nv, tree);
        if (rh != kNavNoRegion && rw != rh) return ecs::kNoCell;
    }
    return tree;
}

// ОПРЕДЕЛЕНЫ НИЖЕ, А СПРАШИВАЮТСЯ ЗДЕСЬ — ВИДОМ рейса: законы дороги живут
// рядом со своей торговой секцией, а дверь к ним обязана стоять ДО классов,
// которые её проносят модулю. Переносить определения вверх значило бы
// разорвать секцию с её шрамами.
float march_days_(const TickContext& ctx, std::uint32_t a, std::uint32_t b,
                  float moveMult);
std::uint32_t route_fear_(const TickContext& ctx, std::uint32_t a,
                          std::uint32_t b);

// Приёмник-МЕСТО (CANON S10): склад + счёт — дверь прихода гасит долг СРАЗУ
// тем, что упало. Сумки в Depot не заворачиваются (неявная конверсия из
// Inventory&, долга нет).
inline Depot depot_(std::uint16_t slot, const MacroWorld& mw) {
    MacroStore& st = store_mw(mw);
    return Depot(st.inventory[slot].inv, st.upkeep[slot].needDebt);
}

}  // namespace

// ── ОКНО МОДУЛЯ: ТЕЛА ВИДА И ПУЛЬТА ─────────────────────────────────────
// Объявления и законы каждого метода — в окне
// (`src/macro/squad_ai/module_window.h`), которое включает всякий модуль;
// здесь только тела. Окно знает мир через конверт думки, и тела его
// дверей поэтому живут рядом с самим миром ИИ, а не в модуле.

AiView::AiView(const TickContext& ctx) : ctx_(ctx) {}

int AiView::map_w() const { return ctx_.mapW; }

std::uint32_t AiView::home(MacroHandle self) const {
    return home_cell(self, ctx_);
}

std::uint16_t AiView::home_slot(MacroHandle self) const {
    return home_rooted_slot(self, ctx_);
}

std::uint32_t AiView::tree_near(std::uint32_t at, std::uint32_t home) const {
    return find_home_tree(ctx_, at, home);
}

std::uint32_t AiView::field_near(FeatureType parcel, std::uint32_t at,
                                 std::uint32_t home) const {
    return find_home_field(ctx_, at, home, parcel);
}

std::uint32_t AiView::fauna_near(std::uint32_t at, std::uint32_t home) const {
    return find_home_fauna(ctx_, at, home);
}

std::uint32_t AiView::vein_near(std::uint32_t home) const {
    return find_home_deposit(ctx_, home);
}

int AiView::yield_at(ResourceFieldId row, std::uint32_t cell) const {
    return field_yield_ordinal(ctx_.mw, row, cell);
}

RowSide AiView::row_of(std::uint16_t slot, int itemIdx) const {
    const ItemDef* d = item_def_at(itemIdx);
    return side_row(store_ctx(ctx_), slot, itemIdx, d ? d->value : 0);
}

int AiView::field_read(ResourceFieldId row, std::uint32_t cell) const {
    return resource_field_read(ctx_.mw, row, cell);
}

bool AiView::within(std::uint32_t a, std::uint32_t b, int cells) const {
    return cell_within(a, b, cells, ctx_.mapW);
}

const std::int32_t* AiView::need_account(std::uint16_t slot) const {
    if (!ctx_.mw.store || slot == kMacroNoSlot) return nullptr;
    return ctx_.mw.store->upkeep[slot].needDebt;
}

int AiView::coin_faction(std::uint16_t slot) const {
    if (!ctx_.mw.store || slot == kMacroNoSlot) return -1;
    return faction_or_freefolk(
        std::int16_t(ctx_.mw.store->kind[slot].factionIdx));
}

std::uint16_t AiView::rooted_slot(int id) const { return rooted_slot_by_id(ctx_, id); }

std::uint32_t AiView::rooted_id(std::uint16_t slot) const {
    return store_ctx(ctx_).spawnId[slot].index;
}

std::uint32_t AiView::rooted_cell(std::uint16_t slot) const {
    return store_ctx(ctx_).cell[slot].idx;
}

bool AiView::is_settlement(std::uint16_t slot) const {
    return rooted_is_settlement(
        SquadType(store_ctx(ctx_).runtime[slot].squadType));
}

std::uint16_t AiView::goal_rooted(const ecs::SquadGoal& g) const {
    return ecs::goal_kind(g.what) == ecs::GoalKind::Squad
        ? rooted_slot_by_id(ctx_, int(ecs::goal_ordinal(g.what)))
        : kMacroNoSlot;
}

bool AiView::owes_tithe(std::uint16_t rootedSlot) const {
    return sm::owes_tithe(store_ctx(ctx_), rootedSlot);
}

std::uint16_t AiView::owing_vassal(std::uint16_t suzerainSlot) const {
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

bool AiView::has_child(MacroHandle self, SquadType t) const {
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

int AiView::children_here(MacroHandle self,
                          std::uint16_t (&slots)[kMaxInterests]) const {
    int n = 0;
    const MacroStore& st = store_ctx(ctx_);
    if (!st.valid(self)) return n;
    const Interests& in = st.interests[self.slot];
    for (int i = 0; i < kMaxInterests; ++i) {
        const Interest& it = in.slots[i];
        if (it.stance == std::uint8_t(Stance::None)) break;
        if (it.stance != std::uint8_t(Stance::Child)) continue;
        const MacroHandle ch =
            macro_handle_by_spawn_id(st, std::uint32_t(it.object));
        if (!st.valid(ch) || st.dead[ch.slot] != 0) continue;
        if (st.runtime[ch.slot].state != std::uint8_t(NS::Idle)
            || st.cell[ch.slot].idx != st.cell[self.slot].idx) {
            continue;
        }
        slots[n++] = ch.slot;
    }
    return n;
}

bool AiView::has_sibling(MacroHandle self, SquadType t) const {
    const MacroStore& st = store_ctx(ctx_);
    const std::uint16_t homeSlot = home_rooted_slot(self, ctx_);
    return homeSlot != kMacroNoSlot
        && has_child(handle_at(st, homeSlot), t);
}

std::int64_t AiView::march_ticks(std::uint32_t a, std::uint32_t b,
                                 float moveMult) const {
    const float days = march_days_(ctx_, a, b, moveMult);
    return days < 0.0f ? -1
                       : std::int64_t(days * float(kTicksPerDay));
}

std::uint32_t AiView::route_fear(std::uint32_t a, std::uint32_t b) const {
    return route_fear_(ctx_, a, b);
}

SquadType AiView::type_of(std::uint16_t slot) const {
    return SquadType(store_ctx(ctx_).runtime[slot].squadType);
}

const Inventory& AiView::bag_of(std::uint16_t slot) const {
    return store_ctx(ctx_).inventory[slot].inv;
}

std::int64_t AiView::free_back_g(std::uint16_t slot) const {
    const MacroStore& st = store_ctx(ctx_);
    const Inventory& hold = st.inventory[slot].inv;
    return carry_capacity_g(st.runtime[slot], hold) - inventory_mass_g(hold);
}

AiPult::AiPult(const TickContext& ctx) : ctx_(ctx) {}

ActSelf AiPult::act_self(AiSelf& s) {
    return ActSelf{s.pools, s.rt, s.sheet, s.bag};
}

int AiPult::gather(AiSelf& s, ResourceFieldId row, std::uint8_t action,
                   std::uint32_t cell, int want) {
    const ActionDef& act = action_def(int(action));
    ActSelf self = act_self(s);
    if (!squad_act_ready(self, act)) return 0;
    MacroWorld mw = ctx_.mw;  // the envelope, whole — never a
                              // partial re-pick of its layers
    const int got = field_harvest(mw, row, cell, want, s.h.slot);
    if (got > 0) charge_squad_act(self, act, got);
    return got;
}

int AiPult::craft(AiSelf& s, int outIdx, int batches) {
    ActSelf self = act_self(s);
    return squad_craft(self, outIdx, batches);
}

std::int64_t AiPult::act_cost(AiSelf& s, std::uint8_t action,
                              std::int64_t volume) const {
    return squad_act_cost(act_self(s), action_def(int(action)), volume);
}

void AiPult::hand(std::uint16_t from, std::uint16_t to, int defIdx, int n) {
    if (!ctx_.mw.store || from == kMacroNoSlot || to == kMacroNoSlot
        || defIdx < 0 || n <= 0) {
        return;
    }
    DealLine lot[1];
    lot[0] = DealLine{};
    lot[0].want = n;
    lot[0].def = std::uint16_t(defIdx);
    (void)deal(store_ctx(ctx_), from, to, lot, {},
               kNoCarryLimitG, kNoCarryLimitG);
}

void AiPult::hand(std::uint16_t from, std::uint16_t to,
                  std::span<DealLine> lot, std::int64_t roomG) {
    if (!ctx_.mw.store || from == kMacroNoSlot || to == kMacroNoSlot
        || lot.empty()) {
        return;
    }
    (void)deal(store_ctx(ctx_), from, to, lot, {}, roomG, kNoCarryLimitG);
}

int AiPult::rand(int n) { return rand_int(ctx_, n); }

std::uint32_t AiPult::rand_nearby(std::uint32_t around, int radius) {
    return cell_nearby(around, radius, ctx_);
}

void AiPult::fact(AiSelf& s, FactKind kind, std::uint32_t cell, int amount,
                  int object) {
    MacroStore& st = store_ctx(ctx_);
    record_rooted_fact(st, *ctx_.mw.gs, kind, parent_of(st, s.h.slot),
                       cell_x(cell, ctx_.mapW), cell_y(cell, ctx_.mapW),
                       amount, object);
}

void AiPult::nomad(AiSelf& s) { ai_nomad(s.h, s.rt, s.pools, ctx_); }

void AiPult::home_wander(AiSelf& s) {
    ai_home_wanderer(s.h, s.rt, s.pools, ctx_);
}

CaravanDeal AiPult::trade_at(AiSelf& s, std::uint16_t market) {
    MacroStore& st = store_ctx(ctx_);
    return trade_caravan_at_station(
        st, s.h.slot, carry_capacity_g(s.rt, s.bag), market);
}

int AiPult::next_station(std::uint32_t at, int currentId, int prevId,
                         float moveMult) {
    return pick_next_station_(ctx_, at, currentId, prevId, moveMult);
}

void AiPult::dump_bag_home(AiSelf& s, std::uint16_t homeSlot) {
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

void AiPult::collect_tithe(AiSelf& s, std::uint16_t vassal,
                           std::uint16_t homeSlot) {
    MacroStore& st = store_ctx(ctx_);
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
    // Чего дому не хватает — сколько не оплачено по его счёту сверх полки,
    // тем же ответом, каким нужду дома читает сделка (`side_row`: запас
    // минус счёт). Строка без счёта (сырьё) нуждой не бывает.
    if (owed > 0) {
        // ПОРЯДОК НУЖДЫ — ПО ТОМУ, ВО СКОЛЬКО ДОМ ЦЕНИТ НЕДОСТАЮЩИЙ ЛОТ
        // (`lot_take` его строки на всю нехватку), а НЕ по плотности.
        // Измерено 2026-09-22: с плотностью еда стоит последней (она
        // самая дешёвая на килограмм), долг кончался на серебре, и
        // город продолжал голодать при работающем сборщике
        // (food_city −99.4 %). Оценка дома уже несёт срочность —
        // непокрытая нужда сама дорожает, — поэтому ноль новых правил.
        int order[kCommodityCount];
        std::int64_t lack[kCommodityCount];
        std::int64_t urgency[kCommodityCount];
        for (int c = 0; c < kCommodityCount; ++c) {
            order[c] = c;
            const int cid = commodity_item_index(c);
            const ItemDef* cd = item_def_at(cid);
            const int cbase = cd ? cd->value : 0;
            const RowSide home = side_row(st, homeSlot, cid, cbase);
            lack[c] = home.net < 0 ? -home.net : 0;
            urgency[c] = lack[c] > 0 && cbase > 0 ? lot_take(home, lack[c])
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
            if (lack[c] <= 0) continue;
            const long long affordable = owed / base;
            if (affordable <= 0) continue;
            const int want = int(std::min<long long>(lack[c], affordable));
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
                             cell_x(s.cell.idx, ctx_.mapW),
                             cell_y(s.cell.idx, ctx_.mapW),
                             int(std::min<long long>(took, 1 << 30)),
                             parent_of(st, s.h.slot));
    }
}

FeatureType AiPult::reshape_feature(MacroHandle self, FeatureType want) {
    if (!ctx_.mw.gs || !ctx_.mw.terrain || !ctx_.mw.store)
        return FT_None;
    const MacroStore& st = store_ctx(ctx_);
    if (!st.valid(self)) return FT_None;
    return feature_put(ctx_.mw.gs->features, *ctx_.mw.terrain,
                       st.cell[self.slot].idx, want);
}

MacroHandle AiPult::spawn(AiSelf& s, SquadType bornType, int crewSouls) {
    if (crewSouls <= 0 || !ctx_.mw.gs || !ctx_.mw.terrain)
        return MacroHandle{};
    MacroStore& st = store_ctx(ctx_);
    if (count_human_souls(s.bag) < crewSouls) return MacroHandle{};
    const std::uint16_t slot = s.h.slot;
    SquadSpec spec{};
    spec.avatarType = NPCType::Peasant;
    spec.x = ecs::cell_x(st.cell[slot], ctx_.mapW);
    spec.y = ecs::cell_y(st.cell[slot], ctx_.mapW);
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

namespace {

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

// (Здесь жил предикат застревания рейса — «позиция не сменилась, марш сказал
// REFUSED, планка свежа», — и спрашивали его артель и корован, каждый у себя.
// СНЕСЁН M-2 КАК СПЯЩИЙ: флаг REFUSED ставит одна ветка ядра марша, и она
// недостижима с 2026-10-06 — некролог у `kMarchArrived@src/bus/squad_row.h`.)

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
int pick_next_station_(const TickContext& ctx, std::uint32_t at,
                       int currentId, int prevId, float moveMult) {
    if (!ctx.mw.gs) return -1;
    const NavWorld* nv = ctx.mw.nav;
    const std::size_t R = nv ? nv->regionRootedId.size() : 0;
    const bool baked = nv && nv->baked();
    const std::uint16_t here =
        baked ? nav_region_at(*nv, at) : kNavNoRegion;
    // ДНИ ДО КАНДИДАТА — одна мера решения (та же, что у road_days_ в
    // аукционе): цена пути, когда мир запечён, хорда — когда дорог ещё нет.
    // В одну сторону: крю идёт ТУДА и едет дальше, а не возвращается.
    // Отрицательное = пути нет (kNavFar), и молчаливой подмены геометрией в
    // запечённом мире не бывает — кандидат просто выбывает.
    // ОДНО НАПИСАНИЕ МЕРЫ НА ВЕСЬ МИР (M-6 закрыт 2026-11-08). Здесь лежала
    // ТРЕТЬЯ копия арифметики «цена → дни» — своя ветка запечённости, своя
    // хорда, свой делитель 16, — и была она ЧАСТНЫМ СЛУЧАЕМ `march_days_`:
    // «от точки стояния до кандидата». Частный случай зовёт общий, а не
    // пишет его заново (DOD п.6); цена — лишняя разыменовка `nav` на
    // кандидата, которых в урне единицы.
    const auto days_to_ = [&](std::uint32_t to) -> float {
        return march_days_(ctx, at, to, moveMult);
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
        const float days = days_to_(stp.cell[slot].idx);
        if (days < 0.0f) return;              // пути нет — не кандидат
        const float w = 1.0f / (1.0f + days);
        total += w;
        bool take = pickId < 0;   // первый кандидат берёт урну целиком
        if (!take && ctx.rng) {
            const float roll =
                float(rand_int(ctx, 1 << 20)) / float(1 << 20);
            take = roll * total < w;
        }
        if (take) pickId = int(stp.spawnId[slot].index);
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
                || feature_number_read(*ctx.mw.gs, stp.cell[slot].idx) <= 0)
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
            || feature_number_read(*ctx.mw.gs, stp.cell[slot].idx) <= 0)
            continue;
        offer_(slot);
    }
    return pickId;   // -1 = тупик: рейс кончается, крю идёт домой
}

// (ЗДЕСЬ ЖИЛА «ЦЕНА ДАЛЬНЕГО РЫНКА» — дверь, отвечавшая абсолютной
// стоимостью строки ВСЕМ И ВСЕГДА. Единственным её читателем была заявка
// будущего плеча рейса, и умерла она вместе с ней 2026-11-12 (M-478). Знание
// о дальних ценах — отдельная система, поле цены над тором, ряд M-479.)

namespace {

// Вес страха на маршруте: threat худшей округи маршрута >> shift — минусом
// из прибыли плеча (те же монеты против той же ценности рейса). Стартовая
// четверть — крутилка дубль-прогона: полный вес после любой резни морил бы
// округу голодом дольше, чем горюет летопись.
constexpr int kThreatFearShift = 2;

// ДНИ МАРША МЕЖДУ ДВУМЯ ТОЧКАМИ, В ОДНУ СТОРОНУ (CANON S7, та же дверь
// nav_path_cost, что у аукциона; хорда — только в мире без дорог). Рейсу со
// станции нужны ноги ОТ МЕСТА СТОЯНИЯ, а лямбда аукциона считает от дома —
// это одна мера с двумя концами, а не второй закон.
// ДВЕ ПРИЧИНЫ РАЗВЕДЕНЫ, И ЭТО ВТОРАЯ ПОЛОВИНА ВЕРДИКТА. Здесь стоял ОДИН
// гейт `cells < 0`, и он сливал «мир не запечён» (ПРИБОР не готов — хорда
// честна) с «пути нет» (ОТВЕТ мира — хорда есть ложь). Сегодня вторая ветка
// ДРЕМЛЕТ: замер четырёх миров (сиды 12345/7/2026/777) дал `noRegion` 0 и
// недостижимых пар округ 0 из 21 309 962 — вода перестала быть стеной, тор
// замощён целиком. Но дремлющая ложь просыпается молча, а разведение стоит
// четырёх строк, поэтому разведено сейчас.
float march_days_(const TickContext& ctx, std::uint32_t a, std::uint32_t b,
                  float moveMult) {
    const float perDay = march_cells_per_day(moveMult);
    if (!(perDay > 0.0f)) return -1.0f;   // стоячий ходок никуда не дойдёт
    const NavWorld* nv = ctx.mw.nav;
    if (nv && nv->baked()) {
        const std::uint32_t c = nav_path_cost(*nv, a, b);
        if (c == kNavFar) return -1.0f;   // ПУТИ НЕТ — ответ мира, не хорда
        return (float(c) / 16.0f) / perDay;
    }
    // МИР НЕ ЗАПЕЧЁН — прибора ещё нет (фикстуры и окно до первого свипа),
    // и путь по ровной решётке здесь честная оценка, а не подмена ответа:
    // та же дверь пути, которой судится «кто ближе» (полушаги → клетки).
    return float(cell_dist(a, b, ctx.mapW)) / float(kStepStraight) / perDay;
}
// СТРАХ МАРШРУТА — В МОНЕТАХ И ЦЕЛЫМ: угроза худшей округи между двумя
// клетками, взвешенная сдвигом. Ноль = поле угрозы не запечено либо клетка
// вне округ. (Отвечала флоатом от целого сдвига — ставка симуляции во флоате
// без единой дробной части; читатель один, и он считает в целых.)
std::uint32_t route_fear_(const TickContext& ctx, std::uint32_t a,
                          std::uint32_t b) {
    NavWorld* nv = ctx.mw.nav;
    if (!nv || !nv->baked() || nv->threat.empty()) return 0;
    const std::uint16_t ra = nav_region_at(*nv, a);
    const std::uint16_t rb = nav_region_at(*nv, b);
    if (ra == kNavNoRegion || rb == kNavNoRegion) return 0;
    return threat_on_route(*nv, ra, rb) >> kThreatFearShift;
}

// (ЗДЕСЬ ЖИЛА «ЗАЯВКА СДЕЛКИ» — оценка БУДУЩЕГО плеча рейса, — И УМЕРЛА ОНА
// 2026-11-12 (M-478) ВМЕСТЕ С РУЛЕТКОЙ «ДАЛЬШЕ ПРОТИВ ДОМОЙ». Она мерила
// выгоду ценами ДОМА (что дома дорого, чего дому не хватает), а чужой рынок
// оценивала абсолютной стоимостью строки — то есть не видела его вовсе.
// Замысел владельца: цель корована — стоимость СВОЕГО инвентаря, а возврат
// решается ПРЕДЕЛЬНОЙ ОТДАЧЕЙ уже сделанного плеча (`caravan_think` ниже):
// ожидание от следующего плеча есть прибыль последнего, и гадать о
// несделанном больше некому.)

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

// (ЗДЕСЬ ЖИЛА `plan_home_load_` — погрузка ребёнка родителем, звавшаяся
//  глаголом окна `AiPult::load_child`: что и сколько отдать ребёнку, решало
//  окно. Это логика ОДНОГО модуля, и 2026-11-13 она уехала целиком в модуль
//  города (`city_load_child_@src/macro/squad_ai/city.cpp`) и копией в модуль
//  столицы (`capital_load_child_@src/macro/squad_ai/capital.cpp`), ряд
//  M-499 (8). Её прежняя шапка «едет то, что ДОМА ДЕШЕВЛЕ БАЗЫ» описывала
//  закон, умерший с единой ценомоделью, и с ней не поехала.)

}  // namespace

void ai_nomad(MacroHandle self, ecs::MacroNpcRuntime& rt,
              [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    const MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    // «Не тот город, где был» — прежний ординал несёт сама цель (род Squad).
    // Ординал чужой погони городом не окажется: пространство ординалов одно,
    // и урна ниже сверяет его с ординалами ГОРОДОВ.
    const int lastRooted = ecs::goal_kind(g.what) == ecs::GoalKind::Squad
                              ? int(ecs::goal_ordinal(g.what))
                              : 0;
    if (rt.state == std::uint8_t(NS::Idle)) {
        // Дошёл до города — постоял (прибытие назвали ноги).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 15)) * kAiTicks));
        }
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
                                  ecs::goal_what(ecs::GoalKind::Squad,
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
    // Traveling: в пути решать нечего — везут ноги свипа.
}

namespace {

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

constexpr int kSquadSightCells = 6;

// ВОСПРИЯТИЕ — свойство сквада, не спец-механика (владелец 2026-09-03,
// CANON S10: «это контекстно … из свойств самого сквада и ролевой системы»).
// Phase 6: the ROLE layer arrived — the avatar's Scouting rank widens the
// base radius by THE skill law (the row's pctPerRank, through the table
// door — never an inline percent), read off the runtime's cached rank
// (refresh_leader_travel_stats, the one refresh door) because this query
// runs per think. A rankless squad sees the v1 constant exactly.
// ОТВЕТ — ПУТЬ В ПОЛУШАГАХ (единица двери `cell_dist@src/core/torus.h`):
// «вижу» значит «в пределах пути в столько клеток», и процент выучки
// умножает его целой арифметикой. Здесь стоял флоат «клетки зрения», который
// возводился в квадрат и сравнивался с квадратом евклидова расстояния.
inline int squad_sight_way(const ecs::NPCKind&,
                           const ecs::MacroNpcRuntime* rt) {
    const int rank = rt ? int(rt->scoutRank) : 0;
    return kSquadSightCells * kStepStraight
         * skill_mult_pct_of(SkillId::Scouting, rank) / 100;
}

// Flee when the enemy is this many times stronger; trait-scaled below.
constexpr float kBraveryBase   = 1.5f;
constexpr float kBraveryCoward = 0.6f;   // Cowardly: breaks far earlier
constexpr float kBraveryBrave  = 1.8f;   // Brave: stands into worse odds
// Pursue only fights the law says we win with margin.
constexpr float kPursueMargin  = 1.1f;
// Как далеко убегает испугавшийся: клеток пути прочь от врага («a screen's
// worth of cells out» — балансное число, прежде литерал `8.0f` в формуле).
constexpr int kFleeCells = 8;

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

MacroHandle nearest_hostile_squad(MacroHandle self, const ecs::NPCKind& kind,
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
    //
    // ВРАЖДА ДВУХ СКВАДОВ — МАТРИЦА НАРОДОВ ПЛЮС ИХ ЛИЧНАЯ СВЯЗЬ (вердикт
    // владельца 2026-11-12, дверь `squads_hostile@src/macro/squad.h`): здесь
    // спрашивали одну матрицу, и договор двух сквадов рефлексу был не виден.
    // Цена — обход реестра связей СВОЕЙ строки до первой пустой записи; у
    // ходячего сквада их единицы.
    const FactionState& fx = ctx.mw.gs->factions;
    const std::uint32_t at = st.cell[self.slot].idx;
    const int cx0 = cell_x(at, ctx.mapW) / b.cellSize;
    const int cy0 = cell_y(at, ctx.mapW) / b.cellSize;
    int best = squad_sight_way(kind, &st.runtime[self.slot]) + 1;
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
                // Враждебность СПЕРВА: гейт и расстояние суть КОНЪЮНКЦИЯ,
                // порядок ответа не меняет (мир вышел побитово тем же), а
                // дешёвый вопрос вперёд дорогого снимает ~12 торовых
                // расстояний со звонка до единиц.
                if (!squads_hostile(st, fx, self.slot, sw.slot)) continue;
                const auto& oc = st.cell[sw.slot];
                const int d = cell_dist(at, oc.idx, ctx.mapW);
                if (d >= best) continue;
                best = d;
                found = handle_at(st, sw.slot);
            }
        }
    }
    return found;
}

}  // namespace

// Returns true when the threat consumed this think (fled, pursued or
// fought); the role behaviour then waits for a calmer half hour.
bool squad_threat_step(MacroHandle self, const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                       [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.squads || !ctx.mw.gs) return false;

    const MacroHandle enemy = nearest_hostile_squad(self, kind, ctx);
    MacroStore& st = store_ctx(ctx);
    if (!st.valid(enemy)) {
        // Threat gone: a fleeing squad calms down and resumes its life.
        // Бежать больше некуда и незачем — адрес бегства брошен.
        if (rt.state == std::uint8_t(NS::Fleeing)) {
            ecs::goal_drop(st.goal[self.slot]);
            rt.state = std::uint8_t(NS::Idle);
        }
        return false;
    }

    const std::uint32_t at = st.cell[self.slot].idx;
    const std::uint32_t foeAt = st.cell[enemy.slot].idx;
    const float myPower = squad_power(auto_battle_side_of(st, self));
    const float theirPower = squad_power(auto_battle_side_of(st, enemy));

    // The geometric meeting: same macro cell = the fight happens, resolved
    // by the ONE law and settled through the ONE ledger. An ambush is a
    // pursuer catching a squad that never saw it coming.
    if (at == foeAt) {
        // A player-controlled squad's meetings belong to the forced-encounter
        // door (Inc 6, main.cpp detect_forced_encounter): the squad stands ON
        // the meeting cell and the door opens the pre-battle screen — never
        // the silent auto-resolve. ОДИН слот: второй (дом) ушёл с `.home`
        // (ломоть 7б M-421) — брошенный сквад под контролем решает свои бои
        // так же, как всякий сквад мира.
        if (enemy.slot == ctx.playerFlagSlot) return true;
        if (cell_is_leased(ctx.mw.session, st.cell[self.slot].idx,
                           ctx.mapW)) return false;
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
        if (!macro_dead(st, self)) {
            // После боя — стоять. Кто ГНАЛСЯ, тот свою погоню кончил: её
            // предикат умирает с ней (`chase_release` выше, M-468). Кого бой
            // застал в его собственном деле, тот бросает один адрес, а
            // предикат цел (`goal_drop@src/bus/squad_row.h`) — роль
            // перерешит по нему (рынок корована после стычки).
            if (rt.state == std::uint8_t(NS::Chasing))
                chase_release(st.goal[self.slot]);
            else
                ecs::goal_drop(st.goal[self.slot]);
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
        // БЕЖАТЬ ПРЯМО ПРОЧЬ НА kFleeCells КЛЕТОК ПУТИ — целой арифметикой
        // над индексами. Смещение «от врага ко мне» растягивается так, чтобы
        // его путь (та же дверь, что судит «кто ближе») стал равен kFleeCells
        // клеткам; клетки разные (встреча обработана выше), значит путь не
        // ноль и делить есть на что.
        //
        // ЧТО ЗДЕСЬ СТОЯЛО: единственная по-настоящему дробная координата
        // макромира — пара флоатов, рукописная складка «больше полмира —
        // вычесть мир» на каждой оси, `sqrt` и нормировка, а итог тут же
        // резался `int()` (и к нулю, а не вниз — у шва тора точка бегства
        // уезжала на клетку). Рефлекс пишет ТУ ЖЕ колонку цели, что и роль, —
        // бегство и есть перерешение цели, второго носителя «куда» нет;
        // везут его общие ноги той же думкой.
        const CellDelta away = cell_delta(foeAt, at, ctx.mapW);
        const int way = cell_dist(foeAt, at, ctx.mapW);
        const int reach = kFleeCells * kStepStraight;
        ecs::goal_set(st.goal[self.slot],
                      cell_step(at, away.dx * reach / way,
                                away.dy * reach / way, ctx.mapW),
                      0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Fleeing);
        return true;
    }

    // Fighters close in when the law says they win: the same data column
    // that decides subworld combat stance decides who is a fighter at all.
    const bool fighter = combatant_behaviour(kNpcTypeDefs[kind.type].ai);
    if (fighter && myPower > theirPower * kPursueMargin) {
        // ПОГОНЯ ЕСТЬ ЦЕЛЬ РОДА `Squad`, И БОЛЬШЕ НИЧЕГО (M-307): рефлекс
        // называет, ЗА КЕМ; адрес каждую думку пересчитывают ноги свипа —
        // модуль рода не читает. Настигнув, сквад остаётся Chasing:
        // засаду выше судит именно это состояние в думку встречи.
        rt.state = std::uint8_t(NS::Chasing);
        ecs::goal_set(st.goal[self.slot], st.cell[enemy.slot].idx,
                      ecs::goal_what(ecs::GoalKind::Squad,
                                     st.spawnId[enemy.slot].index),
                      0, std::uint8_t(NS::Chasing));
        return true;
    }

    if (rt.state == std::uint8_t(NS::Fleeing)) {
        ecs::goal_drop(st.goal[self.slot]);
        rt.state = std::uint8_t(NS::Idle);
    }
    return false;
}

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

void scent_squad_deposit(MacroHandle self, const ecs::NPCKind& kind, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.store) return;
    ScentField& sf = ctx.mw.gs->scent;
    const int f = int(kind.factionIdx);
    if (f < 0 || f >= kFactionCount) return;   // kNoFaction не следит
    const MacroStore& st = store_ctx(ctx);
    const float power = squad_power(auto_battle_side_of(st, self));
    // ОБЕ ПОЛОВИНЫ КОНТЕЙНЕРА ОДНОЙ ДВЕРЬЮ (M-292): души и имущество суть
    // один вопрос «сколько тут добра», и складывать их вручную значило бы
    // держать второй ответ (здесь стояла `creatures_worth`).
    const std::uint32_t worth = std::uint32_t(
        std::max(0, container_worth(st.inventory[self.slot].inv)));
    // След ложится в СВОЮ клетку — колонку сквада (M-462: позицию
    // параметром больше не передают, второго носителя нет).
    const std::uint32_t at = st.cell[self.slot].idx;
    scent_deposit(sf, f, cell_x(at, sf.w), cell_y(at, sf.w),
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
        scent_squad_deposit(h, st.kind[slot], ctx);
    };
    // След кладёт АКТИВНЫЙ. Дубль прохода по дому ушёл с `.home`
    // (ломоть 7б M-421): компенсировать стало нечего.
    put(ctx.playerFlagSlot);
}

// (Крутилки охоты — kHuntBoldShift и kHuntScentFloor — в npc_ai.h: их
// читает тест и вертит дубль-прогон.)
bool scent_hunt_step(MacroHandle self, const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                     [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.gs || !ctx.mw.store) return false;
    if (!combatant_behaviour(kNpcTypeDefs[kind.type].ai)) return false;
    const ScentField& sf = ctx.mw.gs->scent;
    if (!sf.sized_for(ctx.mapW, ctx.mapH)) return false;
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
    for (int b = 0; b < kFactionCount; ++b)
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
        for (int b = 0; b < kFactionCount; ++b) {
            if (!(hostile & (1ull << b))) continue;
            outStr += scent_strength_at(sf, b, x, y);
            outWea += scent_wealth_at(sf, b, x, y);
        }
    };

    const std::uint32_t at0 = store_ctx(ctx).cell[self.slot].idx;
    std::uint32_t hereStr = 0, hereWea = 0;
    sniff(cell_x(at0, sf.w), cell_y(at0, sf.w), hereStr, hereWea);

    // Строго ВВЕРХ по цене: лучший из восьми соседей, что богаче моей клетки,
    // не ниже пола и не страшнее моей смелости. Нет такого — следа нет,
    // думает роль (подъём заканчивается на локальном максимуме сам, и это
    // и есть конец погони: либо жертва в радиусе зрения — рефлекс выше уже
    // перехватил, — либо след остыл и артель возвращается к делу).
    std::uint32_t best = std::max(hereWea, kHuntScentFloor - 1u);
    int bdx = 0, bdy = 0;   // румб лучшего соседа; (0, 0) = следа нет
    // Сосед по запаху — шаг ИНДЕКСА над полем мира (cell_step, ЗАКОН АДРЕСА).
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
            bdx = ox;
            bdy = oy;
        }
    }
    if (bdx == 0 && bdy == 0) return false;
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
    //
    // АДРЕС — САМ ЛУЧШИЙ СОСЕД (M-461): охота называет клетку, которую
    // выбрала по градиенту, ноги туда и везут, а градиент перечитывается
    // следующей же думкой. Здесь стояло «две клетки по румбу» — удвоение
    // существовало только затем, чтобы перешагнуть порог посадки ног
    // (сосед адресом был для них «уже прибыл»), и уводило охотника за пик
    // следа, который он только что выбрал.
    ecs::goal_set(store_ctx(ctx).goal[self.slot],
                  cell_step(at0, bdx, bdy, sf.w), 0, 0,
                  std::uint8_t(NS::Idle));
    rt.state = std::uint8_t(NS::Chasing);
    return true;
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

// РОД → МОДУЛЬ: диспетч есть ТАБЛИЦА по ординалу SquadType, не ветвление
// (ломоть 4 M-72; образец формы — kGenKindRows@src/sub/gens/dispatch.cpp).
// nullptr = «у рода думки нет»: его день идёт своим проходом
// (settle_rooted_day) — это ДАННЫЕ строки, а не гейт по архетипу (шапка
// squad_type.h: архетип законен КЛЮЧОМ таблиц и ЗАПРЕЩЁН ГЕЙТОМ в проходе
// симуляции — прежний гейт M-90 шага 5 умер в эту колонку).
using AiModuleFn = void (*)(MacroHandle, ecs::MacroNpcRuntime&,
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
// ПУЛЬТ. Обещать ему ноги значило бы обещать марш трупу.
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
constexpr AiModuleRow kAiModuleRows[] = {
    { SquadType::None,      ai_untyped,   nullptr       },
    // СТОЛИЦА — СВОЙ МОДУЛЬ (вердикт владельца 2026-11-13: «столица
    // отдельный модуль»). Здесь стояла функция города двум строкам с доводом
    // «это данные»; вердикт его снял: один модуль на два типа запрещён так
    // же, как общий помощник (CANON «ЧТО РАЗБИЕНИЕ ОБНАЖИЛО И КУДА ЭТО
    // ИДЁТ»). Тело столицы сегодня — полная копия городского, и копия
    // законна (AGENTS §4 п.5); различие пишется в её файле.
    { SquadType::Capital,   ai_capital,   capital_death },
    { SquadType::City,      ai_city,      city_death    },
    { SquadType::Village,   ai_village,   village_death },
    { SquadType::Spire,     nullptr,      nullptr       },
    { SquadType::Ruin,      nullptr,      nullptr       },
    { SquadType::Lair,      nullptr,      nullptr       },
    { SquadType::Shrine,    nullptr,      nullptr       },
    { SquadType::Mine,      nullptr,      nullptr       },
    { SquadType::Tower,     nullptr,      nullptr       },
    { SquadType::Artel,       ai_artel,       nullptr },
    { SquadType::Ploughmen,   ai_ploughmen,   nullptr },
    { SquadType::Hunters,     ai_hunters,     nullptr },
    { SquadType::Flaxers,     ai_flaxers,     nullptr },
    { SquadType::Miners,      ai_miners,      nullptr },
    { SquadType::Woodcutters, ai_woodcutters, nullptr },
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

// ОДИН МОДУЛЬ — ОДИН ТИП, ПОД КОМПИЛЯТОРОМ (вердикт владельца 2026-11-13,
// CANON «ЧТО РАЗБИЕНИЕ ОБНАЖИЛО И КУДА ЭТО ИДЁТ»: один модуль на два типа
// запрещён так же, как общий помощник). Ни думка, ни смерть не стоят в двух
// строках таблицы; `nullptr` — значение «род не думает / не пишет», и
// повтором оно не является. Прежде здесь строка `Capital` смотрела на
// функцию города — теперь такая строка не собирается.
template <std::size_t N>
constexpr bool each_module_is_own(const AiModuleRow (&rows)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (rows[i].module != nullptr && rows[i].module == rows[j].module)
                return false;
            if (rows[i].death != nullptr && rows[i].death == rows[j].death)
                return false;
        }
    }
    return true;
}
static_assert(each_module_is_own(kAiModuleRows),
              "kAiModuleRows: a module (think or death) serves ONE type — a "
              "new type writes its own module file, never reuses a "
              "neighbour's row");

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
void dispatch(MacroHandle e, ecs::MacroNpcRuntime& rt,
              ecs::Pools& pools, const TickContext& ctx) {
    const std::size_t row = std::size_t(rt.squadType);
    const AiModuleFn module =
        row < std::size(kAiModuleRows) ? kAiModuleRows[row].module : nullptr;
    // БЕЗ МОДУЛЯ ТЕЛО НЕ ДУМАЕТ ВООБЩЕ — его день идёт своим проходом; это
    // по-прежнему правда, и правдой осталась ЦЕЛИКОМ: строка `nullptr` есть
    // честное «род не думает».
    if (module == nullptr) return;
    module(e, rt, pools, ctx);
}

} // namespace

// ── СДЕЛКА СКВАДА НА РЫНКЕ — МОДУЛЬ КОРОВАНА НАД ЯДРОМ БАРТЕРА ───────────
// (владелец 2026-08-30: «караван приходит в город и НА МЕСТЕ смотрит, что
// выгодно продать и купить» — locality, no omniscience, no rumours.)
//
// ЧТО ЗДЕСЬ РЕШАЕТ МОДУЛЬ, А ЧТО ЯДРО (единая ценомодель, 2026-11-12, слова
// владельца — CANON S25). Цель сквада — растить СВОЮ ОЦЕНКУ своего инвентаря,
// и модуль решает одно: ОТДАТЬ ВСЁ, ЧТО РЫНОК ЦЕНИТ ВЫШЕ МЕНЯ, И ВЗЯТЬ ВСЁ,
// ЧТО Я ЦЕНЮ ВЫШЕ РЫНКА, — с наценкой стоящего. Рынок здесь СТОЯЩИЙ, купец
// ПРИШЕДШИЙ (форма цены 2026-11-13, ряд M-492): взятое у рынка стоит его
// оценку плюс его долю от неё, отданное ему он засчитывает по своей оценке.
// Сколько единиц едет, почём идёт лот и чем за него платят — отвечает ядро
// (`take_lot@src/macro/economy.h`, `give_lot@src/macro/economy.h`,
// `stage_payment@src/macro/deal.h`); обе стороны читаются одной дверью
// `side_row@src/macro/deal.h`. Сделка чисто локальна: судится только рынок,
// на котором сквад СТОИТ, — ни дома, ни слухов, ни рода торговца (торговец
// есть СЛОТ стора).
//
// Отсюда, без единой ветки: в нужду рынка купец отдаёт груз дороже своей
// оценки; завал рынка (излишек, забивший спину его жителей) берёт дешевле,
// чем ценит сам, пока своя спина свободна; рынку без монеты отдаёт за его
// излишек и везёт тот дальше. На малой разнице оценок слабый купец не
// торгует вовсе — наценка рынка её съедает; большую разницу берёт и он. Круг
// «отдал — забрал» пришедшему в убыток самим законом цены, поэтому ни правила
// «строка за визит в одну сторону», ни границы лота по нужде здесь нет.

namespace {
// ОДНА НОГА БАРТЕРА: `giver` отдаёт `taker` до `cap` единиц строки и получает
// за них встречную корзину по закону цены. Ногу предлагает МОДУЛЬ одной из
// сторон (`actorIsGiver`) — он ПРИШЕДШИЙ, другая сторона СТОЯЩАЯ, — и
// исполняется нога, только если ЕГО оценка своего инвентаря от неё растёт:
// цель сквада — своя оценка, а нога без прибыли стоила бы ему восстановления
// даром. `giverFreeG` — свободная спина отдающего (kNoCarryLimitG у склада):
// в оплату он принимает её и место, которое освободит сам отданный груз.
struct LegFact {
    int units = 0;                // сколько единиц строки уехало
    std::int64_t price = 0;       // во что стала корзина оплаты в счёте стоящего
    std::int64_t giverGain = 0;   // прирост оценки у отдающего
    std::int64_t takerGain = 0;   // прирост оценки у берущего
};
LegFact barter_leg_(MacroStore& st, std::uint16_t giver, std::uint16_t taker,
                    int itemIdx, int base, std::int64_t cap,
                    std::int64_t giverFreeG, bool actorIsGiver) {
    LegFact out{};
    if (cap <= 0 || base <= 0) return out;
    const RowSide g = side_row(st, giver, itemIdx, base);
    const RowSide t = side_row(st, taker, itemIdx, base);
    // Стоящий — тот, к кому пришли: не тот, чей модуль предложил ногу.
    const int standingPower = side_trade_power(st, actorIsGiver ? taker : giver);
    const int visitorPower = side_trade_power(st, actorIsGiver ? giver : taker);
    const auto room_for = [&](std::int64_t units) {
        return giverFreeG >= kNoCarryLimitG
            ? kNoCarryLimitG : giverFreeG + units * g.unitMassG;
    };
    constexpr std::int64_t kNoLimit = std::int64_t(1) << 60;
    DealLine pay[64];
    StagedPayment sp{};
    std::int64_t units = 0;
    if (actorIsGiver) {
        // ОТДАЮ СТОЯЩЕМУ. Он засчитывает лот по своей оценке и платит своими
        // единицами по цене взятия — не больше засчитанного. Недоплатить он
        // может по двум причинам, и лечатся они по-разному: ему НЕЧЕМ
        // расплатиться — тогда выгоднее отдать меньше, столько, сколько
        // покрывает уплаченное; либо оплата не помещается в МОЮ спину — тогда
        // меньший лот освободит ещё меньше места, и резать его значит терять.
        // Различает их сама цель сквада: пробую меньший лот и беру его,
        // только если МОЯ оценка от этого выросла. Ниже лежит всегда пара
        // «лот и корзина, собранная под него»; проба, не улучшившая оценку,
        // отбрасывается целиком. (Здесь стояло «режь, пока засчитанное выше
        // уплаченного» — ревью дифа 2026-11-13: при оплате, упёршейся в
        // спину, лот таял в геометрической прогрессии, и на страже числа
        // заходов уезжал лот меньше того, под который собрана корзина.)
        std::int64_t tryUnits =
            give_lot(g, t, standingPower, visitorPower, cap, kNoLimit).units;
        std::int64_t best = 0;   // прирост моей оценки на лучшей пробе
        DealLine tryPay[64];
        // Лот с каждой пробой строго меньше; страж числа проб — с запасом.
        for (int pass = 0; pass < 64 && tryUnits > 0; ++pass) {
            const StagedPayment got = stage_payment(
                st, taker, giver, lot_take(t, tryUnits), kNoLimit,
                room_for(tryUnits), Standing::Giver, tryPay);
            const std::int64_t gain = got.takerWorth - lot_loss(g, tryUnits);
            if (gain <= best) break;
            best = gain;
            units = tryUnits;
            sp = got;
            std::copy(tryPay, tryPay + got.lines, pay);
            const std::int64_t covered =
                cover_lot(g, t, tryUnits, got.price).units;
            if (covered >= tryUnits) break;
            tryUnits = covered;
        }
        if (units <= 0) return out;
    } else {
        // БЕРУ У СТОЯЩЕГО. Корзина собирается под цену взятия; не покрыла —
        // лот режется до того, что я способен дать, и корзина собирается
        // заново. Потолок плательщика — во сколько Я ценю то, что беру.
        LotQuote lot =
            take_lot(g, t, standingPower, visitorPower, cap, kNoLimit);
        for (int pass = 0; pass < 64 && lot.units > 0; ++pass) {
            sp = stage_payment(st, taker, giver, lot.price,
                               lot_take(t, lot.units), room_for(lot.units),
                               Standing::Taker, pay);
            if (sp.price >= lot.price) break;
            lot = take_lot(g, t, standingPower, visitorPower, cap, sp.price);
        }
        if (lot.units <= 0 || sp.price < lot.price) return out;
        units = lot.units;
    }
    const std::int64_t giverGain = sp.takerWorth - lot_loss(g, units);
    const std::int64_t takerGain = lot_take(t, units) - sp.giverWorth;
    if ((actorIsGiver ? giverGain : takerGain) <= 0) return out;
    // ТОВАР И ОПЛАТА — ОДНА СДЕЛКА (ломоть 2 M-72): exact-лоты,
    // недоплаченная поставка невыразима.
    DealLine goods[1];
    goods[0] = DealLine{};
    goods[0].want = int(units);
    goods[0].def = std::uint16_t(itemIdx);
    goods[0].exact = 1;
    const DealFact f =
        deal(st, giver, taker, goods,
             std::span<DealLine>(pay, std::size_t(sp.lines)),
             kNoCarryLimitG, kNoCarryLimitG);
    if (!f.settled) return out;
    out.units = int(units);
    out.price = sp.price;
    out.giverGain = giverGain;
    out.takerGain = takerGain;
    return out;
}
}  // namespace

CaravanDeal trade_caravan_at_station(MacroStore& st,
                                     std::uint16_t traderSlot,
                                     std::int64_t capacityG,
                                     std::uint16_t marketSlot) {
    CaravanDeal out{};
    // Склад и счёт рынка — колонки его ТЕЛА (M-90 шаг 5); гашение долга
    // рынка приходом (CANON S10) — внутри двери deal.
    Inventory& hold = st.inventory[traderSlot].inv;
    Inventory& ms = st.inventory[marketSlot].inv;
    const auto free_back = [&] {
        const std::int64_t freeG = capacityG - inventory_mass_g(hold);
        return freeG > 0 ? freeG : std::int64_t(0);
    };
    // ── ОТДАЮ: всё, что рынок ценит выше меня. Первым — потому что отданный
    // груз освобождает спину под то, что я возьму.
    for (int i = 0; i < kCommodityCount; ++i) {
        const int id = commodity_item_index(i);
        const ItemDef* def = item_def_at(id);
        const int base = def ? def->value : 0;
        const int mine = hold.count_of(id);
        if (base <= 0 || mine <= 0) continue;
        const LegFact leg = barter_leg_(st, traderSlot, marketSlot, id, base,
                                        mine, free_back(),
                                        /*actorIsGiver=*/true);
        if (leg.units <= 0) continue;
        out.soldValue += int(leg.price);
        out.movedTableValue += base * leg.units;
        out.gain += leg.giverGain;
    }
    // ── БЕРУ: всё, что я ценю выше рынка, — его завал. ВЫГОДНЕЙШЕЕ НА ГРАММ —
    // ПЕРВЫМ: спина конечна, и купец грузит её тем, что несёт больше всего
    // прироста оценки на единицу массы. Мерится ЛОТ под всю свободную спину,
    // а не первая единица (ряд M-490: завал меньше шага оценки на одной штуке
    // не виден); выгода на массу — ДРОБЬЮ, а не флоатом
    // (`gain_denser@src/macro/economy.h`).
    struct Lot { int i; std::int64_t gain; std::int64_t lotG; std::int64_t massG; };
    Lot lots[std::size_t(kCommodityCount)];
    int lotCount = 0;
    const std::int64_t roomG = free_back();
    const int marketPower = side_trade_power(st, marketSlot);
    const int myPower = side_trade_power(st, traderSlot);
    for (int i = 0; i < kCommodityCount; ++i) {
        const int id = commodity_item_index(i);
        const ItemDef* def = item_def_at(id);
        const int base = def ? def->value : 0;
        if (base <= 0 || ms.count_of(id) <= 0) continue;
        const std::int64_t massG = item_mass_g(*def);
        const RowSide market = side_row(st, marketSlot, id, base);
        const RowSide mine = side_row(st, traderSlot, id, base);
        const std::int64_t units = gain_units(
            market, mine,
            std::min<std::int64_t>(ms.count_of(id), roomG / massG),
            /*strict=*/true, marketPower, myPower);
        // Выгода лота глазами купца: во сколько он ценит взятое минус цена
        // взятия (оплата — по номиналу; свою цену ей назовёт нога бартера).
        const std::int64_t gain = lot_take(mine, units)
            - take_price(market, units, marketPower, myPower);
        if (units <= 0 || gain <= 0) continue;
        lots[std::size_t(lotCount++)] = Lot{i, gain, units * massG, massG};
    }
    std::sort(lots, lots + lotCount, [](const Lot& a, const Lot& b) {
        return gain_denser(a.gain, a.lotG, b.gain, b.lotG);
    });
    for (int li = 0; li < lotCount; ++li) {
        const int id = commodity_item_index(lots[li].i);
        const int base = item_def_at(id)->value;
        const std::int64_t fit = free_back() / lots[li].massG;
        if (fit <= 0) break;            // спина полна — брать некуда
        const std::int64_t cap =
            std::min<std::int64_t>(ms.count_of(id), fit);
        const LegFact leg = barter_leg_(st, marketSlot, traderSlot, id, base,
                                        cap, kNoCarryLimitG,
                                        /*actorIsGiver=*/false);
        if (leg.units <= 0) continue;
        out.boughtValue += int(leg.price);
        out.movedTableValue += base * leg.units;
        out.gain += leg.takerGain;
    }
    return out;
}

// (ЗДЕСЬ ЖИЛА ВТОРАЯ СДЕЛКА — «рейс сбыта деревни»: продать ВЕСЬ груз, не
// глядя на цену, и на весь кошелёк купить то, чего не хватает ДОМУ, читая
// его склад и счёт. Снесена 2026-11-12 (M-478) замыслом владельца: цель
// корована — стоимость СВОЕГО инвентаря, и судит он рынок, на котором
// стоит. У мира осталась ОДНА сделка сквада на рынке — та, что выше; нужды
// мест обслуживаются ценой, а не знанием купца о чужом складе.)

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
    // Мешок носителя флажка решает ввод, не ИИ — слот из кэша сессии
    // (1е кластер 5), тег-чтение умерло.
    const MacroHandle flagH = player_flag_handle(mw.session);
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
    if (mw.store && mw.session) {
        if (const ecs::MacroCell* pc =
                player_flag_cell(*mw.session, *mw.store)) {
            ctx.playerCell = pc->idx;
        }
        // Store свипа — адресом в конверте (выставлен выше): поведения
        // читают колонки, не спрашивая ctx реестра на каждом think (1е).
        // Слоты игрока — резолв ОДИН раз на свип из кэша сессии
        // (1е кластер 5): распаковка двух полей, ноль сканов реестра.
        // Валидность спрашивается у store — фикстура без игрока несёт
        // сентинель, и оба слота честно остаются kMacroNoSlot.
        if (mw.gs && ctx.mw.store) {
            const MacroStore& st = *ctx.mw.store;
            const MacroHandle flag = player_flag_handle(mw.session);
            if (st.valid(flag)) ctx.playerFlagSlot = flag.slot;
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
    destroy_dead_macro_squads(st, gs, mw.session, &relay_swept_death_, &relay);
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
    // Свип не водит АКТИВНОГО — его ноги у ввода, и двух писателей у одной
    // записи не бывает. Второй слот (дом) ушёл с `.home` (ломоть 7б M-421,
    // вердикт «только за активным»): прочие сквады под контролем ведутся
    // свипом, как всякий сквад мира, — своей целью, а у стоящего её нет:
    // везти некуда.
    for (const SquadWalkEntry& sw : runtime.squadIndex.order) {
        const std::uint16_t slot = sw.slot;
        if (slot == ctx.playerFlagSlot) continue;
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
        // «Я СРАЖАЮСЬ» УГАСАЕТ ВРЕМЕНЕМ ДУМОК тем же моментом, что слив
        // гейта занятости строкой выше (ломоть 6 ЗАХВАТА M-419): снизу его
        // только ВЗВОДЯТ (`squad_mark_fighting` из двери урона), а гасит
        // ОДНА ДВЕРЬ с двумя звонящими — `squad_fighting_elapsed@src/bus/squad_row.h`,
        // ровно как у слива рековери строкой выше (здесь стояло «снятие —
        // одна эта точка», и это было неверно уже при M-412д: вторым
        // звонящим стали часы флажка, а правописание закона скопировалось —
        // ревизия 2026-11-08 свела его в дверь). «Бой кончился» есть
        // ОТСУТСТВИЕ удара: штамп, который перестали обновлять, тает сам
        // за kFightThinks думок.
        ecs::squad_fighting_elapsed(rt);

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
        // КЛЕТКА СТАРТА ДУМКИ. Позицию несёт ОДНА колонка (M-462): модуль
        // читает её, ядро марша и дверь прыжка пишут её сами. Здесь стояло
        // «decode → think in fractional scratch → encode»: клетка
        // разворачивалась в пару флоатов на стеке думки и сворачивалась
        // обратно после хода.
        const std::uint32_t startCell = cell.idx;
        // ЗЕМЛЯ ЭТОГО ЧАСА — ОДИН СЭМПЛ, И СНЯТ ОН ТАМ ЖЕ, ГДЕ ЕГО БЕРЁТ ТЕМП
        // (M-395): в КЛЕТКЕ СТАРТА думки, до поведения, тем же моментом, каким
        // снята сумка выше. Час мира считается по ОДНОЙ клетке — и скорость, и
        // цена обязаны говорить об одной и той же земле, иначе думка «из воды
        // на дорогу» платит по дороге (промах до ×31). Адрес назван СТАРТОМ, а
        // не «текущей клеткой», чтобы следующая правка порядка не сменила
        // смысл молча.
        const float groundWeight = cell_weight(ctx.mw.pathCost, startCell);
        // АРЕНДОВАН ЛИ СТАРТ ДУМКИ (ЗАХВАТ, ломоть 2 M-419) — один сэмпл,
        // тем же моментом, что земля часа строкой выше: вошедший в окно
        // СРЕДИ думки честно оплатил свой марш и станет захваченным только
        // со следующей.
        const bool captured =
            cell_is_leased(ctx.mw.session, startCell, gs.mapW);
        // ДУМКА, ЗАТЕМ НОГИ — ОБЕ У ДИСПЕТЧЕРА (M-2). Модуль читает, чем
        // ноги кончили ПРОШЛУЮ думку (прибыл ли), и решает: пишет
        // цель либо отказывается от адреса. Затем флаги марша гаснут — они
        // живут одну думку, — и ноги везут к цели, какой бы род её ни
        // написал: строка без модуля (`nullptr`) с целью в пути пойдёт так
        // же, гейта по подвижности нет ни здесь, ни в ядре марша.
        // В ЛАГЕРЕ ногам идти некуда и думки нет: гаснут только флаги хода,
        // а исход прошлого марша ЖДЁТ — его прочтёт первая думка после
        // подъёма (иначе артель, уснувшая у крыльца, проснулась бы без
        // своего «я пришёл» и не сдала бы груз).
        if (gate == ThinkGate::Think) {
            dispatch(h, rt, hp, ctx);
            rt.marchFlags = 0;
            // Бой внутри думки мог убить сквад — труп ноги не везут.
            if (st.dead[slot] == 0) march_goal(h, rt, ctx);
        } else {
            rt.marchFlags &= ecs::kMarchArrived;
        }
        // Шло ли тело в эту думку, ритм спрашивает у САМОГО ХОДА — у флага
        // `kMarchAdvanced`, который ядро марша ставит на первом оплаченном
        // кванте (M-465): драйвер ему этого больше не пересказывает.
        settle_march_rhythm(h, rt, hp, captured, ctx, overloadBurden,
                            groundWeight);
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
void try_move(ecs::MacroCell& cell, ecs::MacroNpcRuntime& rt,
              std::uint32_t goal, std::uint32_t quanta,
              const MarchGround& g, MarchRoute* route) {
    // (Гейт ног заминки боя — `if (rt.halted != 0)`, M-319A → M-418 — умер
    //  шагом 0 ЗАХВАТА (M-419): в окне 3×3 тело держит сам бой сцены, вне
    //  окна эмитентов у стопера не было.)
    //
    // ЛЕТУН (v93): воздух — его стихия, как вода у паруса. Море ему не
    // «недостижимо посуху», рельеф не платится, любая клетка держит — ОДИН
    // цикл марша, меняется только стихия (образец кораблей).
    const bool flying = rt.flying != 0;

    // КВАНТОВЫЙ ХОД (вердикт владельца 2026-11-02: «никаких флоатов всё
    // чисто инт», «дошол до 127 … и меняет клетку с другой стороны»).
    // Производство думки/кадра — целые кванты доли; банка между думками
    // умерла вместе с float-бюджетом: прогресс марша И ЕСТЬ доля rt.sub, а
    // кванты, которым некуда идти, сгорают.
    rt.marchFlags = 0;

    // БЮДЖЕТ — В ЕДИНИЦАХ ПУТИ (M-463): квант оси стоит `kStepStraight` на
    // прямом шаге и `kStepDiagonal` на диагональном — пара целых, одна на
    // всю игру (`src/core/torus.h`). Звонящий по-прежнему даёт бюджет квантами
    // ПРЯМОГО хода (254 на клетку); пара живёт внутри ядра.
    std::uint32_t budget = quanta * std::uint32_t(kStepStraight);

    // ИЗЪЯТИЕ НОГ ПО АРЕНДЕ (ЗАХВАТ, ломоть 2 M-419): в девяти клетках окна
    // позицию ведёт СУБМИР, fold сворачивает её на границе тика — макро-ноги
    // здесь отказывают ЦЕЛИКОМ: ни клетки, ни доли (доля тоже fold'а).
    // БЕЗ ФЛАГОВ: «ноги не наши» не есть «мир не пускает», а цели
    // захваченных ЖИВУТ (думка идёт, M-313). Ритму «тело стоит» скажет
    // параметр captured свипа, не флаг.
    if (cell_in_lease_frame(g.leaseCenter, cell.idx, g.side)) return;

    while (budget > 0) {
        std::uint32_t next = ecs::kNoCell;
        if (route != nullptr) {
            // «ШАГ ЗАДАН» (M-412в): маршрут A* игрока исполняется БУКВАЛЬНО
            // — какие клетки выбрал поиск, те ноги и оплачивают; походка
            // ядра спит. Вейпоинт своей же клетки — пропуск, не смена.
            if (route->idx >= route->count) break;
            const PathPoint& wp = route->pts[route->idx];
            next = cell_of(wp.x, wp.y, g.side);
            if (next == cell.idx) {
                ++route->idx;
                continue;
            }
        } else {
            if (cell.idx == goal) break;
            // ЗАПЕЧЁННАЯ ПОХОДКА (CANON S7, 2026-09-02): округи + порталы +
            // граф — три чтения, ни волны, ни поиска. Жадный шаг остаётся
            // миру без запечённого слоя. Гейт «стоит на суше» снят
            // 2026-10-06 вместе со стеной: после единой заливки у КАЖДОЙ
            // клетки тора есть округа — походка отвечает и из воды.
            if (!flying && g.nav && g.nav->baked())
                next = nav_step(*g.nav, cell.idx, goal);
            if (next == ecs::kNoCell) {
                // ЖАДНЫЙ ШАГ — закон дальних маршей вне округ (Session 21,
                // owner's choice over per-squad A*): среди восьми соседей
                // берутся те, что СТРОГО БЛИЖЕ к цели по пути
                // (`cell_dist@src/core/torus.h`), и из них — самый дешёвый
                // НА ЕДИНИЦУ ПРИБЛИЖЕНИЯ: цена ребра, делённая на то,
                // насколько шаг сократил путь. Шаги разной длины иначе не
                // сравнить (M-463): диагональ дороже прямого в полтора раза,
                // но и приближает на три полушага против одного-двух — по
                // голой цене ребра ходок предпочёл бы короткие шаги и шёл
                // лесенкой. Прямой сосед в сторону цели — кандидат
                // БЕЗУСЛОВНО и выигрывает ничьи: ровный мир идётся по прямой,
                // берег обходится, потому что вода дорога (суша 2–5×, вода
                // 10×), а река без дешёвого обхода переходится вброд по
                // честной цене. O(8) на клетку.
                //
                // ШАГ ЕСТЬ ВСЕГДА, И ВЕТКИ «ШАГА НЕТ» ЗДЕСЬ БОЛЬШЕ НЕТ: стены
                // умерли 2026-10-06 (M-236), и прямой сосед в сторону цели
                // существует у всякой клетки тора, пока клетка не цель.
                // Стоявшая здесь ветка с флагом «ноги отказали» была
                // недостижима пять недель; в индексной форме она невыразима,
                // и флаг ушёл вместе с ней (ряд M-458).
                const int dHere = cell_dist(cell.idx, goal, g.side);
                next = cell_toward(cell.idx, goal, g.side);
                const std::uint32_t straight = next;
                float bw = edge_weight(g.pathCost, cell.idx, next, g.side,
                                       flying);
                int bgain = dHere - cell_dist(next, goal, g.side);
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) continue;
                        const std::uint32_t n =
                            cell_step(cell.idx, ox, oy, g.side);
                        if (n == straight) continue;
                        const int gain = dHere - cell_dist(n, goal, g.side);
                        if (gain <= 0) continue;   // only steps that progress
                        const float w = edge_weight(g.pathCost, cell.idx, n,
                                                    g.side, flying);
                        // w / gain < bw / bgain — без деления.
                        if (w * float(bgain) < bw * float(gain)) {
                            next = n;
                            bw = w;
                            bgain = gain;
                        }
                    }
                }
            }
        }

        // РУМБ ШАГА — знак смещения «отсюда → туда» короткой стороной тора
        // (шаг на восток через шов есть +1, а не −(side−1)).
        const CellDelta step = cell_delta(cell.idx, next, g.side);
        const int dx = (step.dx > 0) - (step.dx < 0);
        const int dy = (step.dy > 0) - (step.dy < 0);

        // ЦЕНА ПЕРЕСЕЧЕНИЯ — доехать долей до края по каждой активной оси;
        // оси едут ВМЕСТЕ, и квант оси стоит по роду шага: прямой —
        // `kStepStraight`, диагональный — `kStepDiagonal`. Клетка есть
        // квадрат земли, её диагональ длиннее стороны — и ноги платят за это
        // той же парой, какой путь меряет дверь `cell_dist` и какой маршрут
        // выбирают планировщики (M-463, вердикт владельца 2026-11-12).
        // (Здесь стояло «диагональ стоит те же 2×127 кванта, что и прямой
        // ход»: по диагонали сквад шёл на 41 % быстрее, чем то же тело идёт
        // по той же земле в субмире.)
        const int costX =
            dx != 0 ? ecs::kCellSubEdge - dx * int(rt.sub.x) : 0;
        const int costY =
            dy != 0 ? ecs::kCellSubEdge - dy * int(rt.sub.y) : 0;
        const std::uint32_t rate = std::uint32_t(
            (dx != 0 && dy != 0) ? kStepDiagonal : kStepStraight);
        const std::uint32_t cost =
            std::uint32_t(costX > costY ? costX : costY) * rate;

        // Единицы уходят ДВЕРЬЮ drain (ЗАКОН ТРЁХ ДВЕРЕЙ): взять сколько
        // есть, факт честен — хватило на край или нет, решает возврат.
        const std::uint32_t spent = drain(budget, cost);
        if (spent < cost) {
            // Края не достигли: доля едет ВНУТРИ текущей клетки на целые
            // кванты оси (остаток меньше кванта сгорает; ось у своего края
            // стоит клампом), позиция мира не меняется — «сквад локально
            // хранит свою промежуточную позицию». ФЛАГ ХОДА — только если
            // проехан хотя бы квант: думка без движения маршем не является
            // (M-465).
            const int ride = int(spent / rate);
            if (ride > 0) {
                rt.marchFlags |= ecs::kMarchAdvanced;
                if (dx != 0) {
                    const int nx = int(rt.sub.x) + dx * ride;
                    rt.sub.x = std::int8_t(std::clamp(
                        nx, -ecs::kCellSubEdge, ecs::kCellSubEdge));
                }
                if (dy != 0) {
                    const int ny = int(rt.sub.y) + dy * ride;
                    rt.sub.y = std::int8_t(std::clamp(
                        ny, -ecs::kCellSubEdge, ecs::kCellSubEdge));
                }
            }
            break;
        }
        rt.marchFlags |= ecs::kMarchAdvanced;

        // ПЕРЕСЕЧЕНИЕ КРАЯ — ОДИН ФАКТ, ОДНА ЗАПИСЬ: клетка меняется,
        // активные оси входят с противоположного края, пассивная ось долю
        // СОХРАНЯЕТ, румб входа и часы клетки штампуются тем же действием.
        rt.sub = ecs::cell_sub_after_step(dx, dy, rt.sub);
        rt.entryDir = pack_entry_dir(dx, dy);
        rt.entryTicks = 0;
        cell.idx = next;
        if (route != nullptr) ++route->idx;

        // NOTHING IS CHARGED HERE, AND THAT IS THE LAW (2026-10-06). A step
        // has no price: what the body pays is the HOUR it spends on ground,
        // settled once per think by settle_march_rhythm (НПЦ) или ходом
        // макро-ветки главного цикла (игрок). «Pressing on is priced, never
        // gated» (CANON S14.1) holds on the macro march.

        // Never hop OVER the player's cell in a multi-cell think: the forced
        // encounter (Inc 6) is geometric, so the squad stops ON the meeting
        // cell where the door can see it. (Для марша САМОГО игрока и для
        // мира без игрока гейт спит значением: `kNoCell` клеткой не бывает.)
        if (cell.idx == g.playerCell) break;

        // ШАГ В АРЕНДУ — ПОСЛЕДНИЙ ШАГ ДУМКИ (ЗАХВАТ, ломоть 2 M-419):
        // вошедший в окно встаёт на ПЕРВОЙ арендованной клетке и
        // захватывается следующим слепком — пролёт насквозь за одну думку
        // был бы сквадом в окне, которого слепок не воплотил.
        if (cell_in_lease_frame(g.leaseCenter, cell.idx, g.side)) break;
    }

    // ПРИБЫЛ — ДОЕЗЖАЕТ В ЦЕНТР (M-375, вторая половина хода): пересечение
    // оставило тело на краю клетки-цели, остаток квантов доводит его
    // внутрь. Это не марш (флагов и жжения не добавляет — тело уже дома).
    // Для маршрута «цель» — его исчерпание: тот же доезд закрывает и
    // прежнюю idle-ветку игрока «маршрута нет, а доля не дома» (M-375/Б5).
    const bool goalDone = route != nullptr
        ? route->idx >= route->count
        : cell.idx == goal;
    if (goalDone && budget > 0u) {
        ecs::cell_sub_walk_home(rt.sub,
                                budget / std::uint32_t(kStepStraight));
    }
}


// (tick_macro_npc_visuals жил здесь и умер с M-244a: глайд карты — чистое
// ПРЕДСТАВЛЕНИЕ, и его интегратор переехал в кэш глаз оверлея —
// tick_macro_eyes@src/ui/macro_overlay.cpp; мир несёт клетку и целую долю
// CellSub, float-колонки вида в store больше нет.)

} // namespace sm

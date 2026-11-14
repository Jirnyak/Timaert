// Macroworld NPC AI: behaviour dispatch for persistent macro NPCs.
#pragma once
#include <algorithm>   // std::max — закон темпа марша ниже
#include <limits>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "core/rng.h"
#include "core/time.h"
#include "tables/behaviour.h"
#include "macro/macro_stock.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "bus/session.h"            // kNoWindowLease — сентинел центра аренды
#include "macro/spawners.h"
#include "tables/squad_type.h"
#include "macro/squad_index.h"
#include "bus/squad_walk.h"
#include "bus/entry_context.h"     // румб входа/часы клетки — двери марша ниже
#include "core/subtract.h"         // sub_sat — угасание заминки и рековери
#include "tables/movement_cost.h"  // закон темпа марша (march_cells_per_think)
#include "macro/tree_layer.h"

namespace sm {

struct DepositLayer;

// How often a macro NPC thinks, in WORLD TICKS (core/time.h) — not in wall
// seconds. On the map that is 32 ticks = half a real second, exactly what it
// always was. Underground it is half a second of WORLD time, which is eight
// real seconds, because the AI now rides the same clock as the economy, the
// calendar and the sun instead of keeping a timer of its own. A lord no longer
// crosses the continent while you clear one room, and the macro world costs
// kSubworldTickDivisor times less to simulate while you are down there.
inline constexpr std::uint32_t kAiTicks = 32;
static_assert(kTicksPerDay % kAiTicks == 0,
              "the AI cadence must divide the day");

// (КОНСТАНТА `kAiPeriodSeconds` УНИЧТОЖЕНА 2026-11-08 ВМЕСТЕ С M-425. Она
//  была последним числом макро-ИИ в РЕАЛЬНЫХ СЕКУНДАХ и держалась ровно одним
//  читателем — дверью `set_visual_speed`, делившей путь думки на этот период
//  ради колонки `visualSpeed`. Колонка оказалась величиной ПРЕДСТАВЛЕНИЯ
//  (читателей мира — ноль), темп переехал в кадровый кэш глаз и меряется там
//  реально прошедшим временем, а не предполагаемым периодом. Константа без
//  читателя не живёт (DOD п.9), и с её смертью «калибровать в реальных
//  секундах» стало в этом файле НЕВЫРАЗИМО, а не запрещено прозой.)

// GAME HOURS one AI think covers — the exchange rate that lets a squad pay
// and recover through the same per-game-hour laws the player uses
// (kRestRegenPctPerHour, kMacroWalkCellsPerHour). 24 × 32 / 8192 = 0.09375 h:
// 256 thinks make the day, exactly.
inline constexpr float kAiTickGameHours =
    24.0f * float(kAiTicks) / float(kTicksPerDay);

// kGathererReach moved to macro/resource_field.h (2026-09-16): the reach
// field the deposit layer stamps needs the SAME number, and deposit_layer.cpp
// cannot include this header — it drags EnTT into targets that do not link it.
// A constant two modules must agree on lives below both of them, never inside
// one of them.

// The automaton's CAMP threshold: legs below this fraction of the bar on
// campable ground pitch camp NOW, before any debt — an eighth is the margin
// a crew keeps to make camp at all. The BITE stays a law for whoever still
// chooses to march (the open sea, the player); without this decision the
// fleet lived camp-to-camp on chronic exhaustion and bled to death on the
// road — 447 caravans in 24 days, measured by the balance run 2026-08-30.
// Public so squad_travel_test derives its march anchor from the same number.
inline constexpr int kCampBarDivisor = 8;

struct TreeGrid {
    CellBuckets grid;
    const std::vector<TreePoint>* trees = nullptr;
};

void build_tree_grid(TreeGrid& g, const std::vector<TreePoint>& trees,
                     int mapW, int mapH, int cellSize = 32);

struct MacroNpcAiRuntime {
    Rng           jitter{0xA1F0u};
    // МЕТР-СВИДЕТЕЛЬ (M-379): оплаченных квантов думки за жизнь рантайма.
    // Читатели — смоук subworld_time и приборы каденса; в сейв не едет
    // (свидетельская обвязка, не состояние мира). Бюджетная машинерия
    // sweepAccum/pendingSweeps/sweepCursor и метры сохранения свипов умерли
    // с M-379: закон ритма ОДИН — петля по тикам внутри tick_macro_npc_ai,
    // фаза сквада копится честными тиками в rt.tickAccum, и терять приток
    // больше нечем — долга не существует.
    std::uint64_t thinksDone = 0;
    SquadIndex  squadIndex;         // rebuilt per drive; buckets reused
    // (Свой лист свипа `sweepOrder` умер с M-376: лист на драйв ОДИН —
    //  `squadIndex.order`, живые по ординалу; игрока свипы пропускают телом.)
};

void reset_macro_npc_ai_runtime(MacroNpcAiRuntime& runtime, std::uint32_t seed);

// The AI think's view of the world: THE layer envelope (macro_stock.h
// MacroWorld, CANON S6) plus the drive-state that is not a layer — the RNG,
// the player's position, the transient squad index and the resolution gate.
// Before the door (2026-08-24) this struct carried its own parallel copy of
// eight layer pointers, and the two drivers assembled those copies line by
// line, twice (canon-audit H2) — one of them once forgot `deposits` and every
// miner in the world stopped digging while the player was underground. The
// envelope is embedded, not copied: a layer exists here because it exists in
// the world, and a null layer reads as "no contribution" (fail-closed), so
// tests without a world stay as they were.
struct TickContext {
    MacroWorld      mw{};
    int             mapW = 0;
    int             mapH = 0;
    Rng*            rng = nullptr;
    // (ЗДЕСЬ СТОЯЛА ПАРА `float playerX/playerY` — клетка флажка в типе
    //  флоата. Она переехала индексом `playerCell` ниже, к слоту игрока:
    //  M-462, «никаких флоатов координат».)
    // Squad↔squad perception (Session 15). `mw.world` + `squads` let a
    // behaviour see the OTHER squads.
    //
    // ЗДЕСЬ СТОЯЛ `allowAutoBattle` — ГЛОБАЛЬНЫЙ ФЛАГ, И ОН УМЕР (M-226,
    // вердикт владельца 2026-10-06: «это плохо нельзя отклюячать автобой во
    // всём мире толкьо в этих 3х3 клетках»). Он гасил резолв встреч ВО ВСЁМ
    // МИРЕ, пока игрок внизу, — то есть ровно та форма, которую ЗАКОН ДВУХ
    // ТЕМПОВ п.5 называет признаком нарушения: «глобальный флаг,
    // выключающий систему макромира (вместо изъятия по девяти клеткам)».
    // Теперь изъятие адресуется КЛЕТКАМИ: встреча не решается наверху, если
    // её клетка В АРЕНДЕ у субмира (`cell_is_leased@src/bus/session.h`), а
    // за рамкой мир дерётся как всегда. Второго числа для этого не
    // понадобилось — аренда есть одно число сессии.
    const SquadIndex* squads = nullptr;
    // СЛОТЫ ИГРОКА, резолв один раз на свип (1е): «кем я на карте» (флажок,
    // вселение двигает) и родной сквад (зарезервированный ординал). Читатели
    // — гейты встреч и исключение свипа; до смерти тегов заполняются из
    // тегов (make_tick_context), после — из GameState. kMacroNoSlot = нет.
    std::uint16_t playerFlagSlot  = kMacroNoSlot;
    // КЛЕТКА ФЛАЖКА ИГРОКА — ИНДЕКС, И «ИГРОКА НЕТ» ЕСТЬ ЗНАЧЕНИЕ (M-462).
    // NOT a perception channel (owner, 2026-08-29: «игрок ничем не особенен»
    // — the player's squad sits in the SquadIndex like anyone's). The one
    // consumer is try_move's stop-ON-the-meeting-cell law: a multi-cell
    // march must not hop OVER the player, because the forced-encounter door
    // (Inc 6) is geometric and looks at his cell.
    // ДЕФЕКТ, УШЕДШИЙ С ТИПОМ: пара флоатов по умолчанию была (0, 0), и мир
    // без игрока останавливал марш всякого сквада на клетке (0,0) — «нет
    // игрока» и «игрок в углу тора» были одним значением. Сентинел
    // `kNoCell` клеткой не является, гейт спит значением.
    std::uint32_t playerCell = ecs::kNoCell;
    // ЗДЕСЬ СТОЯЛА `factionHostileMask[kMaxFactions]` — 512 Б запечённой
    // враждебности реестра, и она СНЕСЕНА (M-268, вердикт владельца
    // 2026-10-21: «отношения фракций однозначно динамические … ради полировки
    // минимум систем и чистоты лучше снести»). Копия отвечала за ДИНАМИЧЕСКУЮ
    // матрицу при ровно одном читателе, и скорости не покупала: строка
    // отношений `rows[f].rel` есть одна кеш-линия, а вопрошающий держит `f`
    // постоянной на всю думку. Враждебность спрашивается у матрицы —
    // `factions_hostile@src/macro/factions.h`, ординалами, всегда свежая.
};
// РАЗМЕР НАЗВАН — ЗНАЧИТ ПРИБИТ (DOD п.10: «у структуры, чей размер назван,
// размер закрепляется `static_assert(sizeof)`»). Эта форма была последней в
// макромире без пина, и пин здесь НЕ про байты: экземпляр один на тик и
// живёт на стеке, экономить нечего. Он про то же, что у `SquadKindRow` и
// `MacroNpcRuntime` — ЧТО СЮДА НЕЛЬЗЯ ДОЛИТЬ КОЛОНКУ МОЛЧА. Прецедент стоит
// в самом этом блоке: отсюда уже сносили `factionHostileMask[kMaxFactions]`
// (512 Б запечённой враждебности, M-268) и `allowAutoBattle` — глобальный
// флаг, гасивший автобой ВО ВСЁМ МИРЕ (M-226). Оба приехали тихо и уехали по
// вердикту; третьему приезду теперь нужно объяснение.
//
// ЧИСЛО СПРОШЕНО У КОМПИЛЯТОРА, А НЕ ВЗЯТО ИЗ ДОКУМЕНТА, И ЭТО ОКАЗАЛОСЬ НЕ
// ФОРМАЛЬНОСТЬЮ: `SKELETON.md` называет 160 Б у `MacroNpcAiRuntime`, а промт
// наряда — у `TickContext`, то есть одно число ходило по дереву как свойство
// ДВУХ РАЗНЫХ форм. Замер (clang, 2026-10-26): `TickContext` = 160 Б
// (`alignof` 8, из них `MacroWorld` 120), `MacroNpcAiRuntime` = ТОЖЕ 160 Б.
// Совпадение полное — значит «160 Б» эти две формы не различает вовсе, и
// ссылаться этим числом на одну из них нельзя.
//
// А `MacroNpcAiRuntime` ПИНА НЕ ПОЛУЧАЕТ, И ЭТО РЕШЕНИЕ, А НЕ ПРОПУСК: его
// `sizeof` несёт `std::vector` (бакеты и лист `SquadIndex`), то есть
// является свойством СТАНДАРТНОЙ БИБЛИОТЕКИ, а не нашей раскладки — такой
// пин прибил бы libc++ и покраснел бы на MinGW первой же сборкой. Здесь же
// каждое поле своё: указатели, `int`, `float`, `uint16` — число наше.
//
// ЧЕГО ЭТОТ СТРАЖ НЕ ВИДИТ — НАЗВАНО ЗАМЕРОМ, А НЕ УМОЛЧАНО (§8 п.6:
// детектор, не сказавший о своей слепоте, читается как всевидящий). Два
// слепых пятна, оба проверены мутацией:
//   · КОЛОНКА, ВЛЕЗАЮЩАЯ В ХВОСТОВУЮ ДЫРУ, ПРОХОДИТ МОЛЧА. Данных здесь 140
//     Б (`MacroWorld` 104 + два `int` + указатель + два `float` + указатель +
//     два `uint16`), а `sizeof` 144 при `alignof` 8 — значит в хвосте стоят
//     ЧЕТЫРЕ СВОБОДНЫХ БАЙТА, и дописанный `std::uint16_t` собрался ЗЕЛЁНЫМ.
//     Против этого стража нет, и выдумывать его числом нельзя: вопрос «зачем
//     колонка» решает DOD п.9 ревью, а не байты;
//   · ПЕРЕСТАНОВКА РАВНОШИРОКИХ ПОЛЕЙ ПРОХОДИТ МОЛЧА (`mapW`↔`mapH` —
//     собралось). Это пин РАЗМЕРА, а не раскладки; порядок полей судит
//     `-Wreorder-init-list` у строк-агрегатов, которым эта форма не является.
// 2026-11-03: 160 → 144. −16 Б наследованы от конверта: `MacroWorld` усох на
// КОЛОНКИ `econFacts`/`econFactsUser` (второй фактный путь мира снесён —
// `src/macro/econ_day.h`), своих полей вид думки не терял ни одного.
// 2026-11-10: 144 → 136. −8 Б снова наследованы от конверта: `MacroWorld`
// потерял указатель на лес (лес стал полем состояния мира, ломоть 3
// сейв-эпика), своих полей вид думки не терял. Данных теперь 132 Б, хвостовая
// дыра прежняя — четыре байта.
// 2026-11-10: ±0. Конверт `MacroWorld` получил адрес сессии (вердикт
// владельца В1 сейв-эпика) и потерял указатель на поле фич (M-456) — размер
// вида думки не двинулся, своих полей он не приобрёл и не терял.
// 2026-11-10: 136 → 144. +8 Б наследованы от конверта: `MacroWorld` получил
// адрес индекса летописи (`ChronicleLinks@src/macro/chronicle.h`, M-423) —
// выводные цепочки лежат вне состояния мира, и читатель окрестности находит
// их этим адресом. Своих полей вид думки не приобрёл; хвостовая дыра прежняя.
// 2026-11-12: 144 → 136. −8 Б СВОИ, и это первая убыль собственных полей
// вида: пара `float playerX/playerY` (8 Б, клетка флажка в типе флоата)
// стала одним индексом `playerCell` (4 Б, M-462) и встала к слоту игрока, в
// бывшую хвостовую дыру. Данных теперь 136 Б ровно — хвостовой дыры нет, и
// первое слепое пятно выше («колонка, влезающая в хвостовую дыру, проходит
// молча») закрылось само: влезать больше некуда.
static_assert(sizeof(TickContext) == 136,
              "вид думки на мир: 136 Б на стеке, один экземпляр на тик (не "
              "на сквад) — раскладка изменилась, назови КАКУЮ колонку и "
              "зачем (DOD п.9-10)");

// ── След и охота (scent_field.h, CANON S10 «хищник-жертва», 2026-09-03) ──
// Публично по прецеденту trade_caravan_at_station: тест водит один think.
// Смелость охоты: преследуем след силы ≤ моя сила × 2^shift. Величина следа
// = сила × время присутствия, не сила хозяина — остывший след занижает,
// ошибки консервативны, их ловит визуальный рефлекс при контакте (закон
// боя). Обе — крутилки дубль-прогона.
inline constexpr int kHuntBoldShift = 1;
// Пол запаха в единицах поля (квант kScentQuantShift): след беднее — не
// стоит и шага, иначе боец дёргается на каждую пылинку диффузии.
inline constexpr std::uint32_t kHuntScentFloor = 8u;
// Писатель полей следов — вклад сквада в свою клетку (сила = squad_power,
// цена = души по строкам найма + ценность груза); зовётся из dispatch на
// каждый think, для игрока — свипом (scent_player_deposit внутри драйверов).
// (ЗДЕСЬ ЖИЛ `MacroPos` — пара `float x, y` на стеке думки, «дробный
// скретч марша»: драйвер разворачивал клетку сквада в пару флоатов на входе
// и сворачивал обратно после хода. Дробным он не был ни разу — перепись
// 2026-11-12 нашла 26 мест рождения, все `float(cell_x(...))`, и сто
// преобразований `int(...)` обратно. Снесён M-462 по вердикту владельца
// «никаких флоатов координат … вся симуляция ИСКЛЮЧИТЕЛЬНО через индексы
// клеточек»: позицию сквада несёт ОДИН носитель — колонка `MacroCell`, её
// читает модуль и пишет ядро марша; второго носителя на время думки нет.)

void scent_squad_deposit(MacroHandle self,
                         const ecs::NPCKind& kind, const TickContext& ctx);
// Рефлекс охоты: незанятый боем combatant идёт ВВЕРХ по градиенту чужой
// ЦЕНЫ под фильтром СИЛЫ (след силы ≤ моя сила × 2^kHuntBoldShift); true =
// think съеден охотой, макроцель в rt не тронута (пауза, не амнезия).
bool scent_hunt_step(MacroHandle self,
                     const ecs::NPCKind& kind, ecs::MacroNpcRuntime& rt,
                     ecs::Pools& pools, const TickContext& ctx);

// ── ПАМЯТЬ МОДУЛЯ Dragon: СЛОВО 0 — ЛОГОВО ─────────────────────────────
// Логово — дом-КЛЕТКА для тех, чей дом не сквад (драконья гора): модуль рода
// Dragon кружит вокруг неё и возвращается. Лежит словом СВОЕЙ ПАМЯТИ МОДУЛЯ
// (`MacroNpcRuntime::mem@src/bus/squad_row.h`), и кодировку держит модуль:
// клетка + 1, потому что ноль у слова памяти значит «пусто», а клетка 0 —
// законный адрес. `ecs::kNoCell` = логова нет. Двери две и публичны затем,
// что логово пишет ещё и рождение (спавн анкеты называет вершину).
inline constexpr int kDragonMemLair = 0;
static_assert(kDragonMemLair < ecs::kModuleMemSlots);
inline std::uint32_t dragon_lair(const ecs::MacroNpcRuntime& rt) {
    const std::uint32_t word = rt.mem[kDragonMemLair];
    return word == 0 ? ecs::kNoCell : word - 1;
}
inline void dragon_set_lair(ecs::MacroNpcRuntime& rt, std::uint32_t cell) {
    rt.mem[kDragonMemLair] = cell == ecs::kNoCell ? 0 : cell + 1;
}

// ── ПАМЯТЬ МОДУЛЯ Caravan — ШЕСТЬ СЛОВ ───────────────────────────────────
// Слова своей памяти рейса (`MacroNpcRuntime::mem@src/bus/squad_row.h`):
// станция, с которой рейс пришёл (ординал, ноль = «никто»: рулетка соседей не
// гоняет его обратно на только что обслуженный рынок), и четыре числа правила
// возврата (`trip_goes_on@src/macro/economy.h`) — прибыль рейса (на сколько
// сделки рейса подняли оценку трюма по мерке самого купца), тики пройденного
// пути, отдача предыдущего плеча (за сутки пути: окно в два плеча) и число
// совершённых сделок (вера в среднюю растёт с каждой).
//   · `kCaravanMemWait` — сколько думок рейс ещё стоит дома, прежде чем снова
//     спросить «есть ли груз и куда ехать» (ноль — не ждёт). Ждать модуль
//     вправе только СВОЕЙ памятью: гейт восстановления заряжает одна
//     способность (вердикт владельца 2026-11-13, CANON S25 «ВЗАИМОДЕЙСТВИЕ —
//     НЕ СПОСОБНОСТЬ»; ряд M-494).
// Пишет и читает их один модуль — `caravan_think`; публичны номера затем,
// что свидетель обязан видеть, что память рейса действительно ведётся.
enum : int {
    kCaravanMemPrevStation,
    kCaravanMemTripGain,
    kCaravanMemTripTicks,
    kCaravanMemPrevLegRate,
    kCaravanMemDeals,
    kCaravanMemWait,
    kCaravanMemCount
};
static_assert(kCaravanMemCount <= ecs::kModuleMemSlots,
              "память рейса обязана лечь в кап своей памяти модуля");

// РАЗМЕР КРЮ — СКОЛЬКО ДУШ ДЕРЕВНЯ (`kVillageCrewSouls`), ГОРОД
// (`kCityCrewSouls`) И СТОЛИЦА (`kCapitalCrewSouls`) ВЫПИСЫВАЮТ В РОЖДАЕМЫЙ
// СКВАД; у каждого модуля своё имя, и равенство значений сегодня — совпадение
// трёх балансов, а не общее число (AGENTS §4 п.5: под модули общего не
// создаётся). (Число пришло от умершей
// квоты станков `kCreaturesPerCityWorker` — «одна рука на восемь душ»; квота
// снята 2026-11-13, а размер крю остался своим числом.)
//
// И ЭТО БАЛАНС, А НЕ АВТОРСКОЕ РЕШЕНИЕ (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ): прежний
// аукцион ротации выводил размер иначе — `пул / число живых строк`, где пул
// был `паства >> labourShift` (деревня 1/2, город 1/8), — и крю его жили
// РОВНО ОДИН день, распускаясь на рассвете. Здесь крю живёт, пока жив, значит
// калибровка у числа другая по построению; двигать его свободно.
// Публично затем, что свидетель обязан читать то же число, что и модуль.
inline constexpr int kVillageCrewSouls = 8;
inline constexpr int kCityCrewSouls = 8;
inline constexpr int kCapitalCrewSouls = 8;

// ── ПАМЯТЬ МОДУЛЯ Village — ТРИ СЛОВА ─────────────────────────────────────
// Гейт восстановления заряжает только способность (вердикт владельца
// 2026-11-13, CANON S25), поэтому всё прочее модуль держит своей памятью.
//   · `kVillageMemCraftSeen`, `kVillageMemCraftDebt` — «при каком
//     инвентаре и каком счёте нужды крафтить было нечего»: счётчик изменений
//     своего контейнера (`Inventory::revision@src/bus/anketa.h`, плюс один —
//     ноль у слова значит «не помню») и свёртка счёта. Пока оба не сдвинулись,
//     решение то же, и думка стоит одного сравнения; сдвинулись — модуль
//     решает заново на этой же думке. Таймера нет: привезли сырьё — деревня
//     видит его сразу (ряд M-497).
//   · `kVillageMemBirthWait` — сколько думок до следующего круга решений о
//     рождении: раз в сутки (`AiView::has_child` — холодный проход).
// Публичны номера затем же, зачем у рейса корована.
enum : int {
    kVillageMemCraftSeen,
    kVillageMemCraftDebt,
    kVillageMemBirthWait,
    kVillageMemCount
};
static_assert(kVillageMemCount <= ecs::kModuleMemSlots,
              "память деревни обязана лечь в кап своей памяти модуля");

// ── ПАМЯТЬ МОДУЛЯ City — ЧЕТЫРЕ СЛОВА ────────────────────────────────────
// Свои имена у города, а не общие с деревней и столицей (AGENTS §4 п.5).
//   · `kCityMemCraftSeen`, `kCityMemCraftDebt` — «при каком инвентаре и
//     каком счёте нужды крафтить было нечего»: счётчик изменений своего
//     контейнера (`Inventory::revision@src/bus/anketa.h`, плюс один — ноль у
//     слова значит «не помню») и свёртка счёта. Пока оба не сдвинулись,
//     решение то же, и думка стоит одного сравнения; сдвинулись — модуль
//     решает заново на этой же думке. Таймера нет (ряд M-497).
//   · `kCityMemBirthWait` — сколько думок до следующего круга решений о
//     рождении и погрузке: раз в сутки (`AiView::has_child` — холодный
//     проход).
//   · `kCityMemCollectorSeason` — ординал сезона, в который город последний
//     раз поднимал сборщика, плюс один (ноль — «не поднимал»; ординал сезона
//     мира — `AiView::season`, через год не повторяется): в одном сезоне сборщик поднимается один раз, и
//     замена погибшего посреди сезона не обходит первых вассалов второй раз
//     (ряд M-495).
enum : int {
    kCityMemCraftSeen,
    kCityMemCraftDebt,
    kCityMemBirthWait,
    kCityMemCollectorSeason,
    kCityMemCount
};
static_assert(kCityMemCount <= ecs::kModuleMemSlots,
              "память города обязана лечь в кап своей памяти модуля");

// ── ПАМЯТЬ МОДУЛЯ Capital — ЧЕТЫРЕ СЛОВА ─────────────────────────────────
// Столица — свой модуль (вердикт владельца 2026-11-13: «столица отдельный
// модуль»), и память у неё своя, а не общая с городом (AGENTS §4 п.5).
//   · `kCapitalMemCraftSeen`, `kCapitalMemCraftDebt` — «при каком инвентаре
//     и каком счёте нужды крафтить было нечего»: счётчик изменений своего
//     контейнера (`Inventory::revision@src/bus/anketa.h`, плюс один — ноль у
//     слова значит «не помню») и свёртка счёта. Пока оба не сдвинулись,
//     решение то же, и думка стоит одного сравнения; сдвинулись — модуль
//     решает заново на этой же думке. Таймера нет (ряд M-497).
//   · `kCapitalMemBirthWait` — сколько думок до следующего круга решений о
//     рождении и погрузке: раз в сутки (`AiView::has_child` — холодный
//     проход).
//   · `kCapitalMemCollectorSeason` — ординал сезона, в который столица
//     последний раз поднимала сборщика, плюс один (ноль — «не поднимала»;
//     ординал сезона мира — `AiView::season`, через год не повторяется): в одном сезоне сборщик поднимается один раз, и
//     замена погибшего посреди сезона не обходит первых вассалов второй раз
//     (ряд M-495).
enum : int {
    kCapitalMemCraftSeen,
    kCapitalMemCraftDebt,
    kCapitalMemBirthWait,
    kCapitalMemCollectorSeason,
    kCapitalMemCount
};
static_assert(kCapitalMemCount <= ecs::kModuleMemSlots,
              "память столицы обязана лечь в кап своей памяти модуля");

// ── ПАМЯТЬ МОДУЛЯ Collector — ДВА СЛОВА ──────────────────────────────────
// Обход вассалов дома раз в сезон, по одному за рейс (вердикт владельца
// 2026-11-14 «Пришёл — забрал 1/8, без книги долгов», ряд M-495).
//   · `kCollectorMemSeason` — ординал сезона текущего обхода плюс один (ноль
//     — обхода ещё не было; ординал сезона мира — `AiView::season`, через
//     год не повторяется);
//   · `kCollectorMemCursor` — сколько вассалов дома уже посещено в этом
//     обходе: следующая цель — вассал с этим номером (`AiView::vassal_at`).
// Пишет и читает их один модуль — `collector_think`; публичны номера затем,
// что свидетель обязан видеть, что обход действительно ведётся.
enum : int {
    kCollectorMemSeason,
    kCollectorMemCursor,
    kCollectorMemCount
};
static_assert(kCollectorMemCount <= ecs::kModuleMemSlots,
              "память сборщика обязана лечь в кап своей памяти модуля");

// ── ПАМЯТЬ МОДУЛЯ Nomad — ОДНО СЛОВО ─────────────────────────────────────
//   · `kNomadMemWait` — сколько думок бродяга ещё стоит, прежде чем выбрать
//     следующий город (ноль — не ждёт): постоял по прибытии либо идти некуда.
//     Ждать модуль вправе только СВОЕЙ памятью: гейт восстановления заряжает
//     одна способность (вердикт владельца 2026-11-13, CANON S25
//     «ВЗАИМОДЕЙСТВИЕ — НЕ СПОСОБНОСТЬ»; образец — `kCaravanMemWait`).
// Пишет и читает его один модуль — `nomad_think`; публичен номер затем, что
// свидетель обязан видеть, что ожидание бродяги ведётся его памятью.
enum : int {
    kNomadMemWait,
    kNomadMemCount
};
static_assert(kNomadMemCount <= ecs::kModuleMemSlots,
              "память бродяги обязана лечь в кап своей памяти модуля");
// Сутки в думках: думка идёт раз в `kAiTicks` мировых тиков.
inline constexpr std::uint32_t kAiThinksPerDay =
    std::uint32_t(kTicksPerDay / kAiTicks);

// ── ВЫБОР КРАФТА — РЕШЕНИЯ МОДУЛЕЙ Village, City И Capital ─────────────────
// Что и сколько крафтить, решает модуль (вердикты владельца 2026-10-22 п.4 и
// 2026-11-13, CANON S25); у деревни, города и столицы решение СВОЁ, тело
// каждого написано целиком, и копия у соседа законна (AGENTS §4 п.5). Здесь —
// все три решения на голых числах своей строки, публичные затем, что свидетель
// обязан судить их без мира. Лот с наибольшей выгодой «оценка выхода минус
// оценка входов» среди всего, на что у анкеты есть право; объём — не больше,
// чем кормят материалы и покупает бар `sp` при `units` рабочих юнитах.
// `outIdx` = -1 — крафтить нечего. Акт исполняет
// `squad_craft@src/macro/squad_act.h`.
struct CraftPick {
    int outIdx = -1;         // выходная строка каталога; -1 — нечего
    int batches = 0;         // партий
    std::int64_t gain = 0;   // на сколько лот растит оценку своего инвентаря
};
CraftPick village_craft_pick(const Inventory& bag,
                             const std::int32_t* needDebt,
                             const Skills& skills, std::int64_t backG,
                             int mintFactionIdx, int maxSp, int sp,
                             int units);
CraftPick city_craft_pick(const Inventory& bag,
                          const std::int32_t* needDebt,
                          const Skills& skills, std::int64_t backG,
                          int mintFactionIdx, int maxSp, int sp, int units);
CraftPick capital_craft_pick(const Inventory& bag,
                             const std::int32_t* needDebt,
                             const Skills& skills, std::int64_t backG,
                             int mintFactionIdx, int maxSp, int sp,
                             int units);

// ── СДЕЛКА СКВАДА НА РЫНКЕ (owner, 2026-08-30; CANON S10/S25) ────────────
// Локальность — закон: всякое решение читает рынок, на котором сквад СТОИТ, —
// ни всеведения, ни слухов. Цену, лот и встречную корзину называет ядро
// бартера (`src/macro/economy.h`, единая ценомодель 2026-11-12); модуль
// решает одно — отдать всё, что рынок ценит выше него, и взять всё, что он
// сам ценит выше рынка. На уровне пространства имён (образец
// settle_rooted_day), чтобы caravan_deal_test мог водить одну сделку.
struct CaravanDeal {
    int soldValue = 0;        // стоимость, уплаченная рынком за отданный груз
    int boughtValue = 0;      // стоимость, уплаченная рынку за взятое
    int movedTableValue = 0;  // стоимость всего сменившего руки по базе
                              //   строки (dealValue летописи, как прежде)
    // НА СКОЛЬКО ВЫРОСЛА ОЦЕНКА ТРЮМА ПО МЕРКЕ САМОГО КУПЦА — это и есть
    // прибыль сделки (цель сквада — своя оценка своего инвентаря): её судит
    // закон возврата рейса.
    std::int64_t gain = 0;
};
// Сила и спина каждой стороны читаются её дверями (`side_trade_power`,
// `side_row@src/macro/deal.h`): закон делит силой выгоду, и больше она не
// делает ничего. `capacityG` — спина купца: ни товара, ни оплаты сверх неё
// он не принимает (оплату — в свободную спину и в место отданного груза).
// Торговец — СЛОТ стора (ломоть 2 M-72): товар и оплата — exact-лоты одного
// коммита двери deal.
CaravanDeal trade_caravan_at_station(MacroStore& st,
                                     std::uint16_t traderSlot,
                                     std::int64_t capacityG,
                                     std::uint16_t marketSlot);
// (Второй сделки — «рейс сбыта деревни», читавшей склад и счёт ДОМА, —
// больше нет: снесена 2026-11-12, M-478. Сделка сквада на рынке одна.)

// ТИП СКВАДА живёт в tables/squad_type.h — его называют двое: сквад
// (колонкой) и СТРОКА РЕЕСТРА МЕСТА (объявляя, кого поднимает).

// ── ДВЕРЬ СТАНЦИИ: куда течёт товар отсюда ───────────────────────────────
// Рулетка по весу 1/(1+дни пути) среди жилых мест соседних округ (CANON S7,
// закон тора — расстояние меряется по кратчайшему пути на торе, а не по
// разнице координат). Объявлена здесь, потому что ЗАКОН обязан иметь
// свидетеля напрямую: прежде его пинали через ИИ каравана, и когда род
// каравана умер (2026-09-21), закон остался бы без свидетеля вовсе.
// prevId = -1 — станции «откуда пришли» нет.
// `moveMult` — ТЕМП СПРАШИВАЮЩЕГО из его анкеты: вес кандидата есть
// 1/(1 + дни), а дни считаются той же дверью, которой идут ноги
// (`march_cells_per_day` выше, M-6/M-436). До 2026-11-08 рулетка взвешивала
// кандидатов ОДНИМ глобальным темпом, то есть конный корован выбирал станцию
// по пешему расписанию.
// (Выходы `outX/outY` — клетка выбранной станции парой флоатов — сняты
// M-462: их не читал никто, клетку станции спрашивающий берёт по ординалу.)
int pick_next_station_(const TickContext& ctx, std::uint32_t at,
                       int currentId, int prevId, float moveMult);

// ── ДВЕРЬ ПОДЪЁМА ИЗ ЛАГЕРЯ — ОДНА НА ДВЕ ПРИЧИНЫ ВСТАТЬ ─────────────────
// Объявлена здесь по той же причине, что станция выше: ЗАКОН обязан иметь
// свидетеля НАПРЯМУЮ. Тройка строк подъёма стояла списком в двух местах
// `npc_ai.cpp` (SP добрал половину бара / земля перестала окупать лагерь) —
// два ответа на один вопрос мира (DOD п.6), из которых правка одного молча
// расходится с другим, — и у всей механики не было ни одного ассерта ни в
// одном тесте дерева (M-254, 2026-10-12). Условий по-прежнему два, ПОДЪЁМ
// один. Лагерь — ПАУЗА, НЕ АМНЕЗИЯ: возвращается ПРЕРВАННАЯ нога.
void wake_from_rest(ecs::MacroNpcRuntime& rt);

// (ЗДЕСЬ СТОЯЛА ДВЕРЬ «ЦЕНА ЧУЖОГО РЫНКА»: она отвечала абсолютной
// стоимостью строки всем и всегда, то есть дальнего рынка не видела вовсе.
// Единственный её читатель — заявка будущего плеча рейса — умер 2026-11-12
// (M-478), и дверь ушла с ним. Абсолютную стоимость строки даёт сама строка
// каталога (`ItemDef::value`); знание о дальних ценах — ряд M-479.)

// КЕМ ДУМАЕТ СКВАД БЕЗ ТИПА — лестница приоритетов, ЗАКОН (владелец,
// 2026-09-10): приказ (маршрут в SquadOrders — его наличие И ЕСТЬ приказ)
// > строка стола анкет по ординалу тега > строка КАТАЛОГА ТЕЛ. Поведение не
// хранится — выводится каждый think из данных на сущности; новая ступень =
// данные + одна строка в определении (npc_ai.cpp), никогда ветка в
// диспетче. Объявлена здесь ради свидетелей и будущих читателей лестницы.
//
// ЗВАЛАСЬ `effective_behaviour` и была ЕДИНСТВЕННЫМ ответом на «что делает
// этот сквад». С 2026-09-21 первый ответ даёт ТИП СКВАДА (SquadType выше), а
// эта лестница осталась ветке `None` — сквадам без макро-источника
// поведения. Имя сменилось, потому что прежнее врало о её роли: «effective»
// значило «окончательное», а окончательное теперь тип.
//
// ПОСЛЕДНЯЯ СТУПЕНЬ — ТЕЧЬ ГРАНИЦЫ МИРОВ (CANON S2): каталог тел принадлежит
// микромиру. Ступень умирает вместе с генезисными одиночками (порция Б-6),
// и тогда вся функция уходит целиком.
AIBehaviour untyped_squad_behaviour(const MacroStore& st, MacroHandle h,
                                    const ecs::NPCKind& kind);

// THE SQUAD SEASON WINDOW (owner 2026-08-30/31 + 2026-09-17; CANON S10,
// S19.2, реф M&B): board and pay are for the СУЩЕСТВ КОНТЕЙНЕРА only — the avatar is a
// SUBJECT and needs nothing by himself («0 бойцов = 0 хлеба и жалования» —
// why a lone rider is honestly immune to hunger). On the season BOUNDARY
// every squad with a creatures settles BOTH needs a season ahead, out of its
// OWN bag:
//   · board — РАЦИОН СВОЕЙ СТРОКИ в день на каждую душу контейнера, чей бы он
//     ни был (npc.h `npc_board_per_day`, 2026-09-22). Здесь стояло «одна
//     душа, чья строка на содержании (upkeepGoldPerDay >= 0; звери и
//     монстры — нет), со скидкой на ранг фуражирования ведущего» — и то и
//     другое снесено: рот теперь объявляет колонка рациона, а не колонка
//     платы, и скидки на счёт больше нет («один закон без исключений»).
//     Сам навык умер 2026-10-12 (M-144), так что второй ответ на этот
//     вопрос мира теперь НЕВЫРАЗИМ, а не просто не дан;
//   · pay — the one upkeep law × the season, and the paid coin BURNS into
//     the world loot pool («жалованье сгорает»).
// Each need is covered WHOLE or not debited at all; ANY miss bleeds an
// eighth of the creatures into the deserter pool ONCE per window («ВСЕ НУЖДЫ
// ДОЛЖНЫ БЫТЬ ПОКРЫТЫ, иначе потеря 1/8»). The judge is the SOLDIER'S own
// row, never the avatar's — the old avatar-typed gate was the player-special
// Adventurer.upkeep=0 door, dead by «игрок == нпц»: the player's squad pays
// here through the very same loop. Returns souls deserted.
int squad_season_window(GameState& gs, MacroStore& st, int day);

// Daily bag hygiene — the auto-scrap half of the old daily feed loop (CANON
// «Крафт/Скрап»: авто-скрап ИИ по порогу >50%; the player's bag is NEVER
// touched — his scrap is a manual act). Returns stacks melted.
int squad_bags_hygiene_daily(MacroWorld& mw);

// A creatures's SEASON of needs — the one arithmetic the boundary window bills
// by and the place's crew-loading fills by (drift between the two would
// be a second truth of содержание). Борд идёт по РАЦИОНУ СВОЕЙ СТРОКИ на
// каждую душу контейнера; жалованье — один закон содержания × сезон. (Здесь
// стояло «per soul whose own row is on upkeep, foraging-scaled by the avatar's
// effective sheet» — оба гейта умерли 2026-09-22 вместе с функцией, которую
// этот абзац описывал, а сам навык фуражирования — 2026-10-12, M-144.)
// БОРД и ПЛАТА — ровно две нужды армии, и это вердикт, а не упрощение
// (владелец 2026-09-18): горожанин судится по ВСЕЙ лестнице нужд, солдат —
// по харчу и жалованью. Поле звалось `bread` и тем самым делало литерал
// "bread" на месте списания «правильным на вид»; борд — это ГОЛОДНАЯ СТРОКА
// лестницы, какой бы она ни была (econ_day.h hunger_item_index).
//
// ЗДЕСЬ СТОЯЛИ `SquadSeasonNeeds` И `squad_season_needs` — снесены
// 2026-09-22: счёт контейнера стал ОДНИМ на весь мир и переехал к суду,
// которому служит (macro/upkeep_window.h `upkeep_bill`). Разбор — в теле,
// на месте бывшего определения (npc_ai.cpp).

// (`provision_squad` УМЕР 2026-10-23: 0 звонящих во всём дереве.)

// ЕДИНСТВЕННЫЙ драйвер макро-думок (M-379): `ticks` — тики МИРА, и ровно
// столько внутренних шагов исполняется, по одному тику за шаг. Вызов с N
// тиков байтово равен N вызовам по 1 — эволюция мира есть функция тиков по
// построению; дробление (кадр, пачка теста, фон субмира по делителю, прыжок
// addtime) в состоянии невыразимо. Фон субмира зовёт ЭТУ ЖЕ дверь — бюджетный
// собрат tick_macro_npc_ai_budgeted умер с M-379: его размазка была чистой
// оптимизацией (вердикт владельца) и вторым законом ритма. Envelope несёт как
// минимум `gs` и `store`; остальные слои — опциональный вклад (TickContext).
void tick_macro_npc_ai(MacroWorld& mw,
                       MacroNpcAiRuntime& runtime, std::uint64_t ticks);

// ТЕМП МАРША — КЛЕТКИ ЗА ДУМКУ, ОДИН ЗАКОН (ядро try_move и темп думки;
// вектор пакета жил вторым читателем один день — снесён ЗАХВАТОМ): база
// одна на игрока и сквад (kMacroWalkCellsPerHour), резвость листа
// (moveMult), рельеф ТЕКУЩЕЙ клетки одной пробой на думку; летуну рельеф —
// воздух (его вес звонящий подаёт единицей, внутри он не читается).
inline float march_cells_per_think(const ecs::MacroNpcRuntime& rt,
                                   float cellWeight) {
    return kMacroWalkCellsPerHour * kAiTickGameHours
         * std::max(0.0f, rt.moveMult)
         * (rt.flying != 0 ? 1.0f : terrain_speed_mult(cellWeight));
}

// ── ТОТ ЖЕ ЗАКОН ЗА СУТКИ — МЕРА РЕШЕНИЯ, А НЕ ВТОРОЙ ТЕМП ───────────────
// Вердикт владельца 2026-11-08 на развилку M-6/M-436: «снести дверь и СРАЗУ
// дать макро-маршу анкетный темп».
//
// ЧТО БЫЛО ДЕФЕКТОМ, И ЭТО DOD п.6 В ЧИСТОМ ВИДЕ: на вопрос «как быстро этот
// сквад идёт» мир отвечал ДВАЖДЫ И РАЗНОЕ. Ноги шли дверью выше — анкетным
// `moveMult` (резвость листа, `spd × athletics`) и рельефом клетки. А ОЦЕНКА
// «сколько дней до цели», на которой стоят аукцион рейса и развилка
// «дальше или домой», брала константу `kSustainedMarchCellsPerDay` — ОДИН
// темп на всех ходоков мира, не читавший анкету ни разу. План и ноги
// расходились тем сильнее, чем дальше лист от единицы: конный корован
// торговался по пешему расписанию, а хромой — по конному.
//
// РЕЛЬЕФ ЗДЕСЬ НЕ УМНОЖАЕТСЯ, И ЭТО НЕ ЗАБЫВЧИВОСТЬ: он УЖЕ ВНУТРИ ЦЕНЫ
// ПУТИ. Ребро запекания есть `cell_weight_of(to) * stepLen + climb`
// (`nav_field.cpp`), поэтому `nav_path_cost` возвращает ВЗВЕШЕННЫЕ единицы, и
// второе умножение на вес было бы рельефом, посчитанным дважды. Делитель 16 у
// звонящих — квант цены (`quant32`), а не темп.
//
// ДЕЛИТЕЛЬ 2 — ОТДЫХ: автомат между маршами восстанавливается до половины
// полосы (`think_gate`), то есть половина календаря идёт, половина спит. Это
// то же число, что несла снесённая константа; переехал вывод, не значение.
inline float march_cells_per_day(float moveMult) {
    return kMacroWalkCellsPerHour * 24.0f / 2.0f * std::max(0.0f, moveMult);
}

// ── ОДИН ЗАКОН МАРША — ЯДРО try_move ЭКСПОРТИРОВАНО (M-412) ─────────────
// Вердикт владельца: «игрок — просто один из сквадов, в микромире это
// аватар, и он просто оперирует данными с инпутов» — особенное только
// центрирование рамки 3×3. До M-412 марш игрока был ВТОРОЙ реализацией
// этого закона (`step_macro_walk@src/ui/macro_overlay.cpp` дублировал
// оплату квантов, румб входа и доезд в центр, а гейт заминки боя у него
// ОТСУТСТВОВАЛ — привилегия). Теперь оба ходока зовут одно ядро.

struct NavWorld;

// Реально читаемое маршем — TickContext целиком ему не нужен (M-412а):
// вес-сетка, запечённая походка, сторона тора, клетка игрока.
struct MarchGround {
    const PathCostData* pathCost = nullptr; // вес жадного шага; null = ровный мир
    const NavWorld*     nav      = nullptr; // походка; null = жадный шаг
    int side = 0;   // сторона тора — одна на обе оси (мир КВАДРАТЕН, ЗАКОН АДРЕСА)
    // Закон «не перепрыгнуть встречу» (Inc 6): многоклеточная думка встаёт
    // НА клетке игрока. Для марша САМОГО игрока и для мира без игрока гейта
    // нет (`kNoCell` клеткой не является): встречу игрока судит
    // detect_forced_encounter после шага.
    std::uint32_t playerCell = ecs::kNoCell;
    // ИЗЪЯТИЕ НОГ ПО АРЕНДЕ (ЗАХВАТ, ломоть 2 M-419): центр аренды окна
    // сентинелом, чтобы ядро не читало ни состояния, ни сессии. В девяти арендованных
    // клетках позицию ведёт СУБМИР (fold сворачивает её на границе тика) —
    // ноги макро там отказывают, а шаг В аренду становится последним шагом
    // думки. Сентинел = аренды нет: гейт спит значением, не веткой.
    std::uint32_t leaseCenter = kNoWindowLease;
};

// «ШАГ ЗАДАН» рядом с «цель задана» (M-412в) — ОДНА функция, два входа
// цели: маршрут A* игрока исполняется БУКВАЛЬНО (какие клетки выбрал
// поиск, те ноги и оплачивают; своя походка nav+жадный шаг спит), idx —
// in/out прогресс маршрута. Вейпоинт своей же клетки — пропуск, не смена.
struct MarchRoute {
    const PathPoint* pts = nullptr;
    std::size_t count = 0;
    std::size_t idx = 0;
};

// Бюджет квантов — ЕДИНСТВЕННЫЙ вход темпа (M-412б): его состав — закон
// ЗВОНЯЩЕГО (думка: march_cells_per_think выше; кадр игрока: его темп в
// main.cpp). При route != nullptr `goal` не читается.
//
// ЯДРО ГОВОРИТ ИНДЕКСАМИ (M-462): идёт оно прямо по колонке клетки сквада и
// САМО ЖЕ ЕЁ ПИШЕТ на каждом пересечении края, вместе с долей и румбом
// входа — одним действием, потому что это один факт. Цель — индекс клетки.
// Здесь стояли пара флоатов позиции, пара флоатов цели и возврат «евклидова
// дорога в клетках для картинки»: позиция была целой клеткой в типе флоата,
// а возврат не читал никто (у игрока он упирался в пустое `if`; темп
// картинки меряет кадровый кэш глаз сам, M-425).
void try_move(ecs::MacroCell& cell, ecs::MacroNpcRuntime& rt,
              std::uint32_t goal, std::uint32_t quanta,
              const MarchGround& ground, MarchRoute* route = nullptr);

// (ЗДЕСЬ СТОЯЛА `settle_walk_cell` — «единственный писатель клетки после
// хода», M-412г. Она существовала, потому что ход шёл по черновику-паре, и
// клетку надо было свернуть обратно в колонку; заодно она угадывала прыжок
// по отсутствию румба входа и ставила долю в центр. Черновика больше нет
// (M-462): ШАГ пишет ядро марша выше, ПРЫЖОК — своя дверь
// `squad_jump_to_cell@src/bus/store.h`, которая центр ставит явно, а не по
// догадке. Писателей у клетки сквада теперь ровно столько, сколько есть
// способов её сменить: шаг, прыжок и свёртка шва `squad_fold_pos`.)

// ЧАСЫ ФЛАЖКА (M-412д). Свип игрока не водит НИКОГДА (ни флажок, ни родной
// сквад — пропуск в теле свипа), поэтому его время идёт ЗДЕСЬ, тем же
// kAiTicks-кадансом НА ТЕХ ЖЕ КОЛОНКАХ: время-в-клетке (entryTicks),
// рековери и «я сражаюсь» (fighting). recoverySteps гаснет тиками напрямую
// (его единица — мировой тик; у НПЦ тот же закон амортизирован по kAiTicks
// на думку). tickAccum у флажка — часы времени-в-клетке (смена клетки его
// обнуляет), а не фаза думки.
// ТРЕТЬЯ КОЛОНКА — «Я СРАЖАЮСЬ» (ломоть 6 ЗАХВАТА M-419, тем же квантом,
// каким здесь гасла заминка `halted` до шага 0): сквад игрока ШТАМПУЕТСЯ
// честно — тело флажка несёт бэклинк (slot_set_origin в enter субмира),
// ступень 1 лестницы slot_squad_of возвращает его запись, «игрок ничем не
// особенен». Стенд 2026-11-08 поймал ложь прежней шапки «не штампуется по
// построению»: без этого слива штамп игрока не угасал бы НИКОГДА. Часы
// идут на карте — внизу взведённый штамп честен (бой и идёт), а после
// подъёма гаснет за kFightThinks квантов.
inline void advance_flag_clock(ecs::MacroNpcRuntime& rt, std::uint32_t ticks) {
    ecs::squad_recovery_elapsed(rt, ticks);
    rt.tickAccum += ticks;
    // Квант часов — дверью «всё или ничего» (M-218: гейт и голое `-=`
    // двумя строками и есть try_spend, написанный руками над беззнаковым).
    while (try_spend(rt.tickAccum, kAiTicks)) {
        rt.entryTicks = saturate_entry_ticks(rt.entryTicks);
        ecs::squad_fighting_elapsed(rt);
    }
}

} // namespace sm

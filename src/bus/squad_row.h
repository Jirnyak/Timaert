// КОЛОНКИ СКВАДА — РОД 2 КАНОНИЧЕСКОГО ФРЕЙМА (ЗАКОН ФРЕЙМА п.1.2: «СКВАДЫ
// (АНКЕТЫ) — все акторы макромира, включая неподвижных»). Формы, из которых
// сложен ГЛАДКИЙ МАССИВ сквадов: адрес-клетка, доля клетки, рантайм марша,
// цель, имя, лицо, черты, контейнер, сбруя, ординал рождения, маршрут, метка
// авторской строки. Сюда они приехали из `ecs/components.h` (эпик пакетов v1,
// ломоть 2б-I) ровно потому, что ТАМ лежат формы ТЕЛА и СЦЕНЫ, а это — форма
// МИРА: пакет рода 2 обязан быть виден ОБОИМ мирам, и `src/sub` берёт его
// здесь, не включая `macro/` (§11, ЗАКОН ДВУХ МИРОВ). Никакого ре-экспорта в
// обратную сторону нет: `ecs/components.h` этот заголовок НЕ включает, и
// честный `#include` стоит у каждого читателя — это и есть плата канала.
//
// Пространство имён `sm::ecs` НЕ СМЕНЕНО, и это осознанно: имя — самый слабый
// свидетель (ЗАКОН ИМЕНИ п.1), а автосмена есть ОТДЕЛЬНАЯ работа, которая не
// смеет ехать внутри переезда — иначе переезд и переименование разошлись бы
// одним дифом, и ни одно из двух нельзя было бы проверить отдельно.
#pragma once
#include "core/torus.h"        // cell_of/cell_x/cell_y — ОДИН адрес клетки мира
#include "bus/anketa.h"        // Inventory + Gear — ровно их носят колонки ниже
#include <array>
#include <cstdint>

namespace sm::ecs {

// THE CELL OF A MACRO SQUAD — one number (owner verdict 2026-09-10, CANON
// S2: «мир — плоский массив, связный тор; у каждой клетки ровно одно
// число»): the row-major index of the 2^20-cell torus, the SAME index every
// field of the world is addressed by (S5) — a squad reads the field under
// its feet with no conversion at all. The map side is a power of two, so
// the torus is masks and shifts, and a fractional cell is UNREPRESENTABLE:
// the scale confusion that bred SUB-1 (cells read as tiles) cannot compile
// against this type, and neither can a half-cell position — the fractional
// march lives in the walk's own banked budget, the stated intra-cell point
// in CellSub quanta (M-244). Scene bodies keep Position (tiles + 3D metres);
// a macro squad keeps THIS, and no pass can mistake one for the other.
struct MacroCell { std::uint32_t idx; };

// The three spellings of one address, beside the type they speak for — и все
// три теперь ВЕДУТ В ОДНУ ДВЕРЬ (`core/torus.h`, ЗАКОН АДРЕСА 2026-09-23).
// Здесь лежала вторая, независимая реализация того же адреса: она считала
// маской (правильно и быстро), тогда как `ResourceGrid::index` считал
// делением — два ответа на вопрос «где эта клетка», совпадавшие ровно
// потому, что мир квадратный. Совпадение по случаю — это не согласие.
inline std::uint32_t cell_index(int x, int y, int mapW) {
    return cell_of(x, y, mapW);
}
inline int cell_x(MacroCell c, int mapW) {
    return sm::cell_x(c.idx, mapW);
}
inline int cell_y(MacroCell c, int mapW) {
    return sm::cell_y(c.idx, mapW);
}

// ДОЛЯ КЛЕТКИ МАКРО-СКВАДА (M-244, вердикты владельца 2026-10-09/15, дословно
// в реестре): клетка делится крестом с 0 в середине, квант = 256 на сторону
// клетки, законный диапазон −127..127 (−128 не рождается — один никогда не
// занимаемый квант у края). Клетка (MacroCell) остаётся ЕДИНСТВЕННЫМ адресом:
// макро-механика долю НЕ читает ни одной веткой, мир дискретен, «между
// клетками» не бывает. Читают два потребителя — ВОПЛОЩЕНИЕ (точка тел сквада
// в клетке субмира) и КАРТА (цель глайда спрайта). Пишут три двери: рождение
// (центр, нули), марш при смене клетки (край входа по румбу —
// cell_sub_after_step ниже), дельта шва (свёртка позиции тела; придёт с
// дельта-буфером эпика фрейма). Здесь стоял float-тройкой `MacroVisual`
// {vx,vy,speed} — он умер этим же ходом («да убиваем макровиузуал он флоат
// а флоаты это плохо»): глайд карты стал ПРЕДСТАВЛЕНИЕМ (кэш глаз у
// оверлея), скорость глайда и прежде жила в MacroNpcRuntime::visualSpeed.
struct CellSub { std::int8_t x = 0, y = 0; };
static_assert(sizeof(CellSub) == 2, "доля клетки — два байта-кванта");
// КВАНТ КЛЕТКИ — 256 (вердикт владельца M-244 «да норм»); хранение int8
// (−127..127, 0 = центр) оставляет один никогда не занимаемый квант у
// края — цена названа вердиктом. ПОЗИЦИЯ МИРА ПРИ ЭТОМ — КЛЕТКА (M-375):
// доля — сквадная локаль, и мир её не читает НИ ОДНИМ вопросом; квант нужен
// только развёртке ПРОЕКЦИИ (player_flag_point — вход в субмир и камера) и
// глазам карты. Строка «дистанция встреч читает его» умерла — встречи
// судятся клеткой.
inline constexpr int kCellSubQuantum = 256;
// КРАЙ ДОЛИ: ±127 (предел int8 минус никогда не занимаемый квант). Проход
// клетки от края до края = 2×127 = 254 кванта — это и есть целочисленная
// цена клетки марша (прежний «1.0 бюджета» в любом румбе: диагональ ведёт
// обе оси разом и стоит те же 254).
inline constexpr int kCellSubEdge = 127;

// ДОЛЯ ПОСЛЕ ПЕРЕСЕЧЕНИЯ КРАЯ: вошёл с запада (шаг dx=+1) — стоит у
// ЗАПАДНОГО края новой клетки (subX = −127); ось, по которой шага не было,
// долю СОХРАНЯЕТ (шёл по северной трети — в ней и остался). Прыжок/телепорт
// (шага нет вовсе) — центр, нулями (драйвер, kEntryDirNone).
inline constexpr CellSub cell_sub_after_step(int dx, int dy,
                                             CellSub prev = {}) {
    return CellSub{dx != 0 ? std::int8_t(-dx * kCellSubEdge) : prev.x,
                   dy != 0 ? std::int8_t(-dy * kCellSubEdge) : prev.y};
}

// Флаги марша одной думки (MacroNpcRuntime::marchFlags, вердикт 2026-11-02):
inline constexpr std::uint8_t kMarchAdvanced = 1u;  // кванты проехали
inline constexpr std::uint8_t kMarchRefused = 2u;   // шага к цели нет

// СРОК ЗАМИНКИ БОЯ, В ДУМКАХ (MacroNpcRuntime::fighting, M-319A). Мера —
// ДУМКА, а не тик и не секунда: гейт стоит в марше, марш живёт думкой, и
// третьей единицы у заминки быть не должно (калибровка — только в тиках
// игры). Почему две: удар внизу приходит МЕЖДУ думками сквада, значит одна
// думка гарантирует, что дума, начавшаяся до удара, не уедет дальше, а вторая
// держит заминку при темпе ударов реже думки — иначе ноги мигали бы «встал-
// пошёл» между взмахами. Число — БАЛАНС (не по-двойка, ЗАКОН ПРИОРИТЕТА
// СТРУКТУРЫ): система от него не зависит, темп боя калибруется им свободно.
inline constexpr std::uint8_t kFightingThinks = 2u;

// ДОЕЗД В ЦЕНТР — вторая половина квантового хода (M-375): пересечение края
// оставляет тело НА КРАЮ новой клетки, поэтому всякое ПРИБЫТИЕ (цель этой
// клеткой достигнута, маршрут исчерпан, тело встало) доводит долю к центру
// ТЕМ ЖЕ квантовым ходом — иначе финиш вечно паркует на углу четырёх клеток
// (стенд владельца, Б5). Оси едут вместе; МАРШЕМ это не является: флагов не
// ставит, жжения не несёт — тело уже стоит В СВОЕЙ клетке и «устраивается».
// Возврат — потраченные кванты (картинке).
inline constexpr std::uint32_t cell_sub_walk_home(CellSub& sub,
                                                  std::uint32_t quanta) {
    const std::uint32_t ax = std::uint32_t(sub.x < 0 ? -sub.x : sub.x);
    const std::uint32_t ay = std::uint32_t(sub.y < 0 ? -sub.y : sub.y);
    const std::uint32_t cost = ax > ay ? ax : ay;
    const std::uint32_t spent = quanta < cost ? quanta : cost;
    if (spent == 0u) return 0u;
    const int step = int(spent);
    sub.x = std::int8_t(sub.x > 0 ? (int(sub.x) > step ? int(sub.x) - step : 0)
                                  : (-int(sub.x) > step ? int(sub.x) + step
                                                        : 0));
    sub.y = std::int8_t(sub.y > 0 ? (int(sub.y) > step ? int(sub.y) - step : 0)
                                  : (-int(sub.y) > step ? int(sub.y) + step
                                                        : 0));
    return spent;
}

// NPC link to type registry (NPCType enum value).
struct NPCKind { std::uint16_t type; std::uint16_t factionIdx; };

// Per-NPC visual identity. POD reinterpretation of TS `CharacterData`
// (which is HTML-canvas-targeted: name + sprite-layer indices + palette
// state). For C++ we keep only the gameplay-visible variation needed
// for the macro overlay figures: a deterministic procedural seed that
// drives sprite-variant + body shape, plus an accessory tint and a
// stable display-name index into the per-type names[16] pool.
// 12 → 8 Б (M-246б, 2026-10-18): ушли `nameIdx` и `tintR/G/B`.
//  · `nameIdx` был ВХОДОМ ГЕНЕРАТОРА, персистированным на все 16384 слота и
//    уезжавшим в формат сейва: его читал РОВНО ОДИН сайт и ровно один раз,
//    при рождении. Комментарий на этом месте при этом утверждал «читателей у
//    него нет» — проза врала в обе стороны сразу, и наряд унаследовал эту
//    ложь как диагноз «сирота». Теперь имя рендерится из СВОЕГО броска
//    (`roll_name_index@src/ecs/npc_character.h`) локальной величиной.
//  · `tintR/G/B` — сироты с НУЛЁМ читателей во всём `src/`; о том, что их
//    никто не читает, прямым текстом говорила шапка `derive_face@src/sub/spawn.cpp`.
// Явные пады ОСТАЮТСЯ: без них 5 значащих байт дали бы 3 байта НЕЯВНОГО
// паддинга, а он не зануляется — байтовые свидетели снапшота читали бы мусор
// (шрам `upkeepDebtPad@src/macro/macro_snapshot.h`).
struct NpcCharacter {
    std::uint32_t visualSeed;   // drives procedural sprite variation
    std::uint8_t  bodyShape;    // 0..3 (small / med / large / huge)
    std::uint8_t  pad0, pad1, pad2;
};

// ── ИМЯ АНКЕТЫ — КОЛОНКА КАЖДОГО СКВАДА (вердикт владельца 2026-09-30,
// эпик ЖЁСТКИЙ КАРКАС ход 2: «имя — char name[32] колонкой анкеты КАЖДОГО
// сквада, мутируемо; Landmark::name умирает в неё, индекс имени — дефолт
// генерации»). Плоские чары, никаких строк (ЗАКОН СЛОВАРЯ; прецедент —
// вердикт о фракциях «плоский ЧАРЫ! никаких стрингов и говна»). NUL-строка
// внутри 32 байт; рождение заполняет дефолтом из names[...], переезд
// места (M-90) привезёт сюда Landmark::name. 32 — число вердикта.
struct SquadName {
    char text[32];
};
static_assert(sizeof(SquadName) == 32,
              "имя анкеты: 32 Б × единый кап 16384 = 0.5 МиБ");

// (`NpcLevel` УМЕРЛА 2026-10-18, M-246а — ВТОРОЙ ОТВЕТ НА ОДИН ВОПРОС О
//  МИРЕ. Уровень сквада есть `levelData.level` его анкеты, и приговор
//  колонке ЗАКОН АНКЕТЫ вынес дословно: «колонка ecs::NpcLevel и
//  sheet.levelData.level — два ответа на один вопрос, и колонка умирает в
//  анкету». Дверь чтения — `squad_level@src/bus/store.h`. Колонка держала
//  и живой баг: она умела быть НУЛЁМ там, где анкета не умеет, а нулевой
//  уровень обнулял кривую опыта — см. шрам у двери роста в squad.h.)

// Per-NPC inventory: what this body is CARRYING. Only a body whose belongings
// are state has one — a macro entity and the tracked body that embodies it
// (sub/spawn.h), or a body a quest handed a specific thing. A derived body has
// none on purpose: its loot is rolled from its row through the one registry at
// the moment it dies (tables/items.h roll_loot_profile), so a city of five
// thousand people costs five thousand integers and no bags.
struct NpcInventory { Inventory inv; };

// What this body is WEARING — opt-in, and deliberately rare. A creature's own
// defence is a number on its row (macro/npc.h `NpcTypeDef::armor`, the owner's
// «броня массовки = число из строки»), because a troll's hide is what a troll
// IS; only a body that actually owns things — the player, a named lord —
// carries this. So the sixteen thousand macro squads pay nothing for a system
// they do not use, and the ones that do use it get the whole of it.
struct BodyEquipment { Gear gear; };

// Per-NPC personality traits. TS assigns 1-2 unique traits from the
// `NPCTrait` registry; store raw enum ids here to keep ECS free of a
// hard dependency on the macro registry header.
struct NpcTraits {
    std::uint8_t count = 0;
    std::uint8_t traits[2]{};
};

// Macroworld NPC runtime — per-NPC mutable state for the AI tick
// (mirrors fields on TS `NPC` not already covered by Position / NPCKind).
// Pure POD; its size is stated ONLY by the assert below (84 B) — the prose here
// said «96 bytes» long after the assert said 92, which is exactly how a number
// that lives in two places goes stale. The home/target place ids draw on the ONE
// subject-ordinal space (nextMacroSpawnOrdinal, M-37) — a home may be a city or
// a village, and the id alone says which place it is (0 = none). The v27
// homeIsVillage register bit died with the second id space it existed to
// disambiguate.
// «Нет клетки» — последнее значение типа (ЗАКОН УЗКОГО ИНДЕКСА: клетка 0 —
// законный адрес мира); имя — из продиктованной формы цели (M-1). Один
// сентинел на всякий хранимый адрес-клетку рода 2 (цель, логово).
inline constexpr std::uint32_t kNoCell = ~0u;

struct MacroNpcRuntime {
    // (`homeSettlementId` умерла 2026-10-22, M-245: ДОМ = РОДИТЕЛЬ, ребро
    //  Parent реестра связей; читается дверью `parent_of@src/bus/store.h`.)
    // (`targetSettlementId` умерла 2026-10-23, ломоть 3 M-72: ординал рынка
    //  несёт колонка цели `SquadGoal::what` рода Rooted — второй носитель
    //  «куда еду» рядом с целью был вторым словарём.)
    // РЕЙС, СТАНЦИЯ ЗА СТАНЦИЕЙ (владелец 2026-08-30: «решает с того рынка,
    // на котором стоит — никакого всеведения»).
    //
    // ЗДЕСЬ СТОЯЛИ `stationsLeft` (u8) И `taxCarried` (i32) — СНЕСЕНЫ
    // 2026-10-12, M-252, вердикт владельца «все три». У обеих было НОЛЬ
    // кодовых сайтов во ВСЁМ дереве — ни в `src/`, ни в `tests/`, ни в
    // смоуках, — и судились они ДВЕРЯМИ, а не грепом имени. Первая была
    // счётчиком остановок снесённого `ai_caravan`, то есть НЕВЕРНЫМ ЗАКОНОМ
    // конца рейса (CANON S10: возврат решается ГРУЗОМ, а не числом станций);
    // вторая обещала дань «в сумке к сюзерену», которой никогда не было —
    // феодальный долг живёт ребром `TitheEdge@src/macro/factions.h`, и у него
    // есть настоящие читатели.
    //
    // ЦЕНА СНОСА БОЛЬШЕ, ЧЕМ ВЕСИЛИ ИМЕНА: 92 → 84 Б, то есть −131 072 Б по
    // капу 16384. Названного байта было пять, освободилось восемь — три из них
    // были дырой выравнивания между `stationsLeft` и `prevStationId`, и ровно
    // поэтому «одинокий узкий член рядом с широким не экономит ничего»
    // (DOD п.2) читается здесь в обратную сторону: его снос экономит ВДВОЕ.
    //
    // `prevStationId` — ОЖИЛ 2026-09-22 вместе с многоскачковым рейсом:
    // станция, с которой крю пришло, чтобы рулетка соседей не гоняла его
    // обратно на только что обслуженный рынок. 0 = рейс только начат —
    // у ординала-идентичности ноль навсегда «никто» (ЗАКОН СЛОВАРЯ п.6;
    // сентинел -1 сведён 2026-10-23, ломоть 3 M-72).
    std::int32_t  prevStationId = 0;
    // (`targetX`/`targetY` умерли 2026-10-23, ломоть 3 M-72: адрес цели —
    //  ОДНО число `SquadGoal::cell` (ЗАКОН АДРЕСА п.2, M-3); пара float
    //  жила 37 писателями и была налогом на хранении.)
    // ГЕЙТ ЗАНЯТОСТИ СКВАДА — МАКРО-СУБЪЕКТ ЕДИНОГО ЗАКОНА ВОССТАНОВЛЕНИЯ
    // (ломоть M-369 «stateTimer умирает в рековери», 2026-11-06). ОДНИ часы
    // «занят до» на сквад, зеркало `recoverySteps@src/ecs/components.h` тела:
    // единица — МИРОВОЙ ТИК (= шагу субмира по построению,
    // `kStepsPerSecond = kTicksPerRealSecond@src/core/time.h`). Заряжает ОДНА
    // дверь `charge_squad_recovery` ниже; тают ТОЛЬКО временем — одной точкой
    // слива в свипе (`tick_macro_npc_ai_one@src/macro/npc_ai.cpp`), в походе,
    // в привале и в бою одинаково; смена состояния часов НЕ трогает (прежний
    // закон «часы прерванного состояния не переносятся» СНЯТ — он и был
    // формой шрама M-313: привал, зануляя чужой долг, учетверял крафт).
    // Движение гейт не читает НИКОГДА («двигаться можно всегда»); читают его
    // АКТЫ и шаги программы модуля — перед исполнением, идиомой
    // `squad_recovering`. Здесь лежали `stateTimer` + `teleportCooldown`
    // (2×i16, ДУМКИ) — два счётчика одного субъекта, второй был «своей
    // таблицей восстановления у отдельного рода действий» (ЗАКОН СПОСОБНОСТИ
    // п.6); оба умерли в этот один гейт тех же четырёх байт.
    std::uint32_t recoverySteps = 0;
    // STAMINA IS NOT HERE ANY MORE. `sp`, `maxSp` and the signed `spCarry`
    // moved to ecs::Pools beside the body's other two bars (2026-09-09): a
    // body has three pools, and this struct is the march's RUNTIME, not a
    // second pool store. What stayed are the sheet CACHES the march reads per
    // think — the ranks and the pace — which are not bars.
    //
    // The avatar's sheet, cached as the scalars the macro march actually reads
    // (Session 21). The full CharacterSheet is derived, never stored: make_npc
    // and the level-up recompute both re-derive it from
    // avatar_sheet_seed(MacroSpawnId) and refresh these through ONE helper
    // (squad.h refresh_body_from_sheet), so the cache cannot drift from
    // the sheet law. travelRank/marathonRank: the two SP skills (cost discount
    // / recovery rate). moveMult: spd × athletics, the sheet's own pace.
    std::uint8_t  travelRank = 0;
    std::uint8_t  marathonRank = 0;
    // Phase 6: the scouting rank widens how far this squad SEES (npc_ai
    // squad_sight_cells) — cached beside its two SP siblings because the
    // sight query runs per think, where a sheet re-derivation has no
    // business. Same one refresh door, so it cannot drift either.
    std::uint8_t  scoutRank = 0;
    float         moveMult = 1.0f;
    // What this avatar's back can hold (attributes.h get_carry_capacity), the
    // fifth scalar of the same sheet cache — and the SP surcharge his current
    // load is costing him, recomputed each think from his bag because a bag
    // changes at every market while a sheet does not. Owner's ruling
    // 2026-08-27: «перегруз универсальный всем» — a laden squad marches like a
    // laden man, and the pack a caravan carries is finally part of the price
    // of carrying it.
    // СПИНА АНКЕТЫ — ОДНО ЧИСЛО, ЦЕЛЫМИ ГРАММАМИ. Это его СИЛА и его
    // Weightlifting (`get_carry_capacity`), и колонки строки в ней нет вовсе.
    //
    // ЗДЕСЬ СТОЯЛА ВТОРАЯ КОЛОНКА — `carryCap`, «весь обоз» (спина ПЛЮС
    // объекты контейнера). СНЕСЕНА вердиктом владельца 2026-10-20 «давай А»:
    // она была кэшем РАЗРЯДА Б — то есть хранимым тоталом, свежесть которого
    // держалась ДИСЦИПЛИНОЙ, и её собственная проза это признавала
    // («пересчитывается дверью refresh_squad_carry в КАЖДОМ месте, где состав
    // меняется»). Четыре звонка держались, пятый забыли бы, и устаревший
    // тотал врёт МОЛЧА — хуже, чем перевывод, который всего лишь тратит такты.
    //
    // ЧТО ДОКАЗАЛО ИЗБЫТОЧНОСТЬ, ЧИСЛОМ: все **10** прод-читателей `carryCap`
    // держали в руках ТОТ ЖЕ инвентарь, функцией которого колонка является, —
    // её ни разу не спросили отдельно от сумки. А оба её слагаемых уже
    // разряда А: спина — вот эта колонка от двери листа, объекты —
    // `sum().haulG`, который правит САМА запись слота (у сводки писателя нет).
    // Значит сумма принадлежит МЕСТУ УПОТРЕБЛЕНИЯ, и её отдаёт дверь
    // `carry_capacity_g@src/macro/squad.h` — O(1), чтение хранимой сводки.
    // Устареть ей теперь невыразимо.
    //
    // ЗДЕСЬ ЖЕ СТОЯЛО «спина лидера по его листу и колонке его строки» — имя
    // УМЕРШЕЙ МОДЕЛИ вместе с ошибкой рода (M-297, 2026-10-20): лидера в
    // замысле нет (сквад ЕСТЬ анкета), а колонка строки — величина таблицы
    // ОБЪЕКТОВ и к спине анкеты не относится. Ровно эта проза породила
    // дефект: по ней вклад объекта считали ДОЛЕЙ анкеты хозяина.
    std::int32_t  carryPerSoulG = 0;
    // ЗДЕСЬ СТОЯЛА `overloadCost` — ЦЕНА ПЕРЕГРУЗА В SP ЗА ЧАС, `int16`.
    // СНЕСЕНА вердиктом владельца 2026-10-21 (дословно: «убирай нафиг нам
    // хранить скока перегруз ест сели просто достатчно хранить 2 значения вес
    // который может нести сквад и вес который несёт»). Хранимых значений и
    // осталось ровно два, оба свежие по построению: СПИНА —
    // `carry_capacity_g@src/macro/squad.h` (колонка выше плюс граммы голов из
    // сводки) и НЕСОМЫЙ ВЕС — `inventory_mass_g` (тотал сводки, разряд А).
    // Перегруз есть их разница, цена — её деление; третьего носителя нет.
    //
    // ПОЧЕМУ ЭТО ПОЧИНКА, А НЕ УПРОЩЕНИЕ (M-302): последним шагом записи стоял
    // НЕМОЙ КЛАМП `std::clamp(cost, 0, 32767)` — ширина этой колонки, — и
    // замер назвал его цену в продакшене: 2 попадания из 454 679 260
    // пересчётов (3 сида × 128 дней), в худшем настоящая цена 64 489 против
    // потолка 32 767, то есть час марша оплачен ВДВОЕ дешевле должного.
    // Обрезание шкалы внутри динамики живого мира ЗАКОН КЛАМПА запрещает
    // прямо, а DOD п.2 называет узкий тип «молча названным капом».
    //
    // ПОЧЕМУ СНОС, А НЕ ШИРИНА: колонка не переносила число через ВРЕМЯ —
    // она переносила его через ДЕВЯТЬ СТРОК одной думки, и это ИЗМЕРЕНО, а не
    // выведено: пересчётов 454 679 260, чтений 454 679 260, ровно 1:1 (свип
    // считал цену прямо перед единственным читателем
    // `settle_march_rhythm@src/macro/npc_ai.cpp`). У числа, чей расчёт и чьё
    // чтение стоят в одной думке, тотала нет вовсе; теперь цена едет
    // ПАРАМЕТРОМ и `int`-ом от начала до конца, и обрезать её нечему.
    // Поднятие типа до `int32` было отклонено как форма: оно оставило бы
    // хранимый тотал РАЗРЯДА Б там, где закон (DOD п.9) требует разряда А.
    // ── ЛОКАЛЬНЫЕ КООРДИНАТЫ СКВАДА (вердикт владельца 2026-11-02) ────────
    // «в макромире позиция это КЛЕТКА просто сквад локально хранит свою
    // промежуточную позицю НО это не уровень мира а уровень сквада типа
    // дошол до 127 … и меняет клетку с другой стороны»; «никаких локлаьных
    // кооординат дркгих систем СПЕИАЛЬНО СКВАДНЫЕ В СКВАДЕ ЧТОБЫ МИР НЕ
    // ЗНАЛ». Доля живёт В РАНТАЙМЕ сквада (прежняя колонка мира st.sub
    // умерла): марш ЕДЕТ по ней целыми квантами, край ±127 по румбу — смена
    // клетки, вход с противоположного края. ВСЕ вопросы мира — ТОЛЬКО
    // клеткой; долю читают лишь проекция вниз и глаза карты. Float-банк
    // moveBudget умер вместе с колонкой: прогресс марша И ЕСТЬ доля
    // (один носитель, DOD п.9), банкинга между думками больше нет.
    CellSub       sub{};
    // Флаги марша ЭТОЙ думки: пишет try_move/walker, читает ритм марша,
    // драйвер зануляет перед думкой. ADVANCED — кванты проехали (это
    // «moved» внутриклеточного хода); REFUSED — кванты были, а шага,
    // приближающего к цели, нет (предикат стояния «ноги отказали»).
    std::uint8_t  marchFlags = 0;
    // ── «Я СРАЖАЮСЬ» — ФАКТ РЯДОМ С НОГОЙ (M-319A, вердикт владельца
    // 2026-11-03 на развилку форм: «2)А») ─────────────────────────────────
    // Владелец, дословно: «если в субмире прям локально 3х3 напали на когото
    // из иневартнря вополнещнного этого сквада то на сквад в макро переходит
    // заминка навеное что файт и он типа временно встаёт в макромире „в
    // файте“»; и о охвате: «да только ходить пусть перестаёт … типа
    // заминочка».
    //
    // ПОЧЕМУ НЕ СТРОКА ОСИ `NPCState` (вопрос, который шапка обязана
    // закрыть): `state` ниже есть СЧЁТЧИК ПРОГРАММЫ МОДУЛЯ — 43 чтения
    // `rt.state ==` в `src/macro/npc_ai.cpp` ветвят поведение по нему, а
    // рефлекс угрозы (`squad_threat_step@src/macro/npc_ai.cpp`) стоит В
    // ГОЛОВЕ четырёх ходоков и ПЕРЕПИСЫВАЕТ `state` в Fleeing/Chasing ПЕРЕД
    // маршем. У сквада, которого бьют внизу, враг рядом по определению,
    // значит строка оси стиралась бы ровно на том пути, где она нужна
    // (замер M-319). Своя колонка этого не знает и знать не обязана: ноги
    // читают ЕЁ, а воля сквада идёт прежней думкой — `ThinkGate::Rest` за
    // бой НЕ берётся, и это прямой урок M-313 (гейт, гасивший думку, остановил
    // вместе с ногами ПРОИЗВОДСТВО).
    //
    // ЗАЧЕМ СЧЁТ, А НЕ ФЛАГ: снимать флаг было бы ВТОРОЙ операцией снизу, и
    // забытое снятие осталось бы скваду навсегда — сквад, вставший до конца
    // партии. Счёт УГАСАЕТ сам (`sub_sat` в драйвере думок, одна точка), а
    // удар внизу штампует его заново, поэтому «бой кончился» есть ОТСУТСТВИЕ
    // удара, а не сообщение о нём. Ноль — «не сражаюсь», и это значение, а не
    // отсутствие.
    std::uint8_t  fighting = 0;
    std::uint8_t  state;         // NPCState
    // Entry-side context (bus/entry_context.h): the packed signed step of the
    // last cell change (0xFF = none: spawned here / teleported), and a
    // saturating count of AI ticks since it. Stamped by try_move, consumed by
    // the macro→subworld projection to place this NPC's body on the side of the
    // map it actually walked in from.
    //
    // «Runtime-only — the ECS is never serialized, so no save cost» СТОЯЛО
    // ЗДЕСЬ И БЫЛО ЛОЖЬЮ (поправлено 2026-10-12, M-253). Колонка едет в файл
    // ЦЕЛИКОМ и POD-ом — `snapshot_macro_ecs@src/macro/macro_snapshot.cpp`
    // копирует её байт в байт, `write_macro_npc@src/macro/save.cpp` пишет, и
    // фильтра по полям нет ни одного. Опровергал ту прозу трейлер ЭТОГО ЖЕ
    // файла («their LAYOUT is the format») и абзац про `renown` ниже, который
    // всё это время говорил правду.
    std::uint8_t  entryDir  = 0xFF;
    std::uint8_t  entryTicks = 0;
    float         visualSpeed;   // last-tick travelled distance, cells/real s
    std::uint32_t tickAccum;     // WORLD ticks accumulated toward the next think
    // (Поле `xp` УМЕРЛО 2026-10-18 вместе со вторым законом роста, M-246а:
    //  опыт сквада есть `levelData.exp` его анкеты, и она едет в сейв той же
    //  записью. Сиротой оно было по DOD п.9 в самой чистой форме — ЕДИНСТВЕННЫЕ
    //  его писатель и читатель жили ВНУТРИ снесённой `award_leader_xp`, то
    //  есть за пределами одной функции байт не отвечал ни на один вопрос
    //  о мире, а в сейв ехал на всех 16384 слотах.)
    // WHAT THE WORLD THINKS THIS BAND HAS DONE (macro/chronicle.h). A squad
    // starts as one more band on the map: its deeds are weather, remembered by
    // the ring for a season and then forgotten. Do enough, and it becomes a
    // FIGURE (owner, 2026-08-27: «безымянный сквад может стать именным, как
    // лорд, если накопится деяний»).
    //
    // ЧЕМ ФИГУРА БЫЛА И ЧЕМ СТАЛА (M-184): прежде с этого дня её дела шли в
    // АННАЛЫ навсегда — второй ярус памяти снесён, и кольцо теперь забывает
    // всех одинаково. Сама слава ЖИВА и читается (`renown_is_named`); что
    // именно фигурность даёт миру, назовёт система памяти, когда придёт.
    //
    // ONE number, not a counter plus a flag: "is it named" is DERIVED
    // (`renown >= kRenownToBeNamed`), so the two can never disagree about the
    // same band. Persistent — a figure stays a figure across a save, because
    // the runtime rides the macro snapshot.
    //
    // UNSIGNED, and the reason is the value and not a habit: renown is a
    // COUNT of what a band has done, it is never negative, and it is not an
    // index nor a term of a subtraction — so the real hazards of unsigned
    // arithmetic (a reverse loop that never ends, `a - b` wrapping) do not
    // touch it. Where a field CAN go below zero the type says so, and the
    // fact record next door proves the rule is applied and not recited:
    // `WorldFact::amount` is signed because reputation and coin move both
    // ways, and `day` is signed because -1 means "no day yet".
    std::uint32_t renown = 0;
    // ── НА КОРАБЛЕ (CANON S10 «корабли через фичу», 2026-09-02) ──────────
    // (ПОЛЕ `aboard` ВЫРЕЗАНО 2026-09-22, сессия 14, вместе со всем морским
    // ходом: в ядре нет транспорта.)
    // (ПОЛЯ `dockX`/`dockY` — «где мой корабль» — ВЫРЕЗАНЫ 2026-09-22.)
    // ── ТИП СКВАДА — КТО ЭТОТ СКВАД (CANON S2, владелец 2026-09-21) ──────
    // «Сквад должен знать, кто он: тип задаёт поведение аи агента.» Тип —
    // SquadType (tables/squad_type.h), и спрашивают его ПЕРВЫМ: диспетч есть
    // ТАБЛИЦА по этому ординалу (`kAiModuleRows@src/macro/npc_ai.cpp`).
    // 0 = типа нет — сквад падает на каталог тел, и это ЕДИНСТВЕННАЯ
    // оставшаяся точка течи границы миров (разбор в SquadType::None).
    //
    // ЗДЕСЬ БЫЛ `errandVerb` — «глагол поручения». Та же колонка, то же
    // место, тот же писатель; не хватало закона, что её спрашивают ПЕРВОЙ.
    // Пока её звали глаголом, макромир считал нормальным решать поведение
    // сквада по мобной строке его лидера — и делал это в четырёх точках.
    std::uint8_t  squadType = 0;
    // (ПАРЫ {squadType, errandObject} БОЛЬШЕ НЕТ: объект поручения (u32) УМЕР
    //  2026-10-24 с аукционом ротации и миграцией модулей на цель — ломоть 4
    //  M-72. «Куда и зачем» несёт `SquadGoal::what` (род + ординал), и колонка
    //  была ВТОРЫМ НОСИТЕЛЕМ того же ответа (DOD п.6). Тип остался, и
    //  спрашивают его ПЕРВЫМ.)
    // ЛАГЕРЬ — ПАУЗА, НЕ АМНЕЗИЯ (2026-09-02, найдено замером аукциона):
    // привал прерывает состояние и запоминает его здесь; подъём будит В
    // НЕГО, а не в Idle. Побудка в Idle посреди дороги домой заставляла
    // вендор-машину «продолжить рейс» К ГОРОДУ — артель челноком
    // город↔привал не завершала рейс никогда (измерено: 4 артели × 19 душ
    // деревни 1299 заперты в поле 30 дней, пере-аукцион не наступал).
    // 0 = Idle — старое поведение для тех, кто лёг без прерванной ноги.
    //
    // ЗВАЛАСЬ `stateAfterRest` — «после ОТДЫХА», — И ЭТО БЫЛО УЖЕ НЕВЕРНО ПО
    // ЕЁ ЖЕ ШАПКЕ: работа у колонки «после ПАУЗЫ», отдых же есть лишь ОДНА из
    // причин паузы, и первая строка выше говорит это словом ПАУЗА. Имя —
    // самый слабый и самый старый слой (ЗАКОН ИМЕНИ п.1), а п.5 разрешает
    // чинить дефект ЧИСТО В ИМЕНИ механической автосменой прямо: правка
    // 2026-10-26 (M-319) не тронула ни одного вычисления — 16 вхождений в
    // пяти файлах, все до одного ссылки на ЖИВУЮ колонку, некрологов среди
    // них нет. Дверь подъёма `wake_from_rest@src/macro/npc_ai.cpp` имени НЕ
    // сменила, и это тоже вывод грепа, а не глаз: её единственный
    // производственный звонящий стоит СТРОГО внутри `state ==
    // NPCState::Resting`, то есть она честно про ЛАГЕРЬ — одну причину
    // паузы, а не про паузу вообще.
    std::uint8_t  stateAfterPause = 0;
    // ── ЛЕТУН на карте (v93; владелец 2026-09-10 — полёт честный и
    // системный). Кэш колонки строки (CombatTemplate::cruiseM > 0),
    // положенный сюда make_npc по образцу travelRank: try_move читает его
    // per-think, где лукап строки не нужен. Летящий марш не платит рельеф
    // и не требует корабля — воздух его дорога.
    std::uint8_t  flying = 0;
    // ЛОГОВО — дом-КЛЕТКА для тех, чей дом не ландмарк (драконья гора,
    // стол анкет): модуль рода Dragon (ai_dragon@src/macro/npc_ai.cpp)
    // кружит вокруг неё и возвращается. Адрес — ОДНО число (ЗАКОН АДРЕСА
    // п.2, M-3; пара int16 умерла 2026-10-23); kNoCell = логова нет.
    std::uint32_t lairCell = kNoCell;
};
// РАЗМЕР ЗАКРЕПЛЁН КОМПИЛЯТОРОМ (AGENTS п.10, владелец 2026-09-21): шапка
// этой структуры обещала «~36 bytes» — правда была 96, и разошлось это
// молча, потому что число жило только в прозе. Сторож сделал свою работу
// 2026-09-22: снос морского хода уронил размер, и компилятор сказал об этом
// вслух вместо того, чтобы число тихо разошлось с прозой во второй раз.
//   96 → 92: ушли `aboard` (1 Б) и `dockX`/`dockY` (2 × 2 Б), выравнивание
//   вернуло 1 Б обратно.
//   92 → 84 (2026-10-12, M-252): ушли `stationsLeft` (1 Б) и `taxCarried`
//   (4 Б) — обе с НУЛЁМ кодовых сайтов в дереве. Названных байт было 5,
//   освободилось 8: три добрала дыра выравнивания, стоявшая за u8.
// `prevStationId` из мёртвых вышел 2026-09-22 — его читает и пишет
// многоскачковый рейс корована.
//   84 → 80 (2026-10-18, M-246а): ушёл `xp` (4 Б) — счётчик опыта второго
//   закона роста, чьи единственные писатель и читатель жили ВНУТРИ снесённой
//   `award_leader_xp`. Названный байт сошёлся с освободившимся ровно.
//   80 → 76 (2026-10-20): ушёл `carryCap` (4 Б) — кэш РАЗРЯДА Б над двумя
//   источниками разряда А, чьи 10 читателей все до одного держали в руках
//   инвентарь, функцией которого он был. Сумму отдаёт дверь
//   `carry_capacity_g@src/macro/squad.h`. Названный байт сошёлся с
//   освободившимся ровно: выравнивание структуры осталось 4 (`carryPerSoulG`
//   — самый широкий член, `int32`).
//   76 → 72 (2026-10-21, M-302): ушла `overloadCost` (2 Б) — число ОДНОЙ
//   ДУМКИ, которому выдали хранилище, а вместе с хранилищем немой кламп по его
//   ширине. Названных байт 2, освободилось 4: вторые два добрала дыра
//   выравнивания, стоявшая за `int16` перед `float moveBudget`.
//   72 → 68 (2026-10-22, M-245): ушла `homeSettlementId` (4 Б) — дом стал
//   ребром Parent реестра связей, второй носитель одного вопроса умер.
//   68 → 56 (2026-10-23, ломоть 3 M-72): ушли `targetSettlementId` (4 Б),
//   `targetX`/`targetY` (8 Б) — «куда и зачем» несёт колонка цели
//   `SquadGoal` (M-1); пара `lairX`/`lairY` (4 Б) стала одним числом
//   `lairCell` (4 Б) — адрес = индекс клетки (M-3).
//   56 → 48 (2026-10-24, ломоть 4 M-72): ушёл `errandObject` (4 Б) — объект
//   поручения, чей последний писатель умер с аукционом ротации, а последние
//   читатели — с миграцией модулей Caravan/Collector на `SquadGoal::what`.
//   Названный байт 4, освободилось 8, и число у компилятора СПРОШЕНО, а не
//   посчитано на глаз: три байта были дырой выравнивания ЗА `squadType` (u8
//   перед u32), четвёртый добрала хвостовая дыра перед `lairCell` — она
//   села с 2 Б до 1 Б, потому что `stateAfterPause`/`flying` подъехали.
//   48 → 48 (2026-11-06, ломоть stateTimer M-369): `stateTimer` +
//   `teleportCooldown` (2×i16, думки) умерли в ОДИН гейт занятости
//   `recoverySteps` (u32, тики) — те же четыре байта, ни дыры, ни роста.
static_assert(sizeof(MacroNpcRuntime) == 48,
              "рантайм марша: 48 Б × 16384 сквадов = 0.75 МиБ (AGENTS п.10)");

// ── ДВЕ ДВЕРИ ГЕЙТА ЗАНЯТОСТИ — ЗАРЯД И ВОПРОС (ЗАКОН СПОСОБНОСТИ п.4) ──
// Заряд МОНОТОННЫЙ: дверь не умеет ни укоротить, ни обнулить чужой долг —
// ровно тот класс дефекта, которым M-313 купил себе закон (привал писал
// `stateTimer = 0` поверх суточного восстановления крафта, и место
// производило ЧЕТЫРЕЖДЫ в сутки). Через эту дверь сброс НЕВЫРАЗИМ; слив
// живёт в одной точке свипа и тает только временем. ЭФФЕКТИВНОЕ рековери
// акта считает дверь `recovery_steps@src/bus/anketa.h` НА МЕСТЕ ВЫЗОВА
// (база строки × Spd × генерик домена — CANON «БАЗОВОЕ И ЭФФЕКТИВНОЕ»);
// сюда приходит уже готовое число тиков: у паузы-программы модуля базы
// строки нет, её авторские тики едут той же дверью записи.
inline void charge_squad_recovery(MacroNpcRuntime& rt, std::uint32_t ticks) {
    if (ticks > rt.recoverySteps) rt.recoverySteps = ticks;
}

// ОДНО ПРАВОПИСАНИЕ ВОПРОСА «ЗАНЯТ ЛИ СКВАД» — у `stateTimer` их было три
// (`<= 0` в then-ветке · `> 0 return` · гейт до декремента), то есть второй
// словарь, спрятанный в глаголах (шапка `src/sub/ability.h` о том же гейте
// тела). Зеркало `body_is_free@src/sub/ability.h`, знак обращён нарочно:
// модуль спрашивает «ждать ли», а не «свободен ли».
inline bool squad_recovering(const MacroNpcRuntime& rt) {
    return rt.recoverySteps > 0;
}

// ── ЦЕЛЬ СКВАДА — РОД 2, ФОРМА ПРОДИКТОВАНА ВЛАДЕЛЬЦЕМ (M-1) ─────────────
// 2026-09-22, восстановлено из git 5bf0edaa (CANON «ФОРМА ЦЕЛИ БЫЛА
// ПРОДИКТОВАНА»): {куда, чего ищу, сколько, чем стать}. АДРЕС ЕСТЬ КЭШ
// ПРЕДИКАТА — четыре случая без ветки: адрес без предиката (иди туда),
// предикат без адреса (иди к ближайшему такому), оба (промах чинится
// ПЕРЕРЕШЕНИЕМ, а не пустым рейсом домой), движущийся род (пересчёт).
// Указатель на цель ЗАПРЕЩЁН: сейв, смерть цели между тиками, плоские
// массивы — цель называет мир ОРДИНАЛОМ единого эмитента.
// `amount` — ОСТАТОК, и его читает ЛЮБОЙ род цели (вердикт 2026-10-22 №6:
// «сходить сколько раз и сколько мест»); первый читатель приходит с
// модулями ломтя 4, до него колонка пишется нулём — «не считаем».
struct SquadGoal {
    std::uint32_t cell = kNoCell;  // разрешённый адрес — индекс клетки тора
    std::uint32_t what = 0;        // тег рода (4 бита) + ординал (28 бит)
    std::uint16_t amount = 0;      // остаток «сколько ещё»; 0 = не считаем
    std::uint8_t  onArrival = 0;   // чем стать, когда пришёл — NPCState; 0 = Idle
    // Дыра выравнивания, названная полем (образец M-183): колонка едет в
    // сейв POD-ом, паддинг обязан быть детерминирован.
    std::uint8_t  pad = 0;
};
static_assert(sizeof(SquadGoal) == 12,
              "цель сквада: 12 Б × 16384 = 192 КиБ (M-1; DOD п.10)");

// Роды цели — ЧЕТЫРЕ плюс «ничего», и «предмет» не из них (вердикт M-1):
// у предмета нет адреса в мире — он лежит В инвентаре места или сквада,
// то есть сводится к ним плюс строка товара.
enum class GoalKind : std::uint8_t {
    None = 0, Cell = 1, Feature = 2, Rooted = 3, Squad = 4
};

// Упаковка «чего ищу»: 28 бит ординала хватает единому эмитенту
// идентичностей с запасом (монотонный nextSpawnId: 2^28 = 268 млн рождений
// за жизнь мира; кап живых — 16 384).
inline constexpr std::uint32_t goal_what(GoalKind k, std::uint32_t ordinal) {
    return (std::uint32_t(k) << 28) | (ordinal & 0x0FFFFFFFu);
}
inline constexpr GoalKind goal_kind(std::uint32_t what) {
    return GoalKind(what >> 28);
}
inline constexpr std::uint32_t goal_ordinal(std::uint32_t what) {
    return what & 0x0FFFFFFFu;
}

// ── ДВЕ ДВЕРИ ЦЕЛИ (прецедент stateAfterPause/M-254: укладка и подъём) ────
// Записать цель ЦЕЛИКОМ: поля порознь не пишутся — половина цели (адрес без
// «чем стать») и есть тот дубль «куда + чем стать», что жил 12 парами строк
// у писателей NS::Returning.
inline void goal_set(SquadGoal& g, std::uint32_t cell, std::uint32_t what,
                     std::uint16_t amount, std::uint8_t onArrival) {
    g.cell = cell;
    g.what = what;
    g.amount = amount;
    g.onArrival = onArrival;
}
// Прибытие: стать тем, что несла цель. Ноль onArrival = NPCState::Idle —
// умолчание значением, не веткой. Сама цель НЕ гасится: адрес есть КЭШ
// ПРЕДИКАТА, и работа по прибытии читает его же (клетка жилы у артели);
// новая цель пишется ПОВЕРХ следующим решением — как и прежний носитель,
// которого никто не «очищал». Гашение целей — закон диспетчера (M-2).
inline void goal_arrive(const SquadGoal& g, MacroNpcRuntime& rt) {
    rt.state = g.onArrival;
}
// Остаток цели убывает ОДНОЙ дверью (вердикт №6: «amount — ОСТАТОК, убывает
// до нуля, completed выводится из него», а не хранится флагом). 0 = «не
// считаем» — бюджет не кончается никогда (значение, не отсутствие); иначе
// цикл списывается, и false значит «остаток ВЫШЕЛ». Первый писатель и
// читатель — модуль Artel (ломоть 4 M-72).
inline bool goal_spend(SquadGoal& g) {
    if (g.amount == 0) return true;   // 0 = бессрочно — значение, не дыра
    // Вычитание — дверью (ЗАКОН ТРЁХ ДВЕРЕЙ): семантический гейт выше
    // закрывает андерфлоу и сегодня, но закон места — насыщение, и он
    // обязан стоять в типе, а не держаться на соседней строке.
    g.amount = sub_sat(g.amount, std::uint16_t(1u));
    return g.amount > 0;
}

// Deterministic spawn ordinal for a persistent macro NPC (Inc 5e-2), assigned
// in creation order (0,1,2,…) by the SOLE creation path (make_npc) — the one
// identity that survives save/load. (Possession persists WITHOUT it: the
// player flag rides the macro snapshot as the record's own playerFlag
// column (5б) — no ordinal store, no re-derivation after load.)
//
// AND IT IS SAVE FORMAT. Since v23 the macro-ECS snapshot serializes every
// persistent macro NPC whole (macro/macro_snapshot.h → save.cpp
// write_macro_npc): this ordinal rides the blob, and the POD components
// around it — MacroNpcRuntime included — are written verbatim, so their
// LAYOUT is the format. A "runtime-only" field added to any of them is not
// free: it changes the blob and pays a kSaveVersion bump like Skills does.
struct MacroSpawnId { std::uint32_t index = 0; };   // 0 = «никто»: эмитент
                                    // выдаёт с 1 (закон нуля-ординала, M-37)

// (kPlayerSquadOrdinal = 0x7FFFFFFF УМЕР ЗДЕСЬ, M-255. Он был ВТОРЫМ
// носителем «кто игрок» и делал игрока единственной сущностью мира с
// идентичностью-ЛИТЕРАЛОМ. Оба его оправдания были пусты: столкновения с
// выдачей 1,2,3… не бывает и без резерва — эмитент `nextMacroSpawnOrdinal`
// персистентный и МОНОТОННЫЙ, ординалы не переиспользует (ровно за это его
// и вводили, v23); а «найти его снова после загрузки» делает КОЛОНКА.
// Теперь оба вопроса — биты одной колонки анкеты `PlayerFlag`
// (bus/store.h): `on` — «кем я на карте», `home` — «кто оригинал»;
// ординал игроку выдаёт тот же эмитент, что всякому скваду.)

// A waypoint route a squad was ORDERED onto (Session 15, Inc 7) — OPT-IN,
// and the route's presence IS the order (owner's ruling: no second knob):
// a squad with waypoints walks them in a loop instead of its type row's ai
// — a guard in the player's patrol walks the player's route, not his
// hometown beat — and a squad without this component (or with an empty
// route) never changed. New KINDS of squad AI are rows (NpcTypeDef.ai +
// a behaviour function), never fields here. Flat po2 array of macro
// cells: 8 × (x, y). Runtime-only until the S17 snapshot.
struct SquadOrders {
    std::uint8_t waypointCount = 0;
    std::uint8_t currentWaypoint = 0;
    std::array<std::int16_t, 16> waypoints{};   // 8 × (x, y) macro cells
};

// Метка дизайн-персонажа: ординал строки стола анкет (macro/characters.h
// kDesignCharacterDefs — стол авторских индивидов, owner 2026-09-10).
// OPT-IN, как orders: обычный сквад её не несёт. Ординал, не указатель и
// не копия строки — строка живёт в столе, сейв возит два байта (v92).
// int16 расчётом (ЗАКОН ТИПА): авторских фигур десятки, не тысячи; знак —
// чтобы «нет строки» в записи снапшота был честный −1, не магический max.
struct DesignCharacterTag {
    std::int16_t ordinal = -1;
};

} // namespace sm::ecs

// Включение стоит ЗДЕСЬ, а не наверху файла, по той же причине, что в
// `ecs/components.h`: заголовок пользуется контуром только в блоке ниже, а
// вставленная наверху строка сдвинула бы ВСЁ содержимое на единицу и сгноила
// бы каждую ссылку документов в этот файл. Номер строки — производное
// (§13 п.5), и дешевле не двигать его вовсе.
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА ────────────────────────────────────────────────
// Судит КОМПИЛЯТОР, а не ревью: строка мира, в которую заложили вектор, строку,
// карту, умный указатель, функтор или виртуальный метод, отсюда НЕ СОБЕРЁТСЯ, и
// сообщение назовёт тип поимённо (`core/row_law.h`, наряд M-169).
// Новая структура в этом файле обязана появиться и в этом списке — за полнотой
// списка следит `arch_guard_test`, иначе стену обходили бы молча, новым типом.
TIMAERT_ROW(sm::ecs::MacroCell);
TIMAERT_ROW(sm::ecs::CellSub);
TIMAERT_ROW(sm::ecs::NPCKind);
TIMAERT_ROW(sm::ecs::NpcCharacter);
TIMAERT_ROW(sm::ecs::SquadName);
TIMAERT_ROW(sm::ecs::NpcInventory);
TIMAERT_ROW(sm::ecs::BodyEquipment);
TIMAERT_ROW(sm::ecs::NpcTraits);
TIMAERT_ROW(sm::ecs::MacroNpcRuntime);
TIMAERT_ROW(sm::ecs::SquadGoal);
TIMAERT_ROW(sm::ecs::MacroSpawnId);
TIMAERT_ROW(sm::ecs::SquadOrders);
TIMAERT_ROW(sm::ecs::DesignCharacterTag);

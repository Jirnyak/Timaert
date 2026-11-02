// Place registry — every macroworld point-of-interest type the game can
// place lives in this single table. Adding a place = one entry (CANON S16).
//
// This is also THE place vocabulary (2026-08-24): the subworld's
// "CellPlaceKind" and the fauna router's "PlaceKind" were two more
// five-value copies of this enum, joined by a hand-written bridge — and the
// four kinds the copies could not name (Lair, Shrine, Mine, Tower) physically
// could not reach the microworld (canon-audit C1). One enum, everywhere.
// (The header's former first half — collect_places and its string-typed
// PlaceEntry — had no callers and is gone with place_registry.cpp.)
#pragma once
#include <cstdint>
#include "core/table_guard.h"
#include "tables/npc.h"   // NPCType — the crew rows below name who a place raises
#include "tables/squad_type.h"  // SquadType — строка объявляет, КОГО она поднимает

namespace sm {

// (Алиас `using SquadType = SquadType` УМЕР ломтиком F вместе со
// `std::vector<Landmark>` — ось рода одна и зовётся своим именем.)

// (kPlaceYieldOrder — приоритет владения спорной клеткой — УЕХАЛ к оси
// рода, tables/squad_type.h: его читает и обход мест, и ответ каркаса
// «кто здесь живёт», а ось — их единственный общий нижний слой.)

// Sentinels of the fauna columns below: "the GROUND answers" — the cell's own
// forest/biome row decides, the place states no override of its own.
inline constexpr std::uint16_t kPlaceFaunaGround    = 0xFFFFu;
inline constexpr std::uint8_t  kPlaceFaunaCapGround = 0xFFu;

// ── Crew rows: WHO this place raises (owner 2026-08-31, CANON S10) ────────
// ── Crowd role rows: fixed posts the street crowd fills FIRST (§42) ──────
// The guard/merchant/woodcutter prefix used to be a City/Village branch in
// the street spawner («первые N — стража» as code). Строки постов УМЕРЛИ
// ЦЕЛИКОМ 2026-09-22: Guard-строки ушли ещё с Инк 7 (уличная стража — это
// записи ГАРНИЗОНА места, никаких бесплатных тел), а после них колонка
// стояла пустой У ВСЕХ РОДОВ до единого — читатель в субмире был
// недостижим по данным. Пост как данные вернётся строкой, у которой есть
// хозяин, а не заготовкой в реестре.
struct PlaceDef {
    // MUST equal the row's index in kPlaces (guard below the table).
    SquadType     type;
    // ── КОЛОНКИ `id`, `label`, `glyph`, `color`, `actions` УЕХАЛИ 2026-11-01
    // В ЕДИНУЮ ТАБЛИЦУ РОДОВ `kSquadKinds@src/tables/squad_type.h` (ломоть 5б
    // M-235, вердикт владельца: таблица родов ОДНА на все сквады, спрайт —
    // просто число). Туда же уехал предикат `place_is_settlement` со своими
    // стражами — его дом теперь ось, а не реестр мест. ──────────────────────
    // ── КОЛОНКИ `minZone`/`maxZone` УЕХАЛИ 2026-11-02 К ГЕНЕРАТОРАМ
    // (`kSpireMinZone@src/macro/spires.h`, `kRuinMinZone@src/macro/ruins.h`):
    // их читала ТОЛЬКО генерация шпилей и руин, а числа сценария живут у
    // редактора-генератора (ЗАКОН АГНОСТИЧНОСТИ). Значения прочих родов
    // (город «0..76» и т.п.) не читал НИКТО — умерли молча. ────────────────
    // ── КОЛОНКА `feature` УЕХАЛА 2026-10-27 В СТРОКУ ФИЧИ (вариант В
    // M-235, вердикт владельца) ──────────────────────────────────────────
    // Она отвечала «какую фичу ставит этот род». Теперь на тот же вопрос
    // отвечает колонка `kind@src/macro/features.h` со стороны ФИЧИ, а оба
    // направления дают двери `place_kind_of_feature` и
    // `feature_of_place_kind` там же. Три довода за инверсию стоят у самой
    // колонки; коротко: фича по ЗАКОНУ ГЕНЕРАЦИИ п.6 ставится ДО сквада, у
    // мира на руках всегда БАЙТ, а не род, и строка фичи уже является ключом
    // модуля генерации субмира (вердикт 2026-10-24 про `FT_Capital`).
    // Включение `tables/feature_type.h` ушло вместе с колонкой: роду этой
    // таблицы словарь фич больше не нужен ни на байт.
    // (КОЛОНКА `walkable` УНИЧТОЖЕНА 2026-10-19, M-290, вердикт владельца
    //  «снести оба»: она объявляла «можно ли сюда войти», а вход в субмир
    //  субъекта не спрашивает ВООБЩЕ — `enter_subworld@src/app/main.cpp`
    //  берёт клетку под ногами. Владелец, дословно: «вход теперь у нас
    //  агностичный универсальный просто в клетку где стоишь это
    //  единстывыенный вход в субмир». Прод-читателей у неё был НОЛЬ:
    //  единственный — подмес `kMapActEnter` в `actions_of`, чей бит тоже
    //  никто не читал. Сирота DOD п.9 в чистом виде.)
    std::uint32_t    lightColor;  // ARGB night-emission tint (0 = does not glow)
    float            lightPop;    // synthetic emitter strength for fixed POIs;
                                  // 0 => inhabited type, scale by live population

    // КОЛОНКА `spawnFaction` УНИЧТОЖЕНА 2026-09-28 (M-39, вердикт владельца
    // 2026-09-21 «руине надо дать фракцию»). Она была ВТОРЫМ ответом на «чья
    // это руина»: строковая колонка ВИДА места побеждала колонку ЭКЗЕМПЛЯРА
    // `Landmark::factionIdx`, и держалась только тем, что ruins.cpp и
    // spires.cpp не ставили тот индекс вовсе — то есть была КОМПЕНСАЦИЕЙ
    // незаданной фракции, а не законом (DOD п.6, §7 «ОДИН РЕЕСТР ФРАКЦИЙ»).
    // Теперь руина и шпиль РОЖДАЮТСЯ со своим индексом, а лестница «чьё это
    // существо» — одна ступень: экземпляр места для его толпы и гарнизона,
    // строка вида (`wildFaction`) для дичи на земле. Заодно ушёл
    // `faction_index(строка)` с пути заселения (ЗАКОН СЛОВАРЯ).
    // Вид НЕ ЕСТЬ ВЛАДЕЛЕЦ — колонке фракции в этой таблице места нет.

    // КОЛОНКА `wealthMul` УНИЧТОЖЕНА 2026-10-18 (M-223, вердикт владельца
    // 2026-10-06: «богатсвто места уничтожить … снести не вижу в этом
    // смысла»). Она была КОНСТАНТОЙ ПО РОДУ (City 1.5, Spire/Mine/Lair 1.25,
    // Ruin 0.5, прочие 1.0), то есть отвечала «что это за место», а не
    // «насколько оно богато»: богатая и нищая деревня давали одно число.
    // Её собственное обоснование ссылалось на «the purse law», а кошелёк
    // строки (`purseMin`/`purseMax`) снесён 2026-09-26 (M-139) — у вердикта
    // 2026-08-27 «кошелёк по строке × богатство места» умер СУБЪЕКТ, и
    // модулировать стало нечего. Если богатство места понадобится впредь,
    // честный источник у него другой и живой — стоимость собственного
    // контейнера места (`inventory_value@src/macro/currency.h`), а не
    // константа строки.

    // ── Fauna spawn columns (read by THE spawn law, macro/fauna.cpp) ──────
    // What this place's ground rolls, as data — the three switch ladders over
    // SquadType that answered these questions in the spawn code are dead
    // (CANON S16, 2026-08-29).
    //
    // faunaHabitat — the habitat bit the place asks the body table for.
    //   kPlaceFaunaGround = no override (forest/biome answer); 0 = no wild
    //   fauna at all (a town's crowd is the settlement spawner's kHabTown
    //   stripe). The Ruin/Spire values are cross-checked against fauna.h's
    //   kHabRuin/kHabSpire right beside those bits' definitions.
    std::uint16_t    faunaHabitat = kPlaceFaunaGround;
    // The place's own head-count roll, min/max; faunaMax 0 = no override
    // (the forest/biome counts row rolls).
    std::uint8_t     faunaMin = 0;
    std::uint8_t     faunaMax = 0;
    // The honest-headcount capacity (fauna_cell_capacity, macro_stock
    // baseline). kPlaceFaunaCapGround = the winning counts row's max; a
    // number is the place's own cap — a town is the poorest hunting ground
    // that still is one (the old ruin row's minCount, 2).
    std::uint8_t     faunaCap = kPlaceFaunaCapGround;
    // crowdHabitat — the habitat bit of this place's CROWD: its population
    // made visible (street folk, a household behind a door, a garrisoned
    // hall). A SECOND dictionary from faunaHabitat above, deliberately: a
    // city has a town crowd and no wild fauna, a spire has a demon crowd AND
    // the mountain's own beasts on its slopes. 0 = the place keeps no crowd
    // (§42: the crowd's family is the place's COLUMN, never a name baked
    // into the picker). Cross-checked against fauna.h beside the bits.
    std::uint16_t    crowdHabitat = 0;
    // For a kind whose interior GARRISONS from its population (dungeon kind
    // rows, placeGarrison): how many souls man the STOREYS — pop >> this;
    // the rest are the crowd OUTSIDE (the owner's eye, 2026-09-11: «снаружи
    // больше сотни, внутри десятки на ярус» — the street is the throng, the
    // tower keeps a quarter). 0 = everyone outside — the honest default for
    // a kind with no garrisoned interior (a ruin's crowd haunts its
    // surface). A po2 shift, not a percentage.
    std::uint8_t     crowdInsideShift = 0;
    // ── Born souls (§42 Инк 5): the genesis law's own columns ─────────────
    // ── КОЛОНКИ `bornPopBase`/`bornPopPerScore` УНИЧТОЖЕНЫ 2026-10-26 ────
    // (M-320, вердикт владельца: «Снести колонки целиком».)
    //
    // Они объявляли, с каким населением рождается ДАНЖ, и попутно служили
    // ПРЕДИКАТОМ «души этого рода суть головы его инвентаря, а не число
    // клетки». Оба смысла умерли вместе с предметом: руина и шпиль стали
    // чистыми фичами, сквада у них нет ни одного, а прочие роды оси несли в
    // этих колонках ноль — то есть предикат не был истинен НИГДЕ.
    //
    // ЧТО УШЛО ВМЕСТЕ С НИМИ, ПОИМЁННО: `place_born_population` (дверь
    // рождения душ данжа), `regrow_dungeon_populations@src/macro/world_tick.h`
    // (проход отращивания), и ЧЕТЫРЕ двухсторонние ветки, ставшие
    // односторонними — `souls_flock` и `settle_souls@src/macro/labour.h`,
    // `shift_home_flock@src/macro/squad.h`,
    // `write_creatures@src/macro/macro_stock.cpp` (ряд `population` снесён
    //  целиком M-324 — он дублировал её же).
    //
    // ЧТО ЭТО КУПИЛО: паства перестала быть вопросом с двумя ответами.
    // Теперь она ЧИСЛО ФИЧИ под клеткой, и смысл ему придаёт ЧИТАЮЩИЙ
    // МОДУЛЬ (ЗАКОН ПОЛЯ п.3) — ровно то, что канон и требовал.
    // Населять и отращивать данж будет его СОБСТВЕННЫЙ модуль
    // (CANON S28, механизм 2); гарнизона как механизма нет вовсе.
    // (Колонка `worldPlaces` УМЕРЛА 2026-10-18, вердикт владельца «снести».
    //  Она объявляла, РАССТАВЛЯЕТ ли генезис этот род, — то есть говорила
    //  о том, КАК эту таблицу используют, при нуле прод-читателей: сами
    //  размещатели (`src/macro/spires.cpp`, `src/macro/ruins.cpp`) берут
    //  род ЛИТЕРАЛОМ и колонку не спрашивали ни разу. ЗАКОН АГНОСТИЧНОСТИ
    //  п.3 запрещает это прямо: «в фундаментальной системе появилась
    //  константа, чей вывод упирается в СЦЕНАРИЙ использования». Решает
    //  «какие роды ставить» СЛОЙ ВЫШЕ — он и есть редактор.)
    // ── The GARRISON law (§42 Инк 7, owner: «гарнизон = армия ландмарка»)
    // Target strength = population >> this; 0xFF = the kind keeps no
    // garrison (the honest default — a spire's «garrison» is its
    // population, Инк 4). The garrison is the place's OWN army in the ONE
    // container form (область существ его Inventory, M-71): born with the
    // place, fed
    // and paid by it, thinned by street deaths through the Garrison stock,
    // hired from, patrolled out of. The old law — √pop×0.3 capped at 10 —
    // sized a tavern recruit pool, not a defense force, and the pop/10
    // street-guard fiction painted over it.
    std::uint8_t     garrisonShift = 0xFF;
    // (`econSite` УМЕР 2026-09-18 вместе с EconSite. Он отвечал «по какой
    // ТАБЛИЦЕ это место работает», то есть на вопрос «что оно умеет» — а
    // отвечает на него теперь АНКЕТА места: characters.h place_sheet,
    // ранги ремёсел против ремесла и ранга рецепта. Место, которое не умеет
    // ничего, — анкета в нулях; законный ноль S6 остался на месте, просто
    // перестал быть отдельной колонкой.)

    // ── КОЛОНКА `labourShift` СНЕСЕНА 2026-11-01 (вердикт владельца: «да
    // сноси специализацию сквдов мы будет делать лучше если понадобитсч
    // чисто просто создавая новый типа и модуль сквадов под каждую
    // профессию»). Она была СИРОТОЙ СО СТРАЖЕМ: ноль прод-читателей — у
    // закона труда (owner 2026-08-31, CANON S10) крю-половина казнена
    // ломтём 4 M-72, и субъект колонки умер раньше неё. Решение «сколько
    // рук у рода» возьмёт себе МОДУЛЬ рода своим числом, когда у числа
    // появится читатель; специализация рода = НОВЫЙ ТИП + модуль, никогда
    // колонка-настройка. Страж log2(kCreaturesPerCityWorker) умер с ней,
    // включение macro/econ_day.h — тоже. ─────────────────────────────────
    // ── АМБАР РОЖДЕНИЯ: ДВА ЧИСЛА РОДА, А НЕ БУЛЕВ РОД В АРГУМЕНТЕ ────────
    // Здесь их не было: `seed_place_inventory(..., bool isCity)` тащил
    // РОД булевым параметром и сам решал по нему оба числа (`isCity ? 32 : 8`
    // и `isCity ? 1 : 2`) — ветка по роду внутри фундаментальной двери,
    // ровно то, что запрещает ЗАКОН СТРОКИ КАТАЛОГА. А оба звонящих
    // передавали ТАВТОЛОГИЮ: спрашивали род у слота там, где литералом
    // только что его туда и записали.
    //
    // ЗАЧЕМ ИМЕННО ЭТИ ДВА (DOD п.9 «кто читает и зачем»): их читает
    // `seed_place_inventory@src/macro/econ_day.cpp` — единственный
    // читатель, — чтобы ответить «с чем сквад рождается посреди жизни».
    // Числа БАЛАНСНЫЕ (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ: не по-двойки, значит
    // контент), и по вердикту владельца 2026-10-06 они СОДЕРЖИМОЕ модуля
    // рода — здесь они стоят до эпика M-72/M-235 и уедут в модуль с ним.
    // Дефолты — деревенские: сквад, про который ничего не сказано, рождается
    // скромно, а не богато.
    std::uint8_t     seedComfortDays = 8;   // дней бюджета комфорта в амбаре
    std::uint8_t     seedRawMult = 2;       // множитель сырьевых буферов
};

// Night-light columns (lightColor / lightPop) drive the universal macro
// lighting system (macro_lighting.cpp). Inhabited types (city, village) emit
// the TS warm hearth tint scaled by live population (lightPop = 0). Fixed POIs
// emit their own tint at a synthetic strength. Set lightColor = 0 to opt a type
// out of night glow entirely — the single data-driven gate.
// (Перевод полос 0..9 в байты опасности — owner 2026-08-24 — уехал вместе с
//  колонками зон к генераторам: kSpireMinZone/kRuinMinZone несут его в своих
//  шапках.)
// ── ПОЛЯ НАЗЫВАЮТСЯ, А НЕ СЧИТАЮТСЯ (DOD п.9) ───────────────────────────
// Строки ниже стояли ПОЗИЦИОННЫМИ, и колонки им подписывали ad-hoc
// комментарии (`/*wealth*/`, `/*hab*/`, `/*born*/`) — то есть имя поля
// существовало для ЧИТАТЕЛЯ и не существовало для КОМПИЛЯТОРА. Цена этой
// формы измерена (шрам города; сама система крю казнена ломтём 4 M-72):
// счётчик `crewCount` не убавили, вырезая строку, её место занял ДЕФОЛТ, и
// призрачная артель держала треть мировой добычи дерева. Промах на единицу —
// родовой дефект позиционного агрегата: новая колонка в середине садится в
// соседнее поле МОЛЧА, а мир остаётся правдоподобным.
// С именованными инициализаторами этот класс дефекта ловит СБОРКА, и граница
// названа ЗАМЕРОМ, а не на глаз: порядок полей судит `-Wreorder-init-list`
// (`timaert_build_flags`, CMakeLists.txt) — clang предупреждает, GCC/MinGW
// ОТВЕРГАЕТ, и по §9 «ноль предупреждений» предупреждение здесь и есть стоп.
// Пропуск поля даёт объявленный дефолт ЯВНО (проверено мутацией: шесть полей,
// дописанных шпилю значениями их же дефолтов, не двинули ни одного из четырёх
// TSV замера). Переезд строк в ИИ-модули рода (M-235) становится
// механическим: поля называются, значит их нельзя пересчитать.
inline constexpr PlaceDef kPlaces[std::size_t(SquadType::Count)] = {
    // ТИП БЕЗ НАЗНАЧЕНИЯ — ХОДОК. Его имя, вид и глаголы — строка
    // `kSquadKinds`; здесь остались только колонки, ждущие своих адресатов
    // (ломоть 5б M-235): зоны — генераторам, свет и фауна — строке фичи,
    // crowd*/garrison — модулям генерации субмира, seed* — модулю рода.
    {.type = SquadType::None,
     .lightColor = 0x00000000u, .lightPop = 0.0f },
    // СТОЛИЦА — ГОРОД СТАРШЕГО РАНГА, И ПОКА ЧИСЛА У НЕЁ ГОРОДСКИЕ. Это
    // не лень, а ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ: различие столицы — предмет
    // ЕЁ МОДУЛЯ (ломоть 5) и ЕЁ ГЕНЕРАТОРА субмира (строка `FT_Capital`),
    // а выдумывать ей сейчас второй набор балансных чисел значило бы
    // назначить различие числом раньше, чем оно названо системой. Своё у
    // неё ровно то, что уже СИСТЕМНО: фича-ключ генератора и глиф/цвет.
    //
    // ДО ЭТОЙ СТРОКИ СТОЛИЦА БЫЛА ГОРОДОМ БЕЗ СЮЗЕРЕНА — родом, выведенным
    // из ОТСУТСТВИЯ ребра связи. Выведенный из отсутствия род невидим всем,
    // кто читает колонку, и именно поэтому владелец дал ему строку.
    //
    // РОЖДАТЬ ЕЁ ПОКА НЕКОМУ, И ЭТО НАЗВАННАЯ ДЫРА, А НЕ КОСТЫЛЬ (§1):
    // расселение ставит столицу модулем места (ломоть 5); до тех пор строка
    // стоит голой, а `== SquadType::City`-гейты мира остаются верными по
    // пустоте — переспросить их придётся ровно в день первой столицы.
    {.type = SquadType::Capital,
     .lightColor = 0xFFFFC76Bu, .lightPop = 0.0f,
     .faunaHabitat = 0u, .faunaMin = 0, .faunaMax = 0, .faunaCap = 2,
     .crowdHabitat = 1u << 14, .crowdInsideShift = 0,
     .garrisonShift = 3,
     .seedComfortDays = 32, .seedRawMult = 1 },
    {.type = SquadType::City,
     .lightColor = 0xFFFFC76Bu, .lightPop = 0.0f,
     .faunaHabitat = 0u, .faunaMin = 0, .faunaMax = 0, .faunaCap = 2,
     .crowdHabitat = 1u << 14, .crowdInsideShift = 0,
     .garrisonShift = 3,
     .seedComfortDays = 32, .seedRawMult = 1 },
    {.type = SquadType::Village,
     .lightColor = 0xFFFFC76Bu, .lightPop = 0.0f,
     .faunaHabitat = 0u, .faunaMin = 0, .faunaMax = 0, .faunaCap = 2,
     .crowdHabitat = 1u << 14, .crowdInsideShift = 0,
     .garrisonShift = 3,
     .seedComfortDays = 8, .seedRawMult = 2 },
    // Spire wild fauna returned to the GROUND (§42 Инк 5): its demons are
    // its POPULATION now — the mountain's own beasts roam the slopes, and
    // clearing the tower can never again be ambiguous between garrison and
    // game. The Ruin row keeps kHabRuin: that bit is ALSO the den
    // dictionary (what creeps into cellars and caves), and a ruin's ground
    // honestly crawls.
    {.type = SquadType::Spire,
     .lightColor = 0xFFA86CFFu, .lightPop = 200.0f,
     .faunaHabitat = kPlaceFaunaGround, .faunaMin = 0, .faunaMax = 0,
     .faunaCap = kPlaceFaunaCapGround,
     .crowdHabitat = 1u << 13, .crowdInsideShift = 2,
     },
    {.type = SquadType::Ruin,
     .lightColor = 0xFF8E8576u, .lightPop = 40.0f,
     .faunaHabitat = 1u << 12, .faunaMin = 2, .faunaMax = 6,
     .faunaCap = kPlaceFaunaCapGround,
     .crowdHabitat = 1u << 12, .crowdInsideShift = 0,
     },
    {.type = SquadType::Lair,
     .lightColor = 0xFF883A3Au, .lightPop = 70.0f,
     .faunaHabitat = kPlaceFaunaGround, .faunaMin = 0, .faunaMax = 0,
     .faunaCap = kPlaceFaunaCapGround,
     .crowdHabitat = 1u << 12 },
    {.type = SquadType::Shrine,
     .lightColor = 0xFFE2E2E2u, .lightPop = 90.0f },
    {.type = SquadType::Mine,
     .lightColor = 0xFF8B6332u, .lightPop = 60.0f },
    {.type = SquadType::Tower,
     .lightColor = 0xFF6E6E89u, .lightPop = 80.0f },

    // ── ПОДВИЖНЫЕ РОДЫ ОСИ: ОСТАЛЬНОЕ ЧЕСТНО ПУСТО ───────────────────────
    // Артель, корован, сборщик и дракон стоят на ТОЙ ЖЕ оси, что места
    // (одна ось у одной популяции), и колонок МЕСТА не имеют: они не стоят
    // на клетке, не светятся ночью и не держат фауну. Их строки выписаны
    // ЯВНО, а не оставлены на доинициализацию нулями, по двум причинам:
    // (1) `rows_in_enum_order` ниже сверяет колонку `type` с индексом, и
    // неявная строка приехала бы с `None` на месте `Artel` — молчаливое
    // расхождение таблицы с собственным ключом; (2) пустая строка обязана
    // быть ВИДНА как решение, а не как пропуск. Это цена, названная в шапке
    // `squad_type.h`: платим пустотой за то, что `place_def` остаётся
    // ПОЛНОЙ функцией без гейта по роду. (Имя, вид и глаголы встречи у них
    // — строки `kSquadKinds`, вместе с шрамом M-281 о литерале в ветке.)
    {.type = SquadType::Artel,
     .lightColor = 0x00000000u, .lightPop = 0.0f },
    {.type = SquadType::Caravan,
     .lightColor = 0x00000000u, .lightPop = 0.0f },
    {.type = SquadType::Collector,
     .lightColor = 0x00000000u, .lightPop = 0.0f },
    {.type = SquadType::Dragon,
     .lightColor = 0x00000000u, .lightPop = 0.0f },
};
static_assert(rows_in_enum_order(kPlaces, &PlaceDef::type),
              "kPlaces row order must mirror SquadType");

// (ЗАКОН ГЛАГОЛОВ — «место не говорит и не бьётся, ходок говорит, торгует и
//  бьётся» — УЕХАЛ 2026-11-01 вместе с колонкой `actions` к единой таблице
//  родов: страж стоит под `kSquadKinds@src/tables/squad_type.h`.)

// РАЗМЕР СТРОКИ НАЗВАН — ЗНАЧИТ ПРИБИТ (DOD п.10). Замер: проба
// static_assert на clang, дерево m39-faction, 2026-09-28 — 120 байт.
// Свидетель тут не про байты: их девять строк и они constexpr, экономить
// нечего. Он про то, ЧТО СЮДА НЕЛЬЗЯ ДОЛИВАТЬ КОЛОНКУ МОЛЧА — и в первую
// очередь колонку фракции, снесённую выше как второй ответ о владельце
// (M-39). Возврат `const char* spawnFaction` перестаёт СОБИРАТЬСЯ, а не
// перестаёт проходить тест: запрет под компилятором не флачит и не
// подгоняется (§8 п.6). Строку меняют осознанно — вместе с этим числом.
//
// ЧИСЛО ДВИНУЛОСЬ 120 → 112 ПРИ СНОСЕ `walkable` (2026-10-19, M-290), И ЭТО
// ОБЪЯСНЕНИЕ, КОТОРОГО АССЕРТ И ТРЕБУЕТ. Замер `offsetof`/`alignof` у clang,
// а не рассуждение: `alignof` = 8, последнее поле `seedRawMult` стоит на
// 109, то есть данных 110 — и 112 есть ближайшее кратное восьми. Снос
// одного байта `bool` унёс восемь, потому что он стоял между двумя
// четырёхбайтными полями (`color` и `lightColor`) и тащил на себе три
// байта выравнивания.
//
// ПРЕДЫДУЩИЙ АБЗАЦ ОСТАЁТСЯ ВЕРНЫМ И ПОЛЕЗНЫМ: при сносе `wealthMul`
// (2026-10-18, M-223) число НЕ двинулось вовсе — четыре снесённых байта
// целиком съело ХВОСТОВОЕ выравнивание. Отсюда правило, подтверждённое уже
// дважды с разным знаком: довод «уберём колонку — станет меньше» в этой
// строке НЕ ВЫВОДИТСЯ из числа байт колонки, а считается по раскладке; и к
// сносу он отношения не имеет ни разу — обе колонки умерли за отсутствие
// ЧИТАТЕЛЯ, а не за размер.
// КАЗНЬ АУКЦИОНА (ломоть 4 M-72, вердикт 2026-10-04): `crews[8]` (48 Б
// шестибайтных строк) и `crewCount` умерли вместе с аукционом дневной
// ротации (не путать с аукционом найма M-38/M-40) —
// 112 → 80, разница 32 = 49 байт данных минус хвостовое выравнивание до
// кратного восьми (данных осталось 78).
// СНОС РОЖДЕНИЯ ДУШ ДАНЖА (M-320, 2026-10-26, вердикт владельца «снести
// колонки целиком»): `bornPopBase` и `bornPopPerScore` (два u16 = 4 Б
// данных) — 80 → 72. Разница 8, а не 4: четыре байта данных плюс четыре
// байта хвостового выравнивания до кратного восьми, которые эти колонки
// собой занимали. Третий случай подряд, где число не выводится из ширины
// колонки, — и снова снос не ради байт: у колонок умер ПРЕДМЕТ.
// ОТЪЕЗД КОЛОНКИ `feature` (2026-10-27, вариант В M-235) ЧИСЛО НЕ ДВИНУЛ
// ВОВСЕ: снятый байт целиком съело ХВОСТОВОЕ выравнивание — последнее поле
// `seedRawMult` стояло на 70, данных было 71, и 72 есть ближайшее кратное
// восьми что до, что после. Четвёртый случай подряд в этой строке, где
// довод «уберём колонку — станет меньше» НЕ ВЫВОДИТСЯ из ширины колонки;
// и к отъезду он отношения не имел ни разу — колонка уехала за ПОРЯДОК
// ЗАКОНА (фича ставится до сквада), а не за байты. На приёмной стороне
// картина та же: `sizeof(FeatureDef)` 40 → 40, колонка села в паддинг.
// ПЕРЕЕЗД ИМЕНИ, ВИДА И ГЛАГОЛОВ К ЕДИНОЙ ТАБЛИЦЕ РОДОВ + СНОС labourShift
// (2026-11-01, ломоть 5б M-235): 72 → 24, И ВПЕРВЫЕ УПАЛ САМ `alignof` —
// 8 → 4: оба `string_view` (единственные восьмибайтно выровненные члены)
// уехали в `kSquadKinds`, и хвостовое выравнивание считается теперь до
// кратного четырёх. Последнее поле `seedRawMult` стоит на 23 — данных 24
// ровно, нуль хвоста.
// ОТЪЕЗД `minZone`/`maxZone` К ГЕНЕРАТОРАМ (2026-11-02) ЧИСЛО НЕ ДВИНУЛ:
// оба байта стояли в зазоре между `type` (офсет 0) и `lightColor` (офсет 4,
// align 4) — их место заняло выравнивание. Пятый случай подряд, где довод
// «уберём колонку — станет меньше» не выводится из ширины колонки.
static_assert(sizeof(PlaceDef) == 24,
              "PlaceDef layout changed — say WHICH column and why, and "
              "never re-add a faction column: a KIND is not an OWNER (M-39)");

// ── СТРАЖ ПОЗИЦИОННОЙ СТРОКИ: ДВА АМБАРНЫХ ЧИСЛА СТОЯТ ГДЕ ОБЕЩАНО ───────
// Строки этой таблицы инициализируются ПОЗИЦИОННО, и новая колонка в хвосте
// — ровно тот промах на единицу, который числами не виден: значение село бы
// в соседнее поле, мир остался бы правдоподобным, а свидетель замера молчал
// бы. Поэтому пин под КОМПИЛЯТОРОМ, а не в тесте: неверная раскладка
// перестаёт СОБИРАТЬСЯ (§8 п.6 — лучшая форма свидетеля).
// Числа БАЛАНСНЫЕ и менять их можно; менять ВМЕСТЕ с этой строкой.
static_assert(
    kPlaces[std::size_t(SquadType::City)].seedComfortDays == 32
        && kPlaces[std::size_t(SquadType::City)].seedRawMult == 1
        && kPlaces[std::size_t(SquadType::Village)].seedComfortDays == 8
        && kPlaces[std::size_t(SquadType::Village)].seedRawMult == 2,
    "амбар рождения: город — сезон комфорта и одинарное сырьё, деревня — "
    "восемь дней и двойное (значения перенесены из булева параметра "
    "seed_place_inventory один в один)");

inline constexpr const PlaceDef& place_def(SquadType t) {
    return kPlaces[std::size_t(t)];
}

// ── ЗДЕСЬ ЖИЛА `place_kind_of_feature`, И ОНА УЕХАЛА К СВОЕЙ КОЛОНКЕ ─────
// Вариант В M-235 (2026-10-27). Дверь отвечала «чей это род, если на клетке
// стоит такой байт», и делала это ОБХОДОМ строк этой таблицы в поисках
// совпавшей фичи — то есть частый вопрос мира (сборка фактов клетки, два
// генезиса, постановка шахт) платил обходом, пока редкий вопрос рождения
// читал колонку. После инверсии всё наоборот и дешевизна стоит там, где
// спрашивают часто: дверь живёт в `src/macro/features.h` и ЧИТАЕТ КОЛОНКУ
// `kind` строки фичи, а её круговой страж переехал туда же — он тот же, но
// обходит таблицу фич и ловит заодно двойное клеймо с другой стороны.
// Имя у двери не изменилось НАРОЧНО: четыре прод-звонящих не тронуты ни
// одним символом, двинулось только включение (ЗАКОН ИМЕНИ п.5 — важно не
// имя, а смысл и системность).

// (ПРЕДИКАТ `place_is_settlement` УЕХАЛ 2026-11-01 к единой таблице родов —
//  `place_is_settlement@src/tables/squad_type.h`: он спрашивает ОСЬ и её
//  глаголы, а оба теперь живут там. Шрам M-281 — «предикат на чужом нуле
//  звал поселением артель и корован, панель паузила мир» — переехал с ним.)

// ── ЗДЕСЬ СТОЯЛА `place_born_population`, И ОНА УМЕРЛА С M-320 ────────────
//
// Дверь считала души, с которыми ДАНЖ РОЖДАЕТСЯ: mean = base + perScore ×
// своё число рода (тир спелла у шпиля, байт опасности у руины), дискретным
// колоколом вокруг среднего. Звонящих у неё было РОВНО ДВА — генезис шпиля и
// генезис руины, — и оба сняты вердиктом владельца 2026-10-26: шпиль «да под
// снос», руина «под снос как шпиль, чистая фича без сквада». Рождать души
// стало некому, потому что рождать СКВАД стало некому.
//
// УДАЛЕНА, А НЕ ОСТАВЛЕНА «НА ПОТОМ» (§6: легаси не живёт; §5 п.14: форма
// владельца подразумевает снос — значит снос и есть работа). Понадобится
// данжу население — его даст ЕГО СОБСТВЕННЫЙ ГЕНЕРАТОР (CANON S28, второй
// механизм наполнения), а не колонка строки сквада, которого нет.
//
// И КОЛОНКИ `bornPopBase`/`bornPopPerScore` УШЛИ СЛЕДОМ В ТОТ ЖЕ ДЕНЬ
// (вердикт владельца: «Снести колонки целиком») — надгробие и полный список
// того, что с ними умерло, стоит на их месте в `PlaceDef` выше.

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла, и это не небрежность:
// заголовок пользуется контуром только в блоке ниже, а вставленная
// наверху строка сдвинула бы ВСЁ содержимое на единицу и сгноила бы
// каждую ссылку документов в этот файл (замер: одна такая вставка в 17
// заголовков сломала 10 ссылок в SKELETON.md и промтах). Номер строки —
// производное (§13 п.5), и дешевле не двигать его вовсе.
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА ────────────────────────────────────────────────
// Судит КОМПИЛЯТОР, а не ревью: строка мира, в которую заложили вектор, строку,
// карту, умный указатель, функтор или виртуальный метод, отсюда НЕ СОБЕРЁТСЯ, и
// сообщение назовёт тип поимённо (`core/row_law.h`, наряд M-169).
// Новая структура в этом файле обязана появиться и в этом списке — за полнотой
// списка следит `arch_guard_test`, иначе стену обходили бы молча, новым типом.
TIMAERT_ROW(sm::PlaceDef);

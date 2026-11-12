// THE resource fields of the world — the owner's causality made structure:
//
//   клеточный мир → рельеф → климат → РЕСУРСЫ → и только потом заселение.
//
// A resource field is a per-cell quantity over the whole map whose BASELINE
// is a pure function of terrain/climate + the world seed. Features and
// places NEVER enter a baseline: settlement is downstream of resources
// (a village sits by good land; good land does not appear because a village
// sat down). What settlement does is decide WHERE a resource gets embodied
// (ploughed parcels manifest the wheat a cell could always grow).
//
// STORAGE IS ONE SHAPE FOR EVERY ROW (owner, 2026-09-16, CANON S5 amended):
// a flat array over the world's cells — ResourceGrid below. There is no second
// dialect. What differs between rows is only WHAT the cell holds, and the
// registry already says which:
//   · a row with NO baseline is its own stock — и такой ряд остался ОДИН,
//     лес: клетка ЕСТЬ живой счёт, и он может перерасти девственный вывод,
//     потому что нетронутая земля зарастает в чащобы. Жилы были здесь же до
//     2026-09-22 и переехали в строку ниже: у руды теперь ЕСТЬ базовая.
//   · a row WITH a baseline is capacity minus what play took (wheat, fauna):
//     the cell holds the SCAR, and the world tends back to the baseline. A
//     scar subtracts correctly from any embodied yield a subworld invents;
//     a remaining-count could not, which is why these two rows carry one.
//
// THE DIALECT THAT DIED, and why it is worth a paragraph: three of the seven
// rows used to live in `unordered_map<cellIdx, value>`, blessed by a line of
// S5 that allowed "sparse storage as an implementation". A hash keeps the
// values and throws away the CONNECTEDNESS of the world — it answers "what is
// at exactly this key" and nothing else — so the day geology was asked "is
// there a vein near this cell" the answer became a scan of all 69 624 veins,
// per cell, 4.3 ms of a 6.8 ms seam crossing (problems.md §52). The array
// costs 25 MB more and IS the torus: index arithmetic is adjacency.
//
// The macro_stock ledger stays THE gameplay read/write door — its rows call
// through here.
#pragma once

#include "tables/seasons.h"
#include "tables/resource_field_id.h"   // словарь РЯДОВ — имена, чьи строки ниже
#include "core/caps.h"    // kWorldCellCap — кап клеток мира, один на слои
#include "core/torus.h"   // cell_of — адрес клетки мира, одна дверь
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sm {

// A profession works ITS OWN village's ground: half the live worlds' village
// spacing (~21-23 cells, derive_city_spacing/2) rounded to po2 — a work trip
// stays inside the home hinterland, never the neighbour's. The placement side
// reads the same number: a village is sited so the ore it is meant to work
// lies inside this reach, and the man who walks there can actually reach it.
//
// It lives HERE, with the resource rows, rather than with the AI that walks
// it, because several modules measure by it on both sides: the gatherer's
// errand (npc_ai.h) and village siting (settlement_score.cpp, spawners.cpp).
// A number they must all agree on belongs below all of them.
inline constexpr int kGathererReach = 16;

struct GameState;
struct TerrainData;
struct TreeLayer;
struct MacroWorld;
// Колонка выхода ряда несёт РОД СУЩЕСТВА добычи; полного стола существ этому
// заголовку не нужно — ширина у перечисления фиксирована (tables/npc.h).
enum class NPCType : std::uint8_t;

// (Словарь ResourceFieldId ПЕРЕЕХАЛ в tables/resource_field_id.h ломтём 1а
// эпика пакетов v1: его называет КОЛОНКОЙ строка каталога фич, а каталог по
// закону M-181 живёт нейтральным слоем и не смеет включать macro/. Здесь
// остались СТРОКИ реестра и их гриды — изменяемое состояние мира.)

// The wheat row's SCALE: the fertility channel (0..255) maps onto this many
// stands in a cell. It lives here, at the row's own door, because everyone
// who prices fertility must price it the same way — the field stamp's
// ploughable bar and the settlement score's arable term both derive from
// this number rather than each re-deriving one of their own.
constexpr int kMaxWheatStandsPerCell = 4096;   // po2 scale of the estimate

// ── The ONE growth/diffusion law (owner, R2 track) ────────────────────────
// Every field is BORN from time and context through the same walker; the
// nuances are the row's data, never a second system. The map is walked in
// EPOCH slices: a cell is due once per kGrowthEpochDays, and one game day
// processes cells/32 of the domain — smooth long-term dynamics need no
// full-map day and no per-tick work.
//
// kGrowthEpochDays = 32: the year is 128 days = 4 seasons × 32 — the same
// month the path grid is re-baked on (Session 21), and exactly the old
// wheat/fauna healing period, so their law (+1 per visit) is preserved by
// construction.
// THE season (owner, 2026-08-24: «все росты раз в сезон» — one rhythm for
// everything that grows: forest, beasts, wheat, iron, and the towns quote
// their population rate by the same period in econ_day.h). The epoch used
// to be its own literal 32 that merely HAPPENED to equal the season.
constexpr int kGrowthEpochDays = kDaysPerSeason;

// ── ЗДЕСЬ ЖИЛА `growth_cell_due`, И ОНА СНЕСЕНА КАК ВТОРОЙ ОТВЕТ (M-324) ──
//
// Вопрос «подошла ли эта клетка сегодня» у мира ОДИН, а ответов стояло ДВА:
// предикат `growth_cell_due(idx, day)` здесь и ШАГОВЫЙ ОБХОД среза в самом
// ходоке (`idx = day % kGrowthEpochDays; idx += kGrowthEpochDays` —
// `resource_fields_daily_growth@src/macro/macro_stock.cpp`, обе ветки
// `CarrierSpread` и `HealToBaseline`). Это DOD п.6 в чистом виде, и жив из
// двоих был ровно один: у предиката **НОЛЬ вызовов** в `src/` и в `tests/`.
//
// ПОЧЕМУ ШАГОВЫЙ ОБХОД ПРАВИЛЬНЕЕ, А НЕ ПРОСТО «ПОБЕДИЛ». Предикат заставляет
// СПРОСИТЬ каждую клетку карты, чтобы отсеять 31/32; шаг перечисляет ровно
// свой срез и не касается остальных — та же выборка за 1/32 работы. Поэтому
// снесён предикат, а не обход.
//
// Диагноз стоял в `SKELETON.md` («мёртвый дубль закона роста») и в наряде
// M-112 («`growth_cell_due` снести») — здесь он исполнен. Подтверждено
// КОМПИЛЯТОРОМ: переименование символа оставило сборку зелёной.

// ОДИН ЗАКОН РОСТА: ЗАЛЕЧИВАНИЕ К БАЗОВОЙ (владелец, 2026-09-22, дословно:
// «давай тогда метал не убывает как и фертильность… то есть та же механика с
// шахтой как с полем»).
//
// Ёмкость клетки есть чистая функция терраина и сида — она НЕ УБЫВАЕТ НИКОГДА.
// Убывает и заживает только ВЗЯТОЕ. Пшеница, лён, зверь, табун и вся руда
// ходят этим одним законом; отличается у них ровно диалект хранения (шрам
// против живого числа носителя), а не право на второй закон.
//
//   HealToBaseline — всё, у чего есть базовая: шрам заживает, живое число
//                    носителя дорастает обратно до ёмкости.
//   CarrierSpread  — ЕДИНСТВЕННОЕ ИСКЛЮЧЕНИЕ, и оно названо вслух: лес. У
//                    него базовой НЕТ, потому что нетронутая земля зарастает
//                    ГУЩЕ девственной (чащобы), и прибавка пропорциональна
//                    тому, что стоит в 3×3.
//
// (ЗДЕСЬ БЫЛО ЧЕТЫРЕ ДОМЕНА. `Geology` — рождение новых жил «где скудно» —
// вырезан 2026-09-22 вердиктом владельца: «НЕ БУДЕТ РОЖДАТЬСЯ ЖИЛ В ТЕЧЕНИЕ
// ПАРТИИ МЫ ЭТО ВЫРЕЗАЕМ». `None` умер вместе с ним: не осталось ряда, который
// не растёт. Вместе с `Geology` ушли обратная связь от дефицита, колонка
// хозяина рождения и колонка кома свежей жилы.)
enum class GrowthDomain : std::uint8_t { HealToBaseline, CarrierSpread };

struct ResourceFieldDef {
    const char* id;
    // The pure baseline: world context → capacity of one cell. Клетку ряду
    // называют ОДНИМ ЧИСЛОМ (ЗАКОН АДРЕСА п.2, M-462 ломоть 4): все хуки
    // ниже берут индекс, а не пару координат — ходок роста и пульт держат
    // индекс и раньше раскладывали его в пару, чтобы хук собрал его обратно.
    // Sparse rows read baseline − scar and clamp the scar to it; a carrier
    // row's baseline is its initial condition only (null: the carrier was
    // filled by worldgen and lives its own life from there).
    int (*baseline)(const MacroWorld& w, std::uint32_t cell);
    // The growth law. growthAt = units regained at a DUE cell this visit.
    // Pure and deterministic — context in, delta out; the walker owns dueness,
    // the write, and the clamp to the row's own baseline.
    GrowthDomain growthDomain;
    int (*growthAt)(const MacroWorld& w, std::uint32_t cell);
    // Carrier hooks — both set = the row's live state is a dense structure
    // outside the scar maps (trees: the TreeLayer grid the map renders).
    // Read is the current count; apply clamps to the row's own cap, writes
    // the carrier and bumps its revision. Sparse rows leave both null.
    int  (*carrierRead)(const MacroWorld& w, std::uint32_t cell);
    void (*carrierApply)(MacroWorld& w, std::uint32_t cell, int delta);
    // (Колонка `reachCells` — радиус производного поля «есть ли ряд рядом» —
    // стояла здесь и снята ломтём 4 ряда M-462 вместе с самим полем
    // досягаемости жил: читателя у вопроса в игре не было, колонку читал один
    // сторож. Вопрос об окрестности заводится вместе с первым читателем.)
    // ── ЧТО РЯД КЛАДЁТ В РУКИ (ломоть 1 M-72, словарь «поле→предмет» 3 → 1) ──
    // Читатель — транзакция field_harvest (ниже): умолчание выхода, когда
    // фича клетки не переопределила его своей строкой (FeatureDef::yieldItem —
    // лён поверх пашни, металл шахты поверх жилы). Строка — ключ АВТОРСТВА;
    // ординал кэшируется лениво рядом с таблицей (ЗАКОН СЛОВАРЯ), nullptr =
    // предметом ряд не добывается.
    const char* yieldItem;
    // Выход-СУЩЕСТВО (CANON S10 «лошадь — юнит»): добытое встаёт ДУШОЙ в
    // контейнер сквада, не предметом в сумку. NPCType::Count = выход не
    // существо. Обе колонки разом не живут: существо старше предмета.
    NPCType yieldCreature;
};

// The registry row (macro_stock.cpp owns the table).
const ResourceFieldDef& resource_field_def(ResourceFieldId f);

// (`resource_row_is_vein` и `deposit_kind_ordinal` УМЕРЛИ 2026-10-23 с осью
// родов жилы: ряд жилы один, спрашивать «который род» больше не у чего.)

// Give the world its own fields: the SCAR grid of every sparse-dialect row
// (a carrier row's grid stays empty — its live state is its carrier) and the
// ONE featureNumber layer (CANON S5 «слой разработки — ОДНО поле на всё»). Called
// where the world is born; idempotent, so a second call costs nothing.
// Which rows are sparse is the REGISTRY's answer, which is why this lives
// with the table and not in the state header — the load path, which cannot
// link the table, sizes only the rows whose cells the file actually carries
// (and a carrier row carries none, so the rule holds there by construction).
void allocate_world_fields(GameState& gs, int width, int height);

// ГЕНЕРАТОР ПОЛЯ ФАУНЫ — один раз при рождении мира (M-371, вердикт
// владельца: «его пишет СВОЙ ГЕНЕРАТОР один раз при рождении мира (биом +
// лес…)»). Заполняет `GameState::faunaField` природной ёмкостью клетки;
// дальше полем распоряжается живой мир (охота — выемка, рост — лечение к
// базовой, основание поселения — вычистка своей клетки).
struct MacroWorld;
void generate_fauna_field(MacroWorld& w);

// The current count of a cell. Sparse rows: capacity − scar, floored at
// zero, fail-closed without terrain. Carrier rows: the carrier's cell.
int resource_field_read(const MacroWorld& w, ResourceFieldId f,
                        std::uint32_t cell);

// ── ДВЕРЬ №1 ПУЛЬТА: ДВИЖЕНИЕ ПОЛЯ С ВОЗВРАТОМ ФАКТА (ломоть 1 M-72) ──────
// Move the field: negative spends, positive returns. Sparse rows clamp the
// scar to [0, baseline] and a healed cell self-erases; carrier rows clamp
// to their own cap and bump their carrier's revision.
//
// Возвращает ФАКТ — сколько поле реально сдвинулось, тем же знаком, что
// намерение (семейство drain, ЗАКОН ТРЁХ ДВЕРЕЙ: взять больше, чем есть,
// невыразимо; вернуть выше ёмкости — тоже). Прежнее имя resource_field_apply
// умерло: дверь без факта заставляла каждого звонящего читать поле самому,
// и 57 строк между read и apply у артели дюпали выемку двух сквадов.
int field_move(MacroWorld& w, ResourceFieldId f, std::uint32_t cell, int delta);

// ── ДВЕРЬ №2 ПУЛЬТА: ТРАНЗАКЦИЯ «ПОЛЕ → КОНТЕЙНЕР СКВАДА» ─────────────────
// Форма продиктована владельцем дословно: «списание из поля клетки ->
// зачисление в инвентарь по таблице соответствия -> возврат факта о добытом
// объёме. Если в поле ресурсов нет, транзакция возвращает 0, и модуль
// реагирует на этот факт сам».
//
// КРЕДИТ ПРЕЖДЕ ДЕБЕТА, атомарно: поле платит ровно то, что контейнер ПРИНЯЛ;
// отказ контейнера (нет слота, спина полна) поле не трогает. ВЫХОД решает
// ФИЧА КЛЕТКИ своей строкой (лён поверх пашни, металл шахты поверх жилы —
// вердикт 2026-10-22 «что там окажется решает фича шахты»), иначе умолчание
// ряда (yieldItem/yieldCreature). `want` — намерение звонящего (его руки);
// дверь клампит по «есть», по спине (товар) и по слоту (существо).
// Вход — АДРЕС контейнера (слот сквада), никогда голый Inventory& (вердикт
// 2026-10-22 п.3: второй носитель пишется дверью контейнера, разряд А).
int field_harvest(MacroWorld& w, ResourceFieldId f, std::uint32_t cell,
                  int want, std::uint32_t squadSlot);

// ЧТО ЗДЕСЬ КЛАДУТ В РУКИ — ординал ПРЕДМЕТА выхода транзакции на этой
// клетке: оверрайд фичи, иначе умолчание ряда; -1 = предметного выхода нет
// (выход существо или ряд без выхода). Тот же резолв, которым берёт
// field_harvest, — для скоринга целей и выгрузки домой (ЗАКОН СЛОВАРЯ:
// ординалы кэшированы у таблиц, в тике ходит число).
int field_yield_ordinal(const MacroWorld& w, ResourceFieldId f,
                        std::uint32_t cell);

// (`field_set` / `field_set_allocate` — абсолютная установка поля «вне пульта»
//  — УМЕРЛИ с проводом сейва, M-439. Звонящих у пары было три рода: рождение
//  мира, загрузка и фикстура; рождение давно ходит дверью
//  `allocate_world_fields`, а загрузка перестала быть обходом рядов — поле
//  едет в файле своими байтами. Геймплей полей по-прежнему НЕ СТАВИТ, а
//  двигает (`field_move`/`field_harvest`), и сырые `write`/`allocate` закрыты
//  списком друзей ниже.)
struct ResourceGrid;

// The raw scar of a cell (0 = unscarred) — what subworld generation
// subtracts from its own embodied yield (CellContext.cropHarvested).
// Sparse-dialect rows only; a carrier row has no scar and reads 0.
int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx);

// One daily step for EVERY field — THE growth/diffusion door: each row's
// due slice of its domain is visited, growthAt prices the birth, and the
// write goes through the row's own dialect (scar heal / carrier apply /
// genesis). Replaces both the old scar-heal walk and the bespoke iron
// discovery.
void resource_fields_daily_growth(MacroWorld& w, int day);


// ── THE FIELD OF ONE ROW ─────────────────────────────────────────────────
// CANON S5, as amended by the owner on 2026-09-16: every resource row owns
// ONE field over the world, and it is a flat array over the cells — the shape
// the tree layer has always had. There is no second storage dialect.
//
// WHY THE SHAPE IS THE POINT, not the speed. A flat array over the torus IS
// the connected world: `wrap(y)*w + wrap(x)`, so a neighbouring cell is
// neighbouring memory and "near" is arithmetic. A hash of cell indices keeps
// the values and throws the connectedness away — it answers "what is at
// exactly this key" and nothing else, so every question about a neighbourhood
// has to be rebuilt as a scan by whoever asks it next. That is not a
// hypothetical: geology was a hash from its first commit (81379bf6, whose own
// message claimed "the proven tree-layer discipline"), and the day someone
// asked it "is there a vein near this cell" the answer became 69 624 distance
// tests per cell — 4.3 ms of a 6.8 ms seam crossing, for one byte
// (problems.md §52).
//
// THE COST OF BEING RIGHT: seven rows × 1024² × 4 bytes = 28 MB, against a
// hash that held ~3-4 MB. Twenty-five megabytes, in a world whose own DOD law
// calls 48 MB "ни о чём" and which carries 256 inventory slots on each of
// 16 384 entities. Memory compression is a GATED decision here, never a
// default (AGENTS.md DOD rule 8).
//
// WHAT THE CELLS HOLD is the row's OWN state — the part that is not derivable
// — and the registry already says which that is: a row with no `baseline` is
// its own stock (trees, veins), a row with one is a baseline minus what play
// took, so its field holds the SCAR. One shape, one meaning per row, and the
// `baseline` column is the reader that was already there.
// ШИРИНА КЛЕТКИ РЕСУРСНОГО ПОЛЯ — `uint16` (владелец, 2026-09-22, дословно:
// «uint16 для ресурсов да будет так я одобряю ДЛЯ ВСЕХ НЫНЕШНИХ И БУДУЩИХ
// РЕСУРСОВ»).
//
// ЗАКОН ФОРМЫ, к которому это относится: поле есть плоский массив над тором с
// ОДНОЙ дверью записи; ширина ячейки — свойство РЯДА, минимальная из
// достаточных. Поэтому `uint8` у терраина, фич и зон, `float` у весов и
// `uint16` здесь — не три исключения, а один закон.
//
// ПОЧЕМУ НЕ `int32`. Считано по фактическим потолкам (сторож ниже): самый
// толстый ряд занимает четверть диапазона. Половина памяти — не главное;
// главное КЭШ: на линию 64 Б ложится 32 клетки вместо 16, и проходы, которые
// упираются в память (сезонный ходок роста), ускоряются вдвое. Это свойство скелета «соседняя клетка — соседняя память»,
// а не микрооптимизация.
//
// ПОЧЕМУ БЕЗЗНАКОВЫЙ. Счёт, ёмкость и шрам отрицательными не бывают (ЗАКОН
// ТИПА). Ноль — законная пустота ряда, поэтому сторожевое значение полю не
// нужно и весь диапазон 0…65535 рабочий.
using FieldCell = std::uint16_t;
inline constexpr int kMaxFieldUnitsPerCell = 65535;

// ПОТОЛКИ РЯДОВ, КОТОРЫЕ НЕ ЖИЛЫ (жилы сторожит deposit_layer.h): оба сильно
// ниже клетки, и оба закреплены здесь, чтобы ретюн шкалы не проехал мимо
// ширины молча. Пшеница — четверть шестнадцатой диапазона, лес — четверть.
static_assert(kMaxWheatStandsPerCell <= kMaxFieldUnitsPerCell,
              "шкала пашни переросла клетку поля");

// ── ПЛОСКИЙ МАССИВ ПОД КАПОМ КЛЕТОК МИРА (M-180, 2026-11-10) ─────────────
// Heap-члена у поля нет: клетки лежат в теле структуры, длиной в кап
// (`kWorldCellCap@src/core/caps.h`), и байты поля снимаются одним куском.
// Здесь стояли ДВА вектора. `cells` — и «родилось ли поле» отвечала его
// длина, второй носитель размера мира рядом с `width/height`; теперь носитель
// один — стороны. `reach` (счёт живых клеток ряда в радиусе) — производное,
// которое было непустым у ОДНОГО экземпляра из всех (дикая жила) и вечно
// пустым заголовком у остальных; оно уехало к слою жил и там снято вовсе
// (ломоть 4 ряда M-462 — читателя у него не было). У поля осталось то, что
// есть у КАЖДОГО поля: клетки и их счёт.
//
// Поле весит 2 МиБ и на стеке не живёт; мир меньше капа занимает первые
// side² клеток, хвост стоит нулём (его чистит `allocate`).
struct ResourceGrid {
    int width = 0, height = 0;
    // Live cells, maintained by write(). Never counted: a row is asked "how
    // many veins are left" often enough that an O(cells) answer would be a
    // second §52 waiting to happen.
    std::int32_t liveCells = 0;
    // Bumped on every write, for consumers that mirror the field (the map
    // renderer's tree texture). Едет в файле вместе с полем (прямой дамп).
    std::uint32_t revision = 0;
    FieldCell cells[kWorldCellCap] = {};

    // Клеток у ЭТОГО мира (0 — поле не родилось или мир вне капа).
    std::size_t cell_count() const {
        return world_cells_within_cap(width, height);
    }
    // ЖИВОСТЬ ВКЛЮЧАЕТ ИНВАРИАНТ МИРА (ЗАКОН АДРЕСА, AGENTS.md): сторона —
    // степень двойки, мир КВАДРАТЕН. Проверка стоит ЗДЕСЬ, а не комментарием
    // у `index`, потому что адрес считается маской: поле, родившееся не по
    // инварианту, отвечало бы не «медленно», а НЕВЕРНО и молча. Fail-closed —
    // ровно так же, как уже поступает неродившееся поле (стороны 0).
    bool live() const {
        return width > 0 && width == height
            && std::has_single_bit(std::uint32_t(width))
            && cell_count() != 0u;
    }
    // КЛЕТКУ ПОЛЮ НАЗЫВАЮТ ОДНИМ ЧИСЛОМ (ЗАКОН АДРЕСА п.2, M-462 ломоть 4).
    // Здесь стояли две формы чтения — `at(x, y)` со своей дверью `index` и
    // `at_index(i)` — и пара была основной: звонящий с индексом на руках
    // раскладывал его в координаты, чтобы поле собрало индекс обратно. Индекс
    // уже свёрнут по построению (`cell_of`, `cell_step`), поэтому маска ему
    // не нужна — нужна только граница памяти; у кого на руках геометрия,
    // сворачивает её дверью `cell_of` сам.
    int at(std::uint32_t cell) const {
        return live() && cell < cell_count() ? int(cells[cell]) : 0;
    }

private:
    // ── СТЕНА ЛОМТЯ 1 M-72: СЫРЫЕ ВХОДЫ ЗАКРЫТЫ КОМПИЛЯТОРОМ ────────────
    // Пишут поле только НОСИТЕЛИ ДВЕРЕЙ — пульт (field_move, и только он в
    // геймплее), рождение мира и двери носителей (числа фич,
    // жила). Пятая дверь не заводится одной строкой: ей нужна строка в этом
    // списке друзей, и компилятор молчать не будет. (12 сайтов write до
    // стены: 4 геймплейных ушли в двери, 1 генерация, 5 загрузка, 2 умерли
    // с consolidate_deposit_cluster — перепись 2026-10-23.)
    friend int  field_move(MacroWorld&, ResourceFieldId, std::uint32_t, int);
    friend void allocate_world_fields(GameState&, int, int);
    friend void feature_number_write(GameState&, std::uint32_t, int);
    friend void feature_number_add(GameState&, std::uint32_t, int);
    friend void fauna_field_write(GameState&, std::uint32_t, int);
    friend void light_emission_write(GameState&, std::uint32_t, int);
    // Жила: ОДНА дверь записи на весь слой (`deposit_set`) — рождение мира
    // и фикстуры идут через неё.
    friend void allocate_deposit_fields(struct DepositLayer&, int, int);
    friend void deposit_set(struct DepositLayer&, std::uint32_t, std::int32_t);

    // THE ONE WRITE. Everything a cell's value implies — the live count and
    // the revision — moves with it, here, because a derived number kept in
    // step by its CALLERS is one that drifts the first time somebody adds a
    // fifth caller.
    // Значение ЗАЖИМАЕТСЯ в ширину ряда прямо здесь, у единственной двери:
    // иначе перелив был бы невидимым — клетка молча заворачивалась бы в малое
    // число, и мир беднел бы без единого следа. Сторож ширины (ниже по файлу)
    // держит потолки рядов под этим числом, поэтому зажим — страховка на
    // случай арифметики, а не рабочий режим.
    void write(std::uint32_t i, int value) {
        if (!live() || i >= cell_count()) return;
        const FieldCell v = FieldCell(value < 0 ? 0
                                      : (value > kMaxFieldUnitsPerCell
                                             ? kMaxFieldUnitsPerCell : value));
        const FieldCell was = cells[i];
        if (was == v) return;
        cells[i] = v;
        if (was == 0 && v != 0) ++liveCells;
        else if (was != 0 && v == 0) --liveCells;
        ++revision;
    }

public:
    // Every cell that holds something, as (index, value). O(cells) by nature —
    // a dense field has no shorter honest way to enumerate itself, and the
    // callers that cannot afford it (the nearest-vein errand) must ask a
    // NEIGHBOURHOOD question instead of enumerating the world.
    template <class F>
    void for_each_live(F&& f) const {
        if (!live()) return;
        const std::size_t n = cell_count();
        for (std::size_t i = 0; i < n; ++i) {
            if (cells[i] != 0) f(std::uint32_t(i), cells[i]);
        }
    }

    // The lowest-indexed cell that holds something; kNoCell when the row is
    // empty. For the callers that want "any one of them" rather than all.
    static constexpr std::uint32_t kNoCell = sm::kNoCell;
    std::uint32_t first_live() const {
        if (!live()) return kNoCell;
        const std::size_t n = cell_count();
        for (std::size_t i = 0; i < n; ++i) {
            if (cells[i] != 0) return std::uint32_t(i);
        }
        return kNoCell;
    }

    // (Пара `x_of`/`y_of` и вторая форма чтения `at_index` стояли здесь и
    // сняты ломтём 4 ряда M-462: чтение одно и берёт индекс, а кому нужна
    // координата клетки для экрана или текста, спрашивает двери тора
    // `cell_x`/`cell_y`.)

    // Two fields are the same field when they cover the same world and hold
    // the same values. Сравниваются клетки МИРА, не кап: хвост за миром не
    // клетки (и по закону рождения он нулевой у обоих).
    bool operator==(const ResourceGrid& o) const {
        return width == o.width && height == o.height
            && std::memcmp(cells, o.cells,
                           cell_count() * sizeof(FieldCell)) == 0;
    }

private:
    // Родить поле над миром: стороны названы, ВЕСЬ кап обнулён (хвост за
    // миром обязан стоять нулём — иначе байты поля помнили бы прошлый мир).
    // Мир вне капа рождает поле без сторон — fail-closed, `live()` ложно.
    // За стеной вместе с write: поле рождается у носителей мира, не у
    // звонящего (ленивое взятие — признак нарушения, ЗАКОН СТАБИЛЬНОСТИ).
    void allocate(int w, int h) {
        const bool fits = world_cells_within_cap(w, h) != 0u;
        width = fits ? w : 0;
        height = fits ? h : 0;
        liveCells = 0;
        revision = 0;
        std::memset(cells, 0, sizeof(cells));
    }

};
static_assert(sizeof(ResourceGrid)
                  == 16u + sizeof(FieldCell) * kWorldCellCap,
              "поле ряда: 16 Б шапки + клетка на клетку капа, без хвоста");

// (Структура `ResourceFields` — «все поля рядов одним массивом» — снесена
// 2026-11-10, M-180: экземпляров у неё не было ни одного, стоял только сторож
// её размера. Поля рядов живут поимённо в `GameState`.)

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла: заголовок пользуется контуром
// только в блоке ниже, а вставленная наверху строка сдвинула бы всё содержимое
// и сгноила бы ссылки документов в этот файл (§13 п.5).
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА (M-180, ломоть 4 сейв-эпика) ───────────────────
// Судит КОМПИЛЯТОР, а не ревью (`core/row_law.h`, наряд M-169). Новая структура
// в этом файле обязана появиться и в этом списке — за полнотой списка следит
// `arch_guard_test` (файл стоит в его `kRowHeaders`).
TIMAERT_ROW(sm::ResourceFieldDef);
TIMAERT_ROW(sm::ResourceGrid);

// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются.
TIMAERT_PACKED(sm::ResourceGrid, &sm::ResourceGrid::width,
    &sm::ResourceGrid::height, &sm::ResourceGrid::liveCells,
    &sm::ResourceGrid::revision, &sm::ResourceGrid::cells);

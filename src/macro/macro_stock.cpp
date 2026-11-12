// The rows of the macro-stock table — the only place that knows how each
// borrowable quantity is read and written. See macro_stock.h for the rule this
// serves; adding a quantity is a row here plus a value in the enum, and the
// static_assert below refuses to build if those two ever disagree.
#include "macro/macro_stock.h"
#include "bus/store.h"

#include "bus/anketa.h"
#include "macro/deposit_layer.h"
#include "bus/world_row.h"  // двери существ единого контейнера (M-71)
#include "macro/labour.h"     // settle_souls / souls_flock — души места
#include "macro/fauna.h"
#include "macro/map_generator.h"
#include "macro/squad.h"     // record_deed — THE chronicle door (вердикт №9)
#include "macro/spawners.h"   // the field-stamp contract this file defines
                              //   the runtime half of (plough_*)
#include "macro/state.h"
#include "macro/tree_layer.h"

#include "tables/items.h"   // ординалы выхода транзакции (ЗАКОН СЛОВАРЯ)

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <vector>

#include "core/rng.h"

namespace sm {
namespace {

struct MacroStockRow {
    const char* id;
    int  (*read )(const MacroWorld&, MacroStockKey);
    void (*write)(MacroWorld&, MacroStockKey, int delta);
};

// ── tree_count: the forest of one cell — the registry's carrier row ───────
// КЛЮЧ ВЕДОМОСТИ НЕСЁТ КЛЕТКУ ПАРОЙ (он строка дельты шва —
// `MacroStockKey@src/tables/stock_key.h`), а поля мира спрашивают её одним
// числом: пара сворачивается ЗДЕСЬ, у дверей ведомости, один раз. Без мира
// свернуть нечем — отвечает клетка 0, а ряды ниже без мира и так молчат.
std::uint32_t key_cell(const MacroWorld& w, MacroStockKey k) {
    return w.gs ? cell_of(k.cellX, k.cellY, w.gs->mapW) : 0u;
}

int read_tree_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Trees, key_cell(w, k));
}

void write_tree_count(MacroWorld& w, MacroStockKey k, int delta) {
    field_move(w, ResourceFieldId::Trees, key_cell(w, k), delta);
}

// ── ЗДЕСЬ ЖИЛ РЯД `population`, И ОН УНИЧТОЖЕН КАК ВТОРОЙ СЛОВАРЬ (M-324) ──
//
// Вердикт владельца 2026-10-27, дословно: «никакого закона шва не может быть
// популяции — смотри пример у нас есть город (это сквад с инвентарём) ты
// пришёл в субмире убил когото и по еиной системе это вычитается из инвентаря
// сквада / зачем нужна популяция в законе шва?».
//
// ЧЕМ ОН БЫЛ: вторым ответом на вопрос, на который отвечает ряд `creatures`
// ниже, — «существо вышло из контейнера сквада». При именной отрицательной
// квитанции (единственная, какую шлёт шов) оба делали строка в строку одно и
// то же: резолв субъекта по ординалу → `count_human_souls` до → снятие голов
// по квитанции → `flock_left_home` парной записью числа. Субъект у них был
// ОДИН: `rooted_handle_by_ordinal` есть `macro_handle_by_spawn_id` плюс фильтр
// `archetype == Rooted`, то есть остаток рода, которого нет (ЗАКОН ПОЛЯ п.4).
// Хуже того, шапка соседнего ряда УЖЕ объявляла этот дубль закрытым («теперь
// ответ один»), пока он жил строкой ниже, — ровно тот род лжи, из-за которого
// §0 запрещает считать шапку доказательством.
//
// ДВА ЕГО УНИКАЛЬНЫХ ПОВЕДЕНИЯ ИЗ ШВА НЕДОСТИЖИМЫ, И ЭТО ПРОВЕРЕНО ПОИМЁННО:
//   · delta > 0 (`settle_souls`) — долг гасится ровно в одной точке
//     (`push_stock_op` у смерти тела, `sub/engine.cpp`) и ВСЕГДА `-amount`;
//     возврата займа в мире нет, плюс трогал только тест;
//   · безымянная квитанция (`bleed_flock`) — все три места займа
//     (`sub/spawn.cpp`) зовут `name_creature_in_loan`, потому что тело
//     воплощается ИЗ ГОЛОВЫ инвентаря.
// А отказной гейт `creatures` (`entityId == 0 && level <= 0`) шов не ловит:
// `creature_slot` гонит уровень через `clamp_to_ladder` с полом 1.
//
// ЧТО ЭТО ДАЛО СВЕРХ СНОСА ДУБЛЯ: чтение перестало быть ТОЛКОВАТЕЛЕМ.
// `read_population` брал СУБЪЕКТА и отвечал числом поля фич под его клеткой —
// та же улика, что у снесённой M-320 `souls_flock`; `read_creatures` отвечает
// `creature_count(inv)`, то есть ИЗ ИНВЕНТАРЯ. (Здесь стояло «пару пишет
// `flock_left_home` внутри `write_creatures`» — ВЕРНО НА ОДИН ДЕНЬ: следующим
// шагом M-325 снял и эту запись, потому что число места есть частность ЕГО
// МОДУЛЯ, а не дело шва. Ряд пишет только контейнер.)

// ── creatures: the members of a squad standing on the map ─────────────────────
// The squad IS its avatar entity (ecs::SquadUpkeep doctrine), so the subject
// of this row is the squad's save-stable MacroSpawnId ordinal — the one
// identity that survives the ECS never being serialized — and `detail` names
// the member (SoldierRecord::entityId, compared as a bit pattern because ids
// may use the high bit). This is what makes "a squad cannot hold zero members"
// a consequence instead of a special case: members die through this row, the
// avatar dies through the tracked-body path, and a Dead avatar with an empty
// creatures simply is no squad any more — nothing extra removes it.
// Слияние M-71: члены сквада живут в области существ ЕДИНОГО контейнера
// (NpcInventory), обвязка счетов (SquadUpkeep) им больше не дом.
// ВОЗВРАЩАЕТ СЛОТ, А НЕ ГОЛЫЙ ИНВЕНТАРЬ: у всякой убыли души есть ВТОРОЙ
// носитель — паства её дома, — и адресуется он слотом (дом = ребро Parent,
// M-245). Дверь, отдававшая только `Inventory*`, тем самым
// делала парную запись НЕВЫРАЗИМОЙ у своего звонящего, и та не писалась.
MacroHandle find_creatures_subject(const MacroWorld& w,
                                   std::int32_t subject) {
    // РЕЗОЛВ ОРДИНАЛА — ЧУЖАЯ ДВЕРЬ, И ЗДЕСЬ ЕЁ ВТОРОГО ЭКЗЕМПЛЯРА БОЛЬШЕ НЕТ.
    // Тут стоял свой скан колонки `spawnId` — строка в строку тот же цикл, что
    // у `macro_handle_by_spawn_id@src/macro/squad.h`, то есть второй ответ на
    // один вопрос мира (DOD п.6). Совпадали они не случайно, а потому, что
    // вопрос один: «чьё это имя-ординал». Теперь он задаётся один раз, и когда
    // у резолва появится таблица слота по ординалу (1е/M-37), квитанции
    // подешевеют вместе со всеми — а не отдельной правкой этого файла.
    // Мир без store — это мир без сквадов, честный отказ (нулевой слой
    // конверта, как у всякого слоя MacroWorld).
    if (!w.store || subject <= 0) return MacroHandle{};
    return macro_handle_by_spawn_id(*w.store, std::uint32_t(subject));
}

int read_creatures(const MacroWorld& w, MacroStockKey k) {
    const MacroHandle h = find_creatures_subject(w, k.subject);
    if (!w.store || !w.store->valid(h)) return 0;
    return creature_count(w.store->inventory[h.slot].inv);
}

void write_creatures(MacroWorld& w, MacroStockKey k, int delta) {
    if (delta >= 0) {
        // The creation direction (recruitment, deserters re-raised) cannot be
        // conjured from a count — a member is a kind and a level, which the
        // producers write through add_soldiers with real rows. A bare positive
        // delta names nobody, so it moves nothing: fail closed, like every
        // other malformed receipt.
        return;
    }
    if (!w.store) return;
    MacroStore& st = *w.store;
    const MacroHandle h = find_creatures_subject(w, k.subject);
    if (!st.valid(h)) return;
    Inventory& r = st.inventory[h.slot].inv;
    // The receipt names its member or it pays nothing: by entityId for a
    // storied soul, by {kind, level} for a generic one (detailLevel > 0 is
    // the pair's liveness — kind alone cannot be, Peasant is row 0).
    SoldierRecord who{};
    who.entityId = k.detail == -1 ? 0u : std::uint32_t(k.detail);
    who.kind = k.detailKind;
    who.level = k.detailLevel;
    if (who.entityId == 0 && who.level <= 0) return;
    // СНИЗУ ПИШЕТСЯ ТОЛЬКО ИНВЕНТАРЬ, И БОЛЬШЕ НИЧЕГО (M-325).
    //
    // Вердикт владельца 2026-10-27, дословно: «прописывать вручную смену поля
    // не надо тут потому что им занимается сам модуль города (это не как ты
    // собрал дерево - потому что сбор дерева это воздействие на поле через
    // способность) а тут по единой системе просто убийство существа в
    // субмире». Основание — ЗАКОН КОНТЕЙНЕРА п.3 в его нынешнем виде: «ВСЁ
    // ЧТО В СКВАДЕ ЕСТЬ ЭТО ЕГО ИНВЕНТАРЬ, всё остальное это уже агностичные
    // частности, которые могут как угодно регулироваться через его модуль».
    //
    // ЗДЕСЬ СТОЯЛ `flock_left_home` — то есть эта дверь, живущая В ШВЕ, вела
    // ЧУЖУЮ частность: число поля фич под клеткой места. Оно не второй
    // носитель души, а величина, которой распоряжается модуль стоящего на
    // клетке сквада, и ведёт он её своим дневным проходом
    // (`reconcile_rooted_flock@src/macro/squad.h`), а не по звонку снизу.
    //
    // РАЗЛИЧИТЕЛЬ — НЕ ВЕТКА ПО РОДУ, А ОДИН ВОПРОС: ЦЕЛЬ ЛИ ПОЛЕ У ДЕЙСТВИЯ.
    // У рубки дерева поле ЕСТЬ предмет способности (воздействие на мир,
    // система 3), поэтому её ряд пишет поле по праву и ничей инвентарь не
    // трогает (`write_tree_count` → `field_move`, строкой выше в этом файле).
    // У убийства существа поле — посторонняя величина рядом.
    for (int i = 0; i < -delta; ++i) {
        if (!creatures_remove_one(r, who)) break;
    }
}

// (Сток `garrison` УНИЧТОЖЕН 2026-09-30, v122 — вместе с сословием: он
// адресовал ТОТ ЖЕ инвентарь места, что снесённая строка `population`, то есть
// был вторым ответом на вопрос «душа места умерла». Именная адресация
// квитанции не потеряна — она переехала в `write_creatures` ниже, где и
// стала единственной.)

// ── The ONE resource-field container (macro/resource_field.h) ─────────────
// Baseline = pure terrain/climate (resources come BEFORE settlement — the
// owner's causality); storage follows the row's law (resource_field.h):
// sparse scar rows (fauna, wheat) and carrier rows (trees — the dense grid
// the map renders). Deposits join as carrier rows in Inc B.

// (field_cell_index умер 2026-09-18 вместе с хешем шрамов: поле индексирует
// себя само. С ломтя 4 ряда M-462 клетку ряду называют ИНДЕКСОМ — все хуки
// ниже берут одно число, а не пару координат.)

// Wheat potential: the climate's fertility channel (master G — the same
// number the field stamp scores cells by), scaled to stands per cell by
// kMaxWheatStandsPerCell (resource_field.h — the row's own door, so the
// field stamp and the settlement score share this scale).
// Settlement (FT_Field parcels, garden plots) only decides WHERE this
// potential is embodied — it never enters the baseline.
int wheat_baseline(const MacroWorld& w, std::uint32_t cell) {
    // G = moisture, "the fertility" (macro/spawners.h). Канал читает дверь
    // карты: здесь стоял рукописный индекс `(wy * width + wx) * 4 + 1` с
    // двумя свёртками осей; клетка за картой отвечает нулём там же.
    return int(field01_of(w.terrain->moisture_at(cell))
               * float(kMaxWheatStandsPerCell));
}

int fauna_baseline(const MacroWorld& w, std::uint32_t cell) {
    return fauna_cell_capacity_at(w, cell);
}

// ФАУНА — РЯД-НОСИТЕЛЬ НА СВОЁ ПОЛЕ (M-371, вердикт владельца 2026-11-02:
// «а фауна это отдельное поле»; «его пишет СВОЙ ГЕНЕРАТОР один раз при
// рождении мира»). Живое поголовье лежит в `GameState::faunaField`; базовая
// (природная ёмкость: биом + лес) осталась потолком роста и клампом выемки —
// форма ровно та же, что у стада: своя базовая плюс свой контейнер.
int fauna_read(const MacroWorld& w, std::uint32_t cell) {
    return w.gs ? fauna_field_read(*w.gs, cell) : 0;
}
void fauna_apply(MacroWorld& w, std::uint32_t cell, int delta) {
    if (!w.gs) return;
    const int cap = fauna_baseline(w, cell);
    fauna_field_write(*w.gs, cell,
                      std::clamp(fauna_field_read(*w.gs, cell) + delta,
                                 0, std::max(0, cap)));
}

// Trees: the carrier row. The live state is the dense TreeLayer grid the
// map renders (worldgen filled it; growth and felling move it from there —
// the virgin derivation is an initial condition, not an attractor), so
// read/apply go to the grid, and set_tree_count keeps clamp + revision at
// the layer's own door.
int trees_read(const MacroWorld& w, std::uint32_t cell) {
    return w.gs ? int(w.gs->treeLayer.at(cell)) : 0;
}
void trees_apply(MacroWorld& w, std::uint32_t cell, int delta) {
    if (!w.gs || delta == 0) return;
    const int now = int(w.gs->treeLayer.at(cell));
    set_tree_count(w.gs->treeLayer, cell, now + delta);
}

// ЖИЛА: НА КАКОЙ КЛЕТКЕ ЧИСЛО ЕСТЬ ЗАЛЕЖЬ ШАХТЫ — колонкой строки фичи
// (`FeatureDef::numberRow == Vein`), та же форма, что у стада. Носителей у
// ряда два, и оба чужие: дикая жила — производный грид DepositLayer (чистая
// геология, геймплеем не мутируется), залежь шахты — число фичи в `featureNumber`
// (кап связного кластера, свёрнутый генерацией шахт — вердикт 2026-10-23).
bool vein_mine_cell_(const MacroWorld& w, std::uint32_t cell) {
    return w.gs
        && feature_def(w.gs->features.at(cell)).numberRow
               == ResourceFieldId::Vein;
}

int vein_read(const MacroWorld& w, std::uint32_t cell) {
    if (vein_mine_cell_(w, cell)) return feature_number_read(*w.gs, cell);
    return w.deposits ? int(w.deposits->vein_at(cell)) : 0;
}

// КАП ЗАЛЕЖИ — ФУНДАМЕНТ (вердикт 2026-10-23: «фундментальные поля типа
// фертилити жил и тд дают кап а поля ресурсов либо фич либо лес дают
// количетсво текущее и дорастают до капа просто во времени»): у шахтной
// клетки кап есть СВЯЗНОСТЬ её кластера природного поля — та же дверь, какой
// генерация сеяла залежь. O(кластер), и зовётся только на шахтных клетках
// (гейт numberRow — O(1) первым).
int vein_baseline(const MacroWorld& w, std::uint32_t cell) {
    if (!vein_mine_cell_(w, cell) || !w.deposits) return 0;
    return int(vein_cluster_cap(*w.deposits, cell));
}

// ЗАЛЕЖЬ ДОРАСТАЕТ ДО КАПА ТЕМ ЖЕ ТЕМПОМ, ЧТО ПАШНЯ: четверть ёмкости за
// сезонный визит — полный кап за год. Единый закон роста, не свой; число —
// баланс (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ).
int vein_growth_at(const MacroWorld& w, std::uint32_t cell) {
    const int cap = vein_baseline(w, cell);
    return cap > 0 ? std::max(1, cap / 4) : 0;
}

void vein_apply(MacroWorld& w, std::uint32_t cell, int delta) {
    // ДОБЫЧА СУЩЕСТВУЕТ ПО ШАХТАМ (вердикт 2026-10-23): дикая жила —
    // природное поле, и оно НЕ УБЫВАЕТ («метал не убывает как и
    // фертильность») — двигается только залежь под фичей шахты. Клетка без
    // шахты отказывает записи молча-честно: взять с неё нечего по закону,
    // а не по невезению. Приход клампится КАПОМ СВЯЗНОСТИ: рост не надувает
    // залежь выше фундамента.
    if (!vein_mine_cell_(w, cell) || delta == 0) return;
    const int cap = w.deposits
        ? int(vein_cluster_cap(*w.deposits, cell))
        : feature_number_read(*w.gs, cell);   // фикстура без геологии: тратить можно
    feature_number_write(*w.gs, cell,
                 std::clamp(feature_number_read(*w.gs, cell) + delta, 0,
                            std::max(cap, 0)));
}

// ── The growth laws — the per-row CONTEXT of the one birth mechanism ──────

// Wheat: fertility is the whole context (the baseline already prices it),
// and the field turns over its own POTENTIAL — a quarter per visit (one
// visit per kGrowthEpochDays = a season), so a reaped parcel regrows its
// full baseline in a year. The seasonal-pulse harvest (autumn × moisture,
// W2 design) is deferred with field seasonality (field track); this is its
// smooth stand-in, and the divisor is a balance-run tunable. The old law —
// one stand per season regardless of fertility — fed the world ~7 grain a
// day and starved every farmer within a week of genesis (measured,
// balance_run 2026-08-30).
int wheat_growth_at(const MacroWorld& w, std::uint32_t cell) {
    return std::max(1, wheat_baseline(w, cell) / kWheatSeasonsToRegrow);
}

// Fauna: beasts breed where beasts are. One head per visit while at least
// HALF the 3×3 valley is alive (the old 1/32-days rate at a living edge);
// a wiped-out region has nobody left to breed, and repopulates from its
// edges inward — or never, if the whole valley was emptied.
int fauna_growth_at(const MacroWorld& w, std::uint32_t at) {
    // Обход 3×3 — арифметика ИНДЕКСА через дверь cell_step (ЗАКОН АДРЕСА);
    // без терраина все девять чтений и так отвечали нулём — отказ тот же.
    if (!w.terrain || !world_shape_ok(w.terrain->width, w.terrain->height))
        return 0;
    const int side = w.terrain->width;
    int alive = 0, cap = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t n = cell_step(at, dx, dy, side);
            cap   += fauna_cell_capacity_at(w, n);
            alive += resource_field_read(w, ResourceFieldId::Fauna, n);
        }
    }
    if (cap <= 0) return 0;
    return (2 * alive >= cap) ? 1 : 0;
}

// Trees: the forest plants the forest. Growth per visit derives from
// r = 1/1024 of the local 3×3 density per DAY:
//   perVisit = localSum9 × kGrowthEpochDays / (9 × 1024)
// so a clear-cut cell inside a living massif (neighbour density 16384)
// gains ~512/visit and crosses the forest class (8192) in ~14 visits ≈ 448
// days ≈ 3.5 game years — the owner's "вырубка среди леса зарастает ~4
// года". Below a local mean of ~288 trees the integer term is 0: scattered
// brush does not seed a forest. The biome GATE reuses the ambient table
// (no new constants): min(kBiomeBaseTreeCount, 1024)/1024 — taiga and
// tropics (≥1024) grow at full rate, meadow at ~0.6, desert (40) almost
// never, water not at all.
int trees_growth_at(const MacroWorld& w, std::uint32_t at) {
    if (!w.gs || !w.terrain || !w.terrain->has_rgba_storage()
        || !world_shape_ok(w.terrain->width, w.terrain->height))
        return 0;
    const int side = w.terrain->width;
    int sum = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            sum += int(w.gs->treeLayer.at(cell_step(at, dx, dy, side)));
    int growth = sum * kGrowthEpochDays / (9 * 1024);
    if (growth <= 0) return 0;
    // THE cell cascade (map_generator.h biome_at_cell) — fail-closed to
    // Water, whose base tree count is 0: no storage, no growth.
    const Biome biome = biome_at_cell(*w.terrain, at);
    const int b = int(biome);
    const int base = biome_base_tree_count(b);
    return growth * std::min(base, 1024) / 1024;
}

// СКОЛЬКО ГОЛОВ КОРМИТ ТРАВА КЛЕТКИ — чистая функция фертильности, и она же
// КАП СТАДА (владелец 2026-10-12: «кап из фертильити»). Та же фертильность,
// которую оценивает ряд хлеба, прочитанная как РТЫ: рот съедает
// kDaysPerSeason × 4 пищи в год (по единице в день — тот же рацион, что у
// человека), а трава клетки даёт за год свою хлебную базовую, поэтому лучшая
// земля (4096) держит 32 головы, тощая — ни одной. Ни второй двери
// фертильности, ни второй константы.
//
// Fail-closed без терраина: у ряда-НОСИТЕЛЯ двери зовутся ДО общего гейта
// `resource_field_read`, значит собственную землю они обязаны проверять сами.
int grass_feeds_head(const MacroWorld& w, std::uint32_t cell) {
    if (!w.terrain || w.terrain->width <= 0) return 0;
    return wheat_baseline(w, cell) / (kDaysPerSeason * 4);
}

// НА КАКОЙ КЛЕТКЕ ЧИСЛО ЕСТЬ СТАДО — спрашивается КОЛОНКОЙ СТРОКИ ФИЧИ
// (`FeatureDef::numberRow`), никогда родом фичи веткой: поле чисел фич одно,
// и «что значит это число» есть вопрос о фиче (ЗАКОН ПОЛЯ п.3, ЗАКОН СТРОКИ
// КАТАЛОГА). Без слоя фич стада нет вовсе — fail-closed, ничего не выдумываем.
bool livestock_cell_(const MacroWorld& w, std::uint32_t cell) {
    return w.gs
        && feature_def(w.gs->features.at(cell)).numberRow
               == ResourceFieldId::Livestock;
}

// Кап стада ЭТОЙ клетки: трава её кормит, но только если на ней стоит фича,
// чьё число есть стадо. Ноль у всех прочих клеток — и это он даёт ходоку
// роста право пропустить их одним сравнением (`cap <= 0 → continue`), то
// есть мир больше не платит за выгон, которого нет.
int livestock_baseline(const MacroWorld& w, std::uint32_t cell) {
    return livestock_cell_(w, cell) ? grass_feeds_head(w, cell) : 0;
}

// ПОГОЛОВЬЕ РАСТЁТ, ПОКА ЗАГОН НЕ ПОЛОН: одна голова за сезонный визит —
// ровно тот темп, какой держал прежний закон в своём лучшем случае.
// ЧТО УШЛО ВМЕСТЕ С ПОЛЕМ И НАЗВАНО ВСЛУХ: условие «жив хотя бы половина
// ёмкости 3×3» и заселение выеденной округи с краёв. Оба меряли ДИКОЕ стадо
// соседних клеток, а дикого стада больше нет — число живёт внутри загона,
// и у загона нет соседей, у которых бы оно стояло. Закон стал проще, потому
// что вопрос стал уже, а не потому что его упростили.
int livestock_growth_at(const MacroWorld&, std::uint32_t) { return 1; }

// ЧТЕНИЕ И ЗАПИСЬ — ЧЕРЕЗ ПОЛЕ ЧИСЕЛ ФИЧ. Выемка зажимается в [0, кап] ЗДЕСЬ,
// у единственной двери ряда: `feature_number_write` сам по себе про кап стада не
// знает ничего (под другой фичей там спелл или паства), и знать не должен.
int livestock_read(const MacroWorld& w, std::uint32_t cell) {
    return livestock_cell_(w, cell) ? feature_number_read(*w.gs, cell) : 0;
}
void livestock_apply(MacroWorld& w, std::uint32_t cell, int delta) {
    if (!livestock_cell_(w, cell)) return;
    const int cap = grass_feeds_head(w, cell);
    feature_number_write(*w.gs, cell,
                 std::clamp(feature_number_read(*w.gs, cell) + delta, 0, cap));
}

// (Здесь СТОЯЛО «роста у ряда жилы больше нет — одноразовый ком»; снято
// вердиктом 2026-10-23 в тот же день: «фундаментальные поля дают кап, поля
// фич — текущее, дорастают до капа во времени». Рост жилы — vein_growth_at
// выше, тем же ходоком, что пашня и стадо.)

constexpr ResourceFieldDef kResourceFields[] = {
    // Две хвостовые колонки каждого ряда — УМОЛЧАНИЕ ВЫХОДА транзакции
    // field_harvest (предмет / существо); фича клетки старше умолчания
    // (FeatureDef::yieldItem — лён поверх пашни, металл шахты поверх жилы).
    /* Wheat */ {"wheat", &wheat_baseline,
                 GrowthDomain::HealToBaseline, &wheat_growth_at,
                 nullptr, nullptr, "food", NPCType::Count},
    // ФАУНА — РЯД-НОСИТЕЛЬ НА СВОЁ ХРАНИМОЕ ПОЛЕ (M-371): живое — в
    // `GameState::faunaField` (генератор заполняет при рождении мира), шрам
    // мёртв, рост лечит носителя к природной базовой тем же ходоком.
    /* Fauna */ {"fauna", &fauna_baseline,
                 GrowthDomain::HealToBaseline, &fauna_growth_at,
                 &fauna_read, &fauna_apply, "food", NPCType::Count},
    // ЛЕС — ЕДИНСТВЕННОЕ ИСКЛЮЧЕНИЕ ЗАКОНА, и оно названо вслух: базовой у
    // него нет, потому что нетронутая земля зарастает ГУЩЕ девственной.
    // Лес есть НОСИТЕЛЬ: его живое состояние — это TreeLayer, поэтому
    // allocate_world_fields его пропускает.
    /* Trees */ {"trees", nullptr,
                 GrowthDomain::CarrierSpread, &trees_growth_at,
                 &trees_read, &trees_apply, "wood", NPCType::Count},
    // СТАДО — РЯД-НОСИТЕЛЬ НА ПОЛЕ ЧИСЕЛ ФИЧ (M-258): базовая есть КАП, а
    // живое число лежит в `featureNumber` под фичей, которая объявила его своим.
    // Форма ровно та же, что у жилы ниже: своя базовая плюс чужой контейнер.
    // ЛОШАДЬ-ЮНИТ (CANON S10): выход — СУЩЕСТВО, добытое встаёт душой в
    // контейнер, спина сразу в обозе (сводка — разряд А).
    /* Livestock */ {"livestock", &livestock_baseline,
                 GrowthDomain::HealToBaseline, &livestock_growth_at,
                 &livestock_read, &livestock_apply,
                 nullptr, NPCType::Horse},
    // ЖИЛА — ОДНО ПРИРОДНОЕ ПОЛЕ (вердикт 2026-10-22): рода нет, род РЕСУРСА
    // решает фича шахты (её `yieldItem`) — поэтому умолчание выхода у ряда
    // ПУСТО: без шахты добычи не существует. Кап залежи — связность кластера
    // (фундамент), текущее дорастает к нему единым законом (2026-10-23).
    /* Vein */ {"vein", &vein_baseline,
                GrowthDomain::HealToBaseline, &vein_growth_at,
                &vein_read, &vein_apply,
                nullptr, NPCType::Count},
};
static_assert(sizeof(kResourceFields) / sizeof(kResourceFields[0])
                  == std::size_t(ResourceFieldId::Count),
              "every ResourceFieldId needs its def — the table IS the system");
// (Колонка радиуса досягаемости ряда и её сторож стояли здесь и сняты ломтём
// 4 ряда M-462 вместе с полем досягаемости жил: у вопроса «есть ли ряд рядом»
// не было читателя, а колонку читал один этот сторож.)

// РЯД-ШРАМ РОВНО ОДИН, И ЭТО СТЕНА, А НЕ НАБЛЮДЕНИЕ. Шрам держит ряд без
// носителя (`carrierRead == nullptr`); поле под него в мире одно —
// `GameState::wheatScars`. Второй такой ряд молча лёг бы в ТО ЖЕ поле и
// смешал два ресурса в одной клетке — поэтому он красит сборку здесь и
// требует сперва дать ему собственное поле в состоянии мира.
constexpr bool row_keeps_scar(std::size_t f) {
    return kResourceFields[f].carrierRead == nullptr;
}
constexpr std::size_t scar_row_count() {
    std::size_t n = 0;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        if (row_keeps_scar(f)) ++n;
    }
    return n;
}
// И вторая половина той же стены: `field_move` уводит ряд к носителю по
// `carrierApply`, а чтение — по `carrierRead`. Ряд с ОДНОЙ дверью из двух
// провалился бы записью в шрам пашни, поэтому двери обязаны стоять парой.
constexpr bool carrier_doors_are_paired() {
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        if ((kResourceFields[f].carrierRead == nullptr)
            != (kResourceFields[f].carrierApply == nullptr)) return false;
    }
    return true;
}
static_assert(carrier_doors_are_paired(),
              "у ряда-носителя обе двери (carrierRead и carrierApply) или ни "
              "одной: с одной из двух его запись легла бы в wheatScars");
static_assert(scar_row_count() == 1u
                  && row_keeps_scar(std::size_t(ResourceFieldId::Wheat)),
              "шрам держит ровно один ряд — пашня; новый ряд без носителя "
              "обязан получить своё поле шрамов, а не делить wheatScars");

} // namespace

void generate_fauna_field(MacroWorld& w) {
    if (!w.gs || !w.terrain || w.terrain->width <= 0 || w.terrain->height <= 0)
        return;
    const std::uint32_t n = std::uint32_t(w.terrain->cell_count());
    for (std::uint32_t c = 0; c < n; ++c) {
        const int cap = fauna_cell_capacity_at(w, c);
        if (cap > 0) fauna_field_write(*w.gs, c, cap);
    }
}

void allocate_world_fields(GameState& gs, int width, int height) {
    if (width <= 0 || height <= 0) return;
    // The ONE scar field (the wheat row's — see the wall above the table's
    // tail). A CARRIER row keeps no scar of its own: its live state IS its
    // carrier (the tree grid, the vein grids), so it has no field here.
    if (ResourceGrid& g = gs.wheatScars;
        !(g.width == width && g.height == height && g.live())) {
        // Reach 0: the scar answers «сколько здесь», and no gate asks the
        // neighbourhood question of a scar (the vein rows, which do get
        // asked, own their reach inside the deposit layer).
        g.allocate(width, height);
    }
    if (!(gs.featureNumber.width == width && gs.featureNumber.height == height
          && gs.featureNumber.live())) {
        gs.featureNumber.allocate(width, height);
    }
}

// ── The plough: the runtime half of the field stamp (owner 2026-08-31) ───
// Declared in spawners.h beside the worldgen stamp; DEFINED here because
// the labour rotation (npc_ai.cpp) calls them and its test targets link
// the land's doors, not the worldgen.

// The parcel GROUND gates, one truth for plough and fence alike (roads
// win, water refuses, no rock terraces): what differs between a field and
// a pasture is only WHICH row's fertility bars the gate, never the ground.
static bool parcel_ground_ok_(const FeatureLayer& fl, const MacroWorld& world,
                              std::uint32_t cell) {
    std::size_t total = 0;
    if (!FeatureLayer::cell_count_for(fl.width, fl.height, total)
        || fl.cell_count() < total || !world.terrain)
        return false;
    // ЗАКОН АДРЕСА: адрес клетки — одно число, и считает его одна дверь.
    // Здесь стоял ручной `wy*width+wx` с двумя пред-заворотами — тот же
    // ответ на законном мире и МОЛЧА ДРУГОЙ на незаконном, где у слоя
    // fail-closed, а у этого гейта его не было.
    if (!world_shape_ok(fl.width, fl.height)) return false;
    const TerrainData& td = *world.terrain;
    if (td.width != fl.width || td.height != fl.height
        || td.rgba.size() < total * 4u)
        return false;
    if (fl.at(cell) != FT_None) return false;  // roads win
    // ЕДИНСТВЕННЫЙ ЗАПРЕТ МИРА — ВОДА (владелец, 2026-09-23, дословно:
    // «никаких запретов в расселении у нас подход через веса проходимости
    // единая система (дорога дешевле всего горы и вода дороже всего) и НА
    // ВОДУ НЕЛЬЗЯ В ОСТАЛЬНЫХ МЕСТАХ МОЖНО, поля тоже не запрет а ПО
    // ФЕРТИЛЬНОСТИ СМОТРИМ»).
    // Спрашивается он ОДНОЙ дверью карты: здесь стояло «маска ИЛИ float-
    // высота» — два спеллинга одного порога в одном условии (M-109).
    if (td.is_water(cell)) return false;
    // ЗДЕСЬ СТОЯЛ ВТОРОЙ ЗАПРЕТ: `height01 >= kMountainBiomeLevel` — «no rock
    // terraces». Он снят вердиктом выше, и снят БЕЗ ЗАМЕНЫ: камень отсеивает
    // не запрет, а САМА ФЕРТИЛЬНОСТЬ, потому что на скале её и так нет.
    // (Гейт `wheatOut >= field_wheat_min()` у звонящего тоже снесён
    // 2026-10-01 — он был той же планкой этажом выше, spawners.h.)
    // Запрет был лишним слоем поверх веса — ровно та форма, которую вердикт
    // называет неправильной: мир решает ЦЕНОЙ, а не разрешением.
    // ЗАМЕРЕНО ПРИБОРОМ (колонка `parcels`, четыре сида, 2026-09-23):
    // 4842/4436/4346/4560 до правки — число после стоит в коммите.
    return true;
}

bool plough_cell_ok(const MacroWorld& world, std::uint32_t cell, int& wheatOut)
{
    if (!world.gs) return false;   // нет мира — нет и поля фич
    const FeatureLayer& fl = world.gs->features;
    if (!parcel_ground_ok_(fl, world, cell)) return false;
    // The ONE fertility door: potential minus what play has taken. ВОЗВРАЩАЕТСЯ
    // ЧИСЛО, А НЕ ВЕРДИКТ: планка `wheatOut >= field_wheat_min()` снесена
    // вердиктом владельца 2026-10-01 (spawners.h — там же её эпитафия и вывод).
    // Пахать можно всюду, где земля не вода и не занята; СКОЛЬКО уродится,
    // говорит само поле, и на камне оно говорит «ноль» без всякого запрета.
    wheatOut = resource_field_read(world, ResourceFieldId::Wheat, cell);
    return true;
}

bool plough_field_cell(const MacroWorld& world, std::uint32_t cell,
                       FeatureType parcel)
{
    // ЧЕМ ЗАСЕЯНО — КОЛОНКА ФИЧИ, А НЕ ВТОРАЯ ДВЕРЬ (2026-09-20): земля,
    // проверка и цена у хлебной и льняной парцеллы одни и те же, поэтому
    // вспашка одна, а вид парцеллы — её аргумент.
    int wheat = 0;
    if (!plough_cell_ok(world, cell, wheat)) return false;
    FeatureLayer& fl = world.gs->features;
    // ОДНА ДВЕРЬ ШТАМПА (M-212): воду она спрашивает сама, колонкой строки.
    // Гейт воды в `plough_cell_ok` остаётся — он отвечает на ДРУГОЙ вопрос
    // («стоит ли вообще тратить день»), и без него работа была бы оплачена
    // прежде, чем выяснилось, что ставить нечего.
    return feature_put(fl, *world.terrain, cell, parcel) != FT_None;
}

// (ДВЕ ДВЕРИ ЗАГОНА СНЕСЕНЫ 2026-10-12, M-258: `pasture_cell_ok` и
// `fence_pasture_cell`. У второй был НОЛЬ звонящих с 2026-09-22 — в тот день
// вердикт владельца «СТРОИТЕЛЬСТВО КРЕСТЬЯНАМИ НЕ РАБОТАЕТ НА НЕГО НЕЛЬЗЯ
// ПОЛАГАТЬСЯ» снёс ветку стройки из цикла артели, а дверь осталась жить
// мёртвой. Первая держалась ровно на бутстрап-ветке `find_home_pasture`,
// которая искала «лучшую ОГОРАЖИВАЕМУЮ клетку» и вместе с полем лошадей
// стала ложью: число стада есть число ФИЧИ, а на клетке без фичи его нет и
// быть не может. Ставить загоны будет ГЕНЕРАЦИЯ — вердикт владельца
// 2026-09-22 (наряд M-79) и 2026-10-12, дословно: «у нас будет сложная
// весовая контекстная генерация не всё так просто». Заглушки на место
// снесённых дверей НЕ СТАВИМ — дыра названа и открыта, §1.)

const ResourceFieldDef& resource_field_def(ResourceFieldId f) {
    return kResourceFields[std::size_t(f)
                               < std::size_t(ResourceFieldId::Count)
                           ? std::size_t(f) : 0];
}

int resource_field_read(const MacroWorld& w, ResourceFieldId f,
                        std::uint32_t cell) {
    const ResourceFieldDef& def = resource_field_def(f);
    if (def.carrierRead) return def.carrierRead(w, cell);
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    const int scar = int(w.gs->wheatScars.at(cell));
    return std::max(0, def.baseline(w, cell) - scar);
}

int field_move(MacroWorld& w, ResourceFieldId f, std::uint32_t cell, int delta) {
    if (delta == 0) return 0;
    const ResourceFieldDef& carrier = resource_field_def(f);
    if (carrier.carrierApply) {
        // ФАКТ У НОСИТЕЛЯ — РАЗНОСТЬЮ ЧТЕНИЙ вокруг его же двери: диалекты
        // клампят каждый по-своему (лес — своим капом, стадо — травой, жила —
        // ёмкостью), и спрашивать их «сколько реально встало» честнее, чем
        // переписывать три клампа четвёртым экземпляром.
        const int before = carrier.carrierRead ? carrier.carrierRead(w, cell)
                                               : 0;
        carrier.carrierApply(w, cell, delta);
        const int after = carrier.carrierRead ? carrier.carrierRead(w, cell)
                                              : 0;
        return after - before;
    }
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    ResourceGrid& scars = w.gs->wheatScars;
    // THE write sizes the field over THIS world if nobody did yet — the same
    // fail-open the featureNumber layer's door keeps. A caller that has to remember
    // to allocate is a caller that will forget, and a lost scar is invisible:
    // the read simply answers "whole" (the fold-up lesson, applied to the one
    // door instead of to every fixture).
    if (!scars.live()) {
        scars.allocate(w.terrain->width, w.terrain->height);
        if (!scars.live()) return 0;
    }
    // Spending (−delta) deepens the scar, returning (+delta, regrowth)
    // heals it; the scar never exceeds the baseline, so a runaway writer
    // cannot wind a cell into a millennium of regrowth.
    const int cap = std::max(0, resource_field_def(f).baseline(w, cell));
    const int was = int(scars.at(cell));
    const int scar = std::clamp(was - delta, 0, cap);
    // A healed cell is the field's own legal ZERO — no erase, no key, and
    // the live count the grid keeps is what the wire writes out.
    scars.write(cell, std::int32_t(scar));
    return was - scar;   // факт тем же знаком, что намерение
}

// ── ДВЕРЬ №2 ПУЛЬТА: ТРАНЗАКЦИЯ «ПОЛЕ → КОНТЕЙНЕР СКВАДА» ─────────────────
// Кредит прежде дебета (CANON S5): поле платит ровно за то, что контейнер
// ПРИНЯЛ; отказ контейнера поле не трогает. Выход решает ФИЧА клетки своей
// строкой, иначе умолчание ряда. Руки (want) — закон звонящего; «есть»,
// спина и слот — закон этой двери. Ординалы выхода кэшируются лениво у
// таблиц (ЗАКОН СЛОВАРЯ: строка — ключ авторства, в тике ходит число).
namespace {
int row_yield_ordinal(ResourceFieldId f) {
    static const std::array<int, std::size_t(ResourceFieldId::Count)> kOut =
        [] {
            std::array<int, std::size_t(ResourceFieldId::Count)> m{};
            for (std::size_t i = 0; i < m.size(); ++i) {
                const char* id = kResourceFields[i].yieldItem;
                m[i] = id ? item_index(id) : -1;
            }
            return m;
        }();
    return std::size_t(f) < kOut.size() ? kOut[std::size_t(f)] : -1;
}
int feature_yield_ordinal(FeatureType t) {
    static const std::array<int, std::size_t(FT_Count)> kOut = [] {
        std::array<int, std::size_t(FT_Count)> m{};
        for (std::size_t i = 0; i < m.size(); ++i) {
            const char* id = kFeatureDefs[i].yieldItem;
            m[i] = id ? item_index(id) : -1;
        }
        return m;
    }();
    return std::size_t(t) < kOut.size() ? kOut[std::size_t(t)] : -1;
}
} // namespace

int field_yield_ordinal(const MacroWorld& w, ResourceFieldId f,
                        std::uint32_t cell) {
    if (w.gs) {
        const int fid = feature_yield_ordinal(w.gs->features.at(cell));
        if (fid >= 0) return fid;   // предмет фичи старше умолчания ряда
    }
    const ResourceFieldDef& row = resource_field_def(f);
    return row.yieldCreature == NPCType::Count ? row_yield_ordinal(f) : -1;
}

int field_harvest(MacroWorld& w, ResourceFieldId f, std::uint32_t cell,
                  int want, std::uint32_t squadSlot) {
    if (want <= 0 || !w.store || squadSlot >= kUnifiedCap) return 0;
    if (!w.store->alive[squadSlot]) return 0;
    const int have = resource_field_read(w, f, cell);
    if (have <= 0) return 0;
    // ВЫХОД: фича клетки старше умолчания ряда (вердикт 2026-10-22 «что там
    // окажется решает фича шахты»; лён поверх пашни — тот же закон).
    const ResourceFieldDef& row = resource_field_def(f);
    const int outIdx = field_yield_ordinal(w, f, cell);
    const NPCType creature = outIdx >= 0 ? NPCType::Count : row.yieldCreature;
    Inventory& bag = w.store->inventory[squadSlot].inv;
    if (creature != NPCType::Count) {
        // СУЩЕСТВО: встаёт душой в контейнер (генерик-стак по закону слота),
        // спина сразу в обозе — сводку правит сама запись слота (разряд А).
        const int take = std::min(want, have);
        if (take <= 0) return 0;
        if (!creatures_push_stack(bag, creature,
                                  npc_def(creature).baseLevel, take)) {
            return 0;   // нет слота — ничего не наколдовано, поле не тронуто
        }
        field_move(w, f, cell, -take);
        return take;
    }
    if (outIdx < 0) return 0;   // ряд без выхода — добычи не существует
    const ItemDef* idef = item_def_at(outIdx);
    if (!idef) return 0;        // ординал без строки — выхода нет
    // ПРЕДМЕТ: кламп по спине сквада (CANON S10 «берёт ПО СВОЕЙ
    // ГРУЗОПОДЪЁМНОСТИ» — иначе артель каменела перегрузом навсегда).
    const std::int64_t freeG =
        carry_capacity_g(w.store->runtime[squadSlot], bag)
        - inventory_mass_g(bag);
    const std::int64_t unitG =
        std::max<std::int64_t>(1, std::int64_t(item_mass_g(*idef)));
    const int backMax = freeG <= 0 ? 0
        : int(std::min<std::int64_t>(freeG / unitG,
                                     std::numeric_limits<int>::max()));
    const int take = std::min({want, have, backMax});
    if (take <= 0) return 0;
    if (!bag.add_of(outIdx, take)) return 0;   // отказ слота: поле не тронуто
    field_move(w, f, cell, -take);
    return take;
}

int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx) {
    // Ряд-носитель шрама не держит — у него ответ всегда ноль.
    if (resource_field_def(f).carrierRead) return 0;
    const ResourceGrid& scars = gs.wheatScars;
    if (!scars.live() || cellIdx >= scars.cell_count()) return 0;
    return int(scars.cells[cellIdx]);
}

void resource_fields_daily_growth(MacroWorld& w, int day) {
    if (!w.gs || day <= 0) return;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        const ResourceFieldDef& def = kResourceFields[f];
        const ResourceFieldId row = ResourceFieldId(f);
        switch (def.growthDomain) {
        case GrowthDomain::CarrierSpread: {
            // ЛЕС. Клеток/32 плотного носителя за день: клетка навещается раз
            // в эпоху и получает эпоху роста — плавная долгая динамика без
            // полнокарточного дня и без работы в тике.
            if (!w.gs || !w.gs->treeLayer.has_complete_storage()) break;
            // Срез идёт по ИНДЕКСУ и им же спрашивает ряд: здесь стояла
            // раскладка индекса в пару на каждой клетке среза, а хук и пульт
            // собирали его обратно (M-462 ломоть 4).
            const std::uint32_t n =
                std::uint32_t(w.gs->treeLayer.cell_count());
            for (std::uint32_t c = std::uint32_t(day % kGrowthEpochDays);
                 c < n; c += std::uint32_t(kGrowthEpochDays)) {
                const int born = def.growthAt(w, c);
                if (born > 0) field_move(w, row, c, born);
            }
            break;
        }

        case GrowthDomain::HealToBaseline: {
            // Ряд без закона роста (жила: дикая не убывает, залежь шахты —
            // одноразовый ком) не ходит по карте вовсе.
            if (!def.growthAt) break;
            // ВСЁ ОСТАЛЬНОЕ — ОДНОЙ ПЕТ�енькой. Ходок берёт свой срез КАРТЫ, а
            // не «только поцарапанные клетки»: ряд-носитель (жила) вообще не
            // умеет перечислить недобранные клетки, а ряд-шрам на пустой
            // клетке просто ничего не делает. Одна петля дешевле двух
            // диалектов обхода — клеток/32 в день, тот же срез, что у леса.
            if (!w.terrain || w.terrain->width <= 0) break;
            const std::uint32_t n = std::uint32_t(w.terrain->cell_count());
            for (std::uint32_t c = std::uint32_t(day % kGrowthEpochDays);
                 c < n; c += std::uint32_t(kGrowthEpochDays)) {
                // Полная клетка не лечится: спрашиваем ёмкость ДО закона
                // роста, потому что для жилы ёмкость — это шум, и платить за
                // него на каждой полной клетке незачем.
                const int cap = def.baseline ? def.baseline(w, c) : 0;
                if (cap <= 0) continue;
                if (resource_field_read(w, row, c) >= cap) continue;
                const int born = def.growthAt(w, c);
                if (born > 0) field_move(w, row, c, born);
            }
            break;
        }
        }
    }
}

namespace {

// ── The stock rows over the container ──────────────────────────────────────

int read_fauna_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Fauna, key_cell(w, k));
}
void write_fauna_count(MacroWorld& w, MacroStockKey k, int delta) {
    field_move(w, ResourceFieldId::Fauna, key_cell(w, k), delta);
}
int read_crop_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Wheat, key_cell(w, k));
}
void write_crop_count(MacroWorld& w, MacroStockKey k, int delta) {
    field_move(w, ResourceFieldId::Wheat, key_cell(w, k), delta);
}
// Число фичи клетки — носитель GameState::featureNumber (один на мир, ЗАКОН ПОЛЯ
// п.3); feature_number_write клампит в ноль сам — ниже нуля числа фич не живут.
int read_feature_number(const MacroWorld& w, MacroStockKey k) {
    return w.gs ? feature_number_read(*w.gs, key_cell(w, k)) : 0;
}
void write_feature_number(MacroWorld& w, MacroStockKey k, int delta) {
    if (w.gs) feature_number_add(*w.gs, key_cell(w, k), delta);
}

constexpr MacroStockRow kRows[] = {
    {"tree_count",  &read_tree_count,  &write_tree_count},
    {"creatures",   &read_creatures,   &write_creatures},
    {"fauna_count", &read_fauna_count, &write_fauna_count},
    {"crop_count",  &read_crop_count,  &write_crop_count},
    {"feature_number", &read_feature_number, &write_feature_number},
};
static_assert(sizeof(kRows) / sizeof(kRows[0])
                  == std::size_t(MacroStock::Count),
              "every MacroStock value needs its row — the table IS the system");

const MacroStockRow& row_of(MacroStock s) {
    return kRows[std::size_t(s) < std::size_t(MacroStock::Count)
                     ? std::size_t(s) : 0];
}

} // namespace

int macro_stock_read(const MacroWorld& w, MacroStock s, MacroStockKey k) {
    if (std::size_t(s) >= std::size_t(MacroStock::Count)) return 0;
    return row_of(s).read(w, k);
}

void macro_stock_apply(MacroWorld& w, MacroStock s, MacroStockKey k, int delta) {
    if (std::size_t(s) >= std::size_t(MacroStock::Count)) return;
    row_of(s).write(w, k, delta);
}

const char* macro_stock_id(MacroStock s) {
    if (std::size_t(s) >= std::size_t(MacroStock::Count)) return "?";
    return row_of(s).id;
}


} // namespace sm

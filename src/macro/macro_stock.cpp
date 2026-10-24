// The rows of the macro-stock table — the only place that knows how each
// borrowable quantity is read and written. See macro_stock.h for the rule this
// serves; adding a quantity is a row here plus a value in the enum, and the
// static_assert below refuses to build if those two ever disagree.
#include "macro/macro_stock.h"
#include "macro/store.h"

#include "macro/anketa.h"
#include "macro/deposit_layer.h"
#include "macro/world_row.h"  // двери существ единого контейнера (M-71)
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
int read_tree_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Trees, k.cellX, k.cellY);
}

void write_tree_count(MacroWorld& w, MacroStockKey k, int delta) {
    field_move(w, ResourceFieldId::Trees, k.cellX, k.cellY, delta);
}

// ── population: the people of a named place ────────────────────────────────
// A settlement and a village are the same kind of subject here — a named place
// with people in it — and the ORDINAL alone names it: every place draws on the
// one subject issuer (nextMacroSpawnOrdinal, M-37), so the resolve is the one
// store door (place_handle_by_ordinal).
//
// ПЕРЕВОРОТ v122: колонки population больше нет — чтение отвечает ПАСТВОЙ
// (souls_flock: worked-число у поселения, головы толпы у данжа), запись-
// назад субмира «убил людей» идёт ПАРОЙ дверей: умирают ДОМАШНИЕ головы
// (bleed), и у поселения то же число сходит с worked-паствы. Прибыль
// (delta > 0 — вернувшееся тело) — той же парой в обратную сторону.
MacroHandle find_population_subject(const MacroWorld& w,
                                    std::int32_t subject) {
    if (!w.gs || !w.store || subject <= 0) return MacroHandle{};
    return place_handle_by_ordinal(*w.store, std::uint32_t(subject));
}

int read_population(const MacroWorld& w, MacroStockKey k) {
    const MacroHandle h = find_population_subject(w, k.subject);
    return w.store && w.store->valid(h)
        ? souls_flock(*w.gs, *w.store, h.slot) : 0;
}

void write_population(MacroWorld& w, MacroStockKey k, int delta) {
    if (delta == 0) return;
    const MacroHandle h = find_population_subject(w, k.subject);
    if (!w.store || !w.store->valid(h)) return;
    const std::uint16_t slot = h.slot;
    Inventory& store = w.store->inventory[slot].inv;
    // Колонка, а не род: у данжа «душа» — голова его толпы и человеком не
    // является по природе (Imp), поэтому дверь убыли у него другая. Самой
    // ПАСТВЫ эта ветка больше не касается — её снимает `flock_left_home`,
    // который про данж знает сам.
    const bool dungeon =
        place_def(SquadType(w.store->runtime[slot].squadType))
            .bornPopBase != 0;
    if (delta < 0) {
        // A place can be emptied but never owe people: списывается ФАКТ.
        //
        // КВИТАНЦИЯ НАЗЫВАЕТ СВОЮ ДУШУ — и тогда снимается РОВНО она:
        // именная по entityId, генерик по {роду, уровню} (detailLevel > 0 —
        // живость пары, потому что kind один не может ею быть: Peasant есть
        // строка 0). Так сюда влился сток `Garrison`, уничтоженный вместе с
        // сословием: «душа места умерла» есть ОДИН вопрос мира, и два ответа
        // на него были вторым словарём (DOD п.6).
        //
        // Безымянная квитанция берёт ЛЮБУЮ душу — это законный случай, а не
        // недосмотр: голод места и запись-назад «убил людей» без адреса
        // называют ЧИСЛО, а не человека.
        SoldierRecord who{};
        who.entityId = k.detail == -1 ? 0u : std::uint32_t(k.detail);
        who.kind = k.detailKind;
        who.level = k.detailLevel;
        // Паства мерится ДО убыли и списывается ФАКТОМ — одной дверью на все
        // пути души (`flock_left_home`). Здесь стоял свой счётчик `died` и
        // своя запись worked, и у счётчика был изъян: именная квитанция
        // инкрементировала его на ЛЮБОЕ снятое существо, то есть названный
        // конь списывал человеческую паству.
        const int folkBefore = count_human_souls(store);
        if (who.entityId != 0 || who.level > 0) {
            for (int i = 0; i < -delta; ++i) {
                if (!creatures_remove_one(store, who)) break;
            }
        } else {
            if (dungeon) bleed_creatures(store, -delta);
            else         bleed_flock(store, -delta);
        }
        flock_left_home(*w.gs, *w.store, slot, folkBefore);
    } else {
        // Вернувшееся тело — то же событие «место получило душу», одной
        // дверью (labour.h settle_souls): второй писатель пары
        // «число + голова» был бы вторым законом появления души.
        settle_souls(*w.gs, *w.store, slot, delta);
    }
}

// ── creatures: the members of a squad standing on the map ─────────────────────
// The squad IS its leader entity (ecs::SquadUpkeep doctrine), so the subject
// of this row is the squad's save-stable MacroSpawnId ordinal — the one
// identity that survives the ECS never being serialized — and `detail` names
// the member (SoldierRecord::entityId, compared as a bit pattern because ids
// may use the high bit). This is what makes "a squad cannot hold zero members"
// a consequence instead of a special case: members die through this row, the
// leader dies through the tracked-body path, and a Dead leader with an empty
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
    // ПАСТВА — ТОЙ ЖЕ ПАРОЙ, ЧТО У СТРОКИ `population` РЯДОМ. Эта строка
    // субъекта НЕ ФИЛЬТРУЕТ (скан по ординалу берёт любой живой слот), а
    // квитанции авто-боя приходят и на поселения: индекс сквадов их тоже не
    // отсеивает, так что набег на деревню снимал головы из её инвентаря и
    // worked не трогал. Два ответа на «душа вышла из контейнера» были
    // вторым словарём (DOD п.6) — теперь ответ один.
    const int folkBefore = count_human_souls(r);
    for (int i = 0; i < -delta; ++i) {
        if (!creatures_remove_one(r, who)) break;
    }
    // ПАРА ПИШЕТСЯ ТАМ, ГДЕ ЕСТЬ ВТОРОЙ НОСИТЕЛЬ, А СНЯТИЕ ГОЛОВЫ ОТ НЕГО НЕ
    // ЗАВИСИТ. Паства — число слоя `worked`, то есть колонка `gs`; конверт
    // мира вправе его не нести (тестовые фикстуры — о чём говорит и резолв
    // субъекта выше). Мир без `gs` есть мир без паствы, и пары в нём нет —
    // но квитанция остаётся исполнимой. Гейт на всю дверь был бы отказом
    // СНЯТИЯ по отсутствию ВТОРОГО носителя: поймано `macro_stock_test`
    // (7 из 51) и `body_contract_test`.
    if (w.gs) flock_left_home(*w.gs, st, h.slot, folkBefore);
}

// (Сток `garrison` УНИЧТОЖЕН 2026-09-30, v122 — вместе с сословием: он
// адресовал ТОТ ЖЕ инвентарь места, что строка `population` рядом, то есть
// был вторым ответом на вопрос «душа места умерла». Именная адресация
// квитанции не потеряна — она переехала в `write_population` выше, где и
// стала единственной.)

// ── The ONE resource-field container (macro/resource_field.h) ─────────────
// Baseline = pure terrain/climate (resources come BEFORE settlement — the
// owner's causality); storage follows the row's law (resource_field.h):
// sparse scar rows (fauna, wheat) and carrier rows (trees — the dense grid
// the map renders). Deposits join as carrier rows in Inc B.

// (field_cell_index умер 2026-09-18 вместе с хешем шрамов: поле индексирует
// себя само — клетка спрашивается координатами, а не ключом.)

// Wheat potential: the climate's fertility channel (master G — the same
// number the field stamp scores cells by), scaled to stands per cell by
// kMaxWheatStandsPerCell (resource_field.h — the row's own door, so the
// field stamp and the settlement score share this scale).
// Settlement (FT_Field parcels, garden plots) only decides WHERE this
// potential is embodied — it never enters the baseline.
int wheat_baseline(const MacroWorld& w, int x, int y) {
    const int wx = FeatureLayer::wrap_coord(x, w.terrain->width);
    const int wy = FeatureLayer::wrap_coord(y, w.terrain->height);
    const std::size_t idx =
        (std::size_t(wy) * std::size_t(w.terrain->width) + std::size_t(wx))
        * 4u + 1u;   // G = moisture, "the fertility" (macro/spawners.h)
    if (idx >= w.terrain->rgba.size()) return 0;
    return int(field01_of(w.terrain->rgba[idx]) * float(kMaxWheatStandsPerCell));
}

int fauna_baseline(const MacroWorld& w, int x, int y) {
    return fauna_cell_capacity_at(w, x, y);
}

// Trees: the carrier row. The live state is the dense TreeLayer grid the
// map renders (worldgen filled it; growth and felling move it from there —
// the virgin derivation is an initial condition, not an attractor), so
// read/apply go to the grid, and set_tree_count keeps clamp + revision at
// the layer's own door.
int trees_read(const MacroWorld& w, int x, int y) {
    return w.trees ? int(w.trees->at(x, y)) : 0;
}
void trees_apply(MacroWorld& w, int x, int y, int delta) {
    if (!w.trees || delta == 0) return;
    const int now = int(w.trees->at(x, y));
    set_tree_count(*w.trees, x, y, now + delta);
}

// ЖИЛА: НА КАКОЙ КЛЕТКЕ ЧИСЛО ЕСТЬ ЗАЛЕЖЬ ШАХТЫ — колонкой строки фичи
// (`FeatureDef::numberRow == Vein`), та же форма, что у стада. Носителей у
// ряда два, и оба чужие: дикая жила — производный грид DepositLayer (чистая
// геология, геймплеем не мутируется), залежь шахты — число фичи в `worked`
// (кап связного кластера, свёрнутый генерацией шахт — вердикт 2026-10-23).
bool vein_mine_cell_(const MacroWorld& w, int x, int y) {
    return w.gs && w.features
        && feature_def(w.features->at(x, y)).numberRow
               == ResourceFieldId::Vein;
}

int vein_read(const MacroWorld& w, int x, int y) {
    if (vein_mine_cell_(w, x, y)) return worked_read(*w.gs, x, y);
    return w.deposits ? int(w.deposits->vein_at(x, y)) : 0;
}

// КАП ЗАЛЕЖИ — ФУНДАМЕНТ (вердикт 2026-10-23: «фундментальные поля типа
// фертилити жил и тд дают кап а поля ресурсов либо фич либо лес дают
// количетсво текущее и дорастают до капа просто во времени»): у шахтной
// клетки кап есть СВЯЗНОСТЬ её кластера природного поля — та же дверь, какой
// генерация сеяла залежь. O(кластер), и зовётся только на шахтных клетках
// (гейт numberRow — O(1) первым).
int vein_baseline(const MacroWorld& w, int x, int y) {
    if (!vein_mine_cell_(w, x, y) || !w.deposits) return 0;
    return int(vein_cluster_cap(*w.deposits, x, y));
}

// ЗАЛЕЖЬ ДОРАСТАЕТ ДО КАПА ТЕМ ЖЕ ТЕМПОМ, ЧТО ПАШНЯ: четверть ёмкости за
// сезонный визит — полный кап за год. Единый закон роста, не свой; число —
// баланс (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ).
int vein_growth_at(const MacroWorld& w, int x, int y) {
    const int cap = vein_baseline(w, x, y);
    return cap > 0 ? std::max(1, cap / 4) : 0;
}

void vein_apply(MacroWorld& w, int x, int y, int delta) {
    // ДОБЫЧА СУЩЕСТВУЕТ ПО ШАХТАМ (вердикт 2026-10-23): дикая жила —
    // природное поле, и оно НЕ УБЫВАЕТ («метал не убывает как и
    // фертильность») — двигается только залежь под фичей шахты. Клетка без
    // шахты отказывает записи молча-честно: взять с неё нечего по закону,
    // а не по невезению. Приход клампится КАПОМ СВЯЗНОСТИ: рост не надувает
    // залежь выше фундамента.
    if (!vein_mine_cell_(w, x, y) || delta == 0) return;
    const int cap = w.deposits
        ? int(vein_cluster_cap(*w.deposits, x, y))
        : worked_read(*w.gs, x, y);   // фикстура без геологии: тратить можно
    worked_write(*w.gs, x, y,
                 std::clamp(worked_read(*w.gs, x, y) + delta, 0,
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
int wheat_growth_at(const MacroWorld& w, int x, int y) {
    return std::max(1, wheat_baseline(w, x, y) / kWheatSeasonsToRegrow);
}

// Fauna: beasts breed where beasts are. One head per visit while at least
// HALF the 3×3 valley is alive (the old 1/32-days rate at a living edge);
// a wiped-out region has nobody left to breed, and repopulates from its
// edges inward — or never, if the whole valley was emptied.
int fauna_growth_at(const MacroWorld& w, int x, int y) {
    // Обход 3×3 — арифметика ИНДЕКСА через дверь cell_step (ЗАКОН АДРЕСА);
    // без терраина все девять чтений и так отвечали нулём — отказ тот же.
    if (!w.terrain || !world_shape_ok(w.terrain->width, w.terrain->height))
        return 0;
    const int side = w.terrain->width;
    const std::uint32_t at = cell_of(x, y, side);
    int alive = 0, cap = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t n = cell_step(at, dx, dy, side);
            const int nx = cell_x(n, side), ny = cell_y(n, side);
            cap   += fauna_cell_capacity_at(w, nx, ny);
            alive += resource_field_read(w, ResourceFieldId::Fauna, nx, ny);
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
int trees_growth_at(const MacroWorld& w, int x, int y) {
    if (!w.trees || !w.gs || !w.terrain || !w.terrain->has_rgba_storage()
        || !world_shape_ok(w.terrain->width, w.terrain->height))
        return 0;
    const int side = w.terrain->width;
    const std::uint32_t at = cell_of(x, y, side);
    int sum = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const std::uint32_t n = cell_step(at, dx, dy, side);
            sum += int(w.trees->at(cell_x(n, side), cell_y(n, side)));
        }
    int growth = sum * kGrowthEpochDays / (9 * 1024);
    if (growth <= 0) return 0;
    // THE cell cascade (map_generator.h biome_at_cell) — fail-closed to
    // Water, whose base tree count is 0: no storage, no growth.
    const Biome biome = biome_at_cell(*w.terrain, x, y);
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
int grass_feeds_head(const MacroWorld& w, int x, int y) {
    if (!w.terrain || w.terrain->width <= 0) return 0;
    return wheat_baseline(w, x, y) / (kDaysPerSeason * 4);
}

// НА КАКОЙ КЛЕТКЕ ЧИСЛО ЕСТЬ СТАДО — спрашивается КОЛОНКОЙ СТРОКИ ФИЧИ
// (`FeatureDef::numberRow`), никогда родом фичи веткой: поле чисел фич одно,
// и «что значит это число» есть вопрос о фиче (ЗАКОН ПОЛЯ п.3, ЗАКОН СТРОКИ
// КАТАЛОГА). Без слоя фич стада нет вовсе — fail-closed, ничего не выдумываем.
bool livestock_cell_(const MacroWorld& w, int x, int y) {
    return w.gs && w.features
        && feature_def(w.features->at(x, y)).numberRow
               == ResourceFieldId::Livestock;
}

// Кап стада ЭТОЙ клетки: трава её кормит, но только если на ней стоит фича,
// чьё число есть стадо. Ноль у всех прочих клеток — и это он даёт ходоку
// роста право пропустить их одним сравнением (`cap <= 0 → continue`), то
// есть мир больше не платит за выгон, которого нет.
int livestock_baseline(const MacroWorld& w, int x, int y) {
    return livestock_cell_(w, x, y) ? grass_feeds_head(w, x, y) : 0;
}

// ПОГОЛОВЬЕ РАСТЁТ, ПОКА ЗАГОН НЕ ПОЛОН: одна голова за сезонный визит —
// ровно тот темп, какой держал прежний закон в своём лучшем случае.
// ЧТО УШЛО ВМЕСТЕ С ПОЛЕМ И НАЗВАНО ВСЛУХ: условие «жив хотя бы половина
// ёмкости 3×3» и заселение выеденной округи с краёв. Оба меряли ДИКОЕ стадо
// соседних клеток, а дикого стада больше нет — число живёт внутри загона,
// и у загона нет соседей, у которых бы оно стояло. Закон стал проще, потому
// что вопрос стал уже, а не потому что его упростили.
int livestock_growth_at(const MacroWorld&, int, int) { return 1; }

// ЧТЕНИЕ И ЗАПИСЬ — ЧЕРЕЗ ПОЛЕ ЧИСЕЛ ФИЧ. Выемка зажимается в [0, кап] ЗДЕСЬ,
// у единственной двери ряда: `worked_write` сам по себе про кап стада не
// знает ничего (под другой фичей там спелл или паства), и знать не должен.
int livestock_read(const MacroWorld& w, int x, int y) {
    return livestock_cell_(w, x, y) ? worked_read(*w.gs, x, y) : 0;
}
void livestock_apply(MacroWorld& w, int x, int y, int delta) {
    if (!livestock_cell_(w, x, y)) return;
    const int cap = grass_feeds_head(w, x, y);
    worked_write(*w.gs, x, y,
                 std::clamp(worked_read(*w.gs, x, y) + delta, 0, cap));
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
                 nullptr, nullptr, 0, "food", NPCType::Count},
    /* Fauna */ {"fauna", &fauna_baseline,
                 GrowthDomain::HealToBaseline, &fauna_growth_at,
                 nullptr, nullptr, 0, "food", NPCType::Count},
    // ЛЕС — ЕДИНСТВЕННОЕ ИСКЛЮЧЕНИЕ ЗАКОНА, и оно названо вслух: базовой у
    // него нет, потому что нетронутая земля зарастает ГУЩЕ девственной.
    // The forest reaches nobody: no gate asks «is there wood near» today, и
    // этот 0 — не вся правда: лес есть НОСИТЕЛЬ, его живое состояние — это
    // TreeLayer, поэтому allocate_world_fields его пропускает и диска
    // досягаемости у него нет вовсе.
    /* Trees */ {"trees", nullptr,
                 GrowthDomain::CarrierSpread, &trees_growth_at,
                 &trees_read, &trees_apply, 0, "wood", NPCType::Count},
    // СТАДО — РЯД-НОСИТЕЛЬ НА ПОЛЕ ЧИСЕЛ ФИЧ (M-258): базовая есть КАП, а
    // живое число лежит в `worked` под фичей, которая объявила его своим.
    // Форма ровно та же, что у жилы ниже: своя базовая плюс чужой контейнер.
    // ЛОШАДЬ-ЮНИТ (CANON S10): выход — СУЩЕСТВО, добытое встаёт душой в
    // контейнер, спина сразу в обозе (сводка — разряд А).
    /* Livestock */ {"livestock", &livestock_baseline,
                 GrowthDomain::HealToBaseline, &livestock_growth_at,
                 &livestock_read, &livestock_apply, 0,
                 nullptr, NPCType::Horse},
    // ЖИЛА — ОДНО ПРИРОДНОЕ ПОЛЕ (вердикт 2026-10-22): рода нет, род РЕСУРСА
    // решает фича шахты (её `yieldItem`) — поэтому умолчание выхода у ряда
    // ПУСТО: без шахты добычи не существует. Кап залежи — связность кластера
    // (фундамент), текущее дорастает к нему единым законом (2026-10-23).
    /* Vein */ {"vein", &vein_baseline,
                GrowthDomain::HealToBaseline, &vein_growth_at,
                &vein_read, &vein_apply, kGathererReach,
                nullptr, NPCType::Count},
};
static_assert(sizeof(kResourceFields) / sizeof(kResourceFields[0])
                  == std::size_t(ResourceFieldId::Count),
              "every ResourceFieldId needs its def — the table IS the system");
// THE VEIN ROWS' REACH, pinned where the decision would be made. The deposit
// layer sizes its fields with kGathererReach directly (its table cannot link
// this one — the growth laws below drag the ECS with them), so this is what
// keeps "the radius is the ROW's column" true rather than merely intended: a
// row that ever wants a different reach reddens here, at the table.
static_assert(kResourceFields[std::size_t(ResourceFieldId::Vein)].reachCells
                  == kGathererReach,
              "deposit_layer.cpp sizes the vein field with kGathererReach; "
              "give the vein row its own reach and teach it that first");

ResourceGrid& scars_of(GameState& gs, ResourceFieldId f) {
    return gs.resourceScarCells[std::size_t(f)];
}

} // namespace

void allocate_world_fields(GameState& gs, int width, int height) {
    if (width <= 0 || height <= 0) return;
    for (std::size_t f = 0; f < std::size_t(ResourceFieldId::Count); ++f) {
        ResourceGrid& g = gs.resourceScarCells[f];
        // A CARRIER row keeps no scar of its own — its live state IS its
        // carrier (the tree grid, the vein grids), so it pays for no field
        // here. The registry says which, so nobody has to remember.
        if (kResourceFields[f].carrierRead) continue;
        if (g.width == width && g.height == height && g.live()) continue;
        // Reach 0: the scar answers «сколько здесь», and no gate asks the
        // neighbourhood question of a scar (the vein rows, which do get
        // asked, own their reach inside the deposit layer).
        g.allocate(width, height, 0);
    }
    if (!(gs.worked.width == width && gs.worked.height == height
          && gs.worked.live())) {
        gs.worked.allocate(width, height, 0);
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
                              int x, int y) {
    std::size_t total = 0;
    if (!FeatureLayer::cell_count_for(fl.width, fl.height, total)
        || fl.data.size() < total || !world.terrain)
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
    const std::uint32_t cell = cell_of(x, y, fl.width);
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

bool plough_cell_ok(const FeatureLayer& fl, const MacroWorld& world,
                    int x, int y, int& wheatOut)
{
    if (!parcel_ground_ok_(fl, world, x, y)) return false;
    // The ONE fertility door: potential minus what play has taken. ВОЗВРАЩАЕТСЯ
    // ЧИСЛО, А НЕ ВЕРДИКТ: планка `wheatOut >= field_wheat_min()` снесена
    // вердиктом владельца 2026-10-01 (spawners.h — там же её эпитафия и вывод).
    // Пахать можно всюду, где земля не вода и не занята; СКОЛЬКО уродится,
    // говорит само поле, и на камне оно говорит «ноль» без всякого запрета.
    wheatOut = resource_field_read(world, ResourceFieldId::Wheat,
                                   FeatureLayer::wrap_coord(x, fl.width),
                                   FeatureLayer::wrap_coord(y, fl.height));
    return true;
}

bool plough_field_cell(FeatureLayer& fl, const MacroWorld& world,
                       int x, int y, FeatureType parcel)
{
    // ЧЕМ ЗАСЕЯНО — КОЛОНКА ФИЧИ, А НЕ ВТОРАЯ ДВЕРЬ (2026-09-20): земля,
    // проверка и цена у хлебной и льняной парцеллы одни и те же, поэтому
    // вспашка одна, а вид парцеллы — её аргумент.
    int wheat = 0;
    if (!plough_cell_ok(fl, world, x, y, wheat)) return false;
    // ОДНА ДВЕРЬ ШТАМПА (M-212): воду она спрашивает сама, колонкой строки.
    // Гейт воды в `plough_cell_ok` остаётся — он отвечает на ДРУГОЙ вопрос
    // («стоит ли вообще тратить день»), и без него работа была бы оплачена
    // прежде, чем выяснилось, что ставить нечего.
    return feature_put(fl, *world.terrain, x, y, parcel) != FT_None;
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

int resource_field_read(const MacroWorld& w, ResourceFieldId f, int x, int y) {
    const ResourceFieldDef& def = resource_field_def(f);
    if (def.carrierRead) return def.carrierRead(w, x, y);
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    const int scar = int(w.gs->resourceScarCells[std::size_t(f)].at(x, y));
    return std::max(0, def.baseline(w, x, y) - scar);
}

int field_move(MacroWorld& w, ResourceFieldId f, int x, int y, int delta) {
    if (delta == 0) return 0;
    const ResourceFieldDef& carrier = resource_field_def(f);
    if (carrier.carrierApply) {
        // ФАКТ У НОСИТЕЛЯ — РАЗНОСТЬЮ ЧТЕНИЙ вокруг его же двери: диалекты
        // клампят каждый по-своему (лес — своим капом, стадо — травой, жила —
        // ёмкостью), и спрашивать их «сколько реально встало» честнее, чем
        // переписывать три клампа четвёртым экземпляром.
        const int before = carrier.carrierRead ? carrier.carrierRead(w, x, y)
                                               : 0;
        carrier.carrierApply(w, x, y, delta);
        const int after = carrier.carrierRead ? carrier.carrierRead(w, x, y)
                                              : 0;
        return after - before;
    }
    if (!w.gs || !w.terrain || w.terrain->width <= 0) return 0;
    ResourceGrid& scars = scars_of(*w.gs, f);
    // THE write sizes the field over THIS world if nobody did yet — the same
    // fail-open the worked layer's door keeps. A caller that has to remember
    // to allocate is a caller that will forget, and a lost scar is invisible:
    // the read simply answers "whole" (the fold-up lesson, applied to the one
    // door instead of to every fixture).
    if (!scars.live()) {
        scars.allocate(w.terrain->width, w.terrain->height, 0);
        if (!scars.live()) return 0;
    }
    // Spending (−delta) deepens the scar, returning (+delta, regrowth)
    // heals it; the scar never exceeds the baseline, so a runaway writer
    // cannot wind a cell into a millennium of regrowth.
    const int cap = std::max(0, resource_field_def(f).baseline(w, x, y));
    const int was = int(scars.at(x, y));
    const int scar = std::clamp(was - delta, 0, cap);
    // A healed cell is the field's own legal ZERO — no erase, no key, and
    // the live count the grid keeps is what the wire writes out.
    scars.write(x, y, std::int32_t(scar));
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

int field_yield_ordinal(const MacroWorld& w, ResourceFieldId f, int x, int y) {
    if (w.features) {
        const int fid = feature_yield_ordinal(w.features->at(x, y));
        if (fid >= 0) return fid;   // предмет фичи старше умолчания ряда
    }
    const ResourceFieldDef& row = resource_field_def(f);
    return row.yieldCreature == NPCType::Count ? row_yield_ordinal(f) : -1;
}

int field_harvest(MacroWorld& w, ResourceFieldId f, int x, int y, int want,
                  std::uint32_t squadSlot) {
    if (want <= 0 || !w.store || squadSlot >= kUnifiedCap) return 0;
    if (!w.store->alive[squadSlot]) return 0;
    const int have = resource_field_read(w, f, x, y);
    if (have <= 0) return 0;
    // ВЫХОД: фича клетки старше умолчания ряда (вердикт 2026-10-22 «что там
    // окажется решает фича шахты»; лён поверх пашни — тот же закон).
    const ResourceFieldDef& row = resource_field_def(f);
    const int outIdx = field_yield_ordinal(w, f, x, y);
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
        field_move(w, f, x, y, -take);
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
    field_move(w, f, x, y, -take);
    return take;
}

int resource_field_scar(const GameState& gs, ResourceFieldId f,
                        std::uint32_t cellIdx) {
    const ResourceGrid& scars = gs.resourceScarCells[std::size_t(f)];
    if (!scars.live() || cellIdx >= scars.cells.size()) return 0;
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
            if (!w.trees || !w.trees->has_complete_storage()) break;
            const int W = w.trees->width;
            const std::size_t n = w.trees->cell_count();
            for (std::size_t idx = std::size_t(day % kGrowthEpochDays);
                 idx < n; idx += std::size_t(kGrowthEpochDays)) {
                const int x = cell_x(std::uint32_t(idx), W);
                const int y = cell_y(std::uint32_t(idx), W);
                const int born = def.growthAt(w, x, y);
                if (born > 0) field_move(w, row, x, y, born);
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
            const int W = w.terrain->width;
            const std::size_t n =
                std::size_t(W) * std::size_t(w.terrain->height);
            for (std::size_t idx = std::size_t(day % kGrowthEpochDays);
                 idx < n; idx += std::size_t(kGrowthEpochDays)) {
                const int x = cell_x(std::uint32_t(idx), W);
                const int y = cell_y(std::uint32_t(idx), W);
                // Полная клетка не лечится: спрашиваем ёмкость ДО закона
                // роста, потому что для жилы ёмкость — это шум, и платить за
                // него на каждой полной клетке незачем.
                const int cap = def.baseline ? def.baseline(w, x, y) : 0;
                if (cap <= 0) continue;
                if (resource_field_read(w, row, x, y) >= cap) continue;
                const int born = def.growthAt(w, x, y);
                if (born > 0) field_move(w, row, x, y, born);
            }
            break;
        }
        }
    }
}

namespace {

// ── The stock rows over the container ──────────────────────────────────────

int read_fauna_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Fauna, k.cellX, k.cellY);
}
void write_fauna_count(MacroWorld& w, MacroStockKey k, int delta) {
    field_move(w, ResourceFieldId::Fauna, k.cellX, k.cellY, delta);
}
int read_crop_count(const MacroWorld& w, MacroStockKey k) {
    return resource_field_read(w, ResourceFieldId::Wheat, k.cellX, k.cellY);
}
void write_crop_count(MacroWorld& w, MacroStockKey k, int delta) {
    field_move(w, ResourceFieldId::Wheat, k.cellX, k.cellY, delta);
}
// Число фичи клетки — носитель GameState::worked (один на мир, ЗАКОН ПОЛЯ
// п.3); worked_write клампит в ноль сам — ниже нуля числа фич не живут.
int read_worked(const MacroWorld& w, MacroStockKey k) {
    return w.gs ? worked_read(*w.gs, k.cellX, k.cellY) : 0;
}
void write_worked(MacroWorld& w, MacroStockKey k, int delta) {
    if (w.gs) worked_add(*w.gs, k.cellX, k.cellY, delta);
}

constexpr MacroStockRow kRows[] = {
    {"tree_count",  &read_tree_count,  &write_tree_count},
    {"population",  &read_population,  &write_population},
    {"creatures",   &read_creatures,   &write_creatures},
    {"fauna_count", &read_fauna_count, &write_fauna_count},
    {"crop_count",  &read_crop_count,  &write_crop_count},
    {"worked",      &read_worked,      &write_worked},
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

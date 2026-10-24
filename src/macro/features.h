// Per-cell feature byte grid (between biome and place).
//
// Features are MAN-MADE structures composed ON TOP of the biome ground:
// roads and dirt roads today, future railways / fields / canals tomorrow.
// Natural cover is NOT a feature: mountains are the elevation-classified
// Mountain biome (biomes.h biome_at), and forests are the per-cell
// tree-count field (macro/tree_layer.h) — a forested mountain is the
// Mountain biome with a high tree count, no feature byte involved.
#pragma once
#include "core/table_guard.h"
#include "core/torus.h"
#include "tables/feature_type.h"
// Дверь штампа ниже спрашивает воду САМА, поэтому ей нужен порог карты.
// Цикла нет и быть не может: `map_generator.h` слой фич не включает —
// терраин о фичах не знает, фичи о терраине знают.
#include "macro/map_generator.h"
// Колонка `numberRow` ниже называет РЯД РЕЕСТРА, чьё число стоит на фиче.
// Цикла нет: реестр полей знает про клетки и сезоны, про фичи — ничего.
#include "macro/resource_field.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sm {

// (Словарь FeatureType ПЕРЕЕХАЛ в tables/feature_type.h ломтём 3 эпика
// фрейма: строка клетки пакета несёт фичу, а bus/ не смеет включать macro/ —
// словарь по закону M-181 живёт нейтральным слоем. Здесь остался СЛОЙ —
// изменяемое состояние мира.)
// (ВЫРЕЗАНЫ 2026-09-22, сессия 14: FT_WoodBridge, FT_Port, FT_BeachedShip.
// Деревянный мост — единственное, что артель ещё строила; корабли и порты —
// весь транспортный слой, до которого мир не дошёл. Байты перенумерованы
// сплошь: сейв старых миров не стоит ничего, закон P1.)

// ── THE feature registry (CANON S16, 2026-08-29) ─────────────────────────
// Everything the world says ABOUT a feature is a column of ONE row. These
// numbers lived as three private dialects — a switch of bed weights with a
// silent 0.0 default in movement_cost.h, a bare float array in optics.h, an
// if-chain of civ strengths in zones.cpp — three tables about one byte, each
// free to forget a feature on its own. The values are EXACTLY those homes'
// (the sweep moved the numbers, it did not retune them), and the guard below
// makes a grown FeatureType refuse to compile instead of defaulting quietly.
struct FeatureDef {
    FeatureType type;   // MUST equal the row's index (guard below)
    // SP bed the engineered surface lays (movement_cost.h step-cost law).
    // 0 = nothing built here — the biome's own ground is the bed, the law's
    // silent zero, never a sentinel to branch on. Road 1.0 is the reference
    // march (speed = base/√weight); dirt is half again the paved bed.
    float bedWeight;
    // Optical budget one cell of this feature spends (optics.h
    // optical_sweep) — open land is the 1.0 baseline; a road is a clear,
    // reflective corridor that carries light and sight furthest; ploughed
    // waist-high wheat hides nothing. Mountains and forests are deliberately
    // NOT rows: mountains are a biome (elevation term), forests the
    // tree-count field (kCanopyOpticalCost per unit of density).
    float opticalCost;
    // Civilization pull this feature seeds into the danger field
    // (zones.cpp): how strongly a built thing pushes the wilderness back.
    // 0 = builds no safety of its own (a field is tended, not garrisoned).
    float civStrength;
    // (КОЛОНКА `worksRow` ВЫРЕЗАНА 2026-09-22, сессия 14: ноль читателей за
    // всю жизнь, при том что её шапка звала себя «THE one door». Живая связь
    // «фича → что с неё берут» теперь КОЛОНКА `yieldItem` ниже — но это НЕ
    // воскрешение worksRow: та называла РЯД, эта называет ПРЕДМЕТ, и читатель
    // у неё есть с рождения — транзакция field_harvest.)
    // (КОЛОНКА `buildsPerDay` И КОНСТАНТА `kBuildsPerDay` ВЫРЕЗАНЫ 2026-10-23,
    // ломоть 1 M-72: ноль читателей во всём дереве — ветка стройки артелью
    // умерла 2026-09-22, фичи ставит генерация, цена работы вернётся колонкой,
    // когда у неё родится читатель.)

    // ── ВО ЧТО ЭТА ФИЧА ПРЕВРАЩАЕТСЯ НА ВОДЕ ─────────────────────────────
    // Владелец, 2026-10-03: «ну дорога на воде превращается в фичу мост да?
    // надо без костылей, а системно». Системно это значит, что три случая
    // перестают быть тремя механизмами и становятся тремя ЗНАЧЕНИЯМИ ОДНОЙ
    // колонки:
    //   · `FT_None`   — на воде не встаёт вовсе (пашня, шахта, поселение);
    //   · САМА СЕБЯ   — вода безразлична (мост на воде остаётся мостом);
    //   · другой род  — ПРЕВРАЩАЕТСЯ (дорога на воде ЕСТЬ мост).
    //
    // До этой колонки правило «дорога на воде — мост» стояло ВЕТКОЙ у двух
    // звонящих (`spawners.cpp`, трассер дорог и сборка слоя), то есть одно
    // правило мира было написано дважды и могло разойтись молча. А поселения
    // воду не проверяли ВООБЩЕ и держались на том, что размещатель ставит их
    // на сушу, — неявная зависимость, которая живёт до первого размещателя,
    // который решит иначе.
    //
    // Поселение есть ФИЧА СО СКВАДОМ ПОВЕРХ (владелец, тот же день), поэтому
    // `FT_City`/`FT_Village`/`FT_Spire`/`FT_Ruin` несут эту колонку наравне со
    // всеми: отдельного «водного правила для мест» не существует.
    FeatureType onWater;

    // ── ЧЕЙ РЯД НЕСЁТ ЧИСЛО, СТОЯЩЕЕ НА ЭТОЙ ФИЧЕ ────────────────────────
    // ЗАКОН ПОЛЯ п.3, сделанный СТРУКТУРОЙ. Владелец, дословно: «ЧИСЛО В
    // ПАШНЕ ЧИСЛО В ГОРОДЕ ЧИСЛО В ШАХТЕ ЭТО И БУДЕТ ВСЁ ТАКИ ЕДИНОЕ ПЛОСКОЕ
    // ПОЛЕ НАД МИРОМ ДЛЯ ЧИСЕЛ В ФИЧАХ … смысл числу задаёт фича на той же
    // клетке»; и 2026-10-12 о табуне: «у нас есть поля фич а пастбища это
    // фича числа на нём и будут значить всё». Поле чисел фич ОДНО
    // (`GameState::worked`), значит вопрос «что значит число на этой клетке»
    // есть вопрос О ФИЧЕ — и отвечает на него ЕЁ СТРОКА, а не ветка
    // `if (f == FT_Pasture)` у читателя (ЗАКОН СТРОКИ КАТАЛОГА).
    //
    // ЭТО НЕ ВОСКРЕСШАЯ `worksRow` (вырезана 2026-09-22 с нулём читателей за
    // всю жизнь). Та отвечала «что с фичи БЕРУТ», и этот вопрос действительно
    // написан строкой цели (`kGathererDefs.row`) и только там. Эта колонка
    // отвечает на ДРУГОЙ вопрос — «чьё число на фиче СТОИТ», — и у неё есть
    // читатель с первого дня: двери ряда-носителя стада (`macro_stock.cpp`
    // `livestock_read`/`livestock_apply`), без неё спрашивавшие бы род фичи
    // веткой.
    //
    // `ResourceFieldId::Count` = «число этой фичи рядом реестра НЕ является»:
    // у шпиля там ординал спелла, у города и деревни — паства, у дороги числа
    // нет вовсе. У ПАШНИ сегодня тоже `Count`, и это ПРАВДА, а не недосмотр:
    // стоячий хлеб живёт шрамом ряда `Wheat`, а не полем чисел фич, и
    // переедет сюда нарядом M-78 (консолидированная жила шахты — M-88).
    ResourceFieldId numberRow;

    // ── ВО ЧТО ПРЕВРАЩАЕТСЯ ДОБЫЧА ПОД ЭТОЙ ФИЧЕЙ ────────────────────────
    // Вердикты 2026-10-22/23: «что там окажется решает фича шахты — золотая
    // железеная мденая и тд»; лён уже жил этой формой (одна фертильность,
    // две фичи). nullptr = фича выход ряда НЕ переопределяет, и транзакция
    // field_harvest берёт умолчание строки реестра (ResourceFieldDef). Это
    // ОВЕРРАЙД, а не словарь-дубль: предмет называется ровно в одном месте
    // на каждый случай. Строка — ключ авторства; ординал кэшируется лениво
    // у двери (ЗАКОН СЛОВАРЯ).
    const char* yieldItem;
};

inline constexpr FeatureDef kFeatureDefs[std::size_t(FT_Count)] = {
    //                     bed   optics  civ    на воде   чьё число · выход
    {FT_None,     0.0f, 1.00f, 0.0f,  FT_None,
     ResourceFieldId::Count, nullptr},
    {FT_Road,     1.0f, 0.65f, 0.35f, FT_Bridge,
     ResourceFieldId::Count, nullptr},
    {FT_DirtRoad, 1.5f, 0.85f, 0.22f, FT_Bridge,
     ResourceFieldId::Count, nullptr},
    // Вспаханная парцелла. Какой культурой засеяна — вопрос ТИПА фичи, а не
    // второго ресурсного ряда: картофельное и маковое поля будут новыми
    // строками ЭТОЙ таблицы, работающими то же число. Выход пашни — умолчание
    // ряда Wheat («food»), поэтому оверрайда у неё нет.
    {FT_Field,    1.8f, 1.00f, 0.0f,  FT_None,
     ResourceFieldId::Count, nullptr},
    // Мост несёт колонки каменной дороги: его настил И ЕСТЬ мощёное ложе
    // (марш не замечает реки под ним), тот же коридор свету и взгляду, та же
    // тяга цивилизации. Кладёт его планировщик дорог при генерации мира.
    {FT_Bridge,   1.0f, 0.65f, 0.35f, FT_Bridge,
     ResourceFieldId::Count, nullptr},
    // Шахты делят колонки пашни: разработанная земля, не инженерное ложе
    // (0 = своё основание биома), прятаться не за чем, и рабочее место,
    // которое возделывают, а не держат гарнизоном. ВЫХОД — СТРОКА ШАХТЫ, а
    // не рода жилы: жилы единые (вердикт 2026-10-22), род ресурса решает
    // ровно эта колонка. А ЧИСЛО НА ШАХТЕ ЕСТЬ ЕЁ ЗАЛЕЖЬ (numberRow = Vein,
    // M-78/M-88): кап связного кластера, свёрнутый генерацией шахт.
    {FT_ClayPit,    0.0f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Vein, "clay"},
    {FT_IronMine,   0.0f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Vein, "iron"},
    {FT_Quarry,     0.0f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Vein, "stone"},
    {FT_SilverMine, 0.0f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Vein, "silver"},
    {FT_CopperMine, 0.0f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Vein, "copper"},
    {FT_GoldMine,   0.0f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Vein, "gold"},
    // Пастбище и льняное поле несут колонки пашни: та же возделанная земля,
    // та же трава по пояс, которая ничего не прячет.
    // А ЧИСЛО НА ПАСТБИЩЕ ЕСТЬ СТАДО — единственная строка таблицы, чьё
    // число сегодня принадлежит ряду реестра (вердикт владельца 2026-10-12,
    // M-258): собственного массива у ряда больше нет, его носитель — поле
    // чисел фич, и смысл числу задаёт ВОТ ЭТА колонка. Выход пастбища —
    // умолчание ряда Livestock (конь-душа), оверрайд не нужен.
    {FT_Pasture,  1.8f, 1.00f, 0.0f,  FT_None,
     ResourceFieldId::Livestock, nullptr},
    // ЛЁН — оверрайд выхода поверх ряда Wheat: та же земля, своя культура.
    {FT_FlaxField, 1.8f, 1.00f, 0.0f, FT_None,
     ResourceFieldId::Count, "fibre"},
    // ── Поселения. Ложе — по прецеденту моста («мост несёт колонки
    // каменной дороги»): город есть МОЩЁНЫЙ узел дорожной сети — его клетка
    // несёт ложе камня (1.0); деревня — грунтовый двор (ложе грунтовки,
    // 1.5); шпиль и руина стоят на своём основании биома (0, как шахты).
    // Оптика 1.0 и цивилизация 0 — НУЛЕВОЙ ВКЛАД: свет, взгляд и поле
    // опасности продолжают читать свои прежние источники; собственные числа
    // этим колонкам выдаст тот, кто первым начнёт их читать (ЗАКОН КОНСТАНТ
    // — не выдумывать). Фичи поселений ставит генерация.
    // Число на поселении — ПАСТВА, на шпиле — ординал спелла: ни то, ни
    // другое рядом реестра не является, поэтому здесь `Count` (см. колонку).
    {FT_City,    1.0f, 1.00f, 0.0f, FT_None, ResourceFieldId::Count, nullptr},
    {FT_Village, 1.5f, 1.00f, 0.0f, FT_None, ResourceFieldId::Count, nullptr},
    {FT_Spire,   0.0f, 1.00f, 0.0f, FT_None, ResourceFieldId::Count, nullptr},
    {FT_Ruin,    0.0f, 1.00f, 0.0f, FT_None, ResourceFieldId::Count, nullptr},
};
static_assert(rows_in_enum_order(kFeatureDefs, &FeatureDef::type),
              "kFeatureDefs row order must mirror FeatureType — a new "
              "feature IS its row here");

// ── ЕДИНСТВЕННАЯ ДВЕРЬ ШТАМПА ФИЧИ ─────────────────────────────────────────
//
// Она и задаёт воде ОДИН вопрос на весь слой фич, и отвечает на него ОДНИМ
// способом — колонкой строки. Звонящему не нужно помнить ни про воду, ни про
// мост: он говорит, ЧТО хочет поставить, а что реально встанет, решает таблица.
//
// Возвращает фактически поставленный род (`FT_None` — не встало ничего), чтобы
// звонящий, которому это важно (счётчик дорог, цена работы), читал ФАКТ, а не
// своё намерение.
inline FeatureType feature_on_water(FeatureType want) {
    return kFeatureDefs[std::size_t(want) < std::size_t(FT_Count)
                            ? std::size_t(want) : 0u].onWater;
}

inline constexpr const FeatureDef& feature_def(FeatureType t) {
    return std::size_t(t) < std::size_t(FT_Count)
               ? kFeatureDefs[std::size_t(t)]
               : kFeatureDefs[std::size_t(FT_None)];   // fail-open ground
}

struct FeatureLayer {
    int width = 0, height = 0;
    std::vector<std::uint8_t> data;

    // Validity comes from the ENUM, never a hand list (2026-08-31): the old
    // `<= FT_Bridge` whitelist would have sanitized the mine and wood-bridge
    // bytes to FT_None the day they were born — exactly the drift the
    // feature registry exists to kill.
    static bool is_valid_byte(std::uint8_t value) {
        return value < FT_Count;
    }

    static FeatureType decode(std::uint8_t value) {
        return value < FT_Count ? FeatureType(value) : FT_None;
    }

    static bool cell_count_for(int w, int h, std::size_t &out) {
        out = 0;
        if (w <= 0 || h <= 0)
            return false;
        if (std::size_t(w) > std::numeric_limits<std::size_t>::max() / std::size_t(h))
            return false;
        out = std::size_t(w) * std::size_t(h);
        return true;
    }

    // The one torus wrap (core/torus.h). This copy was the strongest of the
    // six — its guard and its 64-bit intermediate are what `wrapi` now has —
    // and the name survives because a dozen call sites read it as documentation
    // ("wrap this into the feature grid").
    // ЗАКОН АДРЕСА: заворот оси мира — маска, одна реализация на проект.
    // Имя остаётся: у 45 мест вызова оно читается как документация.
    static int wrap_coord(int value, int limit) { return wrap_axis(value, limit); }

    std::size_t cell_count() const {
        std::size_t n = 0;
        return cell_count_for(width, height, n) ? n : 0u;
    }

    bool has_complete_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n;
    }

    bool has_invalid_cell_bytes() const {
        const std::size_t n = cell_count();
        if (n == 0u || data.size() < n)
            return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (!is_valid_byte(data[i]))
                return true;
        }
        return false;
    }

    bool copy_sanitized_cells(std::vector<std::uint8_t> &out) const {
        const std::size_t n = cell_count();
        if (n == 0u || data.size() < n) {
            out.clear();
            return false;
        }
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i)
            out[i] = std::uint8_t(decode(data[i]));
        return true;
    }

    const std::uint8_t *complete_cells_or_sanitized(std::vector<std::uint8_t> &out) const {
        const std::size_t n = cell_count();
        if (n == 0u || data.size() < n) {
            out.clear();
            return nullptr;
        }
        bool copied = false;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t value = data[i];
            if (is_valid_byte(value))
                continue;
            if (!copied) {
                out.assign(data.data(), data.data() + n);
                copied = true;
            }
            out[i] = std::uint8_t(FT_None);
        }
        if (copied)
            return out.data();
        out.clear();
        return data.data();
    }

    bool covers(int w, int h) const {
        std::size_t n = 0;
        return width == w && height == h && cell_count_for(w, h, n) && data.size() >= n;
    }

    void resize(int w, int h) {
        std::size_t n = 0;
        if (!cell_count_for(w, h, n)) {
            width = 0;
            height = 0;
            data.clear();
            return;
        }
        width = w;
        height = h;
        data.assign(n, 0);
    }
    // Адрес — ЕДИНСТВЕННАЯ дверь мира (`core/torus.h`). Гейт включает
    // ИНВАРИАНТ (ЗАКОН АДРЕСА): мир квадратен и степень двойки, иначе
    // fail-closed — потому что маска на незаконном мире врёт молча.
    //
    // ДВЕ ФОРМЫ ОДНОЙ ДВЕРИ, А НЕ ДВЕ ДВЕРИ (ЗАКОН АДРЕСА п.2, M-112): адрес
    // клетки есть ОДНО ЧИСЛО, и форма по индексу — основная; пара `x,y`
    // законна там, где у звонящего ГЕОМЕТРИЯ, и сворачивается в адрес ЗДЕСЬ,
    // а не руками у каждого. Индекс уже свёрнут по построению (`cell_of`,
    // `cell_step`), поэтому маска ему не нужна — нужна только граница памяти.
    // До этого наряда записи фич не было вовсе: девять мест писали
    // `data[y*w+x] = FT_…` мимо двери, то есть девять раз заворот и проверка
    // рода держались на памяти автора, а не на типе.
    FeatureType at(std::uint32_t cell) const {
        return cell < data.size() ? decode(data[cell]) : FT_None;
    }
    FeatureType at(int x, int y) const {
        if (!world_shape_ok(width, height) || data.empty()) return FT_None;
        return at(cell_of(x, y, width));
    }

private:
    // ── СТЕНА ЛОМТЯ 1 M-72: сырой `set` закрыт компилятором ──────────────
    // Род фичи клетки пишет ровно одна дверь — feature_put (вода колонкой
    // строки, FT_None = снять, возврат факта). Друзья: обе формы двери и
    // именованный пробник прибора контракта САМОЙ двери
    // (tests/feature_layer_parity_test.cpp — гейты формы и шов индекс/пара).
    friend FeatureType feature_put(FeatureLayer&, const TerrainData&,
                                   std::uint32_t, FeatureType);
    friend FeatureType feature_put(FeatureLayer&, const TerrainData&,
                                   int, int, FeatureType);
    friend struct FeatureLayerProbe;

    void set(std::uint32_t cell, FeatureType t) {
        if (cell >= data.size()) return;
        data[cell] = std::uint8_t(decode(std::uint8_t(t)));
    }
    void set(int x, int y, FeatureType t) {
        if (!world_shape_ok(width, height) || data.empty()) return;
        set(cell_of(x, y, width), t);
    }
};

// ── ДВЕРЬ №3 ПУЛЬТА: РОД ФИЧИ КЛЕТКИ (ломоть 1 M-72; бывш. stamp_feature) ──
// Ставит и СНИМАЕТ одной дверью: `FT_None` — значение, а не отсутствие
// намерения (выработанную шахту прежде было снять нечем — дыра закрыта).
// Воду фича спрашивает сама, колонкой своей строки (M-212); возвращает ФАКТ —
// что реально стоит в клетке после записи, — чтобы звонящий читал факт, а не
// своё намерение.
inline FeatureType feature_put(FeatureLayer& fl, const TerrainData& td,
                               std::uint32_t cell, FeatureType want) {
    if (want == FT_None) {   // снятие: вода решает только у ПОСТАНОВКИ
        fl.set(cell, FT_None);
        return FT_None;
    }
    // ВОДА СПРАШИВАЕТСЯ У ПОРОГА РЕЛЬЕФА, И ТОЛЬКО У НЕГО (M-212).
    const FeatureType got = td.is_water(cell) ? feature_on_water(want) : want;
    if (got == FT_None) return FT_None;   // вода отказала — не встало ничего
    fl.set(cell, got);
    return got;
}

// Та же дверь парой координат — для звонящих, у которых на руках геометрия
// (ЗАКОН АДРЕСА: индекс первичен, пара законна там, где она уже есть).
inline FeatureType feature_put(FeatureLayer& fl, const TerrainData& td,
                               int x, int y, FeatureType want) {
    return feature_put(fl, td, cell_of(x, y, fl.width), want);
}

} // namespace sm

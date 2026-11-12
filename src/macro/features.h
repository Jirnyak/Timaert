// Per-cell feature byte grid (between biome and place).
//
// Features are MAN-MADE structures composed ON TOP of the biome ground:
// roads and dirt roads today, future railways / fields / canals tomorrow.
// Natural cover is NOT a feature: mountains are the elevation-classified
// Mountain biome (biomes.h biome_at), and forests are the per-cell
// tree-count field (macro/tree_layer.h) — a forested mountain is the
// Mountain biome with a high tree count, no feature byte involved.
#pragma once
#include "core/caps.h"   // kWorldCellCap — кап клеток мира
#include "core/table_guard.h"
#include "core/torus.h"
#include "tables/feature_type.h"
// Строки каталога, чей байт этот слой хранит: дверь штампа ниже спрашивает у
// них, во что фича превращается на воде. Цикла нет и быть не может —
// нейтральный слой о слое фич не знает.
#include "tables/feature_def.h"
// Дверь штампа ниже спрашивает воду САМА, поэтому ей нужен порог карты.
// Цикла нет и быть не может: `map_generator.h` слой фич не включает —
// терраин о фичах не знает, фичи о терраине знают.
#include "macro/map_generator.h"
// Колонка `numberRow` ниже называет РЯД РЕЕСТРА, чьё число стоит на фиче.
// Цикла нет: реестр полей знает про клетки и сезоны, про фичи — ничего.
#include "macro/resource_field.h"
// Колонка `kind` ниже называет РОД СКВАДА, который эту фичу ставит (вариант В
// M-235). Ось рода живёт НЕЙТРАЛЬНЫМ слоем (§11, M-181), поэтому включение
// идёт вниз по слоям и цикла быть не может: `tables/` о фичах не знает.
#include "tables/light_palette.h"
#include "tables/squad_type.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
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

// (КАТАЛОГ ФИЧ — `FeatureDef`, `kFeatureDefs`, `feature_def`, `feature_on_water`
//  и пара `rooted_kind_of_feature`/`feature_of_rooted_kind` — ПЕРЕЕХАЛ в
//  tables/feature_def.h ломтём 1а эпика пакетов v1: строки читают только
//  каталоги, а спрашивает их и субмир, которому `macro/` включать нельзя.
//  Здесь остался СЛОЙ — изменяемое состояние мира.)

// ПЛОСКИЙ МАССИВ ПОД КАПОМ КЛЕТОК МИРА, И ДОМ ЕГО — СОСТОЯНИЕ МИРА (M-456,
// 2026-11-10; вердикт владельца о сейв-эпике: «все поля над тором, лежащие в
// состоянии мира (Род 3), едут дампом единообразно»). Здесь стоял
// `std::vector<std::uint8_t>`, а сам слой жил членом оболочки приложения и
// приходил в симуляцию указателем конверта. Поле при этом ПИСАЛОСЬ В ЖИВОМ
// МИРЕ — смерть города кладёт на клетку руину, смерть деревни снимает фичу
// (`city_death@src/macro/npc_ai.cpp`, `village_death@src/macro/npc_ai.cpp`), —
// а в сейв не ехало и на загрузке перевыводилось из сида: руина после загрузки
// снова становилась городом. Теперь форма та же, что у леса
// (`TreeLayer@src/macro/tree_layer.h`): heap-члена нет, слой жив, когда его
// стороны называют мир в пределах капа; живые клетки — первые `cell_count()`,
// хвост до капа стоит нулём. Структура весит 1 МиБ и на стеке не живёт — она
// лежит полем `GameState::features@src/macro/state.h`.
struct FeatureLayer {
    int width = 0, height = 0;
    // СЧЁТЧИК ПРАВОК (M-248 / M-457 п.7) — та же форма, что у леса и знания
    // (`TreeLayer@src/macro/tree_layer.h`, `KnowledgeLayer@src/macro/knowledge.h`):
    // растёт, когда байт клетки РЕАЛЬНО сменился, и когда слой рождён заново.
    // Читает его картина: текстура карты сверяет своё зеркало с этим числом и
    // перезаливает поле фич, поэтому структура, поставленная на клетку в живой
    // игре (город стал руиной, встал шпиль), видна на карте в том же кадре, а
    // не после загрузки. Величиной мира он не является, но едет в файле вместе
    // со слоем (сейв есть прямой дамп), поэтому дверь, заменяющая слой целиком,
    // обязана назвать зеркало протухшим сама (`upload_macro_world@src/app/main.cpp`).
    // Запись того же байта поверх себя счётчик НЕ двигает: перезалив мегабайта
    // за правку, которой не было, платил бы каждый дневной тик.
    std::uint32_t revision = 0;
    // НАЗВАННЫЙ ХВОСТ ВЫРАВНИВАНИЯ, всегда ноль. Счётчик правок сделал шапку
    // 12-байтной, и состояние мира (выравнивание 8) получило бы безымянную
    // дыру в 4 Б перед следующим членом — а безымянных байтов в прямом дампе
    // нет (`TIMAERT_PACKED@src/core/row_law.h`). Величиной не является и
    // читателя не имеет; первый же осмысленный u32 шапки занимает это место.
    std::uint32_t pad = 0;
    std::uint8_t data[kWorldCellCap] = {};

    // Validity comes from the ENUM, never a hand list (2026-08-31): the old
    // `<= FT_Bridge` whitelist would have sanitized the mine and wood-bridge
    // bytes to FT_None the day they were born — exactly the drift the
    // feature registry exists to kill.
    static bool is_valid_byte(std::uint8_t value) {
        return value < FT_Count;
    }

    // Толкование байта живёт У ОСИ (`feature_decode@src/tables/feature_type.h`,
    // ломоть 3 эпика пакетов v1): арифметика над словарём состояния мира не
    // касается, а спрашивал её и субмир — девять его сайтов читали ЭТОТ статик
    // и тянули за него весь слой в канал. Имя остаётся звонком: у макро-мест
    // оно читается как «просей байт этой решётки».
    static FeatureType decode(std::uint8_t value) { return feature_decode(value); }

    // Сколько клеток у решётки w×h — БЕЗ суда о капе мира. Этой формой
    // спрашивают слои, которые под кап не легли (зоны, рельеф генератора):
    // им нужен только счёт без переполнения.
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

    // Живые клетки слоя — ровно мир, не кап. Мир вне капа (или с
    // неположительной стороной) даёт ноль: слой молчит, fail-closed.
    std::size_t cell_count() const {
        return world_cells_within_cap(width, height);
    }
    std::span<const std::uint8_t> cells() const {
        return {data, cell_count()};
    }

    bool has_complete_storage() const { return cell_count() > 0u; }

    bool has_invalid_cell_bytes() const {
        const std::size_t n = cell_count();
        for (std::size_t i = 0; i < n; ++i) {
            if (!is_valid_byte(data[i]))
                return true;
        }
        return false;
    }

    bool copy_sanitized_cells(std::vector<std::uint8_t> &out) const {
        const std::size_t n = cell_count();
        if (n == 0u) {
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
        if (n == 0u) {
            out.clear();
            return nullptr;
        }
        bool copied = false;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t value = data[i];
            if (is_valid_byte(value))
                continue;
            if (!copied) {
                out.assign(data, data + n);
                copied = true;
            }
            out[i] = std::uint8_t(FT_None);
        }
        if (copied)
            return out.data();
        out.clear();
        return data;
    }

    bool covers(int w, int h) const {
        return width == w && height == h && cell_count() > 0u;
    }

    // Родить ПУСТОЙ слой над миром w×h: стороны названы, ВЕСЬ кап обнулён —
    // хвост за миром обязан стоять нулём, иначе байты слоя помнили бы прошлый
    // мир. Мир вне капа рождает слой без сторон (fail-closed, `at()` отвечает
    // `FT_None`).
    void resize(int w, int h) {
        const bool fits = world_cells_within_cap(w, h) > 0u;
        width = fits ? w : 0;
        height = fits ? h : 0;
        std::memset(data, 0, sizeof(data));
        ++revision;   // слой рождён заново — картина по нему протухла
    }
    // Адрес — ЕДИНСТВЕННАЯ дверь мира (`core/torus.h`). Гейт включает
    // ИНВАРИАНТ (ЗАКОН АДРЕСА): мир квадратен и степень двойки, иначе
    // fail-closed — потому что маска на незаконном мире врёт молча.
    //
    // ФОРМА ОДНА — ИНДЕКС (ЗАКОН АДРЕСА п.2; парная форма `at(x, y)` снята
    // ломтём 4 ряда M-462): адрес клетки есть ОДНО ЧИСЛО. У кого на руках
    // геометрия (генерация, экран), сворачивает её дверью `cell_of` сам —
    // пара на доступе к полю была налогом для всех, кто уже держал индекс.
    // Индекс свёрнут по построению (`cell_of`, `cell_step`), поэтому маска
    // ему не нужна — нужна только граница памяти.
    // До наряда M-112 записи фич не было вовсе: девять мест писали
    // `data[y*w+x] = FT_…` мимо двери, то есть девять раз заворот и проверка
    // рода держались на памяти автора, а не на типе.
    FeatureType at(std::uint32_t cell) const {
        return cell < cell_count() ? decode(data[cell]) : FT_None;
    }

private:
    // ── СТЕНА ЛОМТЯ 1 M-72: сырой `set` закрыт компилятором ──────────────
    // Род фичи клетки пишет ровно одна дверь — feature_put (вода колонкой
    // строки, FT_None = снять, возврат факта). Друзья: обе формы двери и
    // именованный пробник прибора контракта САМОЙ двери
    // (tests/feature_layer_parity_test.cpp — гейты формы и шов индекс/пара).
    friend FeatureType feature_put(FeatureLayer&, const TerrainData&,
                                   std::uint32_t, FeatureType);
    friend struct FeatureLayerProbe;

    void set(std::uint32_t cell, FeatureType t) {
        if (cell >= cell_count()) return;
        const std::uint8_t byte = std::uint8_t(decode(std::uint8_t(t)));
        if (data[cell] == byte) return;   // правки не было — счётчик стоит
        data[cell] = byte;
        ++revision;
    }
};
static_assert(sizeof(FeatureLayer) == 16u + kWorldCellCap,
              "слой фич: 16 Б шапки + байт на клетку капа, без хвоста");

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

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла: заголовок пользуется контуром
// только в блоке ниже, а вставленная наверху строка сдвинула бы всё содержимое
// и сгноила бы ссылки документов в этот файл (§13 п.5).
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА (M-456) ────────────────────────────────────────
// Слой фич — член состояния мира и едет прямым дампом; структура, в которую
// заложили вектор или строку, отсюда не соберётся (`core/row_law.h`). За
// полнотой списка следит `arch_guard_test` (файл стоит в его `kRowHeaders`).
TIMAERT_ROW(sm::FeatureLayer);

// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются.
TIMAERT_PACKED(sm::FeatureLayer, &sm::FeatureLayer::width,
    &sm::FeatureLayer::height, &sm::FeatureLayer::revision,
    &sm::FeatureLayer::pad, &sm::FeatureLayer::data);

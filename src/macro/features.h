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

// (КАТАЛОГ ФИЧ — `FeatureDef`, `kFeatureDefs`, `feature_def`, `feature_on_water`
//  и пара `rooted_kind_of_feature`/`feature_of_rooted_kind` — ПЕРЕЕХАЛ в
//  tables/feature_def.h ломтём 1а эпика пакетов v1: строки читают только
//  каталоги, а спрашивает их и субмир, которому `macro/` включать нельзя.
//  Здесь остался СЛОЙ — изменяемое состояние мира.)

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

    // Толкование байта живёт У ОСИ (`feature_decode@src/tables/feature_type.h`,
    // ломоть 3 эпика пакетов v1): арифметика над словарём состояния мира не
    // касается, а спрашивал её и субмир — девять его сайтов читали ЭТОТ статик
    // и тянули за него весь слой в канал. Имя остаётся звонком: у макро-мест
    // оно читается как «просей байт этой решётки».
    static FeatureType decode(std::uint8_t value) { return feature_decode(value); }

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

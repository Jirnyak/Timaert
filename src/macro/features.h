// Per-cell feature byte grid (between biome and landmark).
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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sm {

enum FeatureType : std::uint8_t {
    FT_None = 0, FT_Road = 1, FT_DirtRoad = 2, FT_Field = 3, FT_Bridge = 4,
    // The MINES (owner 2026-08-31, CANON S10 «шахта — фича клетки, как
    // поле»): one feature per DepositKind — «шахты-фичи разных типов…
    // несколько независимых фич — это нормально» (DOD-инкапсуляция). A
    // mining crew arriving at a bare vein spends its first work day
    // BUILDING the kind's mine (ai_gatherer), which consolidates the
    // locally CONNECTED cluster of same-kind veins into this one cell —
    // the cells zero out, the mine holds their sum, «как поле переводит
    // фертильность в зерно». The deposit stock itself stays in the deposit
    // layer AT the mine's cell, so every worksite law reads on unchanged.
    FT_ClayPit = 5, FT_IronMine = 6, FT_Quarry = 7, FT_SilverMine = 8,
    // The WOODEN bridge (owner 2026-08-31): crews span one-cell water gaps
    // on the way to their veins with whatever the home store holds more of
    // — stone lays the road planner's own FT_Bridge, timber lays this. Same
    // water-only law as FT_Bridge; the bed column below prices the
    // difference (a plank deck marches like a dirt lane, not a paved road).
    FT_WoodBridge = 9,
    // КОРАБЛИ — ЧЕРЕЗ ФИЧУ (владелец 2026-09-02, CANON S10): порт — фича
    // берега со СЧЁТЧИКОМ кораблей («у поля урожай, у шахты залежи, у
    // порта корабли»); счётчик живёт в gs.shipsAtCell (v74). Порт строит
    // верфь-работа сквада за дерево; БРОШЕННЫЙ КОРАБЛЬ — та же форма без
    // намерения: пристал к дикому берегу — корабль остаётся фичей («можно
    // много бросить»), вернёшься — уплывёшь.
    FT_Port = 10, FT_BeachedShip = 11,
    FT_Count,
};

// Internal byte-layout invariants for the feature grid. (The legacy TS port is
// reference-only; C++ owns this contract. FT_Tree was byte 2 until the
// tree-count field took over forests; FT_DirtRoad moved 3 → 2. FT_Field is
// the ploughed farmland stamped around villages on fertile ground — the
// grain DEPOSIT of the economy loop, owner-requested man-made feature.
// FT_Bridge is the road's water crossing — the ONLY feature that stands on a
// water cell, and it stands ONLY there (owner, 2026-08-29): a road byte on
// land, a bridge byte on water, never mixed. Every bridge is stone — a dirt
// lane that crosses a river lays the same stone span the highway does — and
// its own byte (rather than "road on water", which the subworld generator
// could already read) is DELIBERATE: the special status is the hook future
// mechanics hang on (tolls, the troll under the bridge, destruction) as
// data against this row, not as a new system.)
static_assert(FT_None == 0, "FeatureType byte layout");
static_assert(FT_Road == 1, "FeatureType byte layout");
static_assert(FT_DirtRoad == 2, "FeatureType byte layout");
static_assert(FT_Field == 3, "FeatureType byte layout");
static_assert(FT_Bridge == 4, "FeatureType byte layout");
static_assert(FT_ClayPit == 5 && FT_IronMine == 6 && FT_Quarry == 7
                  && FT_SilverMine == 8,
              "FeatureType byte layout (mines, v71)");
static_assert(FT_WoodBridge == 9, "FeatureType byte layout (v72)");
static_assert(FT_Port == 10 && FT_BeachedShip == 11,
              "FeatureType byte layout (ships, v74)");

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
};

inline constexpr FeatureDef kFeatureDefs[std::size_t(FT_Count)] = {
    //                     bed   optics  civ
    {FT_None,     0.0f, 1.00f, 0.0f },
    {FT_Road,     1.0f, 0.65f, 0.35f},
    {FT_DirtRoad, 1.5f, 0.85f, 0.22f},
    {FT_Field,    1.8f, 1.00f, 0.0f },
    // The bridge carries the stone road's own columns: its deck IS the paved
    // bed (the march never notices the river under it), it is the same open
    // corridor to light and sight, and it seeds the same civilization pull.
    {FT_Bridge,   1.0f, 0.65f, 0.35f},
    // Mines share the field's columns: worked ground, not an engineered
    // bed (0 = the biome's own footing), nothing to hide behind, and a
    // workplace that is tended, not garrisoned.
    {FT_ClayPit,    0.0f, 1.00f, 0.0f },
    {FT_IronMine,   0.0f, 1.00f, 0.0f },
    {FT_Quarry,     0.0f, 1.00f, 0.0f },
    {FT_SilverMine, 0.0f, 1.00f, 0.0f },
    // Planks march like the dirt lane (bed 1.5 — the same half-again the
    // paved bed dirt pays), carry the bridge's open sight line, and seed
    // the dirt lane's own modest civilization pull.
    {FT_WoodBridge, 1.5f, 0.65f, 0.22f},
    // The harbour is worked shore: a plank apron (dirt-lane bed), open to
    // sight, with the dirt lane's modest civilization pull.
    {FT_Port,        1.5f, 0.85f, 0.22f},
    // A beached hull builds nothing and guards nothing — it just waits.
    {FT_BeachedShip, 0.0f, 1.00f, 0.0f },
};
static_assert(rows_in_enum_order(kFeatureDefs, &FeatureDef::type),
              "kFeatureDefs row order must mirror FeatureType — a new "
              "feature IS its row here");

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
    static int wrap_coord(int value, int limit) { return wrapi(value, limit); }

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
    FeatureType at(int x, int y) const {
        if (width <= 0 || height <= 0 || data.empty()) return FT_None;
        const int wx = wrap_coord(x, width);
        const int wy = wrap_coord(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        if (i >= data.size()) return FT_None;
        return decode(data[i]);
    }
    void set(int x, int y, FeatureType t) {
        if (width <= 0 || height <= 0 || data.empty()) return;
        const int wx = wrap_coord(x, width);
        const int wy = wrap_coord(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        if (i >= data.size()) return;
        data[i] = std::uint8_t(decode(std::uint8_t(t)));
    }
};

} // namespace sm

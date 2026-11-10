// Universal markers (quests, POI, danger, waypoints). Mirrors markers.ts.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "core/caps.h"   // kUnifiedCap — кап списка меток выведен из него

namespace sm {

enum class MarkerStyle : std::uint8_t { Quest = 0, POI, Danger, Waypoint };

// ── МЕТКА — ПЛОСКАЯ ЗАПИСЬ ФИКСИРОВАННОЙ ДЛИНЫ (ломоть 3 сейв-эпика, M-180 /
// M-439, 2026-11-10) ─────────────────────────────────────────────────────
// Здесь стояли два `std::string` (`id`, `label`), а список меток был
// `std::vector<Marker>`: три heap-объявления в состоянии, которое едет в сейв,
// и запись без размера — её нельзя было ни снять одним куском, ни ограничить
// потолком payload иначе как «префикс длины, тело неизвестно». Теперь у записи
// есть РАЗМЕР, и дыр выравнивания в ней нет (байт стиля стоит в первой
// 32-байтной ячейке вместе с ключом), поэтому она едет в сейв целиком.
//
// ШИРИНЫ ВЫВЕДЕНЫ, А НЕ ВЗЯТЫ С ПОТОЛКА:
//   · ключ — 31 байт с завершающим нулём: вместе с байтом стиля это ячейка в
//     32 байта, ширина имени сквада (`SquadName@src/bus/squad_row.h`).
//     Единственный живой производитель ключа — пин игрока `user_X_Y`, при
//     стороне мира до 1024 это не длиннее 14 знаков;
//   · подпись — 64 байта с завершающим нулём: ровно буфер, в который игрок её
//     и раньше набирал на странице карты (`draw_map_screen`), то есть 63
//     знака были потолком ввода всегда — теперь это и потолок хранения.
inline constexpr std::size_t kMarkerIdBytes    = 31u;
inline constexpr std::size_t kMarkerLabelBytes = 64u;
struct Marker {
    MarkerStyle style;
    char id[kMarkerIdBytes];
    char label[kMarkerLabelBytes];
    float x, y;
};
static_assert(sizeof(Marker) == 1u + kMarkerIdBytes + kMarkerLabelBytes
                                    + 2u * sizeof(float),
              "метка: стиль + ключ + подпись + две координаты, без дыр — "
              "запись едет в сейв одним куском");
static_assert(sizeof(Marker) == 104u, "метка весит 104 Б");

// КАП СПИСКА МЕТОК. Число не новое: провод сейва и раньше отказывал списку
// длиннее 16 384 — только отказывал он ПОЗДНО, на записи файла, и игра с
// лишним пином молча переставала сохраняться. Теперь это кап самого списка, и
// отказ случается у двери постановки, вслух. Равенство единому капу
// макромира — вывод, а не совпадение: метка-сигнал мира (задание, место,
// опасность) указывает на субъект, а субъектов в мире не больше
// `kUnifiedCap`; пины игрока делят тот же список. Упёрлось — поднимается
// одним числом (ЗАКОН КАПА): запись 104 Б, список на капе 1.6 МиБ.
inline constexpr std::size_t kMaxMarkers = kUnifiedCap;

// Список меток мира: счёт живых и плоский массив под капом в теле структуры.
// Живые — первые `count`; хвост стоит нулём (его держат двери ниже — снятая
// запись зануляется), так что байты списка не помнят снятых меток. Весит
// 1.6 МиБ и на стеке не живёт — лежит полем состояния мира.
struct MarkerList {
    std::uint32_t count = 0;
    Marker rows[kMaxMarkers] = {};

    std::span<const Marker> live() const { return {rows, count}; }
    std::span<Marker> live() { return {rows, count}; }
    bool empty() const { return count == 0u; }
    bool full() const { return count >= kMaxMarkers; }
};
static_assert(sizeof(MarkerList) == 4u + kMaxMarkers * sizeof(Marker),
              "список меток: счёт + записи под капом, без хвоста");

// Glyph + ARGB colour per style, indexed by enum value. Colours match TS
// MARKER_COLORS (CSS hex -> 0xAARRGGBB). Glyphs mostly match TS
// (markers.ts MARKER_GLYPHS); the quest glyph deliberately diverges from
// TS "?" to "!" — an owner call: a quest pin on the world map reads as an
// exclamation, not a question. NB colours here are 0xAARRGGBB, NOT
// ImGui-packed; ImGui renderers must repack via IM_COL32 (see
// macro_overlay.cpp `marker_imcol`).
inline constexpr const char* kMarkerGlyph[4] = {
    "!",                  // quest    (owner: "!" not TS "?")
    "\xE2\x98\x85",       // poi      ★ U+2605
    "!",                  // danger
    "\xE2\x97\x86",       // waypoint ◆ U+25C6
};
inline constexpr std::uint32_t kMarkerColor[4] = {
    0xFFFFD700u,  // quest    #ffd700 gold
    0xFF87CEEBu,  // poi      #87ceeb sky-blue
    0xFFFF4444u,  // danger   #ff4444 red
    0xFF90EE90u,  // waypoint #90ee90 light-green
};

// Which SURFACE a style lives on. The map is an IMAGE of the world, not the
// world (owner ruling 2026-08-15): the player's own waypoints are ink on the
// chart and never float in the world; quest / POI / danger pins are world
// signals and show on both. One row per style — a new style states its
// surface here, never a branch in a renderer.
inline constexpr std::uint8_t kMarkerOnWorld = 1u;
inline constexpr std::uint8_t kMarkerOnMap   = 2u;
inline constexpr std::uint8_t kMarkerSurface[4] = {
    kMarkerOnWorld | kMarkerOnMap,  // quest
    kMarkerOnWorld | kMarkerOnMap,  // poi
    kMarkerOnWorld | kMarkerOnMap,  // danger
    kMarkerOnMap,                   // waypoint — map annotation only
};

// ── ДВЕРИ СПИСКА ─────────────────────────────────────────────────────────
// Ключ и подпись входят видом на байты и ложатся в запись с завершающим
// нулём. Ключ длиннее ячейки — ОТКАЗ, а не усечение: усечённый ключ совпал бы
// с чужим. Подпись длиннее ячейки усекается — это текст для глаз, не адрес.
inline std::string_view marker_id(const Marker& m) { return {m.id}; }
inline std::string_view marker_label(const Marker& m) { return {m.label}; }

inline void set_marker_label(Marker& m, std::string_view label) {
    const std::size_t n = label.size() < kMarkerLabelBytes - 1u
        ? label.size() : kMarkerLabelBytes - 1u;
    std::memset(m.label, 0, sizeof(m.label));
    std::memcpy(m.label, label.data(), n);
}

inline bool has_marker(const MarkerList& m, std::string_view id) {
    for (const Marker& mk : m.live()) if (marker_id(mk) == id) return true;
    return false;
}

// Поставить метку. Отвечает, ВСТАЛА ли она: нет — если такой ключ уже стоит
// (TS dedups by id), если ключ пуст или не помещается в ячейку, или если
// список полон (`MarkerList::full` — звонящий говорит об этом игроку).
inline bool add_marker(MarkerList& m, std::string_view id, MarkerStyle s,
                       float x, float y, std::string_view label = {}) {
    if (id.empty() || id.size() > kMarkerIdBytes - 1u) return false;
    if (m.full() || has_marker(m, id)) return false;
    Marker& row = m.rows[m.count];
    row = Marker{};
    row.style = s;
    std::memcpy(row.id, id.data(), id.size());
    row.x = x;
    row.y = y;
    set_marker_label(row, label);
    ++m.count;
    return true;
}

// Снять метку по месту в списке. Порядок остальных сохраняется (список пинов
// на странице карты стоит в порядке постановки), освободившаяся хвостовая
// запись зануляется.
inline void remove_marker_at(MarkerList& m, std::size_t index) {
    if (index >= m.count) return;
    // index < count выше — значит живых не меньше одной, и «последняя»
    // существует: счёт уменьшается присваиванием уже проверенного числа.
    const std::size_t last = std::size_t(m.count) - 1u;
    if (index < last) {
        std::memmove(&m.rows[index], &m.rows[index + 1u],
                     (last - index) * sizeof(Marker));
    }
    m.count = std::uint32_t(last);
    std::memset(&m.rows[last], 0, sizeof(Marker));
}

// Remove ONE marker by exact id. Returns whether it existed — the map page's
// double-click uses the answer as its toggle (absent → place, present → lift).
inline bool remove_marker(MarkerList& m, std::string_view id) {
    for (std::size_t i = 0; i < m.count; ++i) {
        if (marker_id(m.rows[i]) == id) {
            remove_marker_at(m, i);
            return true;
        }
    }
    return false;
}

// (`remove_markers_by_prefix` снесена 2026-10-27, M-324: НОЛЬ вызовов в
//  `src/` и в `tests/`, подтверждено компилятором. Снятие метки в игре — одно
//  место и одна дверь, `remove_marker` по ТОЧНОМУ id: двойной клик карты
//  (`map_screen.cpp`) ею же и переключает. Групповое снятие по ПРЕФИКСУ
//  требовало читать id как структурированный ключ — второй, строковый способ
//  адресовать метки рядом с единственным законным, — и ни один звонящий его
//  так и не попросил.)

} // namespace sm

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

// ── МЕТКА — ПЛОСКАЯ ЗАПИСЬ: КЛЕТКА, СТИЛЬ, ПОДПИСЬ (ломти 3–4 сейв-эпика,
// M-180 / M-439, 2026-11-10) ─────────────────────────────────────────────
// Ломоть 3 сделал запись плоской (стояли два `std::string` и вектор), но
// оставил в ней строковый КЛЮЧ и пару float-координат. Ломоть 4 снял оба по
// вердикту владельца («выбери наиболее чистое системное решение»):
//
//   · КЛЮЧА НЕТ. Единственный живой производитель — пин игрока — писал в ключ
//     координаты клетки (`user_X_Y`), то есть второй раз называл то, что уже
//     стояло в записи, а страница карты узнавала «пин игрока» по префиксу
//     ключа, хотя это и есть стиль `Waypoint` (строка `kMarkerSurface` ниже:
//     путевая точка — чернила игрока на чарте). ИДЕНТИЧНОСТЬ МЕТКИ — ПАРА
//     (стиль, клетка): на одной клетке стоит не больше одной метки стиля.
//     Строкового адреса у метки больше нет вовсе (ЗАКОН СЛОВАРЯ);
//   · МЕСТО — ОДНО ЧИСЛО, адрес клетки мира (ЗАКОН АДРЕСА п.2), а не пара
//     float: оба рисовальщика и раньше читали метку как клетку (`floor` и
//     «+0.5» до центра), дробной части не писал никто.
//
// ЗАПИСЬ БЕЗ ДЫР: клетка 4 Б, затем байт стиля и подпись — вместе ячейка в
// 64 Б. Подпись — 63 байта с завершающим нулём; буфер ввода страницы карты
// (`draw_map_screen`) берёт ширину из этой же константы, так что набранное
// ложится в запись целиком.
inline constexpr std::size_t kMarkerLabelBytes = 63u;
struct Marker {
    std::uint32_t cell;
    MarkerStyle style;
    char label[kMarkerLabelBytes];
};
static_assert(sizeof(Marker) == sizeof(std::uint32_t) + 1u + kMarkerLabelBytes,
              "метка: клетка + стиль + подпись, без дыр — запись едет в сейв "
              "одним куском");
static_assert(sizeof(Marker) == 68u, "метка весит 68 Б");

// КАП СПИСКА МЕТОК. Число не новое: провод сейва и раньше отказывал списку
// длиннее 16 384 — только отказывал он ПОЗДНО, на записи файла, и игра с
// лишним пином молча переставала сохраняться. Теперь это кап самого списка, и
// отказ случается у двери постановки, вслух. Равенство единому капу
// макромира — вывод, а не совпадение: метка-сигнал мира (задание, место,
// опасность) указывает на субъект, а субъектов в мире не больше
// `kUnifiedCap`; пины игрока делят тот же список. Упёрлось — поднимается
// одним числом (ЗАКОН КАПА): запись 68 Б, список на капе 1.06 МиБ.
inline constexpr std::size_t kMaxMarkers = kUnifiedCap;

// Список меток мира: счёт живых и плоский массив под капом в теле структуры.
// Живые — первые `count`; хвост стоит нулём (его держат двери ниже — снятая
// запись зануляется), так что байты списка не помнят снятых меток. Весит
// 1.06 МиБ и на стеке не живёт — лежит полем состояния мира.
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
// Подпись входит видом на байты и ложится в запись с завершающим нулём;
// длиннее ячейки — усекается (это текст для глаз, не адрес).
inline std::string_view marker_label(const Marker& m) { return {m.label}; }

inline void set_marker_label(Marker& m, std::string_view label) {
    const std::size_t n = label.size() < kMarkerLabelBytes - 1u
        ? label.size() : kMarkerLabelBytes - 1u;
    std::memset(m.label, 0, sizeof(m.label));
    std::memcpy(m.label, label.data(), n);
}

// Место метки (стиль, клетка) в списке; `count` — такой метки нет.
inline std::size_t find_marker(const MarkerList& m, MarkerStyle s,
                               std::uint32_t cell) {
    for (std::size_t i = 0; i < m.count; ++i)
        if (m.rows[i].cell == cell && m.rows[i].style == s) return i;
    return m.count;
}
inline bool has_marker(const MarkerList& m, MarkerStyle s,
                       std::uint32_t cell) {
    return find_marker(m, s, cell) < m.count;
}

// Поставить метку. Отвечает, ВСТАЛА ли она: нет — если метка этого стиля на
// этой клетке уже стоит или если список полон (`MarkerList::full` — звонящий
// говорит об этом игроку). Клетку судит звонящий: список сторон мира не знает.
inline bool add_marker(MarkerList& m, MarkerStyle s, std::uint32_t cell,
                       std::string_view label = {}) {
    if (m.full() || has_marker(m, s, cell)) return false;
    Marker& row = m.rows[m.count];
    row = Marker{};
    row.cell = cell;
    row.style = s;
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

// Снять метку (стиль, клетка). Отвечает, СТОЯЛА ли она — двойной клик страницы
// карты читает ответ как переключатель (не стояла → поставить, стояла → снять).
inline bool remove_marker(MarkerList& m, MarkerStyle s, std::uint32_t cell) {
    const std::size_t i = find_marker(m, s, cell);
    if (i >= m.count) return false;
    remove_marker_at(m, i);
    return true;
}

// (`remove_markers_by_prefix` снесена 2026-10-27, M-324: ноль вызовов. Со
//  сносом строкового ключа ломтём 4 сейв-эпика группового адреса по префиксу
//  у меток не осталось и в выразимом виде.)

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла: заголовок пользуется контуром
// только в блоке ниже, а вставленная наверху строка сдвинула бы всё содержимое
// и сгноила бы ссылки документов в этот файл (§13 п.5).
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА (M-180, ломоть 4 сейв-эпика) ───────────────────
// Судит КОМПИЛЯТОР, а не ревью: структура состояния мира, в которую заложили
// вектор, строку, карту, умный указатель, функтор или виртуальный метод, отсюда
// НЕ СОБЕРЁТСЯ, и сообщение назовёт тип поимённо (`core/row_law.h`, наряд
// M-169). Новая структура в этом файле обязана появиться и в этом списке — за
// полнотой списка следит `arch_guard_test` (файл стоит в его `kRowHeaders`).
TIMAERT_ROW(sm::Marker);
TIMAERT_ROW(sm::MarkerList);

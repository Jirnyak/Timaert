// 3x3 biome matrix — temperature × moisture. Mirrors src/game/biomes.ts.
#pragma once
#include <cstdint>
#include "core/table_guard.h"
#include <array>

namespace sm {

enum Biome : std::uint8_t {
    Tundra = 0, Taiga = 1, Snow = 2,
    Valley = 3, Meadow = 4, Swamp = 5,
    Desert = 6, Steppe = 7, Tropics = 8,
    Water = 9,
    // Elevation-classified biomes live outside the 3x3 climate matrix, exactly
    // like Water. Mountain is the massif base terrain (see biome_at); trees and
    // roads remain orthogonal *features* composed on top of it.
    Mountain = 10,
};
// Number of biomes produced by the 3x3 temperature x moisture matrix. Water
// (9) and Mountain (10) are elevation overrides outside the matrix, so this
// count is unchanged by their existence.
constexpr int kBiomeLandCount = 9;

struct BiomeDef {
    Biome id;
    const char* name;
    float r, g, b;
};

inline constexpr BiomeDef kBiomes[11] = {
    {Tundra,   "Tundra",   0.50f, 0.52f, 0.45f},
    {Taiga,    "Taiga",    0.22f, 0.38f, 0.28f},
    {Snow,     "Snow",     0.90f, 0.92f, 0.96f},
    {Valley,   "Valley",   0.55f, 0.52f, 0.32f},
    {Meadow,   "Meadow",   0.40f, 0.52f, 0.28f},
    {Swamp,    "Swamp",    0.28f, 0.38f, 0.22f},
    {Desert,   "Desert",   0.82f, 0.72f, 0.48f},
    {Steppe,   "Steppe",   0.68f, 0.60f, 0.32f},
    {Tropics,  "Tropics",  0.10f, 0.35f, 0.10f},
    {Water,    "Water",    0.18f, 0.30f, 0.55f},
    {Mountain, "Mountain", 0.55f, 0.53f, 0.50f},
};
static_assert(sizeof(kBiomes) / sizeof(kBiomes[0]) == std::size_t(Mountain) + 1,
              "kBiomes must have exactly one row per Biome");
static_assert(rows_in_enum_order(kBiomes, &BiomeDef::id),
              "kBiomes row order must mirror Biome");

// Temperature row × moisture col → Biome. Mirrors BIOME_MATRIX +
// `biomeFromClimate` (round-to-nearest of t01 * (rows-1)).
inline Biome biome_from_climate(float temperature01, float moisture01) {
    int row = int(temperature01 * 2.0f + 0.5f); if (row > 2) row = 2; if (row < 0) row = 0;
    int col = int(moisture01    * 2.0f + 0.5f); if (col > 2) col = 2; if (col < 0) col = 0;
    static const Biome kMatrix[3][3] = {
        {Tundra, Taiga,  Snow},
        {Valley, Meadow, Swamp},
        {Desert, Steppe, Tropics},
    };
    return kMatrix[row][col];
}

// Cells at or above this normalized elevation are the Mountain biome (the
// procedural massif), overriding the climate matrix — the elevation parallel to
// how cells below sea level are Water. Trees/roads remain features on top.
//
// ЭТО ДОЛЯ МИРА, А НЕ МАГИЧЕСКОЕ ЧИСЛО, и переставлено оно замером. 0.75 стояло
// при поле, которое ПРИБАВЛЯЛО континентальный сдвиг и кламмилось: 5.64 % суши
// лежало ровно на единице (плоские столы), а порог отрезал 23 % карты. Синтез
// 2026-10-01 стал бескламповым, поле сузилось в тот же отрезок, в который
// старое срезалось, и 0.75 оставляло горам 2 % мира. 0.625 возвращает ИМЕННО
// прежнюю долю: замер `height_census` на пяти сидах даёт квантиль суши ≈q68.5,
// то есть снова ~23 % карты. Число дробится надвое (5/8).
//
// ВОЗДУХ ИЗ НЕГО БОЛЬШЕ НЕ ВЫВОДИТСЯ, и это не потеря связи, а смена субъекта
// (M-192, 2026-10-01). Пока высота была ЛИНЕЙНОЙ, горная линия стояла на 56 %
// пути к вершине, и «полоса от линии до потолка» была честным масштабом
// рельефа. С кривой переноса линия стоит на 8 % пути (813 м против пика
// 10.6 км), а та же полоса стала 11 км — больше самого мира. Воздух теперь
// выведен из ИЗМЕРЕННОГО распределения суши
// (`air_scale_height_m@src/sub/lighting.h`), а не из порога биома.
inline constexpr float kMountainBiomeLevel = 0.625f;

// The single elevation-aware biome classifier: Water below sea level, Mountain
// at/above the massif line, otherwise the climate matrix. This is the CPU
// mirror of the shader's bt_biome (shaders/macro.frag); keep the two in sync.
inline Biome biome_at(float temperature01, float moisture01, float height01,
                      float seaLevel, float mountainLevel) {
    if (height01 < seaLevel)       return Water;
    if (height01 >= mountainLevel) return Mountain;
    return biome_from_climate(temperature01, moisture01);
}

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
TIMAERT_ROW(sm::BiomeDef);

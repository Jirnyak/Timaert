// СВЯЗНОСТЬ ПАРЫ КЛЕТОК СИММЕТРИЧНА — И ЭТО ПЕРЕБОР ВСЕГО СЛОВАРЯ, НЕ ВЫБОРКА.
//
// ЗАКОН, КОТОРЫЙ ОХРАНЯЕТСЯ (владелец 2026-10-26, дословно): «дорога дорога
// частный случай дорога фича» — дорога связывается с ЛЮБОЙ ненулевой фичей, а
// «дорога ↔ дорога» выпадает частным случаем. И главное требование владельца
// там же: «оно должно быть сисметрчным».
//
// ШРАМ (bugs.md Б3, кадры владельца). Полотно рвалось ПОСРЕДИ поля, потому что
// связность спрашивали ДВЕ стороны ПО-РАЗНОМУ: место и поле резали полосу к
// дорожному соседу, а `gen_road` — только к дорожному, то есть к месту и к пашне
// не резал никогда. Одна сторона вела к шву, встречной не было.
//
// ЧТО ИМЕННО УТВЕРЖДАЕТСЯ, И ПОЧЕМУ ИМЕННО ТАК:
//  1. СИММЕТРИЯ по ВСЕМУ квадрату `FT_Count × FT_Count`. Выборка здесь была бы
//     слабее закона: несимметричность — свойство ПАРЫ, и пара, которую не
//     перебрали, есть пара, о которой свидетель молчит.
//  2. ЧАСТНЫЙ СЛУЧАЙ НЕ ПОТЕРЯН: дорога ↔ дорога по-прежнему связана. Если бы
//     обобщение сломало прежнее поведение, это была бы не генерализация, а
//     подмена.
//  3. НУЛЬ НЕ СВЯЗЫВАЕТСЯ НИ С ЧЕМ — иначе дорога тянулась бы в пустоту.
//  4. ПАРА БЕЗ ДОРОГИ НЕ СВЯЗАНА (место ↔ место, пашня ↔ пашня): ровно то
//     требование владельца, что «если дорог вокруг нет то и не будет тянуться
//     дороги». Это утверждение об ОТСУТСТВИИ, и у него свой перебор.
//  5. ДОРОГА СВЯЗАНА С КАЖДОЙ ненулевой фичей словаря — включая те, которых
//     сегодня ещё нет: перебор идёт по `FT_Count`, поэтому новая строка
//     каталога попадает под закон САМА, без правки свидетеля.
//
// Свидетель НЕ утверждает геометрию (куда именно легла полоса) — это дело
// генератора и кадра; он утверждает ЗАКОН ПАРЫ, который генераторы обязаны
// спрашивать одной дверью.

#include "check.h"

#include "macro/features.h"
#include "sub/gens/kit/streets.h"

#include <cstdint>

using sm::FeatureType;
using sm::FT_Count;
using sm::FT_None;
using sm::FT_Road;
using sm::FT_DirtRoad;
using sm::FT_Bridge;
using sm::FT_City;
using sm::FT_Field;
using sm::sub::kit::features_link;
using sm::sub::kit::is_road_feature;

int main() {
    // 1. СИММЕТРИЯ — весь квадрат словаря.
    int asym = 0, pairs = 0;
    for (int a = 0; a < int(FT_Count); ++a) {
        for (int b = 0; b < int(FT_Count); ++b) {
            ++pairs;
            if (features_link(std::uint8_t(a), std::uint8_t(b))
                != features_link(std::uint8_t(b), std::uint8_t(a)))
                ++asym;
        }
    }
    CHECK(pairs == int(FT_Count) * int(FT_Count),
          "перебор обязан покрыть ВЕСЬ квадрат словаря фич");
    CHECK(asym == 0,
          "связность есть свойство ПАРЫ: link(a,b) обязан равняться link(b,a)");

    // 2. Частный случай не потерян: дорога ↔ дорога.
    CHECK(features_link(std::uint8_t(FT_Road), std::uint8_t(FT_Road)),
          "дорога ↔ дорога — частный случай, он обязан остаться связным");
    CHECK(features_link(std::uint8_t(FT_Road), std::uint8_t(FT_DirtRoad)),
          "просёлок с дорогой связан");
    CHECK(features_link(std::uint8_t(FT_Bridge), std::uint8_t(FT_Road)),
          "мост есть дорога для связности (берега целятся в него)");

    // 3. Ноль не связывается ни с чем — иначе дорога тянется в пустоту.
    int zeroLinks = 0;
    for (int a = 0; a < int(FT_Count); ++a)
        if (features_link(std::uint8_t(FT_None), std::uint8_t(a))) ++zeroLinks;
    CHECK(zeroLinks == 0,
          "пустая клетка не связывается ни с одной фичей");

    // 4. Пара БЕЗ дороги не связана — «нет дорог вокруг, дороги не тянутся».
    int roadlessLinked = 0, roadlessPairs = 0;
    for (int a = 0; a < int(FT_Count); ++a) {
        if (a == int(FT_None) || is_road_feature(std::uint8_t(a))) continue;
        for (int b = 0; b < int(FT_Count); ++b) {
            if (b == int(FT_None) || is_road_feature(std::uint8_t(b))) continue;
            ++roadlessPairs;
            if (features_link(std::uint8_t(a), std::uint8_t(b))) ++roadlessLinked;
        }
    }
    CHECK(roadlessPairs > 0,
          "бездорожных пар обязано быть много — иначе перебор смотрит не туда");
    CHECK(roadlessLinked == 0,
          "пара без дороги не связана: дороги не тянутся там, где их нет");

    // 5. Дорога связана с КАЖДОЙ ненулевой фичей словаря, включая будущие.
    int reached = 0, nonZero = 0;
    for (int b = 0; b < int(FT_Count); ++b) {
        if (b == int(FT_None)) continue;
        ++nonZero;
        if (features_link(std::uint8_t(FT_Road), std::uint8_t(b))) ++reached;
    }
    CHECK(nonZero > 0, "словарь фич обязан быть непустым");
    CHECK(reached == nonZero,
          "дорога связывается с ЛЮБОЙ ненулевой фичей — новая строка каталога "
          "попадает под закон сама, без правки свидетеля");

    // 6. ПОВЕДЕНЧЕСКАЯ ПОЛОВИНА: набор направлений, которым пользуются ОБЕ
    // стороны, обязан увидеть место с дорожной стороны — ровно та половина,
    // которой не было и из-за которой рвалось полотно. Сосед кладётся на восток.
    {
        std::uint8_t nb[9]{};
        for (int i = 0; i < 9; ++i) nb[i] = std::uint8_t(FT_None);
        nb[1 * 3 + 2] = std::uint8_t(FT_City);          // восток — город
        const auto dirs = sm::sub::kit::connected_road_dirs(
            std::uint8_t(FT_Road), nb);
        bool east = false;
        for (int i = 0; i < dirs.count; ++i)
            if (dirs.dx[std::size_t(i)] == 1 && dirs.dy[std::size_t(i)] == 0)
                east = true;
        CHECK(east,
              "дорожная клетка обязана видеть МЕСТО соседом: без этого полосу "
              "режет только сторона места, и полотно рвётся посреди поля");

        // И обратное: пашня рядом с пашней связи не даёт (нет дороги в паре).
        std::uint8_t nb2[9]{};
        for (int i = 0; i < 9; ++i) nb2[i] = std::uint8_t(FT_None);
        nb2[1 * 3 + 2] = std::uint8_t(FT_Field);
        const auto dirs2 = sm::sub::kit::connected_road_dirs(
            std::uint8_t(FT_Field), nb2);
        CHECK(dirs2.count == 0,
              "пашня ↔ пашня связи не даёт: дороги не тянутся там, где их нет");
    }

    std::printf("road_link: пар в словаре %d · бездорожных пар %d · "
                "ненулевых фич %d\n", pairs, roadlessPairs, nonZero);
    return sm::test::report("road_link_symmetry_test");
}

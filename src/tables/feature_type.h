// FeatureType — словарь рукотворных фич клетки (дороги, мосты, пашни,
// шахты, парцеллы, фичи поселений). ПЕРЕЕХАЛ из macro/features.h ломтём 3
// эпика фрейма (M-171): строка клетки локального фрейма несёт байт фичи, а
// нейтральный слой словарей — tables/ (M-181, граница ТИП против
// ЭКЗЕМПЛЯРА). Слой фич — изменяемое состояние — остался в macro/features.h.
#pragma once
#include <cstdint>

namespace sm {

enum FeatureType : std::uint8_t {
    FT_None = 0, FT_Road = 1, FT_DirtRoad = 2, FT_Field = 3, FT_Bridge = 4,
    // The MINES (owner 2026-08-31, CANON S10 «шахта — фича клетки, как
    // поле»): one feature per DepositKind — «шахты-фичи разных типов…
    // несколько независимых фич — это нормально» (DOD-инкапсуляция). Фичу
    // шахты ставит МИР при генерации (вердикт владельца 2026-09-22: артели
    // не строят ничего), и она конденсирует связный кластер жил своего рода.
    FT_ClayPit = 5, FT_IronMine = 6, FT_Quarry = 7, FT_SilverMine = 8,
    FT_CopperMine = 9, FT_GoldMine = 10,
    // ЛОШАДЬ — ЮНИТ (CANON S10, 2026-09-19): огороженная парцелла, работающая
    // ряд ТАБУНА — точный близнец пашни, засеянный конями. Владелец
    // 2026-09-22: «пастбище лошадей / овечник / поле пшеницы — единая
    // система», и различие между ними живёт одной колонкой строки цели.
    FT_Pasture = 11,
    // ЛЬНЯНОЕ ПОЛЕ (владелец, 2026-09-20): та же плодородная земля, своя
    // культура. Нового ресурсного ряда нет: клетка под льном — это клетка,
    // НЕ занятая хлебом, и конкуренция за землю честна.
    FT_FlaxField = 12,
    // ── ФИЧИ ПОСЕЛЕНИЙ (владелец 2026-09-30, дословно: «добавим просто
    // такие строки фич ОНИ ЕСТЕТСВЕННО НУЖНЫ оч просто это чистая же дата»;
    // ЗАКОН ГЕНЕРАЦИИ п.6 — порядок: мир → поля → ФИЧИ (столица → города →
    // деревни → дороги и мосты) → сквады этих фич). «Что стоит на клетке»
    // отвечает ЭТОТ байт; «кто здесь живёт» — неподвижный сквад (мест как
    // рода не существует). Штампует генерация ПОСЛЕ дорог одним проходом
    // (stamp_settlement_features@src/macro/place_iter.h); байт свежести —
    // тот же закон, что у LandmarkGrid. Четыре строки — ровно рода, которые
    // мир сегодня ставит; логово/святилище/башня добавятся строкой в день,
    // когда их начнёт ставить генерация.
    FT_City = 13, FT_Village = 14, FT_Spire = 15, FT_Ruin = 16,
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
                  && FT_SilverMine == 8 && FT_CopperMine == 9
                  && FT_GoldMine == 10,
              "FeatureType byte layout (mines)");
static_assert(FT_Pasture == 11 && FT_FlaxField == 12,
              "FeatureType byte layout (parcels)");

} // namespace sm

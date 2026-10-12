// Subworld save/regen cache — port of subworld/map-factory.ts.
// In-memory snapshot store keyed by (seed, mode); on revisit, the saved
// STRUCTURES are merged into a freshly generated map so player-induced
// changes (felled trees, abandoned houses) persist. Рельеф снимок НЕ несёт —
// и шапка обещала обратное до 2026-10-12 (M-262, разбор у структуры ниже).
//
// Diff semantics by structure Kind, mirroring the TS factory:
//   saved.count <= fresh.count → keep saved positions, append the
//                                additional fresh structures.
//   saved.count >  fresh.count → keep matching prefix, mark the surplus
//                                as Withered (Tree) or Abandoned (other).
#pragma once
#include <cstdint>
#include <cmath>
#include <memory>
#include "sub/map_data.h"

namespace sm::sub {

// ── ЧТО СНИМОК НЕСЁТ, А ЧЕГО НЕ НЕСЁТ (M-262, 2026-10-12) ─────────────────
// Здесь стояла КОЛОНКА РЕЛЬЕФА — `std::vector<std::uint16_t> heightmap`, —
// и она была ЛОССИ-КОПИЕЙ ДЕТЕРМИНИРОВАННОЙ ФУНКЦИИ, то есть вторым ответом
// на вопрос «какой здесь рельеф» (DOD п.6). Снесена по трём замерам:
//   · ЦЕНА: 1024² клетка × u16 = РОВНО 2 МиБ на КАЖДУЮ клетку, уходящую из
//     окна, и кэш не вытесняет ничего — он растёт на всю сессию;
//   · ИНФОРМАЦИИ НОЛЬ: игра рельеф сцены не мутирует вовсе. Единственное
//     место, которое правит содержимое клетки, говорит это о себе САМО —
//     `fell_prop_near@src/sub/seamless_manager.cpp`: «Structures changed +
//     one cell's material byte changed; heights did not». Регенерация из того
//     же сида даёт тот же рельеф по построению;
//   · ХУЖЕ ТОГО, СНИМОК ВРЁТ: квант был 2.5/65535 ≈ 3.8e-5 единицы поля, то
//     есть клетка, в которую игрок ВЕРНУЛСЯ, получала рельеф, чуть ОТЛИЧНЫЙ
//     от свежесгенерированного. Кэш делал мир менее точным, чем его отсутствие.
// Вместе с колонкой ушли квантователь, обратное преобразование и константа
// `kHmaxQuant`, которая стояла В ДВУХ МЕСТАХ сразу (ЗАКОН КОНСТАНТ).
//
// ОСТАЁТСЯ ровно то, что игра ДЕЙСТВИТЕЛЬНО меняет: список пропов (срубленное
// снято дверью выше). Долг самого кэша при этом НЕ закрыт и назван вслух в
// M-262: он живёт функциональной статикой с мьютексом (§12 «никакого
// глобального состояния»), и его место — колонка сцены.
struct SavedSubworld {
    std::uint32_t seed = 0;
    SubworldMode  mode = SubworldMode::Open;
    std::vector<Structure>     structures;
};

// Compact "state" tag overlaid on Structure::height sign bit since
// Structure has no state field yet. Negative height => abandoned/withered.
inline bool structure_is_decayed(const Structure& s) { return s.height < 0.0f; }
inline void mark_structure_decayed(Structure& s)     { s.height = -std::fabs(s.height); }

// Snapshot the current subworld for later restoration.
SavedSubworld snapshot_subworld(std::uint32_t seed, SubworldMode mode,
                                const SubworldMapData& src);

// Merge a saved snapshot into a freshly generated map: structures are diffed
// by Kind as documented above. РЕЛЬЕФ НЕ ВОССТАНАВЛИВАЕТСЯ И НЕ ДОЛЖЕН —
// свежая генерация из того же сида и есть он сам (см. колонку выше).
void restore_into(const SavedSubworld& saved, SubworldMapData& fresh);

// Per-session in-memory cache — never serialized: the subworld is a
// PROJECTION of the macro truth (CANON S21), and what must outlive a scene
// is written back up to the macro layer, not snapshotted down here.
void store_saved_subworld(const SavedSubworld& s);
void store_saved_subworld(SavedSubworld&& s);
std::shared_ptr<const SavedSubworld> find_saved_subworld_ref(std::uint32_t seed,
                                                             SubworldMode mode);
const SavedSubworld* find_saved_subworld(std::uint32_t seed, SubworldMode mode);
void clear_saved_subworlds();

} // namespace sm::sub

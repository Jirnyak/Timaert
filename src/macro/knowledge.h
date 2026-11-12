// The player's knowledge of the macro map — the fog-of-war layer.
//
// One byte per cell, three states:
//   0  Unknown   terra incognita — the world draws BLACK, nothing overlays.
//   1  Explored  memory — grey relief and roads, faded places, markers.
//   2  Visible   in sight right now — the full living picture.
//
// Only the EXPLORED bit outlives a session: "what I see now" is a projection of
// where the player stands, recomputed by update_player_sight whenever their
// cell changes. The layer rides the save as it lies (a direct dump, M-439), so
// the load door dims Visible back to Explored (`knowledge_dim_sight` below) and
// the first sweep from the restored position re-opens its own.
//
// SIGHT IS LIGHT. The sweep is the same bounded terrain-optical Dijkstra the
// night-glow bake runs (macro/optics.h optical_sweep): open land carries the
// eye far, a road corridor further, canopy smothers it per unit of tree
// density, and a ridge walls it off while a hilltop overlooks the valley
// below. One physics, one tuning — a cell a town's glow cannot reach is a
// cell an eye standing in that town cannot see.
//
// The sight BUDGET (in cells of open ground) is the caller's: the app derives
// it from the standing eye's horizon and will later scale it by attributes
// and skills — this layer only spends whatever budget it is handed.
//
// (Здесь жила вторая дверь, `reveal_area` — «открыть карту событием», с нулём
// звонящих в игре за всю свою жизнь. Снесена вердиктом владельца 2026-11-12:
// дверь будущей системы без читателя есть спящий закон. Первое событие,
// открывающее карту, придёт своей строкой и назовёт клетку индексом.)
//
// L1 macro layer, TreeLayer's shape: grid + revision (renderer refresh gate).
// GameState owns one; the renderer samples it as R8
// (binding 5) once Increment 2 lands.
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "bus/squad_row.h"  // ecs::kNoCell — «нет клетки», одно имя на игру
#include "core/caps.h"     // kWorldCellCap — кап клеток мира, один на слои
#include "macro/optics.h"

namespace sm {

enum : std::uint8_t {
    kKnowledgeUnknown  = 0,
    kKnowledgeExplored = 1,
    kKnowledgeVisible  = 2,
};

// ПЛОСКИЙ МАССИВ ПОД КАПОМ КЛЕТОК МИРА (M-180, 2026-11-10): heap-члена у слоя
// нет. Здесь стоял `std::vector<std::uint8_t>`, и «построен ли слой» отвечала
// ДЛИНА ВЕКТОРА — второй носитель размера мира рядом с `width/height`. Теперь
// носитель один: слой жив, когда его стороны называют мир в пределах капа.
// Живые клетки — первые `cell_count()`; хвост до капа стоит нулём (Unknown),
// его чистит `knowledge_reset`. Байты слоя снимаются одним куском.
struct KnowledgeLayer {
    int width = 0, height = 0;
    // Dirty counter — bumped on every mutation so the renderer knows to
    // refresh its field texture. Не величина мира, но счётчик едет в файле вместе со слоем (сейв есть прямой дамп, M-439), поэтому кэш, сверяющий себя с ним, обязан быть назван протухшим той дверью, что заменяет слой целиком — загрузкой.
    std::uint32_t revision = 0;
    std::uint8_t data[kWorldCellCap] = {};

    std::size_t cell_count() const {
        return world_cells_within_cap(width, height);
    }
    bool has_complete_storage() const { return cell_count() > 0u; }
    // Живые клетки слоя — ровно мир, не кап. Обход «по всем клеткам» идёт
    // отсюда: хвост за миром не клетки, и читать его как клетки нельзя.
    std::span<const std::uint8_t> cells() const { return {data, cell_count()}; }
    std::span<std::uint8_t> cells() { return {data, cell_count()}; }
    // THE door every consumer asks — overlays, NPC draw filters, the map
    // screen. Клетку называют ОДНИМ ЧИСЛОМ (ЗАКОН АДРЕСА п.2, M-462 ломоть
    // 4); экран сворачивает свою пару дверью `cell_of`. An absent grid
    // answers Unknown (fail closed: a world with no knowledge layer shows
    // nothing, not everything).
    std::uint8_t at(std::uint32_t cell) const {
        return cell < cell_count() ? data[cell] : kKnowledgeUnknown;
    }
};
static_assert(sizeof(KnowledgeLayer) == 12u + kWorldCellCap,
              "слой знания: 12 Б шапки + байт на клетку капа, без хвоста");

// Session half of the player's sight: which cells are Visible right now (so
// a move can decay exactly those to Explored), the Dijkstra scratch, and the
// cell the last sweep ran from. App-owned, reset with the world — never saved.
struct SightRuntime {
    OpticalScratch scratch;
    std::vector<std::uint32_t> visibleCells;
    // Клетка, с которой смотрел прошлый обзор, — ОДНИМ числом (ЗАКОН АДРЕСА
    // п.2); `ecs::kNoCell` = обзора ещё не было, и первый же вопрос его даст.
    std::uint32_t lastCell = ecs::kNoCell;
};

// All-Unknown grid of the given dims — a new world starts dark.
// Чистится ВЕСЬ кап, а не только клетки нового мира: хвост за миром обязан
// стоять нулём, иначе байты слоя помнили бы прошлый мир (M-180). Мир вне капа
// рождает слой без сторон — fail-closed. Тело в заголовке: дверь зовут
// приборы, которым оптика этого слоя (и её .cpp) не нужна.
inline void knowledge_reset(KnowledgeLayer& k, int width, int height) {
    static_assert(kKnowledgeUnknown == 0, "нулевой байт слоя — Unknown");
    const bool fits = world_cells_within_cap(width, height) > 0u;
    k.width = fits ? width : 0;
    k.height = fits ? height : 0;
    std::memset(k.data, 0, sizeof(k.data));
    ++k.revision;
}

// ПОГАСИТЬ «ВИДНО СЕЙЧАС» В «РАЗВЕДАНО» ПО ВСЕМУ СЛОЮ — ДВЕРЬ ЗАГРУЗКИ. Что
// видно сейчас, есть проекция места, где стоит игрок, а список этих клеток
// живёт в сессии (`SightRuntime` ниже) и с миром не сохраняется. Слой же едет
// прямым дампом как есть — с клетками, видимыми на момент записи. После
// загрузки список пуст, и гасить их при уходе игрока было бы нечем: они
// остались бы «видимыми» навсегда. Дверь возвращает слою то, что в нём
// переживает сессию по праву, — память о разведанном; первый же обзор от
// загруженного места откроет своё заново. Счётчик правок двигается.
inline void knowledge_dim_sight(KnowledgeLayer& k) {
    for (std::uint8_t& c : k.cells())
        if (c == kKnowledgeVisible) c = kKnowledgeExplored;
    ++k.revision;
}

// Recompute the player's sight if their cell changed since the last call:
// previous Visible cells decay to Explored, then the optical sweep from the
// player's cell marks the new Visible set (Explored underneath forever).
// Returns true when the layer changed (revision bumped) — the renderer's cue
// to refresh. One integer compare when the cell is unchanged, so calling it
// every frame costs nothing between crossings.
//
// ГЛАЗ СТОИТ В КЛЕТКЕ, И ВХОД — ЕЁ ИНДЕКС (M-462). Здесь стояла пара флоатов:
// звонящий разворачивал индекс клетки флажка в `float x, y`, а дверь тут же
// резала их `floor` обратно — круг через дробную точку, которой у мира нет
// (позиция мира есть клетка, внутри неё у мира позиции нет вовсе).
bool update_player_sight(KnowledgeLayer& k, SightRuntime& rt,
                         const OpticalWorld& world,
                         std::uint32_t playerCell, float budgetCells);

} // namespace sm

#include "core/row_law.h"   // стена «без дыр» — включение в хвосте, содержимое файла не сдвигается
// ── СТЕНА «БЕЗ ДЫР» (`TIMAERT_PACKED@src/core/row_law.h`, сейв-эпик, В2) ─────
// Каждый байт этих строк принадлежит названному члену: они лежат в кусках
// памяти, которые снимает прямой дамп (CRAFT §10). Новый член — новая строка
// списка; дыра выравнивания или забытый член не собираются.
TIMAERT_PACKED(sm::KnowledgeLayer, &sm::KnowledgeLayer::width,
    &sm::KnowledgeLayer::height, &sm::KnowledgeLayer::revision,
    &sm::KnowledgeLayer::data);

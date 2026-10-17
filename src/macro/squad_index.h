// КАРКАС КЛЕТОК ПО СКВАДАМ — ЕДИНСТВЕННЫЙ ОТВЕТ МИРА НА «КТО НА ЭТОЙ КЛЕТКЕ»
// (ЗАКОН КЛЕТОЧНОГО КАРКАСА п.2; вердикт владельца 2026-10-01, M-90 шаг 5).
//
// Родился скрэтчем одного драйва («кто рядом» для шага угрозы) в npc_ai.h.
// Стал носителем МИРА (ломтик F), потому что после флипа мест в сквады вопрос
// «кто здесь живёт» задаётся той же популяции: место есть неподвижный сквад,
// и второй сетки под него не заводится. `LandmarkGrid` отвечала ровно это по
// своему, отдельному списку — носителей было два, остался один. Свой
// заголовок, а не npc_ai.h: ответ мира о клетке не смеет тянуть за собой
// шапку поведения (ecs/world.h, pathfinding, spawners) в каждого читателя.
//
// КАПА НА КЛЕТКУ У НЕГО НЕТ, И ЭТО НЕ НЕДОСМОТР (вердикт владельца
// 2026-10-01: «я ХОЧУ без капа»). Преаллокации кап на клетку не нужен:
// полезная нагрузка counting-sort размером с ГЛОБАЛЬНЫЙ кап популяции
// (`kUnifiedCap`), и сумма по клеткам переполнить её не может по
// построению — одна клетка вправе держать хоть всех.
//
// И ЗАМЕР ГОВОРИТ, ЧТО КАП БЫЛ БЫ СТЕНОЙ, А НЕ ЗАПАСОМ (зонд M-90 шаг 5,
// снесён после ответа; три мира, день 48, 13.7-14.4 тыс. тел на 1024²):
// занята 1 % клеток, в 81 % занятых стоит РОВНО ОДИН сквад, но максимум —
// **54 на клетку**, и он одинаков на всех трёх сидах, потому что это не
// хвост распределения, а СТОЛИЦЫ: клеток с одиннадцатью и более ровно
// десять, их число задано фракциями, их крю — строкой реестра. Кап «с
// запасом над десятью» отказал бы десяти клеткам мира, а кап «64» стоял бы
// впритык к структурному числу и пробился бы первым же ростом столицы.
// Худший бакет 8×8 при этом 63-68 записей — скан девяти кеш-линий на самой
// плотной клетке мира, то есть сторону бакета замер не двигает.
//
// ВЫВОДИМЫЙ, А НЕ ПОДДЕРЖИВАЕМЫЙ. Он пересобирается из `MacroStore` целиком,
// поэтому соврать дольше одной пересборки не умеет. Цепь через колонку
// сквада (`cellHead` + `nextInCell`) была бы дешевле по тику и свежее, но
// она ПОДДЕРЖИВАЕТСЯ записью на каждом ходе: испортившись однажды (сквад в
// двух цепях, висячая ссылка), она остаётся испорченной молча. Все шрамы
// этого проекта одного рода — «два писателя» и «устаревшая копия», — и
// выводимое бьёт поддерживаемое. Это и есть цена, которую мы платим
// пересборкой. Свежесть — закон сетки мест, унаследованный: генезис,
// перепёк загрузки (rebake_world) и каждый драйв АИ зовут build_squad_index.
#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "macro/squad_walk.h"      // SquadWalkEntry + порядок закона; store.h
#include "tables/squad_type.h"     // ось рода + kLandmarkYieldOrder

namespace sm {

// ── THE flat bucket grid ─────────────────────────────────────────────────
//
// Prefix sums plus one sorted item array — a counting sort, the shape
// `sub::UnitGrid` already uses for the same job in the battle. It replaces a
// `vector<vector<T>>`, which is the DOD defect CANON S26 names: a heap
// container PER CELL, so a 128×128 grid was sixteen thousand vector headers
// with sixteen thousand possible allocations, rebuilt from scratch at the top
// of every AI sweep.
//
// Items are u32 because both users address by one: a tree grid stores indices
// into the tree array, a squad grid stores entity bits. Two passes and, after
// the first build, ZERO allocations — the scatter cursors are a member for
// exactly that reason.
struct CellBuckets {
    int cellSize = 8;
    int cols = 0;
    int rows = 0;
    std::vector<std::uint32_t> begin;    // cols*rows + 1 prefix sums
    std::vector<std::uint32_t> items;    // bucket-sorted payload
    std::vector<std::uint32_t> cursor;   // scatter cursors; members = no churn

    std::size_t cell_of(int gx, int gy) const {
        return std::size_t(gy) * std::size_t(cols) + std::size_t(gx);
    }
    const std::uint32_t* cell_begin(int gx, int gy) const {
        return items.data() + begin[cell_of(gx, gy)];
    }
    const std::uint32_t* cell_end(int gx, int gy) const {
        return items.data() + begin[cell_of(gx, gy) + 1];
    }
};

// Size the grid and clear the counts. Call, then `bucket_count` once per item,
// then `bucket_prefix`, then `bucket_scatter` once per item — the counting
// sort's three steps, spelled out so a caller cannot do them out of order
// without noticing.
void bucket_reset(CellBuckets& g, int mapW, int mapH, int cellSize);
void bucket_count(CellBuckets& g, int gx, int gy);
void bucket_prefix(CellBuckets& g, std::size_t itemCount);
void bucket_scatter(CellBuckets& g, int gx, int gy, std::uint32_t item);

struct SquadIndex {
    CellBuckets grid;
    // Порядок закона (macro/squad_walk.h): скаттер идёт по ординалу, поэтому
    // содержимое бакетов не зависит от внутренностей EnTT. Член — чтобы
    // пересборка на каждый свип не аллоцировала (тот же довод, что cursor).
    std::vector<SquadWalkEntry> order;
};

// Читает ТОЛЬКО store: `ecs::World&` стоял здесь, чтобы достать из него
// `store_of(w)`, — то есть просил целый реестр ради одного поля (M-150).
void build_squad_index(SquadIndex& g, const MacroStore& st, int mapW,
                       int mapH, int cellSize = 8);

// ── «КТО В РАМКЕ (2r+1)² ВОКРУГ КЛЕТКИ» — КАНДИДАТЫ КАРКАСОМ ────────────
//
// Всякий локальный вопрос о сквадах отвечается ОКРЕСТНОСТЬЮ клетки, никогда
// сканом капа (ЗАКОН КЛЕТОЧНОГО КАРКАСА п.1). Кольцо бакетов, покрывающее
// рамку, затем точный отбор по КОРОТКОЙ клеточной дельте (заворот — маска
// тора); колбэк получает слот и дельту (dx, dy) ∈ [−r, r]². Порядок — бакет
// за бакетом, внутри бакета по ординалу (counting-sort). Fail-closed:
// несобранный каркас не зовёт колбэк ни разу (как settlement_at ниже).
// Цена — O(кольца), капонезависимая (§5 п.13: заголовку можно).
template <typename Fn>
inline void squads_in_frame(const SquadIndex& g, const MacroStore& st,
                            int cx, int cy, int radiusCells, Fn&& fn) {
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0 || b.begin.empty()) return;
    const int mapW = b.cols * b.cellSize;
    const int wx = cx & (mapW - 1);
    const int wy = cy & (mapW - 1);   // тор квадратен: одна маска у осей
    // floor-деление, не трункация: у западного/северного края рамка уходит в
    // минус, и (−2)/8 == 0 потерял бы завёрнутое кольцо бакетов (шрам ломтя
    // 6: ровно так ходило кольцо сборщика фрейма).
    const auto fdiv = [](int a, int b) {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    };
    const int g0x = fdiv(wx - radiusCells, b.cellSize);
    const int g1x = fdiv(wx + radiusCells, b.cellSize);
    const int g0y = fdiv(wy - radiusCells, b.cellSize);
    const int g1y = fdiv(wy + radiusCells, b.cellSize);
    const int half = mapW / 2;
    for (int gy = g0y; gy <= g1y; ++gy) {
        for (int gx = g0x; gx <= g1x; ++gx) {
            const int bx = ((gx % b.cols) + b.cols) % b.cols;
            const int by = ((gy % b.rows) + b.rows) % b.rows;
            for (const std::uint32_t* it = b.cell_begin(bx, by),
                                    * end = b.cell_end(bx, by);
                 it != end; ++it) {
                const std::uint16_t slot = g.order[*it].slot;
                const auto& c = st.cell[slot];
                int dx = (ecs::cell_x(c, mapW) - wx) & (mapW - 1);
                int dy = (ecs::cell_y(c, mapW) - wy) & (mapW - 1);
                if (dx > half) dx = dx - mapW;
                if (dy > half) dy = dy - mapW;
                if (dx < -radiusCells || dx > radiusCells
                    || dy < -radiusCells || dy > radiusCells) {
                    continue;
                }
                fn(slot, dx, dy);
            }
        }
    }
}

// ── «КТО ЗДЕСЬ ЖИВЁТ» — ТЕЛО НЕПОДВИЖНОГО СКВАДА КЛЕТКИ ─────────────────
//
// Ответ, который давала запечённая сетка мест (`LandmarkGrid::at`, умерла
// ломтиком F): бакет каркаса → фильтр по клетке и ОСИ РОДА. Приоритет
// спорной клетки — kLandmarkYieldOrder (один закон, живёт у оси); внутри
// рода побеждает меньший ординал, и это не новое правило: бакет отсортирован
// по ординалу по построению, а старая сетка отдавала клетку первой строке
// append-only вектора — тот же порядок рождения.
//
// Fail-closed: несобранный каркас (пустая сетка) отвечает «никто» — пустой
// хэндл, ровно как несобранная LandmarkGrid отвечала ординалом 0.
inline MacroHandle settlement_at(const SquadIndex& g, const MacroStore& st,
                                 int x, int y) {
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0 || b.begin.empty()) return MacroHandle{};
    const int mapW = b.cols * b.cellSize;
    const int mapH = b.rows * b.cellSize;
    // Заворот мира — МАСКА (ЗАКОН АДРЕСА: сторона — степень двойки, у обеих
    // осей одна арифметика; деление по рантайм-делителю — дефект ревью).
    const int wx = x & (mapW - 1);
    const int wy = y & (mapH - 1);
    const int gx = wx / b.cellSize;
    const int gy = wy / b.cellSize;
    MacroHandle best{};
    std::size_t bestRank = std::size(kLandmarkYieldOrder);
    for (const std::uint32_t* it = b.cell_begin(gx, gy),
                            * end = b.cell_end(gx, gy);
         it != end; ++it) {
        const std::uint16_t slot = g.order[*it].slot;
        const auto& c = st.cell[slot];
        if (ecs::cell_x(c, mapW) != wx || ecs::cell_y(c, mapW) != wy)
            continue;
        const SquadType t = SquadType(st.runtime[slot].squadType);
        if (squad_archetype(t) != SquadArchetype::Place) continue;
        for (std::size_t r = 0; r < bestRank; ++r) {
            if (kLandmarkYieldOrder[r] != t) continue;
            bestRank = r;
            best = handle_at(st, slot);
            break;   // внутри рода первый = меньший ординал (порядок бакета)
        }
        if (bestRank == 0) break;   // город — высший приоритет, искать нечего
    }
    return best;
}

} // namespace sm

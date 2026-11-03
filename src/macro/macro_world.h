// THE envelope of world layers (CANON S6, grown 2026-08-24). Everything a
// consumer may need in order to ask "what is this cell / what stands here". It
// grows by a FIELD when a new system needs one — never by a new argument at a
// call site. That law is the whole point: before the door, enter(), the two AI
// drivers and a dozen call sites each carried their own parallel list of layer
// arguments, three of them forgot `deposits`, and the deposit rows refused
// silently (canon-audit C4). One envelope, assembled ONCE per owner (the app
// for the live game, a test for its fixture), handed everywhere by reference.
//
// Every pointer is optional and fail-closed: a null layer reads as "that
// system contributes nothing here" — the zero contribution of S6, expressed by
// data, not by a second code path.
//
// Deliberately ecs-free and POD: the lightest consumers (hand-curated test
// executables, pure-data routers like fauna) take the envelope without
// dragging entt or GameState into their translation units.
#pragma once
#include <cstdint>
#include "core/macro_handle.h"   // MacroHandle {слот, ID} — нищий заголовок

// A fact the macro layer produces about a fight it resolved. One kind so far;
// it grows like every other row-shaped thing here. `victim` and `killer` are
// store handles {slot, id} CARRIED DIRECTLY (M-220: the 32-bit packing died
// with the generation column); an empty MacroHandle{} (id 0 — «никто», закон
// нуля-ординала) = a creature row, which has no slot of its own — it is named
// by its record id in `detail`. The app raises this as the ordinary NpcDeath
// the whole story layer already listens to: an auto-resolved death and a
// fought death are the SAME fact, which is exactly the point (CANON S13: one
// law of battle at both scales).
namespace sm {

struct BattleFact {
    enum class Kind : std::uint8_t { Death = 0 };
    Kind          kind    = Kind::Death;
    std::uint16_t npcType = 0;       // the fallen body's row (NpcDeath.ix)
    MacroHandle   victim{};          // {slot, id}; пустой = строка существа
    MacroHandle   killer{};          // {slot, id} победителя
    std::int32_t  detail  = -1;      // the creatures record id, -1 for a avatar
    int           level   = 1;
    const char*   factionId = "";    // whose colours the fallen wore
};
using BattleFactSink = void (*)(void* user, const BattleFact& fact);

// (ПАРА `EconFact`/`EconFactSink` СТОЯЛА ЗДЕСЬ ОБЪЯВЛЕНИЕМ ВПЕРЁД и умерла
//  2026-11-03 вместе со вторым фактным путём мира — некролог в
//  `src/macro/econ_day.h`. Факт мира есть род 5, `WorldFact@src/bus/fact.h`.)

struct GameState;
struct MacroStore;
struct TreeLayer;
struct DepositLayer;
struct TerrainData;
struct FeatureLayer;
struct ZoneLayer;
struct PathCostData;
struct TreeGrid;
struct SquadIndex;
struct NavWorld;

struct MacroWorld {
    GameState*  gs    = nullptr;
    TreeLayer*  trees = nullptr;
    MacroStore* store = nullptr;   // гладкая память макро-сквадов (M-106):
                                   //   состояние — колонками по слоту;
                                   //   реестра сцены в конверте НЕТ (ломоть A
                                   //   M-150): субмир берёт его явным
                                   //   параметром enter(), макро не видит
                                   //   вовсе — шапка «ecs-free» снова правда
    const TerrainData* terrain = nullptr;   // the fauna row derives its
                                            //   baseline from the cell's biome
    DepositLayer* deposits = nullptr;       // the Clay/Iron/Stone carrier
    // Roads / dirt roads / fields. Non-const since 2026-08-31 (CANON S10
    // «фичи создаются сквадами»): the plough writes ONE cell through
    // plough_field_cell — the same carrier-layer precedent trees and
    // deposits set. Every other reader still only reads.
    FeatureLayer* features = nullptr;
    const ZoneLayer*    zones    = nullptr; // danger byte 0..255 (zones.h)
    const PathCostData* pathCost = nullptr; // baked SP-weight grid + water flag
    const TreeGrid*     treeGrid = nullptr; // tree-point buckets (npc_ai.h) —
                                            //   the woodcutter's target search
    const SquadIndex* squads = nullptr; // каркас клеток по сквадам — ответ
                                        //   мира «кто на клетке», включая
                                        //   неподвижные (settlement_at,
                                        //   macro/squad_index.h)
    NavWorld* nav = nullptr;   // запечённая навигация: округи + порталы +
                               //   граф (macro/nav_field.h, CANON S7); null =
                               //   походка падает в жадный шаг — нулевой
                               //   вклад отсутствующего слоя

    // ── The way OUT: facts the macro layer produces ───────────────────────
    // The macro layer must not see the event bus (that is L3; econ_day
    // already reports through a POD sink for the same reason), but a system
    // that stays SILENT is invisible to the story layer — «система обязана
    // объявить, какие факты она эмитит» (work_vector §1). So the envelope
    // carries the channel: the app plugs the bus in once, and every macro
    // system that has something to report finds it here rather than growing
    // an out-parameter. Null = nobody is listening, which is the honest
    // state of a headless fixture and costs a null check.
    BattleFactSink facts     = nullptr;
    void*          factsUser = nullptr;
    // (КОЛОНКИ `econFacts`/`econFactsUser` — КАНАЛ ЭКОНОМИЧЕСКИХ ФАКТОВ —
    //  УНИЧТОЖЕНЫ 2026-11-03. Их не ставил НИ ОДИН путь игры, то есть девять
    //  эмиссий в проде были no-op всегда, а читали их только приборы: вторая
    //  форма факта рядом с родом 5 фрейма (DOD п.6). Вердикт владельца и
    //  названная дыра — в шапке `src/macro/econ_day.h`.)
};

} // namespace sm

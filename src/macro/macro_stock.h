// THE ledger between the two layers: what the subworld takes from the world
// above it, and how it pays it back.
//
// THE RULE (owner, 2026-08-06). The subworld is a CONTEXT of the macro world,
// never a peer of it. A body or a prop down there is not a thing in its own
// right — it is a macro quantity, borrowed and made visible. Fell the trees and
// the cell's tree count drops; kill the townsfolk and the settlement's
// population drops. Nothing below the map is saved, because everything below
// the map is derivable from what is above it plus the seed.
//
// WHY A SYSTEM AND NOT A COUNTER PER THING. Trees already wrote back, through
// their own hand-made path. Population did not, and neither did garrisons,
// deposits or squad containers. Copying the tree counter four more times is how a
// codebase acquires four dialects of one idea and then loses one of them: the
// audit of 2026-08-06 found exactly that pattern — five spawn sites already
// disagreeing about what a body is made of. So there is one mechanism here and
// one TABLE. Teaching the world a new borrowable quantity is a ROW; it is never
// a new call site.
//
// THE SYMMETRY THAT MAKES IT HOLD. A row states both directions at once:
// `read` is what a spawner asks when it decides how much to embody, `write` is
// how that embodiment is paid back. You cannot add a projection without its
// return path, because they are the same row — and the return is not optional,
// because the only way to draw is to stamp an ecs::MacroDebt that names it.
//
// Settlement is IMMEDIATE (owner's ruling): the macro world learns of a death
// on the tick it happens, while the player is still underground. The world
// clock runs down there too, so the macro sim must think with current numbers,
// not with the ones it had when you climbed down. No pending queue to lose.
//
// Deltas are SIGNED (owner's ruling): one row serves ruin and creation alike.
// Planting, building, settling people are the positive direction and will need
// no new machinery when they arrive.
//
// LAYERING. This header is the INTERFACE — a key and the rows over it — and
// deliberately knows nothing of GameState, so the day loop can borrow without
// dragging macro state into its translation unit. The rows, which do touch
// GameState and the tree layer, live in macro_stock.cpp.
//
// СЛОВАРЬ ВЕДОМОСТИ (`MacroStock` + `MacroStockKey`) УЕХАЛ в tables/stock_key.h
// ломтём 1б эпика пакетов v1: его называет расписка субмира, а §11 запрещает
// субмиру включать macro/. Здесь остались ПОДПИСИ с `MacroWorld` — то есть
// состояние мира, которому в нейтральном слое места нет.
#pragma once

#include "ecs/components.h"
#include "macro/macro_world.h"
#include "macro/resource_field.h"
#include "tables/stock_key.h"

// entt этот модуль больше не трогает: последнее касание (stamp_macro_debt,
// писавший компонент на тело сцены) уехало в sub/spawn.h ломтём 4 M-150.

#include <cstdint>

namespace sm {

// A reaped parcel regrows its WHOLE potential in this many seasons — the
// seasonal harvest rhythm (smooth stand-in for the deferred autumn pulse).
// Swept 2026-08-30: /4 starved the world at ~2.6k bread/day; /1 feeds
// ~12k/day and villages amass real surpluses — «деревня среди полей ломится
// от зерна» (owner's anchor, CANON S10). Public so crop_stock_test derives
// its expectation from the SAME number the law reads.
inline constexpr int kWheatSeasonsToRegrow = 1;

// THE envelope of world layers lives in macro/macro_world.h (CANON S6) — the
// rows below read and pay the world through it.

// How much of this stock stands here — what a spawner asks before it embodies.
int  macro_stock_read(const MacroWorld& w, MacroStock s, MacroStockKey k);

// Move the stock. Negative spends it, positive returns it; both go through the
// same row, so ruin and creation can never drift apart.
void macro_stock_apply(MacroWorld& w, MacroStock s, MacroStockKey k, int delta);

// The row's name, for logs and for tests that walk the table.
const char* macro_stock_id(MacroStock s);

// (stamp_macro_debt — дверь штампа квитанции — живёт в sub/spawn.h с ломтя 4
// M-150: квитанция стала КОЛОНКОЙ арены субмира, и макро-слою её не видно.
// Здесь остаются чтение, движение и расчёт стока — макро-половина закона.)

// (`settle_macro_debt` УМЕР 2026-10-23, ломоть 1 M-72: 0 прод-звонящих —
// расписка мертвеца едет СТРОКОЙ ДЕЛЬТЫ с тем же ключом, собранным у писателя
// (`push_stock_op@src/sub/engine.cpp`), и применяется границей тика через
// `macro_stock_apply`. Закон «один ключ расписки» живёт у дельты.)

// Birth walks through ONE door now — resource_fields_daily_growth
// (macro/resource_field.h): every field grows by the one law, its context
// a row, never a second mechanism.

} // namespace sm

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
// deposits or squad rosters. Copying the tree counter four more times is how a
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
// LAYERING. This header is the INTERFACE — an enum, a key and the stamp — and
// deliberately knows nothing of GameState, so spawners (sub/) can borrow
// without dragging macro state into their translation unit. The rows, which do
// touch GameState and the tree layer, live in macro_stock.cpp.
#pragma once

#include "ecs/components.h"
#include "macro/resource_field.h"

#include <cstdint>

namespace sm {

struct GameState;
struct TreeLayer;
struct DepositLayer;
struct TerrainData;
namespace ecs { struct World; }

// The borrowable quantities. One row per value in the table (macro_stock.cpp);
// the enum value IS the row index, and ecs::MacroDebt stores it as a plain byte
// so the ECS layer never has to learn this header exists.
enum class MacroStock : std::uint8_t {
    TreeCount = 0,   // macro/tree_layer.h — the forest standing in one cell
    Population,      // the people of a settlement or village
    Roster,          // the members of a squad standing on the map (ecs::SquadRoster)
    FaunaCount,      // the wild headcount of one cell (macro/fauna.h capacity
                     //   + GameState::faunaOverrides — the hunted delta)
    CropCount,       // the standing wheat of one cell (fertility-derived
                     //   estimate − GameState::cropOverrides harvest scar)
    Count,
};

// Where a stock lives. Everything in the world sits in a macro cell; a stock
// belonging to a NAMED thing standing in that cell also carries its id.
struct MacroStockKey {
    std::int32_t subject = -1;   // settlement / village id; -1 = the cell itself
                                 //   (the roster row: the squad's MacroSpawnId)
    std::int16_t cellX = 0;
    std::int16_t cellY = 0;
    // Which row WITHIN the subject, when the stock is a TABLE rather than a
    // count. The roster row stores the member's SoldierRecord::entityId (a bit
    // pattern — ids may use the high bit); -1 = no name, the stock is a plain
    // number. Anonymous rows ignore it, so every existing key stays valid.
    std::int32_t detail = -1;
    // WHICH REGISTER the subject id is drawn on. Cities and villages are
    // numbered from zero INDEPENDENTLY (state.cpp: `s.id = int(i)` and
    // `vil.id = villageId++`), so an id alone does not name a place — and the
    // lookup searched the cities first. Killing ten peasants in village 3 took
    // ten souls off CITY 3 on the other side of the map; past the last city's
    // id the deaths vanished with no payer at all. Trailing and defaulted, so
    // every key that means a CELL (subject = -1) is unaffected.
    bool subjectIsVillage = false;
};

// Everything a row may need in order to read or write itself. It grows by a
// FIELD when a new stock needs one — never by a new argument at a call site.
struct MacroWorld {
    GameState*  gs    = nullptr;
    TreeLayer*  trees = nullptr;
    ecs::World* world = nullptr;   // the roster row lives on squad entities
    const TerrainData* terrain = nullptr;   // the fauna row derives its
                                            //   baseline from the cell's biome
    DepositLayer* deposits = nullptr;       // the Clay/Iron/Stone carrier
};

// How much of this stock stands here — what a spawner asks before it embodies.
int  macro_stock_read(const MacroWorld& w, MacroStock s, MacroStockKey k);

// Move the stock. Negative spends it, positive returns it; both go through the
// same row, so ruin and creation can never drift apart.
void macro_stock_apply(MacroWorld& w, MacroStock s, MacroStockKey k, int delta);

// The row's name, for logs and for tests that walk the table.
const char* macro_stock_id(MacroStock s);

// Stamp a freshly embodied thing with the debt it owes. This is the ONLY way to
// take from a macro stock: no stamp, no borrowing — so a spawner that forgets
// is not one that quietly leaks, it is one that never took anything.
inline void stamp_macro_debt(entt::registry& reg, entt::entity e,
                             MacroStock stock, MacroStockKey key,
                             std::uint16_t amount = 1) {
    reg.emplace_or_replace<ecs::MacroDebt>(
        e, std::uint8_t(stock), key.subject, key.cellX, key.cellY, amount,
        key.detail, std::uint8_t(key.subjectIsVillage ? 1 : 0));
}

// Settle one debt. `sign` is -1 when the borrowed thing is consumed (a citizen
// killed, a tree felled) and +1 when it is handed back. Called from ONE place
// per event class, never from whichever system happens to notice first.
void settle_macro_debt(MacroWorld& w, const ecs::MacroDebt& d, int sign);

// Birth walks through ONE door now — resource_fields_daily_growth
// (macro/resource_field.h): every field grows by the one law, its context
// a row, never a second mechanism.

} // namespace sm

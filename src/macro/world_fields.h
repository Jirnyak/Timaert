// THE registry of saved world FIELDS (CANON S5/S16/S21, 2026-08-24).
//
// A per-cell layer that is WORLD TRUTH — grown forest, hunted heads, opened
// veins, explored map — is a ROW of this table: the row writes and reads its
// own bytes, and save.cpp only decides WHERE in the payload the block sits.
// Teaching the world a new truth-field (the blood field, the dark field, a
// weather field) is one row here — it saves and loads itself the day it
// exists, with no new code in save.cpp. Row order IS the byte order; append
// rows, never reorder (the save's own append-only law).
//
// What is NOT here, deliberately:
//   - DERIVED fields (terrain, features, zones, cost grid, glow, the
//     landmark grid) are never saved — they are rebaked from what IS saved.
//     Their list lives with the rebaker, because a rebake is a dependency
//     CHAIN (zones read features, glow reads optics) and a flat table cannot
//     express an order.
//   - The storage dialects behind the rows (dense u16, dense u8, sparse
//     maps) are implementations, which S5 explicitly allows — the row is the
//     one door in front of each.
#pragma once
#include "macro/save_stream.h"

#include <cstdint>
#include <vector>

namespace sm {

struct GameState;
struct DepositLayer;

enum class WorldField : std::uint8_t {
    Trees = 0,   // dense u16 carrier — the living forest (since v36)
    Knowledge,   // dense u8 — the explored map; Visible decays to Explored
                 //   on write, sight is recomputed after load (since v40)
    Deposits,    // sparse i32 per kind — Clay/Iron/Stone veins (since v37)
    Scars,       // sparse u16 per resource row — harvest/hunt debts (v35)
    Count,
};

// The owning stores of the saved rows. Not a second world envelope: load
// runs BEFORE the app's layers exist (it fills these, and the boot moves
// them into place), so the rows name the stores directly.
struct WorldFieldStores {
    const GameState* gs = nullptr;
    const std::vector<std::uint16_t>* treeCounts = nullptr;
    const DepositLayer* deposits = nullptr;
};
struct WorldFieldStoresMut {
    GameState* gs = nullptr;
    std::vector<std::uint16_t>* treeCounts = nullptr;
    DepositLayer* deposits = nullptr;
};

// Walk the table in row order. The byte stream is exactly what the four
// hand-written blocks in save.cpp produced before the registry — the cut
// changed who OWNS the bytes, not the bytes.
void write_world_fields(savefmt::Writer& w, const WorldFieldStores& st);
bool read_world_fields(savefmt::Reader& r, const WorldFieldStoresMut& st);

// The row's name, for logs and for tests that walk the table.
const char* world_field_id(WorldField f);

} // namespace sm

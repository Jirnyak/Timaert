// Entry-side context — one byte of WHERE a walker came into its current macro
// cell from, plus one byte of HOW LONG it has been there. Pure data, stamped by
// the macro movement paths and consumed by the macro→subworld spawn placement:
// an army that walked into a cell from the south materialises along the south
// edge of the subworld map, and the longer it has been in the cell the deeper
// it can have pushed in. No actor kind is named anywhere — the player, a lord's
// war party and a lone peasant all carry the same two bytes through the same
// functions.
//
// Direction is a PACKED SIGNED STEP, not a compass enum: (dx+1)*3 + (dy+1) for
// the last cell change, dx/dy ∈ {-1,0,1} in MACRO axes (+Y = north; the
// subworld shares that axis, engine.cpp cell mapping). Packing the delta keeps
// the two existing, mutually-contradicting compass conventions (macro_overlay
// "dy>0 = N" vs sub::Dir "N = {0,-1}") out of the data entirely.
//
// The reachable band: a walker that entered T AI-ticks ago is SOMEWHERE between
// the entry edge and how far it could have walked since — uniform over that
// band. As T grows the band widens until it covers the whole cell, which IS the
// old uniform scatter, so "been here forever" (locals, garrisons, teleports,
// pre-first-move spawns via kEntryDirNone) needs no special case: the formula
// degrades to exactly the previous behaviour.
#pragma once
#include <algorithm>
#include <cstdint>

namespace sm {

// No known entry: spawned in place, teleported, or never moved yet.
inline constexpr std::uint8_t kEntryDirNone = 0xFF;

// Reachable-band advance per macro AI tick (macro/npc_ai.h kAiTicks = 32 world
// ticks = half an hour of GAME time), in SUBWORLD tiles: a humanoid walks
// ~35 tiles/s (macro/npc.h combat rows), so one AI tick buys ~17.5 tiles of
// possible depth.
inline constexpr float kEntryAdvancePerTick = 17.5f;

// Keep spawn bands off the very edge of the cell: room for the player's squad
// ring (radius ≤ 21, spawn.cpp) and clear of the seam re-centre thresholds.
inline constexpr float kEntryEdgeMargin = 48.0f;

// Depth of the band at T = 0: a fresh entrant is a column crossing the border,
// not a line painted on it.
inline constexpr float kEntryBandDepth = 64.0f;

// Pack the signed step of the last cell change. (0,0) is "did not move" and
// deliberately maps to the sentinel — a non-move never restamps an entry.
inline std::uint8_t pack_entry_dir(int dx, int dy) {
    if (dx == 0 && dy == 0) return kEntryDirNone;
    const int cx = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
    const int cy = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
    return std::uint8_t((cx + 1) * 3 + (cy + 1));
}

// Decompose a packed direction; false for the sentinel (and the unused (0,0)
// slot), leaving dx/dy = 0 — which the band function reads as "no info".
inline bool unpack_entry_dir(std::uint8_t dir, int& dx, int& dy) {
    dx = 0;
    dy = 0;
    if (dir > 8u || dir == 4u) return false;
    dx = int(dir) / 3 - 1;
    dy = int(dir) % 3 - 1;
    return true;
}

// Saturating tick-in-cell counter (caps at ~2 min of macro time; the band has
// long since covered the whole cell by then).
inline std::uint8_t saturate_entry_ticks(std::uint8_t t) {
    return t < 0xFFu ? std::uint8_t(t + 1u) : t;
}

// Local spawn coordinate along ONE axis of a cellSize-wide cell, in [0,
// cellSize). `step` is that axis' component of the entry step: stepping +axis
// crosses the LOW edge of the new cell (you came from the cell below), so the
// band grows from 0; -axis mirrors from the far edge; 0 means no information —
// uniform, exactly the pre-context scatter. `u` ∈ [0,1) picks the point within
// the band (pass 0.5 for a deterministic mid-band placement).
inline float entry_axis_pos(int step, std::uint8_t ticks, float cellSize,
                            float u) {
    if (step == 0) return std::min(u * cellSize, cellSize - 1.0f);
    const float reach = std::min(
        cellSize - kEntryEdgeMargin,
        kEntryEdgeMargin + kEntryBandDepth + float(ticks) * kEntryAdvancePerTick);
    const float pos = kEntryEdgeMargin + u * (reach - kEntryEdgeMargin);
    return step > 0 ? pos : cellSize - pos;
}

} // namespace sm

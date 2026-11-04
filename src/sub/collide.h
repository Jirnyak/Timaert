// Subworld body-vs-structure collision & support — the ONE solidity authority.
// ----------------------------------------------------------------------------
// Until this module, the subworld's 3D structures (houses, walls, towers, gate
// lintels — sub/map_data.h Structure) were pure scenery: every mover waded
// through them and nothing could stand on them. This header turns the SAME
// records the renderer draws into an honest spatial index of solid volumes:
//
//   • SUPPORT — the surface under a body's feet is max(terrain, the highest
//     structure top it can rest on). Walkers pinned per-tick and the flying
//     floor both query it, so anyone can STAND on a wall walk, a roof, a gate
//     lintel — universally, not per-kind.
//   • BLOCKING — a horizontal step that would push a body's volume through a
//     solid's side is refused (with axis sliding). One rule for the player
//     (walking AND flying), wander/flee AI, the mass-battle pass and spawn.
//     The step is judged over the whole SWEPT PATH, never by its destination
//     alone: a destination test only ever refuses a body that LANDS inside
//     the Minkowski-expanded solid, so the band able to refuse it is
//     2·halfXy + 2·r wide and any step wider crosses it clean. M-398 measured
//     that band against the engine's own ceiling — a fence is 1.70 tiles wide
//     and one frame step is 2.24 — so the hole was not a calibration but the
//     shape of the question. `resolve_step` is the ONE door that asks it, for
//     the direct mover and for the mass pass alike (DOD п.6).
//   • LAYERING — solids are z-SPANS, not footprints. A lintel 5 m up blocks
//     nobody at street level, carries a defender on top, and stops a flyer
//     trying to rise through it: over/under crossings work honestly.
//   • ESCAPE — a body that finds itself INSIDE a solid (teleport, possession,
//     legacy save) may always move; blocking only ever refuses entry. Nothing
//     can be trapped by this module.
//
// Geometry comes from the shared map_data.h helpers (structure_half_x/y,
// structure_solid_span, per-kind minimum floors), which the renderer's
// instance builder uses verbatim — what you see is exactly what collides.
// Trees stay non-solid by design (undergrowth is a battle speed cost, and
// pinning a charging army against every trunk would read worse than brushing
// through); Rock is not rendered in 3D and is skipped the same way. A Bridge
// is solid masonry: its zBase-lifted deck carries walkers over the water and
// its piers stand in it — the arches between them are simply the absence of
// solids, which is why nothing under a bridge is ever blocked.
//
// The index is rebuilt from the seamless composite whenever its structure set
// changes (the same CompositeDirty.structs signal that re-uploads the GPU
// instances) and is Vulkan-free: the terrain sampler comes in as a function
// pointer, so tests drive it with a flat plane.
#pragma once

#include <cstdint>
#include <vector>

#include "sub/map_data.h"

namespace sm::sub {

// How high a foot can step up onto an adjacent solid top without it counting
// as a wall. Kerbs and door sills yes; battlements no.
constexpr float kStepUpM = 0.9f;

// Vertical extent of a body for blocking purposes (feet → crown). One humanoid
// constant: creature-specific heights can become data later without touching
// the queries' shape.
constexpr float kBodyHeightM = 1.7f;

class StructureIndex {
public:
    using HeightFn = float (*)(void* user, float x, float y);

    // Rebuild from a structure set (cell-local or composite tile coords —
    // the index spans whatever the records span). `heightFn` samples the
    // terrain surface in metres at a tile coordinate; it seats each solid
    // exactly like the renderer seats its box.
    void rebuild(const std::vector<Structure>& structs,
                 HeightFn heightFn, void* heightUser);
    void clear();
    bool empty() const { return entries_.empty(); }
    std::size_t solid_count() const { return entries_.size(); }

    // Highest solid top a body of radius `r` at (x, y) with feet at `zFeet`
    // can rest on (top ≤ zFeet + stepUp). Returns a very large negative value
    // when nothing applies — callers max() it with the terrain sample.
    float support_at(float x, float y, float r, float zFeet,
                     float stepUp = kStepUpM) const;

    // Would a body of radius `r`, feet at `zFeet`, crown at `zFeet + bodyH`,
    // overlap the SIDE of any solid at (x, y)? A solid whose top is within
    // stepUp of the feet is a floor, not a wall.
    bool blocked_at(float x, float y, float r, float zFeet,
                    float bodyH = kBodyHeightM, float stepUp = kStepUpM) const;

    // Point-in-solid test (projectiles: bolts detonate on walls like they
    // detonate on terrain).
    bool solid_at(float x, float y, float z) const;

    // ── THE GROUND UNDER THE FEET (owner's law, 2026-08-30) ──────────────
    // What a body at (x, y) with feet at `zFeet` is actually standing on: the
    // tile laid by the solid CARRYING it (map_data.h walkTile), or
    // `terrainTile` when the ground itself carries it. Every mover asks this
    // instead of the tile grid, so a bridge deck is walked as road while the
    // river runs underneath, and a rampart walk is walked as pavement
    // whatever lies at its foot. A tile is one value per column of the map
    // and can never say "water below, masonry above"; the support can.
    //
    // "Carried BY it" is the floor test the whole module already uses — a top
    // within `stand` of the feet. A body wading past a pier is not standing
    // on the pier, and a body under a deck is not standing on the deck.
    std::uint8_t walk_tile_at(float x, float y, float r, float zFeet,
                              std::uint8_t terrainTile,
                              float stand = kStepUpM) const;

    // THE door: resolve one horizontal step. Refuses a step whose SWEPT PATH
    // touches a solid's side (not merely one whose end point lands in it),
    // slides along whichever axis reaches its destination by a clear path, and
    // always allows a body already inside a solid to move (escape rule).
    // Mutates to* to the reached point.
    //
    // Every mover in the subworld goes through here — the direct one
    // (engine.cpp move_player) and the mass pass (movement.cpp steer_bodies,
    // through MoveGround::resolveStep). The pass used to carry its own
    // spelling of the same question — three point tests plus its own axis
    // fallback — so "may a body step there" had two answers about one world
    // and a sweep fitted to either would have left the other tunnelling.
    void resolve_step(float fromX, float fromY, float& toX, float& toY,
                      float r, float zFeet,
                      float bodyH = kBodyHeightM,
                      float stepUp = kStepUpM) const;

private:
    struct Entry {
        float x, y;        // centre, tile coords
        float cs, sn;      // cos/sin yaw
        float hx, hy;      // half-extents, tiles (local X / local Y)
        float z0, z1;      // absolute solid span, metres
        float boundR;      // XZ bounding radius for binning
        std::uint8_t shape;
        // What this solid IS — carried so a support query can answer not only
        // how high the floor is but what the floor is MADE of (walk_tile_at
        // below). One byte per solid; the record is already in cache.
        std::uint8_t kind;
    };

    bool entry_overlaps(const Entry& e, float x, float y, float r) const;
    // Does the SEGMENT (x0,y0)→(x1,y1) touch this entry's Minkowski-expanded
    // body? A degenerate segment reduces EXACTLY to entry_overlaps, so the
    // sweep and the point test can never disagree about the same place — which
    // is what keeps a swept body from being parked where blocked_at calls it
    // trapped.
    bool entry_swept(const Entry& e, float x0, float y0, float x1, float y1,
                     float r) const;

    template <typename Fn>
    void for_candidates(float x, float y, float r, Fn&& fn) const;
    template <typename Fn>
    void for_candidates_seg(float x0, float y0, float x1, float y1, float r,
                            Fn&& fn) const;

    // Would a body of radius `r` sweeping from (x0,y0) to (x1,y1) at feet
    // `zFeet` cross the SIDE of any solid? Private on purpose: the only public
    // way to ask about a step is `resolve_step`, so a second mover cannot grow
    // its own half-answer beside it.
    bool swept_blocked(float x0, float y0, float x1, float y1, float r,
                       float zFeet, float bodyH, float stepUp) const;

    // Uniform bin grid over the full composite (kFullSize tiles), CSR layout.
    static constexpr int kBinShift = 5; // 32-tile bins → 96×96 over 3072
    static constexpr int kBinDim = kFullSize >> kBinShift;

    std::vector<Entry> entries_;
    std::vector<std::int32_t> binStart_;  // kBinDim² + 1 prefix offsets
    std::vector<std::int32_t> binItems_;  // entry indices per bin
};

} // namespace sm::sub

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
// through); Bridge/Rock are not rendered in 3D and are skipped the same way.
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

    // Resolve one horizontal step universally: refuse entry into a solid,
    // slide along whichever axis stays free, and always allow a body already
    // inside a solid to move (escape rule). Mutates to* to the reached point.
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
    };

    bool entry_overlaps(const Entry& e, float x, float y, float r) const;

    template <typename Fn>
    void for_candidates(float x, float y, float r, Fn&& fn) const;

    // Uniform bin grid over the full composite (kFullSize tiles), CSR layout.
    static constexpr int kBinShift = 5; // 32-tile bins → 96×96 over 3072
    static constexpr int kBinDim = kFullSize >> kBinShift;

    std::vector<Entry> entries_;
    std::vector<std::int32_t> binStart_;  // kBinDim² + 1 prefix offsets
    std::vector<std::int32_t> binItems_;  // entry indices per bin
};

} // namespace sm::sub

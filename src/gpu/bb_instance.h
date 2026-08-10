// The ONE per-instance record of the subworld billboard idiom.
//
// Trees, procedural creatures and paper-doll NPCs are the same idea drawn
// three ways: a camera-facing quad standing on its base point, lit by the one
// lighting law, casting its own silhouette in the shadow pass. Until Inc 2
// each pass declared its own instance struct and its own attribute table —
// three near-twins in the renderer, each REPEATED in the shadow pipeline and
// COPIED wholesale into tests/gpu_smoke3d.cpp, with the creature aspect table
// duplicated across two shaders under a "must match" comment. This header is
// the contract, and everyone (renderer, harness, both vertex stages) includes
// it; there is nothing left to keep in sync by hand.
//
// What the fields mean is decided PER PASS by `kind`:
//   trees      kind = sprite row (species / crop), seed = procedural variation
//   creatures  kind = CreatureArchetype,           seed = procedural variation
//   NPCs       kind = paper-doll pool layer,       seed = unused (0)
//
// `seed` carries a FLOAT'S BITS (floatBitsToUint on the CPU side,
// uintBitsToFloat in the shader): the procedural coverage functions take the
// same float values they always did, bit for bit, so the pinned graphics
// captures cannot drift by a reinterpretation.
//
// `tint` is packed RGBA8 (unpackUnorm4x8 in the shader == the /255.0f the CPU
// used to do); 0xFFFFFFFF = white = no tint.
//
// The quad is sized by halfW/height directly — every aspect decision (tree
// atlas law, creature body plan, doll squareness) is made ONCE on the CPU, so
// the lit pass and the shadow pass cannot disagree about a silhouette.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace gpu {

struct BbInstance {
    float px, py, pz;    // base/feet world position (metres)
    float halfW;         // quad half-width (metres)
    float height;        // quad full height (metres)
    std::uint32_t kind;  // pass-specific discrete id (see header comment)
    std::uint32_t seed;  // per-instance variation (a float's bits)
    std::uint32_t tint;  // packed RGBA8; 0xFFFFFFFF = no tint
};
static_assert(sizeof(BbInstance) == 32, "billboard instance is 8 lanes of 4");

// The matching vertex-input attribute table (binding 0, per-instance rate).
// Locations line up with billboard.vert / shadow_bb.vert.
inline constexpr VkVertexInputAttributeDescription kBbInstanceAttrs[] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BbInstance, px)},
    {1, 0, VK_FORMAT_R32_SFLOAT,       offsetof(BbInstance, halfW)},
    {2, 0, VK_FORMAT_R32_SFLOAT,       offsetof(BbInstance, height)},
    {3, 0, VK_FORMAT_R32_UINT,         offsetof(BbInstance, kind)},
    {4, 0, VK_FORMAT_R32_UINT,         offsetof(BbInstance, seed)},
    {5, 0, VK_FORMAT_R32_UINT,         offsetof(BbInstance, tint)},
};
inline constexpr std::uint32_t kBbInstanceAttrCount = 6;

// Reinterpret a float's bits for the `seed` lane (std::bit_cast spelled by
// hand — the tree/creature seeds were always floats and must stay bit-exact).
inline std::uint32_t bb_seed_bits(float v) {
    std::uint32_t u;
    __builtin_memcpy(&u, &v, sizeof u);
    return u;
}

inline std::uint32_t bb_pack_tint(std::uint8_t r, std::uint8_t g,
                                  std::uint8_t b) {
    return std::uint32_t(r) | (std::uint32_t(g) << 8)
         | (std::uint32_t(b) << 16) | 0xFF000000u;
}

} // namespace gpu

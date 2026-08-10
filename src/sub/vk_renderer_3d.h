// Vulkan port of Renderer3D (subworld first-person 3D). Compiles unused next
// to the GL Renderer3D until the flip (PHASE B); each pass is filled per
// vulkan_plan.md PHASE A by copying the matching pass from tests/gpu_smoke3d.cpp
// and feeding it the same real data the GL Renderer3D reads today.
//
// Rendering is split so the depth-only SHADOW pass can be recorded BEFORE the
// main render pass opens (a Vulkan render pass cannot be nested):
//   frame(): acquire -> record_shadow(cmd) -> begin_render_pass -> record_main(cmd)
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

#include "core/math.h"

#include "assets/paperdoll_atlas.h"
#include "gpu/vk_buffer.h"
#include "gpu/vk_pipeline.h"
#include "gpu/vk_shadow.h"
#include "gpu/vk_texture.h"

namespace gpu { struct VulkanDevice; }

namespace sm {
struct WorldTime;
namespace ecs { struct World; }
namespace sub {

struct Camera;
struct CompositeDirty;
class SeamlessSubworldManager;

class Renderer3DVk {
public:
    static constexpr int kMeshDim = 192; // quads per side (matches GL)
    // Per-frame-in-flight ring depth. MUST equal gpu::VulkanRenderer::
    // kMaxFramesInFlight — the per-frame light SSBO ring is indexed by the
    // renderer's currentFrame, so a mismatch would alias two frames onto one
    // buffer. A static_assert in the .cpp pins the two together.
    static constexpr int kFramesInFlight = 2;

    // Vertical scale lives in sub/height.h (kHeightScaleM) — the renderer is
    // a consumer of the one height authority, not its owner.

    // Hard ceiling on particle instances drawn per frame. Matches
    // ParticleSystem::kMaxParticles (sub/particles.h): 2048 × 32B = 64 KiB, the
    // vkCmdUpdateBuffer per-call maximum. A static_assert in the .cpp pins them.
    static constexpr std::uint32_t kMaxParticleInstances = 2048;

    void init(const gpu::VulkanDevice& dev, VkRenderPass mainPass);
    void destroy(const gpu::VulkanDevice& dev);
    void prepare_frame(VkCommandBuffer cmd, ecs::World* ecs, float elapsed);

    // Upload the frame's live particle instances into the device-local particle
    // buffer (same per-frame vkCmdUpdateBuffer + barrier path as NPCs). `data`
    // points at `count` packed sub::ParticleInstance records (32B each); count
    // is clamped to kMaxParticleInstances. Call from prepare_frame BEFORE the
    // render pass opens. Passing count==0 draws nothing (the pass self-skips).
    void stage_particles(VkCommandBuffer cmd, const void* data,
                         std::uint32_t count);

    // Rebuild device-local terrain mesh + instance buffers from the seamless
    // manager. Load-time / on seam-cross only — never per frame. `dirty` scopes
    // the work: a full rebuild (first upload, seam shift, height smooth) or only
    // the 1024-tile cells the manager stitched in on async drains — the latter
    // is what keeps a boundary crossing off the frame-time budget.
    void upload(const gpu::VulkanDevice& dev, const SeamlessSubworldManager& mgr,
                const CompositeDirty& dirty);

    // Depth-only shadow casters into the shadow map. MUST run before the main
    // render pass begins.
    void record_shadow(VkCommandBuffer cmd, const Camera& cam,
                       const WorldTime& time);

    // Main-pass draws: sky -> terrain -> trees -> structures -> NPCs -> water.
    // `frameIndex` selects the per-frame light-SSBO / descriptor-set ring slot;
    // it MUST be the renderer's currentFrame (the frame whose fence acquire_frame
    // just reset) so the buffer we write is GPU-idle. Range [0, kFramesInFlight).
    void record_main(VkCommandBuffer cmd, VkExtent2D ext, const Camera& cam,
                     const WorldTime& time, float waterLevel,
                     const SeamlessSubworldManager* mgr, ecs::World* ecs,
                     bool haste, bool flight, float px, float py, float elapsed,
                     std::uint32_t frameIndex);

    float sample_height_m(float x, float y) const;
    // Highest terrain vertex (metres) of the loaded 3×3 window. Recomputed
    // whenever heightVtxM_ changes (full rebuild or seam re-centre); the
    // flight ceiling derives from it (sub/height.h). 0 until first upload.
    float max_height_m() const { return maxHeightM_; }
    static void tile_to_world(float px, float py, float& wx, float& wz);

private:
    // Fill lightBuf_[slot] (the current frame's host-mapped point-light SSBO)
    // from the ONE universal gather: every subworld entity carrying a
    // LightEmitter (player lantern, NPC torches, spell/projectile glows, lit
    // windows) is packed as a GpuLight in window/composite space — the same
    // space as vWorld — via tile_to_world + sample_height_m + the emitter's
    // offset. When more than kSubworldMaxLights emitters are live the set is
    // culled to the N NEAREST the camera (camPos, world metres) so the closest
    // pools always survive — the player's own light rides the camera at ≈0 and
    // is never dropped. Writing straight through the persistent mapping is
    // fence-safe: the slot's frame fence was reset in acquire_frame, so the GPU
    // is done reading it (see create_host_mapped).
    // skyParams is copied into the SSBO's sky lane (time, wind, cloudiness —
    // the cloud-shadow context every lit pass reads; see sub/lighting.h).
    void gather_point_lights(ecs::World* ecs, std::uint32_t slot,
                             const sm::vec3& camPos,
                             const float (&skyParams)[4],
                             const sm::vec3& sunDir);
    const gpu::VulkanDevice* dev_ = nullptr;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    bool uploaded_ = false;
    sm::mat4 lightMvp_{}; // cached light MVP for shadow pass

    // ── A1: Terrain mesh ──
    gpu::VulkanPipeline terrainPipe_{};
    gpu::VulkanBuffer   terrainVtx_{};
    gpu::VulkanBuffer   terrainIdx_{};
    std::uint32_t       indexCount_ = 0;
    // Full-resolution tile material id grid (kFullSize²), sampled per-fragment
    // by mesh.frag at set 1 so roads/fields stay crisp at any mesh tessellation.
    // (Re)built in upload(); the set is allocated once and rewritten each build.
    gpu::VulkanTexture    materialTex_{};
    // Ping-pong sibling of materialTex_ for the seam-crossing GPU relocation
    // (3c): on a shift the 6/9 (axis) or 4/9 (diagonal) overlap is copied
    // materialTex_→materialTexAlt_ on the GPU (no host round-trip), the fresh
    // cells are filled into the alt, then the two swap and the descriptor is
    // rewritten. Created lazily alongside materialTex_ on the first full build.
    gpu::VulkanTexture    materialTexAlt_{};
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      materialPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       materialSet_       = VK_NULL_HANDLE;
    // Cached heightmap in metres at vertex-grid resolution (Nv × Nv).
    // Used by sample_height_m() so the engine can seat the first-person
    // camera on the terrain without keeping a second copy.
    std::vector<float>  heightVtxM_;
    // Min/max of heightVtxM_ — the flight ceiling reads the max
    // (max_height_m()); the SHADOW volume is fitted vertically to BOTH plus a
    // structure allowance, so a low sun no longer projects a fictitious
    // ±600/900 m box into a kilometres-wide light span (the morning-blob bug).
    float minHeightM_ = 0.0f;
    float maxHeightM_ = 0.0f;
    // Window heightfield (metres, Nv×Nv = vertex grid) on the GPU: sampled by
    // lighting.glsl's terrain_visibility() march, so mountains and hills
    // occlude the sun/moon analytically — no depth map, no zebra, any range.
    // Created zeroed in init() (the set-0 descriptor needs it before the first
    // upload), pixels refreshed by upload() whenever heightVtxM_ changes.
    gpu::VulkanTexture heightTex_{};
    // Absolute world-space origin (metres) of the current composite: the world
    // position that composite-local (0,0) maps to, up to a global constant.
    // Recomputed in upload() from the manager's centre cell and fed to mesh.frag
    // (packed into the unused sunDir.w / sunColor.w push lanes) so procedural
    // ground detail is keyed to ABSOLUTE coords and does not resample when the
    // 3×3 window recentres at a seam. Mirrors trees' absolute seeding.
    float groundOriginX_ = 0.0f;
    float groundOriginY_ = 0.0f;

    // ── A2: Sky ──
    gpu::VulkanPipeline skyPipe_{};
    // A2b: precipitation overlay — the atmosphere submodule's second
    // fullscreen pass (rain/snow/hail between the camera and the world).
    gpu::VulkanPipeline precipPipe_{};
    // Constellation stars: a tiny STATIC UBO (sub/sky.h SkyStarsUbo) written
    // once at create() from macro/celestial.h's authored tables; set 0 on the
    // sky pipeline. Never touched per frame.
    gpu::VulkanBuffer     skyStarsBuf_{};
    VkDescriptorSetLayout skySetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      skyPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       skySet_       = VK_NULL_HANDLE;

    // ── A3: Water ──
    gpu::VulkanPipeline waterPipe_{};
    // ── A4: Tree billboards ──
    gpu::VulkanPipeline treePipe_{};
    gpu::VulkanBuffer   treeInstBuf_{};
    std::uint32_t       treeCount_ = 0;
    // Instances the CURRENT allocation can hold. The set changes on every
    // crossing and every async drain, and re-creating a device-local buffer
    // each time — allocate, bind, stage, submit, wait — was ~90% of what those
    // rebuilds cost. Keep the allocation, overwrite it in place, and grow it
    // only when the set outgrows it; `*Count_` is what the draw uses, so spare
    // capacity beyond it is simply never read.
    std::size_t         treeInstCap_ = 0;
    // ── A5: Structures (walls/houses = boxes; towers/jambs/spire = cylinders,
    // same instance layout + material, separate procedural geometry) ──
    gpu::VulkanPipeline structPipe_{};
    gpu::VulkanBuffer   structInstBuf_{};
    std::uint32_t       structCount_ = 0;
    std::size_t         structInstCap_ = 0;
    gpu::VulkanPipeline cylPipe_{};
    gpu::VulkanBuffer   cylInstBuf_{};
    std::uint32_t       cylCount_ = 0;
    std::size_t         cylInstCap_ = 0;
    // ── A6: Shadow map ──
    gpu::VulkanShadowMap shadow_{};
    gpu::VulkanPipeline  shadowMeshPipe_{};
    gpu::VulkanPipeline  shadowTreePipe_{};
    gpu::VulkanPipeline  shadowStructPipe_{};
    gpu::VulkanPipeline  shadowCylPipe_{};
    // Set 0, shared by ALL lit pipelines: binding 0 = shadow-map sampler,
    // binding 1 = per-frame point-light SSBO. Because the SSBO is rewritten
    // every frame while up to kFramesInFlight frames are in flight, the set and
    // its backing buffer are RINGED: shadowSet_[f] points binding 1 at
    // lightBuf_[f], and record_main binds/writes the copy for the current
    // frame. Binding 0 is the same immutable shadow sampler in every set.
    VkDescriptorSetLayout shadowSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      shadowPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       shadowSet_[kFramesInFlight] = {};
    // Persistently-mapped host-visible light SSBO ring (one per frame in
    // flight). Written in record_main after the frame's fence is known idle, so
    // no staging copy and no queue stall (see VulkanBuffer::create_host_mapped).
    gpu::VulkanBuffer     lightBuf_[kFramesInFlight] = {};

    // ── A7: NPCs ──
    gpu::VulkanPipeline npcPipe_{};
    gpu::VulkanBuffer   npcInstBuf_{};
    std::uint32_t       npcCount_ = 0;
    gpu::VulkanPipeline shadowNpcPipe_{};
    character::PaperdollAtlas paperdoll_{};

    // ── A8: Creatures (procedural fauna billboards; monsters ≠ NPCs) ──
    // Any entity with a Sprite whose archetype != 0xFF is drawn here as a
    // camera-facing procedural silhouette (the universal-resolver default,
    // overridable by drawn art later). Filled per-frame like NPCs.
    gpu::VulkanPipeline creaturePipe_{};
    gpu::VulkanBuffer   creatureInstBuf_{};
    std::uint32_t       creatureCount_ = 0;
    gpu::VulkanPipeline shadowCreaturePipe_{};

    // ── FX: additive particle billboards (spell trails, impacts, blood, embers,
    //    explosions). Drawn after creatures, before water, with depth-test on /
    //    depth-write off so terrain occludes them but they never occlude each
    //    other; additive blend is order-independent so the pool needs no sort.
    //    The sim lives in the engine (sub/particles.h) — this pass only draws
    //    the packed instances handed to stage_particles(). No shadow caster. ──
    gpu::VulkanPipeline particlePipe_{};
    gpu::VulkanBuffer   particleInstBuf_{};
    std::uint32_t       particleCount_ = 0;
};

} // namespace sub
} // namespace sm

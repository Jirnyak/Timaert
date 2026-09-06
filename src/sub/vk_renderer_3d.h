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

#include "assets/sprite_bank.h"
#include "gpu/bb_instance.h"
#include "gpu/vk_buffer.h"
#include "gpu/vk_canvas.h"
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
    // Shadow-march heightfield domain = the window plus a one-window apron on
    // every side (3× span, 9216 m). Same 16 m texel as the window grid; the
    // apron rows come from the macro cell skeleton (see upload()). 3 windows
    // because the march's own reach (16 geometric steps from 12 m ≈ 3.2 km)
    // is one window span — a wider apron would never be sampled.
    static constexpr int kHeightExtFactor = 3;
    static constexpr int kHeightExtDim =
        kHeightExtFactor * kMeshDim + 1; // 577 texels per side
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
    // Stain canvas (Inc C): 1024² world tiles at 8 px/tile = one 8192² RGBA8
    // image; the engine's stamp ring is truncated to kMaxStamps per frame.
    static constexpr std::uint32_t kCanvasTiles = 1024;
    static constexpr std::uint32_t kStainPxPerTile = 8;
    static constexpr std::uint32_t kMaxStamps = 1024;

    void init(const gpu::VulkanDevice& dev, VkRenderPass mainPass);
    void destroy(const gpu::VulkanDevice& dev);
    // camPos (world metres) splits lights between the hero loop and the
    // light field — see gather_point_lights and rebuild_light_field.
    void prepare_frame(VkCommandBuffer cmd, ecs::World* ecs, float elapsed,
                       const sm::vec3& camPos);

    // Upload the frame's live particle instances into the device-local particle
    // buffer (same per-frame vkCmdUpdateBuffer + barrier path as NPCs). `data`
    // points at energy+matter packed sub::ParticleInstance records (32B each),
    // laid out exactly as ParticleSystem::pack() returns them: energy first
    // [0, energyCount), then matter [energyCount, energyCount+matterCount)
    // sorted back-to-front. The draw issues TWO ranged instanced draws over the
    // one buffer — matter (alpha-over, lit) first, energy (additive) on top —
    // via firstInstance, so the split costs no second upload. Totals clamp to
    // kMaxParticleInstances (matter clamps first — it sits at the tail). Call
    // from prepare_frame BEFORE the render pass opens; zero counts self-skip.
    void stage_particles(VkCommandBuffer cmd, const void* data,
                         std::uint32_t energyCount, std::uint32_t matterCount);

    // One stain-canvas stamp COMMAND, GPU instance layout (24 B): where a
    // mark lands, how big, its colour, and which shape shader draws it
    // (shaders/stamp.vert|frag). The engine fills these from particle
    // landings and death pools (the gigahrush landMark law); the renderer
    // only draws them into the canvas.
    struct StampInstance {
        float wx, wz;      // world position (m), same plane as vWorld.xz
        float radiusM;     // mark radius (m)
        float seed;        // shape variation (integer counter as float)
        std::uint8_t r, g, b;  // mark colour (unorm)
        std::uint8_t intensity; // max alpha 0-255
        float type;        // sub::MarkType as float (vertex attr)
    };

    // Upload this frame's stamp commands and record the stain-canvas pass:
    // clear the strips entering the camera-centred sliding window (they hold
    // stale marks from the toroidal far side), then draw every stamp. Must be
    // recorded OUTSIDE the shadow/main passes (call between prepare_frame and
    // record_shadow). Zero stamps + no camera movement ⇒ the pass self-skips
    // entirely. (camWx, camWz) = the CAMERA's world position — the canvas
    // follows the camera, which must stay separable from the player.
    void stage_stamps(VkCommandBuffer cmd, const StampInstance* cmds,
                      std::uint32_t count, float camWx, float camWz);

    // Forget every mark (subworld enter/leave: the ground under the canvas
    // changed identity). Takes effect in the next stage_stamps.
    void request_canvas_clear() { canvasClearAll_ = true; }

    // CPU stage of a terrain/instance rebuild: resample heights, fill material
    // bytes and rebuild instance lists into persistent scratch, then queue the
    // matching GPU writes in pend_. Runs in the SIM tick (overlapping the GPU's
    // previous frame); the queued writes are recorded into the next frame's
    // command buffer by prepare_frame → flush_uploads. It used to also perform
    // the GPU writes as blocking submits — up to seven vkQueueWaitIdle drains
    // per seam crossing, plus a buffer destroy the in-flight frame could still
    // be reading (audit III.9/III.14). `dirty` scopes the work exactly as
    // before: full rebuild, seam shift, or only the stitched cells.
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
    // `lightdbg` bisect mask (lighting.glsl lit_surface): bit0 march, bit1
    // clouds, bit2 object maps, bit3 N·L — set bit lifts that term to 1.
    void set_light_debug_mask(std::uint32_t m) { lightDebugMask_ = m; }
    std::uint32_t light_debug_mask() const { return lightDebugMask_; }
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
    sm::mat4 lightMvp_{};    // crisp near shadow level (camera-fitted ±256 m)
    sm::mat4 lightMvpFar_{}; // wide shadow level (the whole loaded window)

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
    // TWO material sets, written ONCE when the textures are born: [0] points
    // at the image originally in materialTex_, [1] at the alt. A ping-pong
    // swap just flips matFront_ — rewriting one set in place was a write to a
    // descriptor the in-flight frame still reads (audit III.14; the "safe
    // here" comment above it claimed a fence contract that did not exist).
    VkDescriptorSet       materialSets_[2]   = {};
    std::uint32_t         matFront_          = 0;
    // Cached heightmap in metres at vertex-grid resolution (Nv × Nv).
    // Used by sample_height_m() so the engine can seat the first-person
    // camera on the terrain without keeping a second copy.
    std::vector<float>  heightVtxM_;
    // kHeightExtDim² staging for the march heightfield upload: exact window
    // heights in the interior block, macro-skeleton apron around (upload()).
    std::vector<float>  heightExtM_;
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

    // ── THE LIGHT FIELD (sub/lighting.h) — thousands of small lights as one
    // texture sample. CPU-splatted every kLightFieldRebuildFrames from every
    // emitter beyond the hero radius, uploaded through a frames-in-flight
    // staging ring ON THE FRAME'S COMMAND BUFFER — never a blocking mid-frame
    // submit (problems.md §20). ──
    gpu::VulkanTexture lightFieldTex_{};
    gpu::VulkanBuffer  lightFieldStaging_[kFramesInFlight] = {};
    std::vector<std::uint8_t> lightFieldPixels_;
    std::uint32_t lightFieldFrame_ = 0;
    std::uint32_t lightDebugMask_ = 0; // `lightdbg`; 0 in shipping frames
    void rebuild_light_field(VkCommandBuffer cmd, ecs::World* ecs,
                             const sm::vec3& camPos, std::uint32_t slot);
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
    gpu::VulkanPipeline rainPipe_{}; // world-space precipitation (Inc D)
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
    // ── A6: Object shadow map — ONE idea at TWO scales ("the sphere
    // subtracted from the base"): `shadow_` is the crisp level fitted ±256 m
    // around the camera (every caster; ~15 cm texels — people cast
    // silhouettes), `shadowFar_` the wide level over the whole window (trees
    // and masonry only — a body's shadow past 256 m is subpixel). Receivers
    // hand off from crisp to wide across the crisp volume's edge band
    // (shadow_common.glsl shadowFactorHandoff). ──
    gpu::VulkanShadowMap shadow_{};
    gpu::VulkanShadowMap shadowFar_{};
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

    // ── A7: THE bodies — every living thing, drawn or procedural, through ONE
    //    pipeline and one instance buffer. The split into an "NPC" pass and a
    //    "creature" pass asked what SORT of thing a body was; the sprite law
    //    asks what its ROW has, and that question is answered per instance in
    //    the fragment stage (shaders/body.frag). ──
    gpu::VulkanPipeline bodyPipe_{};
    gpu::VulkanBuffer   bodyInstBuf_{};
    std::uint32_t       bodyCount_ = 0;
    gpu::VulkanPipeline shadowBodyPipe_{};
    SpriteBank bank_{};

    // ── Deferred GPU writes (the seam path) ──
    // upload() queues here; flush_uploads() records into the frame's command
    // buffer through the staging arena. A second upload() before the flush
    // MERGES (ticks normally flush every frame, but a skipped frame — resize,
    // or a smoke that ticks without rendering — stacks uploads): the height /
    // vertex / instance scratches are absolute recomputes so overwrite is the
    // merge; matCells OR; a full material write supersedes everything; and a
    // NEW shift cannot land on unflushed material (pending cells are in the
    // pre-shift frame) — upload() degrades that round to a full material
    // rebuild, mirroring CompositeDirty::merge's fallback.
    struct PendingGpu {
        bool heightTex = false;   // heightExtM_ → heightTex_ (full image)
        bool terrainVtx = false;  // vtxScratch_ → terrainVtx_
        bool terrainIdx = false;  // idxScratch_ → terrainIdx_ (first build)
        enum class Mat : std::uint8_t {
            None,   // no base op (matCells may still hold per-cell writes)
            Create, // first build: create both images + write both sets
            Full,   // matScratch_ → full-image overwrite
            Shift   // GPU overlap copy; consumes matCells as its fresh rects
        } mat = Mat::None;
        std::int32_t shiftSrcX = 0, shiftSrcY = 0;
        std::int32_t shiftDstX = 0, shiftDstY = 0;
        std::int32_t shiftW = 0, shiftH = 0;
        bool matCells[9] = {};
        bool trees = false, boxes = false, cyls = false;
        bool selfCheck = false; // TIMAERT_SEAM_SELFCHECK: blocking twins + readback
        bool any() const {
            bool cells = false;
            for (bool c : matCells) cells |= c;
            return heightTex || terrainVtx || terrainIdx || mat != Mat::None
                || cells || trees || boxes || cyls;
        }
    };
    PendingGpu pend_{};
    // Persistent CPU scratch the pending ops read from. Byte vectors where the
    // element type is renderer-internal (Vtx / StructInstance).
    std::vector<std::uint8_t>  vtxScratch_;
    std::vector<std::uint32_t> idxScratch_;
    std::vector<std::uint8_t>  matScratch_;
    std::vector<std::uint8_t>  matCellScratch_[9];
    std::vector<std::uint8_t>  matSelfRef_; // selfcheck reference (env only)
    std::vector<gpu::BbInstance> treeScratch_;
    std::vector<std::uint8_t>  boxScratch_;
    std::vector<std::uint8_t>  cylScratch_;
    // Staging arena ring (host-mapped, one per frame in flight): flush_uploads
    // memcpys scratch into the CURRENT slot and records copies from it. The
    // slot advances once per prepared frame, so a slot is rewritten only after
    // its frame's fence has been waited — same contract as lightFieldStaging_.
    // Grows on demand; the outgrown buffer goes to the graveyard because the
    // in-flight frame's copies may still source from it.
    gpu::VulkanBuffer stageArena_[kFramesInFlight] = {};
    std::uint32_t     arenaSlot_ = 0;
    VkDeviceSize      arenaOff_ = 0;
    // Copy `bytes` of `src` into the current arena slot; returns the offset,
    // or VK_WHOLE_SIZE on allocation failure.
    VkDeviceSize arena_push(const void* src, VkDeviceSize bytes);
    // Record every queued GPU write into `cmd` (prepare_frame calls this
    // before anything else records). Under pend_.selfCheck falls back to the
    // blocking twins so the readback-and-compare sees executed results.
    void flush_uploads(VkCommandBuffer cmd);

public:
    // SMOKE/DIAGNOSTIC door: flush the queued GPU writes through a one-shot
    // submitted-and-fenced command buffer. The real loop flushes on the
    // frame's command buffer (prepare_frame); a smoke that ticks the sim
    // WITHOUT rendering frames must call this between ticks, or every upload
    // sees its buffers still unborn and degrades to the always-correct full
    // rebuild — and the incremental seam machinery under test never runs.
    void flush_uploads_blocking();

private:
    // Ensure `buf` holds `count` elements of `elemSize` (grow = graveyard the
    // old allocation + create uninitialised), then record the copy from the
    // arena. Returns false on failure (caller zeroes its draw count).
    bool flush_instances(VkCommandBuffer cmd, gpu::VulkanBuffer& buf,
                         std::size_t& cap, const void* data, std::size_t count,
                         std::size_t elemSize, const char* what);

    // ── FX: particle billboards (spell trails, impacts, blood, dust, embers,
    //    explosions), TWO pipelines over ONE instance buffer, split by the
    //    preset table's blend class (sub/particles.h FxBlend):
    //      matter — alpha-over + lit (blood, dust, smoke): binds the shared
    //        litSet (shadow + point lights), drawn FIRST, back-to-front (the
    //        sim sorts its segment);
    //      energy — additive emissive (magic, fire, sparks): no descriptors,
    //        order-independent, drawn on top so glow adds over blood.
    //    Both after creatures, before water, depth-test on / depth-write off.
    //    The sim lives in the engine (sub/particles.h) — this pass only draws
    //    the packed ranges handed to stage_particles(). No shadow caster. ──
    gpu::VulkanPipeline particlePipe_{};       // energy (additive)
    gpu::VulkanPipeline particleMatterPipe_{}; // matter (alpha-over, lit)
    gpu::VulkanBuffer   particleInstBuf_{};
    std::uint32_t       particleEnergyCount_ = 0;
    std::uint32_t       particleMatterCount_ = 0;

    // ── Stain canvas (particles-unified-matter Inc C): persistent GPU-only
    //    surface marks — blood splats, death pools, drips, scorch. A single
    //    8192² RGBA8 image = 1024² world tiles at 8 px/tile, addressed
    //    toroidally (world tile mod 1024 — the REPEAT sampler wraps reads, so
    //    a seam recentre of exactly kCellSize=1024 tiles lands on the SAME
    //    texels: the canvas survives the seam with zero work). It slides with
    //    the CAMERA: strips entering the window are cleared in stage_stamps;
    //    what leaves the half-kilometre ring is forgotten — the gigahrush
    //    eviction law ("farthest from the action dies first") as geometry.
    //    mesh.frag samples it into the terrain albedo BEFORE lit_surface, so
    //    marks are shadowed/moonlit like the ground they lie on. ──
    gpu::VulkanStainCanvas stainCanvas_{};
    gpu::VulkanPipeline    stampPipe_{};
    gpu::VulkanBuffer      stampInstBuf_{};
    bool canvasClearAll_ = true;  // first frame / subworld enter: start clean
    int  canvasCamTileX_ = 0;     // camera tile of the last stamp pass
    int  canvasCamTileZ_ = 0;
};

} // namespace sub
} // namespace sm

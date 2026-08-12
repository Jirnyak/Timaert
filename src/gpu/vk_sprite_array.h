// A layered 2D texture array ("sprite pool"): many equal-size RGBA8 sprite
// frames stored as array layers, sampled in-shader via `sampler2DArray` with
// ONE shared sampler and ONE descriptor set bound once — no matter how many
// frames / monster kinds / spell effects exist.
//
// This replaces the per-frame `VkImage`+`ImGui_ImplVulkan_AddTexture`-per-sprite
// path that exhausted the descriptor pool and stalled the queue with a
// submit+fence per texture (see problems.md). Adding a sprite becomes "write a
// layer", not "allocate another descriptor set".
//
// A "slot" is one resident sprite frame: a tileW×tileH RGBA image at a fixed
// integer index. A single animated NPC needs MANY slots (direction × frame ×
// animation); each unique composited (seed, anim, dir, frame) occupies one.
// slot_count() is therefore the resident working-set size, not a monster
// count — the LRU cache (in assets/) evicts the oldest slot when full.
//
// Slots and Vulkan array layers are DIFFERENT axes: `subTiles` packs a
// subTiles×subTiles grid of slots into each layer (slot = layer·subTiles² +
// quadrant), because MoltenVK caps maxImageArrayLayers at 2048 while a city's
// active working set of paper-doll frames measures ~4.3k (2026-08-13, 5k
// settlement) — capacity has to grow INSIDE a layer, not by adding layers.
// The shader decodes the same packing (shaders/doll_pool.glsl).
//
// Tile size is per-POOL and matches the source art, not a power-of-two rule:
// with NEAREST + no mips + no compression, POT buys nothing here.
//   - paperdolls (people/NPCs/mobs): 48×48, NEAREST  (kLogicalTileSize; art-locked)
//
// Slots are filled on demand: a CPU LRU cache (in assets/) maps a
// sprite-frame hash -> slot index, then calls upload_slot(cmd, slot, rgba),
// which records a staging copy onto the FRAME command buffer — no per-upload
// submit/fence (the no-stall transfer rule from ARCHITECTURE.md). Record these
// uploads BEFORE begin_render_pass(): a layout transition of a non-attachment
// image is illegal inside an active render pass. upload_slot_now() is the
// load-time blocking variant for preloading a batch before the first frame.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gpu
{
    struct VulkanDevice;

    struct SpriteArray
    {
        // Fixed per-SLOT tile size + array-layer capacity, chosen per size
        // class (e.g. 48x48 paperdolls). All slots share one sampler and live
        // in one 2D-array image of (tileW·subTiles)×(tileH·subTiles) layers.
        std::uint32_t tileW = 0;
        std::uint32_t tileH = 0;
        std::uint32_t layerCount = 0;
        std::uint32_t subTiles = 1;

        std::uint32_t slot_count() const {
            return layerCount * subTiles * subTiles;
        }

        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;   // 2D_ARRAY view over all layers
        VkSampler sampler = VK_NULL_HANDLE;

        // Bind-once descriptor: one COMBINED_IMAGE_SAMPLER at binding 0,
        // FRAGMENT stage. Pipelines that sample the pool are created with
        // `setLayout`; `descriptorSet` is bound before their draws.
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        // Create the array (all layers cleared to transparent and left in
        // SHADER_READ_ONLY_OPTIMAL) + sampler + descriptor. `stagingRing`
        // host-visible tiles back the per-frame upload_slot() path.
        //
        // `framesInFlight` MAKES the old "size it to uploads × frames in
        // flight" advice a mechanism: the ring is sliced into that many equal
        // windows and begin_frame() rotates through them, so a slot is never
        // rewritten until its own frame's fence has been waited on. Before
        // this, the cursor reset to slot 0 EVERY frame while two frames were
        // in flight — the CPU memcpy raced the previous frame's pending GPU
        // copy out of the same slot, and a walking crowd (a miss almost every
        // frame) uploaded torn pixels: the 2026-08-10 flickering villagers.
        // Uploads per frame are capped at stagingRing / framesInFlight.
        bool init(const VulkanDevice& dev, std::uint32_t tileW,
                  std::uint32_t tileH, std::uint32_t layerCount,
                  bool linearFilter, std::uint32_t stagingRing = 32,
                  std::uint32_t framesInFlight = 2,
                  std::uint32_t subTiles = 1);

        // Call once at frame start (before recording uploads): advances the
        // staging ring to the next frame's slice.
        void begin_frame();

        // Record a single-slot upload (one tileW×tileH frame) into `cmd`. MUST
        // be recorded before the main render pass begins. Returns false if the
        // staging ring is exhausted this frame (caller uploads it a later
        // frame) — never blocks, never submits.
        bool upload_slot(VkCommandBuffer cmd, std::uint32_t slot,
                         const std::uint8_t* rgba);

        // Load-time blocking upload of one slot (own transient pool + fence).
        // For preloading a batch before the first frame; never call per frame.
        bool upload_slot_now(const VulkanDevice& dev, std::uint32_t slot,
                             const std::uint8_t* rgba);

        void destroy(const VulkanDevice& dev);

    private:
        struct Staging
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            void* mapped = nullptr;
        };
        std::vector<Staging> staging_;
        std::uint32_t stagingCursor_ = 0;   // next free slot in current slice
        std::uint32_t sliceSize_ = 0;       // stagingRing / framesInFlight
        std::uint32_t sliceEnd_ = 0;        // one past current slice
        std::uint32_t frameParity_ = 0;     // rotates 0..framesInFlight-1
        std::uint32_t framesInFlight_ = 2;
    };

} // namespace gpu

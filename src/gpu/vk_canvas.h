// GPU-resident stain canvas (particles-unified-matter, Inc C): a persistent
// RGBA8 colour target that accumulates procedural surface marks — blood
// splats, death pools, drips, scorch — entirely on the GPU. The CPU only
// sends stamp COMMANDS (a few dozen bytes each); the mark's every pixel is
// computed by a fragment shader (the gigahrush surface_marks law, returned to
// its native home) and composited by fixed-function blending:
//   colour: SRC_ALPHA / ONE_MINUS_SRC_ALPHA — a new drop mixes over what is
//           there in proportion to its own alpha;
//   alpha:  ONE / ONE — coverage ACCUMULATES, the UNORM target saturates at
//           255. One drop = a faint pixel, ten drops = a dense stain, blood
//           over soot = maroon. Emergent, no code.
// The image never round-trips to the CPU. Layout contract: SHADER_READ_ONLY
// outside the stamp pass (the terrain fragment stage samples it every frame);
// the render pass transitions to COLOR_ATTACHMENT and back, with an external
// dependency ordering stamp writes before the main pass's reads.
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gpu
{
    struct VulkanDevice;

    struct VulkanStainCanvas
    {
        std::uint32_t size = 0; // square, texels
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;   // sampled by the terrain pass
        VkSampler sampler = VK_NULL_HANDLE;  // NEAREST — crisp pixel-art marks
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;

        // Create the image (cleared to fully transparent black — no marks),
        // the LOAD-preserving render pass and the framebuffer. Blocks once on
        // a fence for the initial clear; never again.
        bool init(const VulkanDevice& dev, std::uint32_t size);
        // Record: open the mark-accumulation pass (loadOp LOAD — marks
        // persist) + set viewport/scissor to the full canvas.
        void begin(VkCommandBuffer cmd) const;
        void end(VkCommandBuffer cmd) const;
        // Record a full-canvas clear INSIDE begin()/end() — used when the
        // whole surface changes identity (subworld enter/leave, a camera
        // teleport farther than the canvas span).
        void clear_all(VkCommandBuffer cmd) const;
        // Record a rect clear INSIDE begin()/end() — the strips entering the
        // sliding window as the camera moves (they hold stale marks from the
        // toroidal far side).
        void clear_rect(VkCommandBuffer cmd, std::int32_t x, std::int32_t y,
                        std::uint32_t w, std::uint32_t h) const;
        void destroy(const VulkanDevice& dev);
    };

} // namespace gpu

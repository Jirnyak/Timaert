#include "ui/ui_gpu.h"
#include "gpu/vk_device.h"
#include "gpu/vk_texture.h"
#include "backends/imgui_impl_vulkan.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace sm::ui {
namespace {

const gpu::VulkanDevice* g_dev = nullptr;

// ImTextureID is unsigned long long on macOS (MoltenVK) but VkDescriptorSet
// is a pointer.  We round-trip through memcpy to avoid strict-aliasing UB.
inline ImTextureID ds_to_texid(VkDescriptorSet ds) {
    ImTextureID t{};
    static_assert(sizeof(t) >= sizeof(ds));
    std::memcpy(&t, &ds, sizeof(ds));
    return t;
}

inline VkDescriptorSet texid_to_ds(ImTextureID t) {
    VkDescriptorSet ds{};
    static_assert(sizeof(ds) <= sizeof(t));
    std::memcpy(&ds, &t, sizeof(ds));
    return ds;
}

struct TexEntry {
    gpu::VulkanTexture vk{};
    ImTextureID        id{};
};

std::vector<TexEntry> g_entries;

} // namespace

void set_gpu_device(const gpu::VulkanDevice* dev) { g_dev = dev; }

ImTextureID create_ui_texture(int w, int h, const std::uint8_t* rgba,
                              bool linear) {
    if (!g_dev || !rgba || w <= 0 || h <= 0) return ImTextureID();
    gpu::VulkanTexture vk{};
    if (!vk.create_rgba8(*g_dev, w, h, rgba, linear, /*repeat=*/false)) {
        std::fprintf(stderr, "[ui_gpu] create_rgba8 failed %dx%d\n", w, h);
        return ImTextureID();
    }
    std::fprintf(stderr, "[ui_gpu] creating texture %dx%d sampler=%p view=%p\n",
                 w, h, (void*)vk.sampler, (void*)vk.view);
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        vk.sampler, vk.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!ds) {
        std::fprintf(stderr, "[ui_gpu] AddTexture returned null! entries=%zu\n",
                     g_entries.size());
        vk.destroy(*g_dev);
        return ImTextureID();
    }
    ImTextureID id = ds_to_texid(ds);
    TexEntry e;
    e.vk = vk;
    e.id = id;
    g_entries.push_back(e);
    return id;
}

ImTextureID recreate_ui_texture(ImTextureID old, int w, int h,
                                const std::uint8_t* rgba, bool linear) {
    // Same dimensions ⇒ overwrite the pixels IN PLACE and keep the id: no
    // image/view/sampler allocation, no ImGui descriptor add/remove — just
    // one small staged copy, queue-ordered after the in-flight frame. This
    // function used to be a lie (destroy + create), and its steady 2-second
    // callers — the subworld minimap above all — dragged a full blocking
    // allocate/submit/register cycle through the middle of the frame every
    // time: the periodic fps dips of 2026-08-10 (problems.md §20).
    if (old && g_dev && rgba && w > 0 && h > 0) {
        for (auto& e : g_entries) {
            if (e.id == old && e.vk.width == std::uint32_t(w)
                && e.vk.height == std::uint32_t(h)) {
                if (e.vk.update_region(*g_dev, 0, 0, std::uint32_t(w),
                                       std::uint32_t(h), rgba)) {
                    return old;
                }
                break; // update failed — fall through to the full rebuild
            }
        }
    }
    destroy_ui_texture(old);
    return create_ui_texture(w, h, rgba, linear);
}

void destroy_ui_texture(ImTextureID tex) {
    if (!tex || !g_dev) return;
    for (auto it = g_entries.begin(); it != g_entries.end(); ++it) {
        if (it->id == tex) {
            ImGui_ImplVulkan_RemoveTexture(texid_to_ds(tex));
            it->vk.destroy(*g_dev);
            g_entries.erase(it);
            return;
        }
    }
}

void destroy_all_ui_textures() {
    if (!g_dev) {
        g_entries.clear();
        return;
    }
    for (auto& e : g_entries) {
        if (e.id)
            ImGui_ImplVulkan_RemoveTexture(texid_to_ds(e.id));
        e.vk.destroy(*g_dev);
    }
    g_entries.clear();
}

} // namespace sm::ui

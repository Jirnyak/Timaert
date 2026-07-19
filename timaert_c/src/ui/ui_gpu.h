// Vulkan texture helper for UI layers. Provides a thin API so overlays can
// create/destroy GPU textures without including Vulkan headers directly.
// Call set_gpu_device() once at boot; all subsequent create/destroy calls
// use that device.  Created textures are registered with ImGui_ImplVulkan
// so they can be passed directly to ImGui::Image() as ImTextureID.
#pragma once

#include <cstdint>
#include "imgui.h"

namespace gpu { struct VulkanDevice; }

namespace sm::ui {

// Must be called once after VulkanDevice + ImGui_ImplVulkan are initialised.
void set_gpu_device(const gpu::VulkanDevice* dev);

// Create an RGBA8 texture and register it with ImGui. Returns nullptr on
// failure. The returned ImTextureID is valid until destroy_ui_texture().
ImTextureID create_ui_texture(int w, int h, const std::uint8_t* rgba,
                              bool linear = true);

// Replace an existing texture's pixels (same dimensions). Cheaper than
// destroy + create when only the content changes.
ImTextureID recreate_ui_texture(ImTextureID old, int w, int h,
                                const std::uint8_t* rgba, bool linear = true);

// Destroy a texture previously created by create_ui_texture.
void destroy_ui_texture(ImTextureID tex);

// Destroy ALL textures created via create_ui_texture. Call at shutdown.
void destroy_all_ui_textures();

} // namespace sm::ui

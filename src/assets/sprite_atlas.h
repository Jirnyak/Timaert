// Sprite atlas — lazy-loaded Vulkan textures for the drawn half of THE sprite
// table (macro/sprite_rows.h). It owns no list of its own: a row names its PNG,
// this file turns that name into a texture once and serves it forever. A row
// with no art (`asset == nullptr`) has no texture here and `sprite_get` returns
// null — its consumer draws the procedural body or its own mark instead, which
// is the law, not a failure (sprites.md).
//
// Usage from any thread with a valid VulkanDevice:
//   const Sprite* s = sprite_get(SpriteId::City);
//   if (s) ImGui::GetBackgroundDrawList()->AddImage(s->tex, tl, br);
#pragma once
#include "imgui.h"
#include "macro/sprite_rows.h"

namespace sm {

struct Sprite {
    ImTextureID tex = 0;
    int    w     = 0;
    int    h     = 0;
    bool   tried = false; // already attempted load (avoid retry storm)
};

// Returns nullptr if the row names no art, or the asset failed to load (file
// missing, decode error, no GPU device). Otherwise the cached sprite. Safe to
// call every frame.
const Sprite* sprite_get(SpriteId id);

// Free all loaded textures (call once at shutdown). Optional.
void sprite_atlas_shutdown();

} // namespace sm

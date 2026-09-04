// UI images from disk — THE one door from a PNG path to an ImTextureID.
// Story slides, the title backdrop and creation portraits all come through
// here; before 2026-09-04 the cache lived buried in overlays.cpp and the
// title screen would have needed a second copy to show a picture at all.
//
// Loading is lazy and cached forever (per path, by value — not by pointer,
// so the caller may hand in a built string, not only a literal). A missing
// file is remembered as missing and never retried: draw code may probe every
// frame for free.
#pragma once

#include "imgui.h"

namespace sm::ui {

struct UiImage {
    ImTextureID tex = 0;
    int w = 0;
    int h = 0;
};

// Lazy-load `path` (relative to the working directory, `assets/...`) and
// return the cached image, or nullptr while it is missing/failed.
const UiImage* ui_image_for(const char* path);

// Aspect-fit `img` into maxW×maxH as an ImGui item at the current cursor,
// optionally centred horizontally.
void draw_ui_image(const UiImage& img, float maxW, float maxH, bool center);

// Forget every cached entry. Call at shutdown AFTER destroy_all_ui_textures()
// (which owns the GPU side) so no slot survives holding a dangling id.
void ui_image_reset();

} // namespace sm::ui

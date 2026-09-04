#include "ui/ui_image.h"
#include "ui/ui_gpu.h"
#include <stb_image.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sm::ui {
namespace {

// Fixed pool, keys BY VALUE. The old overlay cache keyed on the pointer,
// which only worked because every path was a constexpr literal — one built
// string and it would have cached garbage.
struct ImageSlot {
    char key[192] = {0};
    UiImage img{};
    bool used = false;
};

constexpr std::size_t kUiImageCapacity = 32;
std::array<ImageSlot, kUiImageCapacity> g_images{};

bool ui_image_trace_enabled() {
    static const bool enabled = std::getenv("TIMAERT_STORY_UI_TRACE") != nullptr;
    return enabled;
}

unsigned char* load_image_pixels(const char* path, int& w, int& h) {
    // Two roots only: the working directory (running from the repo, where
    // build/assets is a symlink into assets/) and its parent (running from
    // build/ itself). The TS-era public/ fallbacks died with the leading
    // slashes in the slide paths.
    static const char* kPrefixes[] = {"", "../"};
    char candidate[256];
    int comp = 0;
    for (const char* prefix : kPrefixes) {
        const int n = std::snprintf(candidate, sizeof(candidate),
                                    "%s%s", prefix, path);
        if (n <= 0 || std::size_t(n) >= sizeof(candidate)) continue;
        if (unsigned char* px = stbi_load(candidate, &w, &h, &comp, 4))
            return px;
    }
    return nullptr;
}

} // namespace

const UiImage* ui_image_for(const char* path) {
    if (!path || path[0] == '\0') return nullptr;

    ImageSlot* freeSlot = nullptr;
    for (ImageSlot& slot : g_images) {
        if (slot.used && std::strcmp(slot.key, path) == 0)
            return slot.img.tex ? &slot.img : nullptr;
        if (!slot.used && !freeSlot) freeSlot = &slot;
    }
    if (!freeSlot) return nullptr;

    freeSlot->used = true;
    std::snprintf(freeSlot->key, sizeof(freeSlot->key), "%s", path);
    int w = 0;
    int h = 0;
    unsigned char* px = load_image_pixels(path, w, h);
    if (!px) {
        if (ui_image_trace_enabled())
            std::fprintf(stderr, "[ui-image] missing image: %s\n", path);
        return nullptr;
    }
    freeSlot->img.tex = create_ui_texture(w, h, px, /*linear=*/true);
    freeSlot->img.w = w;
    freeSlot->img.h = h;
    stbi_image_free(px);
    if (!freeSlot->img.tex) return nullptr;
    if (ui_image_trace_enabled())
        std::fprintf(stderr, "[ui-image] loaded image %s (%dx%d)\n", path, w, h);
    return &freeSlot->img;
}

void draw_ui_image(const UiImage& img, float maxW, float maxH, bool center) {
    if (img.w <= 0 || img.h <= 0 || img.tex == 0) return;
    const float scale = std::min(maxW / float(img.w), maxH / float(img.h));
    const ImVec2 size(float(img.w) * scale, float(img.h) * scale);
    if (center && size.x < maxW)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (maxW - size.x) * 0.5f);
    ImGui::Image(img.tex, size);
}

void ui_image_reset() {
    g_images.fill(ImageSlot{});
}

} // namespace sm::ui

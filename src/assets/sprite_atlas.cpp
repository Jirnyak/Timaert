// Sprite atlas implementation. Uses stb_image (header-only, FetchContent)
// to decode PNGs and uploads them as RGBA8 Vulkan textures via the ui_gpu
// helper. Asset paths are searched relative to the binary's CWD with a
// couple of common fallbacks so the game runs from `build/` or repo root.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include "assets/sprite_atlas.h"
#include "ui/ui_gpu.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace sm {
namespace {

std::array<Sprite, std::size_t(SpriteId::Count_)> g_sprites{};

// Try a list of candidate paths until one decodes.
unsigned char* try_load(const char* file, int& w, int& h) {
    static const char* kPrefixes[] = {
        "assets/sprites/",
        "../assets/sprites/",
        "../public/assets/sprites/",
        "../../public/assets/sprites/",
        "public/assets/sprites/",
    };
    char buf[512];
    int comp = 0;
    for (const char* pre : kPrefixes) {
        std::snprintf(buf, sizeof buf, "%s%s", pre, file);
        unsigned char* px = stbi_load(buf, &w, &h, &comp, 4);
        if (px) return px;
    }
    return nullptr;
}

void load_one(SpriteId id) {
    auto& s = g_sprites[std::size_t(id)];
    if (s.tried) return;
    s.tried = true;

    // The row IS the list. A row that names no art is not a failure — it is a
    // procedural body, and its consumer knows to draw one.
    const char* file = sprite_row(id).asset;
    if (!file) return;

    int w = 0, h = 0;
    unsigned char* px = try_load(file, w, h);
    if (!px) {
        std::fprintf(stderr, "[sprite] missing: %s\n", file);
        return;
    }
    s.tex = sm::ui::create_ui_texture(w, h, px, /*linear=*/true);
    s.w = w;
    s.h = h;
    stbi_image_free(px);
    if (s.tex) std::fprintf(stderr, "[sprite] loaded %s (%dx%d)\n",
                            file, w, h);
}

} // namespace

const Sprite* sprite_get(SpriteId id) {
    if (std::size_t(id) >= g_sprites.size()) return nullptr;
    auto& s = g_sprites[std::size_t(id)];
    if (!s.tried) load_one(id);
    return s.tex ? &s : nullptr;
}

void sprite_atlas_shutdown() {
    for (auto& s : g_sprites) {
        if (s.tex) sm::ui::destroy_ui_texture(s.tex);
        s = {};
    }
}

} // namespace sm

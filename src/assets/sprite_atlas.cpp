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

struct Entry { SpriteId id; const char* file; };

// Map enum → real filename in public/assets/sprites/.
constexpr std::array<Entry, std::size_t(SpriteId::Count_)> kAssets = {{
    { SpriteId::City,      "city_256.png"      },
    { SpriteId::Village,   "village_256.png"   },
    { SpriteId::Spire,     "spireA_256.png"    },
    { SpriteId::SpireDark, "spireD_256.png"    },
    // The player has no drawn figure of his own yet: `player.png` is the
    // title-screen skull, not a walker. A row whose art is missing borrows
    // the nearest kind's — one line to change when the figure is drawn.
    { SpriteId::Player,    "peasant_256.png"   },
    { SpriteId::Peasant,   "peasant_256.png"   },
    { SpriteId::Caravan,   "corovan_256.png"   },
    { SpriteId::Witch,     "witch_256.png"     },
    { SpriteId::Cultistka, "cultistka_256.png" },
    { SpriteId::ImpGolem,  "imp_golem_256.png" },
    { SpriteId::Coins,     "coins.png"         },
}};

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

    const char* file = nullptr;
    for (const auto& a : kAssets) if (a.id == id) { file = a.file; break; }
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

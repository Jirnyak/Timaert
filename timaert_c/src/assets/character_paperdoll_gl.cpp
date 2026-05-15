// Character paper-doll GL cache. Loads the TS atlas image/bin once, composes
// 48x48 paper-doll frames on demand, uploads each composed frame once, then
// serves the cached GL texture to macro/sub renderers.

#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include "assets/character_paperdoll_gl.h"
#include "gl/helpers.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace sm::character {
namespace {

std::uint8_t chan(std::uint32_t rgb, int shift) {
    return std::uint8_t((rgb >> shift) & 0xFFu);
}

bool close_rgb(std::uint32_t a, std::uint32_t b) {
    const int ar = int(chan(a, 16));
    const int ag = int(chan(a, 8));
    const int ab = int(chan(a, 0));
    const int br = int(chan(b, 16));
    const int bg = int(chan(b, 8));
    const int bb = int(chan(b, 0));
    const int dr = ar - br;
    const int dg = ag - bg;
    const int db = ab - bb;
    return dr * dr + dg * dg + db * db <= 162;
}

std::uint32_t apply_palette(std::uint8_t r,
                            std::uint8_t g,
                            std::uint8_t b,
                            const PaletteConfig& palette) {
    const int rg = int(r) - int(g);
    const int gb = int(g) - int(b);
    const std::uint32_t src = (std::uint32_t(r) << 16)
                            | (std::uint32_t(g) << 8)
                            | std::uint32_t(b);
    if (rg < -2 || rg > 2 || gb < -2 || gb > 2) {
        return src;
    }
    for (int i = 0; i < int(palette.colorCount); ++i) {
        if (close_rgb(src, palette.grayscale[std::size_t(i)])) {
            return palette.colors[std::size_t(i)];
        }
    }
    return src;
}

void blend_pixel(std::uint8_t* dst,
                 std::uint8_t sr,
                 std::uint8_t sg,
                 std::uint8_t sb,
                 std::uint8_t sa) {
    if (sa == 0) return;
    if (sa == 255 || dst[3] == 0) {
        dst[0] = sr;
        dst[1] = sg;
        dst[2] = sb;
        dst[3] = sa;
        return;
    }
    const int da = int(dst[3]);
    const int inv = 255 - int(sa);
    const int outA = int(sa) + da * inv / 255;
    if (outA <= 0) return;
    dst[0] = std::uint8_t((int(sr) * int(sa) + int(dst[0]) * da * inv / 255) / outA);
    dst[1] = std::uint8_t((int(sg) * int(sa) + int(dst[1]) * da * inv / 255) / outA);
    dst[2] = std::uint8_t((int(sb) * int(sa) + int(dst[2]) * da * inv / 255) / outA);
    dst[3] = std::uint8_t(outA);
}

bool path_exists(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool find_character_asset(const char* file, char* out, std::size_t outSize) {
    static constexpr const char* kPrefixes[] = {
        "assets/character/",
        "../assets/character/",
        "../public/assets/character/",
        "../../public/assets/character/",
        "public/assets/character/",
        "C:/Timaert/public/assets/character/",
    };
    for (const char* prefix : kPrefixes) {
        const int n = std::snprintf(out, outSize, "%s%s", prefix, file);
        if (n <= 0 || std::size_t(n) >= outSize) continue;
        if (path_exists(out)) return true;
    }
    out[0] = 0;
    return false;
}

std::uint64_t texture_key(const CharacterDescriptor& descriptor,
                          const AnimationState& animation) {
    std::uint64_t h = descriptor_hash(descriptor);
    h ^= (std::uint64_t(animation.frame) + 0x9e3779b97f4a7c15ull
        + (h << 6) + (h >> 2));
    h ^= (std::uint64_t(animation.animation) << 48);
    h ^= (std::uint64_t(animation.direction) << 56);
    return h;
}

bool same_descriptor(const CharacterDescriptor& a,
                     const CharacterDescriptor& b) {
    return a.seed == b.seed
        && a.hiddenMask == b.hiddenMask
        && a.sprites == b.sprites
        && a.paletteRows == b.paletteRows;
}

} // namespace

bool CharacterTextureCache::texture_entry_matches(const TextureEntry& entry,
                                                  const CharacterDescriptor& descriptor,
                                                  const AnimationState& animation) {
    return entry.animation == animation.animation
        && entry.direction == animation.direction
        && entry.frame == animation.frame
        && same_descriptor(entry.descriptor, descriptor);
}

const CharacterTexture* CharacterTextureCache::texture_for(
    const CharacterDescriptor& descriptor,
    const AnimationState& animation) {
    if (!load_assets()) return nullptr;
    const std::uint64_t key = texture_key(descriptor, animation);

    const std::size_t start = std::size_t(key % textures_.size());
    std::size_t firstFree = textures_.size();
    for (std::size_t probe = 0; probe < textures_.size(); ++probe) {
        const std::size_t idx = (start + probe) % textures_.size();
        TextureEntry& entry = textures_[idx];
        if (entry.occupied) {
            if (entry.key == key && texture_entry_matches(entry, descriptor, animation)) {
                return entry.texture.tex ? &entry.texture : nullptr;
            }
        } else {
            firstFree = idx;
            break;
        }
    }

    std::array<std::uint8_t, std::size_t(kLogicalTileSize) * kLogicalTileSize * 4u> pixels{};
    CharacterTexture texture{};
    if (!compose_rgba8(descriptor, animation, pixels.data())) {
        const std::size_t idx = firstFree < textures_.size() ? firstFree : start;
        if (textures_[idx].occupied && textures_[idx].texture.tex) {
            glDeleteTextures(1, &textures_[idx].texture.tex);
        }
        TextureEntry& entry = textures_[idx];
        entry.key = key;
        entry.descriptor = descriptor;
        entry.animation = animation.animation;
        entry.direction = animation.direction;
        entry.frame = animation.frame;
        entry.texture = texture;
        entry.occupied = true;
        return nullptr;
    }

    texture.w = kLogicalTileSize;
    texture.h = kLogicalTileSize;
    texture.tex = gl_make_texture_rgba8(texture.w, texture.h, pixels.data(),
                                        GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE);

    const std::size_t idx = firstFree < textures_.size() ? firstFree : start;
    if (textures_[idx].occupied && textures_[idx].texture.tex) {
        glDeleteTextures(1, &textures_[idx].texture.tex);
    }
    TextureEntry& entry = textures_[idx];
    entry.key = key;
    entry.descriptor = descriptor;
    entry.animation = animation.animation;
    entry.direction = animation.direction;
    entry.frame = animation.frame;
    entry.texture = texture;
    entry.occupied = true;
    if (!texture.tex) return nullptr;
    return &textures_[idx].texture;
}

const CharacterDescriptor& CharacterTextureCache::descriptor_for_seed(std::uint32_t seed,
                                                                      AppearancePreset preset) {
    const std::uint32_t stableSeed = seed ? seed : 1u;
    const std::uint32_t key = stableSeed
        ^ (std::uint32_t(preset) * std::uint32_t{2654435761u});
    const std::size_t start = std::size_t(key) % descriptors_.size();
    std::size_t firstFree = descriptors_.size();
    for (std::size_t probe = 0; probe < descriptors_.size(); ++probe) {
        const std::size_t idx = (start + probe) % descriptors_.size();
        DescriptorEntry& entry = descriptors_[idx];
        if (entry.occupied) {
            if (entry.seed == stableSeed && entry.preset == preset) {
                return entry.descriptor;
            }
        } else {
            firstFree = idx;
            break;
        }
    }

    DescriptorEntry& entry =
        descriptors_[firstFree < descriptors_.size() ? firstFree : start];
    entry.seed = stableSeed;
    entry.preset = preset;
    entry.descriptor = generate_character(stableSeed, preset);
    entry.occupied = true;
    return entry.descriptor;
}

void CharacterTextureCache::destroy() {
    for (TextureEntry& entry : textures_) {
        if (entry.occupied && entry.texture.tex) {
            glDeleteTextures(1, &entry.texture.tex);
        }
        entry = TextureEntry{};
    }
    descriptors_ = {};
    atlasPixels_.clear();
    atlas_ = AtlasData{};
    atlasW_ = 0;
    atlasH_ = 0;
    loaded_ = false;
    loadAttempted_ = false;
}

bool CharacterTextureCache::load_assets() {
    if (loaded_) return true;
    if (loadAttempted_) return false;
    loadAttempted_ = true;

    char binPath[512];
    char pngPath[512];
    if (!find_character_asset("atlas.bin", binPath, sizeof binPath)
        || !find_character_asset("atlas.png", pngPath, sizeof pngPath)) {
        std::fprintf(stderr, "[character] missing atlas.bin/atlas.png\n");
        return false;
    }
    if (!atlas_.load_bin(binPath)) {
        std::fprintf(stderr, "[character] invalid atlas bin: %s\n", binPath);
        return false;
    }

    int comp = 0;
    unsigned char* px = stbi_load(pngPath, &atlasW_, &atlasH_, &comp, 4);
    if (!px || atlasW_ <= 0 || atlasH_ <= 0) {
        std::fprintf(stderr, "[character] invalid atlas png: %s\n", pngPath);
        if (px) stbi_image_free(px);
        return false;
    }
    atlasPixels_.assign(px, px + std::size_t(atlasW_) * std::size_t(atlasH_) * 4u);
    stbi_image_free(px);
    loaded_ = true;
    std::fprintf(stderr, "[character] loaded paperdoll atlas (%dx%d sheets=%u entries=%u)\n",
                 atlasW_, atlasH_, unsigned(atlas_.sheetCount), unsigned(atlas_.entryCount));
    return true;
}

bool CharacterTextureCache::compose_rgba8(const CharacterDescriptor& descriptor,
                                          const AnimationState& animation,
                                          std::uint8_t* outPixels) {
    if (!loaded_ || atlasPixels_.empty()) return false;
    std::array<RenderLayer, kCategoryCount> layers{};
    const std::size_t layerCount = build_render_plan(atlas_, descriptor, animation,
                                                     layers.data(), layers.size());
    if (layerCount == 0) return false;

    std::memset(outPixels, 0, std::size_t(kLogicalTileSize) * kLogicalTileSize * 4u);
    for (std::size_t li = 0; li < layerCount; ++li) {
        const RenderLayer& layer = layers[li];
        const AtlasEntry& e = layer.entry;
        for (int y = 0; y < int(e.h); ++y) {
            const int dy = int(e.oy) + y;
            if (dy < 0 || dy >= kLogicalTileSize) continue;
            const int sy = int(e.v0) + y;
            if (sy < 0 || sy >= atlasH_) continue;
            for (int x = 0; x < int(e.w); ++x) {
                const int dx = int(e.ox) + x;
                if (dx < 0 || dx >= kLogicalTileSize) continue;
                const int sx = int(e.u0) + x;
                if (sx < 0 || sx >= atlasW_) continue;

                const std::size_t src = (std::size_t(sy) * std::size_t(atlasW_)
                                      + std::size_t(sx)) * 4u;
                const std::uint8_t sa = atlasPixels_[src + 3u];
                if (sa < 3) continue;
                const std::uint32_t col = apply_palette(atlasPixels_[src + 0u],
                                                        atlasPixels_[src + 1u],
                                                        atlasPixels_[src + 2u],
                                                        layer.palette);
                const std::size_t dst = (std::size_t(dy) * std::size_t(kLogicalTileSize)
                                      + std::size_t(dx)) * 4u;
                blend_pixel(outPixels + dst,
                            chan(col, 16),
                            chan(col, 8),
                            chan(col, 0),
                            sa);
            }
        }
    }
    return true;
}

} // namespace sm::character

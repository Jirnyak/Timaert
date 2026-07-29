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
#include "ui/ui_gpu.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace sm::character {
namespace {

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
    return paperdoll_frame_key(descriptor, animation);
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
            sm::ui::destroy_ui_texture(textures_[idx].texture.tex);
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
    texture.tex = sm::ui::create_ui_texture(texture.w, texture.h, pixels.data(),
                                             /*linear=*/false);

    const std::size_t idx = firstFree < textures_.size() ? firstFree : start;
    if (textures_[idx].occupied && textures_[idx].texture.tex) {
        sm::ui::destroy_ui_texture(textures_[idx].texture.tex);
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
            sm::ui::destroy_ui_texture(entry.texture.tex);
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
    return compose_paperdoll_rgba8(atlas_, atlasPixels_.data(), atlasW_, atlasH_,
                                   descriptor, animation, outPixels);
}

} // namespace sm::character

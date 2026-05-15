#pragma once

#include "assets/character_paperdoll.h"
#include "gl/gl.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sm::character {

struct CharacterTexture {
    GLuint tex = 0;
    int w = 0;
    int h = 0;
};

class CharacterTextureCache {
public:
    const CharacterDescriptor& descriptor_for_seed(std::uint32_t seed,
                                                   AppearancePreset preset = AppearancePreset::None);
    const CharacterTexture* texture_for(const CharacterDescriptor& descriptor,
                                        const AnimationState& animation);
    void destroy();
    bool atlas_loaded() const { return loaded_; }

private:
    bool load_assets();
    bool compose_rgba8(const CharacterDescriptor& descriptor,
                       const AnimationState& animation,
                       std::uint8_t* outPixels);

    struct DescriptorEntry {
        std::uint32_t seed = 0;
        AppearancePreset preset = AppearancePreset::None;
        CharacterDescriptor descriptor{};
        bool occupied = false;
    };

    static constexpr std::size_t kDescriptorCapacity = 1024;
    static constexpr std::size_t kTextureCapacity = 4096;
    struct TextureEntry {
        std::uint64_t key = 0;
        CharacterDescriptor descriptor{};
        AnimationType animation = AnimationType::Idle;
        Direction direction = Direction::Front;
        std::uint8_t frame = 0;
        CharacterTexture texture{};
        bool occupied = false;
    };
    static bool texture_entry_matches(const TextureEntry& entry,
                                      const CharacterDescriptor& descriptor,
                                      const AnimationState& animation);

    AtlasData atlas_;
    std::vector<std::uint8_t> atlasPixels_;
    std::array<DescriptorEntry, kDescriptorCapacity> descriptors_{};
    std::array<TextureEntry, kTextureCapacity> textures_{};
    int atlasW_ = 0;
    int atlasH_ = 0;
    bool loadAttempted_ = false;
    bool loaded_ = false;
};

} // namespace sm::character

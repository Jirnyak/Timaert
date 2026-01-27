#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sokol_gfx.h"

#include "core/types.h"

// Forward declaration
struct GameContext;
std::string resolve_path(const GameContext& ctx, std::string_view relative);

inline constexpr std::size_t TILE_TEXTURE_COUNT = static_cast<std::size_t>(TerrainType::Count);
inline constexpr std::size_t SPRITE_TEXTURE_COUNT = static_cast<std::size_t>(ObjectType::Count);
inline constexpr std::size_t ITEM_TEXTURE_COUNT = static_cast<std::size_t>(ItemType::Count);
inline constexpr std::size_t BACKGROUND_TEXTURE_COUNT = 1;

struct Texture {
    sg_image image = {0};
    sg_view view = {0};  // Texture view for sgl_texture
    sg_sampler sampler = {0};
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // Raw pixel data for CPU operations

    [[nodiscard]] bool valid() const noexcept {
        return image.id != SG_INVALID_ID;
    }

    void destroy() {
        if (view.id != SG_INVALID_ID) {
            sg_destroy_view(view);
            view = {0};
        }
        if (image.id != SG_INVALID_ID) {
            sg_destroy_image(image);
            image = {0};
        }
        if (sampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(sampler);
            sampler = {0};
        }
        pixels.clear();
        width = 0;
        height = 0;
    }
};

// Load texture from file using stb_image
[[nodiscard]] Texture load_texture(const std::string& path);

// Create texture from raw RGBA pixel data
[[nodiscard]] Texture create_texture_from_pixels(const std::uint8_t* pixels,
                                                  int width,
                                                  int height);

// Create a dynamic texture that can be updated
[[nodiscard]] Texture create_dynamic_texture(int width, int height);

// Update dynamic texture with new pixel data
void update_texture(Texture& texture, const std::uint8_t* pixels, int width, int height);

class TextureManager {
private:
    std::array<Texture, TILE_TEXTURE_COUNT> tile_textures_{};
    std::array<Texture, SPRITE_TEXTURE_COUNT> sprite_textures_{};
    std::array<Texture, ITEM_TEXTURE_COUNT> item_textures_{};
    std::array<Texture, BACKGROUND_TEXTURE_COUNT> background_textures_{};
    Texture heatmap_texture_{};
    
    // For tile_background compatibility
    int tile_bg_x_ = 0;
    int tile_bg_y_ = 0;
    int tile_bg_w_ = 0;
    int tile_bg_h_ = 0;

    // Shared sampler for nearest-neighbor filtering (pixel art)
    sg_sampler nearest_sampler_ = {0};

public:
    TextureManager() = default;

    ~TextureManager() {
        cleanup();
    }

    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    TextureManager(TextureManager&&) = delete;
    TextureManager& operator=(TextureManager&&) = delete;

    void init(int window_width, int window_height, const GameContext& ctx);
    void cleanup() noexcept;

    [[nodiscard]] const Texture& tile(TerrainType t) const noexcept {
        return tile_textures_[static_cast<std::size_t>(t)];
    }
    [[nodiscard]] const Texture& sprite(std::size_t idx) const noexcept {
        return sprite_textures_[idx];
    }
    [[nodiscard]] const Texture& item(ItemType type) const noexcept {
        return item_textures_[static_cast<std::size_t>(type)];
    }
    [[nodiscard]] const Texture& bg(std::size_t idx) const noexcept {
        return background_textures_[idx];
    }
    [[nodiscard]] Texture& heatmap() noexcept {
        return heatmap_texture_;
    }
    [[nodiscard]] const Texture& heatmap() const noexcept {
        return heatmap_texture_;
    }

    [[nodiscard]] sg_sampler get_nearest_sampler() const noexcept {
        return nearest_sampler_;
    }

    void set_tile_background_size(int w, int h) noexcept {
        tile_bg_w_ = w;
        tile_bg_h_ = h;
    }
};

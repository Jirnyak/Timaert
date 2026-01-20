#pragma once

#include <SDL.h>
#include <SDL_image.h>
#include <array>
#include "game_context.h"

enum class ObjectType : std::uint8_t
{
    City = 0,
    Tree = 1,
    Band = 2,
    Village = 3,
    Town = 4,
    Player = 5,
    Peasant = 6,
    Merchant = 7,
    Caravan = 8,
    Bandit = 9,
    Guard = 10,
    Door = 11,
    Count
};

inline constexpr std::size_t TILE_TEXTURE_COUNT = static_cast<std::size_t>(TerrainType::Count);
inline constexpr std::size_t SPRITE_TEXTURE_COUNT = static_cast<std::size_t>(ObjectType::Count);
inline constexpr std::size_t BACKGROUND_TEXTURE_COUNT = 1;

class TextureManager
{
private:
    std::array<SDL_Texture*, TILE_TEXTURE_COUNT> tile_textures_{};
    std::array<SDL_Texture*, SPRITE_TEXTURE_COUNT> sprite_textures_{};
    std::array<SDL_Texture*, BACKGROUND_TEXTURE_COUNT> background_textures_{};
    SDL_Texture* heatmap_texture_ = nullptr;
    SDL_Rect tile_background_{};
    
public:
    TextureManager() = default;
    
    ~TextureManager()
    {
        cleanup();
    }
    
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;
    TextureManager(TextureManager&&) = delete;
    TextureManager& operator=(TextureManager&&) = delete;
    
    void init(SDL_Renderer* renderer, int window_width, int window_height, const GameContext& ctx)
    {
        tile_background_.w = window_width;
        tile_background_.h = window_height;
        tile_background_.x = 0;
        tile_background_.y = 0;

        auto load_texture = [&ctx, renderer](const char* path) {
            return IMG_LoadTexture(renderer, resolve_path(ctx, path).c_str());
        };

        const std::array<const char*, TILE_TEXTURE_COUNT> tile_paths = {
            "sprites/dirt.png",   // TerrainType::Nothing
            "sprites/sand.png",   // TerrainType::Sand
            "sprites/grass.png",  // TerrainType::Grass
            "sprites/dirt.png",   // TerrainType::Dirt
            "sprites/mount.png",  // TerrainType::Mount
            "sprites/water.png",  // TerrainType::Water
            "sprites/snow.png",    // TerrainType::Snow
            "sprites/jungle.png",  // TerrainType::Jungle
            "sprites/swamp.png",   // TerrainType::Swamp
            "sprites/tundra.png"   // TerrainType::Tundra
        };

        for (std::size_t i = 0; i < tile_paths.size(); ++i) {
            tile_textures_[i] = load_texture(tile_paths[i]);
        }

        std::array<const char*, SPRITE_TEXTURE_COUNT> sprite_paths{};
        sprite_paths[static_cast<std::size_t>(ObjectType::Tree)] = "sprites/tree.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::City)] = "sprites/city.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::Village)] = "sprites/city.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::Town)] = "sprites/city.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::Player)] = "sprites/player.png";
        //sprite_paths[static_cast<std::size_t>(ObjectType::Witch)] = "sprites/ngirl1.png"; 
        sprite_paths[static_cast<std::size_t>(ObjectType::Peasant)] = "sprites/peasant.png"; 
        sprite_paths[static_cast<std::size_t>(ObjectType::Merchant)] = "sprites/peasant.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::Caravan)] = "sprites/corovan.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::Bandit)] = "sprites/ngirl1.png.png"; //witch sprite test
        sprite_paths[static_cast<std::size_t>(ObjectType::Guard)] = "sprites/peasant.png";
        sprite_paths[static_cast<std::size_t>(ObjectType::Door)] = "sprites/door.png";

        for (std::size_t i = 0; i < sprite_paths.size(); ++i) {
            if (sprite_paths[i]) {
                sprite_textures_[i] = load_texture(sprite_paths[i]);
            }
        }

        const std::array<const char*, BACKGROUND_TEXTURE_COUNT> background_paths = {
            "backgrounds/0.png"
        };

        for (std::size_t i = 0; i < background_paths.size(); ++i) {
            background_textures_[i] = load_texture(background_paths[i]);
        }
        
        heatmap_texture_ = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING,
            WORLD_WIDTH,
            WORLD_WIDTH
        );
    }
    
    void cleanup() noexcept
    {
        for (auto& tex : tile_textures_) {
            if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        }
        for (auto& tex : sprite_textures_) {
            if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        }
        for (auto& tex : background_textures_) {
            if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        }
        if (heatmap_texture_) { SDL_DestroyTexture(heatmap_texture_); heatmap_texture_ = nullptr; }
    }
    
    [[nodiscard]] SDL_Texture* tile(TerrainType t) const noexcept { return tile_textures_[static_cast<std::size_t>(t)]; }
    [[nodiscard]] SDL_Texture* sprite(std::size_t idx) const noexcept { return sprite_textures_[idx]; }
    [[nodiscard]] SDL_Texture* bg(std::size_t idx) const noexcept { return background_textures_[idx]; }
    [[nodiscard]] SDL_Texture* heatmap() const noexcept { return heatmap_texture_; }
    [[nodiscard]] const SDL_Rect& tile_background() const noexcept { return tile_background_; }
    [[nodiscard]] SDL_Rect& tile_background() noexcept { return tile_background_; }
};

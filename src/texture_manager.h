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
    Count
};

inline constexpr std::size_t TEXTURE_ARRAY_SIZE = 100;

class TextureManager
{
private:
    std::array<SDL_Texture*, TEXTURE_ARRAY_SIZE> tile_texture_{};
    std::array<SDL_Texture*, TEXTURE_ARRAY_SIZE> sprite_texture_{};
    std::array<SDL_Texture*, TEXTURE_ARRAY_SIZE> background_{};
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
        
        tile_texture_[static_cast<std::size_t>(TerrainType::Nothing)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/dirt.png").c_str());
        tile_texture_[static_cast<std::size_t>(TerrainType::Sand)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/sand.png").c_str());
        tile_texture_[static_cast<std::size_t>(TerrainType::Grass)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/grass.png").c_str());
        tile_texture_[static_cast<std::size_t>(TerrainType::Dirt)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/dirt.png").c_str());
        tile_texture_[static_cast<std::size_t>(TerrainType::Mount)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/mount.png").c_str());
        tile_texture_[static_cast<std::size_t>(TerrainType::Water)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/water.png").c_str());
        
        sprite_texture_[0] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/player.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Tree)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/tree.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::City)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/city.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Village)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/city.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Town)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/city.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Player)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/player.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Peasant)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/peasant.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Merchant)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/peasant.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Caravan)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/corovan.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Bandit)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/peasant.png").c_str());
        sprite_texture_[static_cast<std::size_t>(ObjectType::Guard)] = IMG_LoadTexture(renderer, resolve_path(ctx, "sprites/peasant.png").c_str());
        
        background_[0] = IMG_LoadTexture(renderer, resolve_path(ctx, "backgrounds/0.png").c_str());
        
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
        for (auto& tex : tile_texture_) {
            if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        }
        for (auto& tex : sprite_texture_) {
            if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        }
        for (auto& tex : background_) {
            if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
        }
        if (heatmap_texture_) { SDL_DestroyTexture(heatmap_texture_); heatmap_texture_ = nullptr; }
    }
    
    [[nodiscard]] SDL_Texture* tile(TerrainType t) const noexcept { return tile_texture_[static_cast<std::size_t>(t)]; }
    [[nodiscard]] SDL_Texture* sprite(std::size_t idx) const noexcept { return sprite_texture_[idx]; }
    [[nodiscard]] SDL_Texture* bg(std::size_t idx) const noexcept { return background_[idx]; }
    [[nodiscard]] SDL_Texture* heatmap() const noexcept { return heatmap_texture_; }
    [[nodiscard]] const SDL_Rect& tile_background() const noexcept { return tile_background_; }
    [[nodiscard]] SDL_Rect& tile_background() noexcept { return tile_background_; }
};

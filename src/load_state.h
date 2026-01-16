#pragma once

#include "game_state.h"
#include <fstream>

class LoadState : public GameState
{
public:
    void handle_event(SDL_Event& /*event*/, GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        entities.init_pool();
        
        load_array("field.dat", ctx.field.get(), WORLD_SIZE);
        entities.load("objects.dat");

        generate_terrain_map(ctx);
        
        ctx.world_image.reset(img_mapo(ctx.renderer, ctx.world_image.release(), ctx.world_map.get(), WORLD_WIDTH));

        ctx.game_mod = GameMode::Game;
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx.renderer);
        render_text(ctx.renderer, ctx.font.get(), "Loading...", 
                    ctx.window_width / 2 - 50, ctx.window_height / 2, 100, 30, {255, 255, 255, 255});
    }
    
private:
    void generate_terrain_map(GameContext& ctx)
    {
        for (std::size_t i = 0; i < WORLD_SIZE; i++)
        {
            if (ctx.field[i] < 0.4f) 
            {
                ctx.relief[i] = TerrainType::Water;
                ctx.world_map[i] = {0, 0, 255};
            }
            else if (ctx.field[i] < 0.45f) 
            {
                ctx.relief[i] = TerrainType::Sand;
                ctx.world_map[i] = {255, 255, 0};
            }
            else if (ctx.field[i] < 0.8f) 
            {
                const int drop = randomer(ctx.rng, 1);
                if (drop == 0)
                {
                    ctx.relief[i] = TerrainType::Dirt;
                    ctx.world_map[i] = {128, 255, 0};
                }
                else
                {
                    ctx.relief[i] = TerrainType::Grass;
                    ctx.world_map[i] = {0, 255, 0};
                }
            }
            else
            {
                ctx.relief[i] = TerrainType::Mount;
                ctx.world_map[i] = {128, 128, 128};
            }
        }
    }
    
    template<typename T>
    static void load_array(const std::string& filename, T* arr, std::size_t size) 
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in) return;
        in.read(reinterpret_cast<char*>(arr), static_cast<std::streamsize>(sizeof(T) * size));
    }
};

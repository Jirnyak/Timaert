#pragma once

#include "game_state.h"
#include "ui.h"
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
        
        load_array(resolve_path(ctx, "field.dat"), ctx.field.get(), WORLD_SIZE);
        entities.load(resolve_path(ctx, "objects.dat"));

        build_terrain_map(ctx);
        
        ctx.world_image.reset(update_map_texture(ctx.renderer, ctx.world_image.release(), ctx.world_map.get(), WORLD_WIDTH));

        ctx.game_mod = GameMode::Game;
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear(ctx.renderer, ui_color(0, 0, 0, 255));
        render_text(ctx.renderer, ctx.font.get(), "Loading...", 
                    ctx.window_width / 2 - 50, ctx.window_height / 2, 100, 30, {255, 255, 255, 255});
    }
    
private:
    
    template<typename T>
    static void load_array(const std::string& filename, T* arr, std::size_t size) 
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in) return;
        in.read(reinterpret_cast<char*>(arr), static_cast<std::streamsize>(sizeof(T) * size));
    }
};

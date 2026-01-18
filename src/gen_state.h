#pragma once

#include "game_state.h"
#include "tergen.h"
#include "ui.h"
#include "world_manager.h"
#include <fstream>

class GenState : public GameState
{
public:
    WorldManager* world_manager = nullptr;
    
    void set_world_manager(WorldManager* wm) { world_manager = wm; }
    
    void handle_event(SDL_Event& /*event*/, GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        ctx.seed = random_u32_inclusive(ctx.rng, 10000);
        generateUniversalField(ctx.field.get(), ctx.temp.get(), WORLD_WIDTH,
            6,      // octaves
            64,     // diffusion steps
            0.25f,  // base diffusion
            0.1f,   // base noise
            ctx.seed
        );
        normalize01(ctx.field.get(), WORLD_SIZE);

        save_array(resolve_path(ctx, "field.dat"), ctx.field.get(), WORLD_SIZE);

        build_terrain_map(ctx);
        
        ctx.world_image.reset(update_map_texture(ctx.renderer, ctx.world_image.release(), ctx.world_map.get(), WORLD_WIDTH));

        entities.init_pool();

        int checker = 0;
        while (checker < MAX_OBJECTS)
        {
            const auto drop = static_cast<int>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_SIZE - 1)));
            if (ctx.relief[drop] == TerrainType::Grass || ctx.relief[drop] == TerrainType::Dirt)
            {
                [[maybe_unused]] auto* e = entities.new_entity(static_cast<int>(ObjectType::Tree), drop);
                checker++;
            }
        }

        if (world_manager)
        {
            world_manager->init();
            world_manager->generate_settlements(ctx);
            world_manager->spawn_initial_npcs(ctx);
            world_manager->init_player(ctx);
            world_manager->rebuild_pos_map(ctx.pos_map);
        }

        entities.rebuild_pos_map(ctx.pos_map);
        entities.save(resolve_path(ctx, "objects.dat"));

        ctx.game_mod = GameMode::Game;
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear(ctx.renderer, ui_color(0, 0, 0, 255));
        render_text(ctx.renderer, ctx.font.get(), "Generating world...", 
                    ctx.window_width / 2 - 100, ctx.window_height / 2, 200, 30, {255, 255, 255, 255});
    }
    
private:
    
    template<typename T>
    static void save_array(const std::string& filename, const T* arr, std::size_t size) 
    {
        std::ofstream out(filename, std::ios::binary);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(arr), static_cast<std::streamsize>(sizeof(T) * size));
    }
};

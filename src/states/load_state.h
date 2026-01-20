#pragma once

#include "core/game_state.h"
#include "systems/save_game.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include <algorithm>

class LoadState : public GameState
{
public:
    WorldManager* world_manager = nullptr;

    void set_world_manager(WorldManager* wm) { world_manager = wm; }

    void handle_event(SDL_Event& /*event*/, GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        entities.init_pool();
        if (world_manager) {
            world_manager->init();
        }

        const bool loaded = world_manager ? save_game::read_save(ctx, entities, *world_manager) : false;
        if (loaded) {
            ctx.world_image.reset(update_map_texture(ctx.renderer, ctx.world_image.release(), ctx.world_map.get(), WORLD_WIDTH));

            std::fill(ctx.pos_map.begin(), ctx.pos_map.end(), 0);
            if (world_manager) {
                world_manager->rebuild_pos_map(ctx.pos_map);
                const Player& player = world_manager->player_ctrl.player();
                if (player.active) {
                    ctx.pos_cam = player.pos;
                }
            }
            entities.rebuild_pos_map(ctx.pos_map, false);
        }

        ctx.game_mod = GameMode::Game;
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear(ctx.renderer, ui_color("#000000"));
        render_text(ctx.renderer, ctx.font.get(), "Loading...", 
                    ctx.window_width / 2 - 50, ctx.window_height / 2, 100, 30, {255, 255, 255, 255});
    }
    
private:
};

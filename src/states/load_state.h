#pragma once

#include "core/game_state.h"
#include "systems/save_game.h"
#include "ui/ui.h"
#include "systems/world_manager.h"

class LoadState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Load; }

    void handle_event(SDL_Event& /*event*/, GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& entities) override
    {
        entities.init_pool();
        if (ctx.world_manager) {
            ctx.world_manager->init();
        }

        const bool loaded = ctx.world_manager ? save_game::read_save(ctx, entities, *ctx.world_manager) : false;
        if (loaded) {
            ctx.world_image.reset(update_map_texture(ctx.renderer, ctx.world_image.release(), ctx.world_map.data(), WORLD_WIDTH));

            ctx.pos_map.fill(0);
            if (ctx.world_manager) {
                ctx.world_manager->rebuild_pos_map(ctx.pos_map);
                const Player& player = ctx.world_manager->player_ctrl.player();
                if (player.active) {
                    ctx.pos_cam = player.pos;
                }
            }
            entities.rebuild_pos_map(ctx.pos_map, false);
        }

        if (!loaded) {
            clear_states(ctx);
        }
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear_black(ctx.renderer);
        render_text(ctx, "Loading...", 
                    ctx.window_width / 2 - 50, ctx.window_height / 2, 100, 30, {255, 255, 255, 255});
    }
    
private:
};

inline StateRegistrar<LoadState> register_load_state_{GameMode::Load};

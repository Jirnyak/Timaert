#pragma once

#include "core/game_state.h"
#include "systems/save_game.h"
#include "ui/ui.h"
#include "systems/world_manager.h"

class LoadState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Load; }

    void handle_event(SDL_Event& /*event*/, GameContext& /*ctx*/, TextureManager& /*textures*/) override
    {
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/) override
    {
        SDL_Log("LOAD: Starting load process...");
        
        if (ctx.world_manager) {
            ctx.world_manager->init();
        }

        const bool loaded = ctx.world_manager ? save_game::read_save(ctx, *ctx.world_manager) : false;
        
        if (loaded) {
            SDL_Log("LOAD: Save file loaded successfully");
            ctx.world_image.reset(update_map_texture(ctx.renderer, ctx.world_image.release(), ctx.world_map.data(), WORLD_WIDTH));

            ctx.pos_map.fill(0);
            if (ctx.world_manager) {
                ctx.world_manager->rebuild_pos_map(ctx.pos_map);
                const Player& player = ctx.world_manager->player_ctrl.player();
                if (player.active) {
                    ctx.pos_cam = player.pos;
                }
                
                // TODO: ECS serialization - for now, regenerate NPCs on load
                // Trees and NPCs need to be saved/loaded via ECS serialization
                if (ctx.ecs_world) {
                    // Spawn initial NPCs directly to ECS
                    ctx.world_manager->spawn_initial_npcs(ctx);
                    SDL_Log("LOAD: Spawned NPCs to ECS");
                }
            }
            // Note: read_save() already cleared and rebuilt ctx.state_stack from the save file,
            // so LoadState is implicitly gone. No need to pop - just request redraw.
            ctx.redraw_requested = true;
            SDL_Log("LOAD: Transitioned to game (stack size: %zu)", ctx.state_stack.size());
        } else {
            SDL_Log("LOAD: Failed to load save file (file may not exist or version mismatch)");
            // Return to menu on failure
            clear_states(ctx);
            push_state(ctx, StateRegistry::instance().create(GameMode::Menu));
            ctx.redraw_requested = true;
        }
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/) override
    {
        ui_clear_black(ctx.renderer);
        render_text(ctx, "Loading...", 
                    ctx.window_width / 2 - 50, ctx.window_height / 2, 100, 30, {255, 255, 255, 255});
    }
    
private:
};

inline StateRegistrar<LoadState> register_load_state_{GameMode::Load};

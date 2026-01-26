#pragma once

#include "core/game_state.h"
#include "rendering/renderer.h"
#include "systems/save_game.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include <print>

class LoadState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Load;
    }

    void handle_event(GameContext& /*ctx*/, TextureManager& /*textures*/) override {}

    void update(GameContext& ctx, TextureManager& /*textures*/) override {
        std::println("[LOAD] Starting load process...");

        if (ctx.world_manager) {
            ctx.world_manager->init();
        }

        const bool loaded =
            ctx.world_manager ? save_game::read_save(ctx, *ctx.world_manager) : false;

        if (loaded) {
            std::println("[LOAD] Save file loaded successfully");
            // World texture update - TODO: implement with Sokol

            ctx.pos_map.fill(0);
            if (ctx.world_manager) {
                ctx.world_manager->rebuild_pos_map(ctx.pos_map);
                const Player& player = ctx.world_manager->player_ctrl.player();
                if (player.active) {
                    ctx.pos_cam = player.pos;
                }

                if (ctx.ecs_world) {
                    ctx.world_manager->spawn_initial_npcs(ctx);
                    std::println("[LOAD] Spawned NPCs to ECS");
                }
            }
            ctx.redraw_requested = true;
            std::println("[LOAD] Transitioned to game (stack size: {})", ctx.state_stack.size());
        } else {
            std::println("[LOAD] Failed to load save file (file may not exist or version mismatch)");
            clear_states(ctx);
            push_state(ctx, StateRegistry::instance().create(GameMode::Menu));
            ctx.redraw_requested = true;
        }
    }

    void render(GameContext& ctx, TextureManager& /*textures*/) override {
        ui_clear_black();
        // Scale factor based on window size (baseline: 720p height)
        const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
        const int font_size = static_cast<int>(30 * scale);
        const int text_width = static_cast<int>(100 * scale);
        render_text(ctx,
                    "Loading...",
                    ctx.window_width / 2 - text_width / 2,
                    ctx.window_height / 2,
                    text_width,
                    font_size,
                    {255, 255, 255, 255});
    }

private:
};

inline StateRegistrar<LoadState> register_load_state_{GameMode::Load};

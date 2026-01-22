#pragma once

#include "core/game_context.h"
#include "rendering/texture_manager.h"
#include "systems/entity_manager.h"

class GameState
{
public:
    virtual ~GameState() = default;
    virtual void update(GameContext& ctx, TextureManager& textures, EntityManager& entities) = 0;
    virtual void render(GameContext& ctx, TextureManager& textures, EntityManager& entities) = 0;
    virtual void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& textures, EntityManager& entities) = 0;
    [[nodiscard]] virtual GameMode mode() const noexcept = 0;
    [[nodiscard]] virtual bool is_overlay() const noexcept { return false; }
};

[[nodiscard]] inline GameMode current_game_mode(const GameContext& ctx) noexcept
{
    GameState* state = current_state(ctx);
    if (!state) return GameMode::Menu;
    return state->mode();
}

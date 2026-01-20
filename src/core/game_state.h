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
};

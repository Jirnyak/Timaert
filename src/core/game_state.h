#pragma once

#include "core/game_context.h"
#include "rendering/texture_manager.h"

class BinaryWriter;
class BinaryReader;

class GameState {
public:
    GameState() = default;
    virtual ~GameState() = default;
    
    // Rule of Five - prevent copying and moving
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) = delete;
    GameState& operator=(GameState&&) = delete;
    
    virtual void update(GameContext& ctx, TextureManager& textures) = 0;
    virtual void render(GameContext& ctx, TextureManager& textures) = 0;
    [[nodiscard]] virtual GameMode mode() const noexcept = 0;
    [[nodiscard]] virtual bool is_overlay() const noexcept {
        return false;
    }

    // Serialization interface - override in states that need to save data
    [[nodiscard]] virtual bool can_save() const noexcept {
        return true;
    }
    [[nodiscard]] virtual GameMode fallback_mode() const noexcept {
        return GameMode::Game;
    }
    virtual void save_state(BinaryWriter& /*writer*/) const {}
    virtual void load_state(BinaryReader& /*reader*/) {}
};

[[nodiscard]] inline GameMode current_game_mode(const GameContext& ctx) noexcept {
    const GameState* state = current_state(ctx);
    if (!state)
        return GameMode::Menu;
    return state->mode();
}

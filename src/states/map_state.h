#pragma once

#include "core/game_state.h"
#include "ui/ui_events.h"
#include <SDL_render.h>

class MapState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Map;
    }

private:
    InputManager input_manager_;
    enum class MapMode : std::uint8_t {
        World = 0,
        Iron = 1,
        Clay = 2,
        Fertility = 3,
        Politics = 4,
        Count
    };

    MapMode mode_ = MapMode::World;
    SDL_Texture* resource_texture_ = nullptr;  // lazy-built texture for resource maps

public:
    ~MapState();
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& textures) override;
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;

private:
    void render_politics_map(GameContext& ctx, const SDL_Rect& ui) const noexcept;
};

inline StateRegistrar<MapState> register_map_state_{GameMode::Map};

#pragma once

#include "core/game_state.h"
#include "ui/ui_events.h"
#include "core/game_context.h"
#include "core/types.h"
#include "entt/entt.hpp"
#include "rendering/texture_manager.h"

struct Rect;

class MapState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Map;
    }

    MapState() = default;
    MapState(const MapState&) = delete;
    MapState& operator=(const MapState&) = delete;
    MapState(const MapState&&) = delete;
    MapState& operator=(const MapState&&) = delete;
    ~MapState() override;
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;

private:
    InputManager input_manager_;
    enum class MapMode : std::uint8_t {
        World,
        Iron,
        Clay,
        Fertility,
        Politics,
        Count
    };

    MapMode mode_ = MapMode::World;
    Texture map_texture_{};  // Texture for world map rendering
    bool texture_dirty_ = true;

    static void render_politics_map(GameContext& ctx, const Rect& ui) noexcept;
    void rebuild_map_texture(GameContext& ctx);
};

inline StateRegistrar<MapState> register_map_state_{GameMode::Map};

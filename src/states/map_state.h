#pragma once

#include "core/game_state.h"
#include "rendering/tile_view.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class MapState : public GameState
{
private:
    InputManager input_manager_;
    enum class MapMode : std::uint8_t {
        World = 0,
        Iron = 1,
        Clay = 2,
        Count
    };

    MapMode mode_ = MapMode::World;
    SDL_Texture* resource_texture_ = nullptr; // lazy-built texture for resource maps

public:
    ~MapState()
    {
        if (resource_texture_) SDL_DestroyTexture(resource_texture_);
    }
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            switch (evt.action)
            {
                case InputAction::Press:
                    begin_map_drag(ctx);
                    break;
                
                case InputAction::Drag:
                    apply_map_drag(ctx, static_cast<float>(evt.dx), static_cast<float>(evt.dy));
                    break;

                case InputAction::Release:
                    end_map_drag(ctx);
                    break;
                    
                default: break;
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            switch(event.key.keysym.sym)
            {
                case SDLK_ESCAPE:
                    enter_pause(ctx);
                    break;
                case SDLK_0:
                    handle_fullscreen_key(ctx, event.key.keysym.sym);
                    break;
                case SDLK_RETURN:
                    pop_state(ctx);
                    reset_map_view(ctx);
                    break;
                case SDLK_k:
                    trigger_screenshot(ctx);
                    break;
                case SDLK_RIGHT:
                case SDLK_LEFT:
                {
                    // cycle map mode with left/right arrows
                    const int dir = (event.key.keysym.sym == SDLK_RIGHT) ? 1 : -1;
                    int next = static_cast<int>(mode_) + dir;
                    if (next < 0) next = static_cast<int>(MapMode::Count) - 1;
                    if (next >= static_cast<int>(MapMode::Count)) next = 0;
                    mode_ = static_cast<MapMode>(next);
                    ctx.redraw_requested = true;
                    break;
                }
                default:
                    break;
            }
        }
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        const float prev_offset_x = ctx.map_offset_x;
        const float prev_offset_y = ctx.map_offset_y;
        const float delta_time = calc_frame_delta_time(ctx);
        
        update_map_inertia(ctx, delta_time);

        if (ctx.map_dragging || ctx.velocity_x != 0.0f || ctx.velocity_y != 0.0f) {
            ctx.redraw_requested = true;
        }
        if (ctx.map_offset_x != prev_offset_x || ctx.map_offset_y != prev_offset_y) {
            ctx.redraw_requested = true;
        }
    }
    
    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        ui_clear_black(ctx.renderer);
        
        const int size = ctx.window_height;
        SDL_Rect ui = centered_rect(
            ctx,
            size,
            size,
            static_cast<int>(ctx.map_offset_x),
            static_cast<int>(ctx.map_offset_y));
        
        if (mode_ == MapMode::World)
        {
            SDL_RenderCopy(ctx.renderer, ctx.world_image.get(), nullptr, &ui);
            // Render text label on left
            render_text(ctx, "MAP", 20, 20, 100, 32, {200, 200, 200, 255});
        }
        else if (mode_ == MapMode::Iron)
        {
            // build or update iron texture when requested
            if (!resource_texture_ || ctx.redraw_requested)
            {
                if (resource_texture_) SDL_DestroyTexture(resource_texture_);
                resource_texture_ = SDL_CreateTexture(ctx.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WORLD_WIDTH, WORLD_WIDTH);
                if (resource_texture_)
                {
                    void* texPixels = nullptr;
                    int pitch = 0;
                    if (SDL_LockTexture(resource_texture_, nullptr, &texPixels, &pitch) == 0)
                    {
                        const std::uint8_t* map = ctx.resource_iron.get();
                        for (int y = 0; y < WORLD_WIDTH; ++y)
                        {
                            auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
                            for (int x = 0; x < WORLD_WIDTH; ++x)
                            {
                                const std::size_t idx = static_cast<std::size_t>(x) * WORLD_WIDTH + static_cast<std::size_t>(y);
                                const int v = map ? static_cast<int>(map[idx]) : 0;
                                std::uint8_t r,g,b;
                                if (v <= 0) { r = 0; g = 0; b = 0; }
                                else {
                                    const double f = static_cast<double>(v) / 255.0;
                                    r = static_cast<std::uint8_t>(std::min(255, 120 + static_cast<int>(135.0 * f)));
                                    g = static_cast<std::uint8_t>(std::min(255, 60 + static_cast<int>(80.0 * f)));
                                    b = static_cast<std::uint8_t>(std::min(255, 30 + static_cast<int>(60.0 * f)));
                                }
                                row[x] = (255u << 24) | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
                            }
                        }
                        SDL_UnlockTexture(resource_texture_);
                    }
                }
            }
            if (resource_texture_)
                SDL_RenderCopy(ctx.renderer, resource_texture_, nullptr, &ui);
            render_text(ctx, "IRON", 20, 20, 100, 32, {200, 200, 200, 255});
        }
        else if (mode_ == MapMode::Clay)
        {
            // build or update clay texture when requested
            if (!resource_texture_ || ctx.redraw_requested)
            {
                if (resource_texture_) SDL_DestroyTexture(resource_texture_);
                resource_texture_ = SDL_CreateTexture(ctx.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WORLD_WIDTH, WORLD_WIDTH);
                if (resource_texture_)
                {
                    void* texPixels = nullptr;
                    int pitch = 0;
                    if (SDL_LockTexture(resource_texture_, nullptr, &texPixels, &pitch) == 0)
                    {
                        const std::uint8_t* map = ctx.resource_clay.get();
                        for (int y = 0; y < WORLD_WIDTH; ++y)
                        {
                            auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
                            for (int x = 0; x < WORLD_WIDTH; ++x)
                            {
                                const std::size_t idx = static_cast<std::size_t>(x) * WORLD_WIDTH + static_cast<std::size_t>(y);
                                const int v = map ? static_cast<int>(map[idx]) : 0;
                                std::uint8_t r,g,b;
                                if (v <= 0) { r = 0; g = 0; b = 0; }
                                else {
                                    const double f = static_cast<double>(v) / 255.0;
                                    r = static_cast<std::uint8_t>(std::min(255, 150 + static_cast<int>(100.0 * f)));
                                    g = static_cast<std::uint8_t>(std::min(255, 110 + static_cast<int>(140.0 * f)));
                                    b = static_cast<std::uint8_t>(std::min(255, 70 + static_cast<int>(180.0 * f)));
                                }
                                row[x] = (255u << 24) | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
                            }
                        }
                        SDL_UnlockTexture(resource_texture_);
                    }
                }
            }
            if (resource_texture_)
                SDL_RenderCopy(ctx.renderer, resource_texture_, nullptr, &ui);
            render_text(ctx, "CLAY", 20, 20, 100, 32, {200, 200, 200, 255});
        }
    }
};

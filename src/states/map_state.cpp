#include "states/map_state.h"
#include "rendering/tile_view.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/politics.h"
#include "core/tile_map.h"
#include <SDL_blendmode.h>
#include <SDL_keycode.h>
#include <SDL_pixels.h>
#include <cstddef>
#include <string>

MapState::~MapState()
{
    if (resource_texture_) SDL_DestroyTexture(resource_texture_);
}

void MapState::handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/)
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
                if (current_game_mode(ctx) != GameMode::Pause)
                    push_state(ctx, StateRegistry::instance().create(GameMode::Pause));
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

void MapState::update(GameContext& ctx, TextureManager& /*textures*/)
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

void MapState::render(GameContext& ctx, TextureManager& /*textures*/)
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
    }
    else if (mode_ == MapMode::Politics)
    {
        render_politics_map(ctx, ui);
    }
    else
    {
        SDL_Texture* base_map = SDL_CreateTexture(ctx.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WORLD_WIDTH, WORLD_WIDTH);
        if (base_map)
        {
            void* texPixels = nullptr;
            int pitch = 0;
            if (SDL_LockTexture(base_map, nullptr, &texPixels, &pitch) == 0)
            {
                for (int y = 0; y < WORLD_WIDTH; ++y)
                {
                    auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
                    for (int x = 0; x < WORLD_WIDTH; ++x)
                    {
                        const TilePosition tile_pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
                        std::uint8_t gray = (ctx.relief[tile_pos] == TerrainType::Water) ? 255 : 0;
                        row[x] = (static_cast<std::uint32_t>(255) << 24) | 
                               (static_cast<std::uint32_t>(gray) << 16) | 
                               (static_cast<std::uint32_t>(gray) << 8) | 
                               static_cast<std::uint32_t>(gray);
                    }
                }
                SDL_UnlockTexture(base_map);
            }
            SDL_RenderCopy(ctx.renderer, base_map, nullptr, &ui);
            SDL_DestroyTexture(base_map);
        }
    }
    
    if (mode_ != MapMode::World)
    {
        const std::uint8_t* active_map = nullptr;
        if (mode_ == MapMode::Iron)
            active_map = ctx.resource_iron.data();
        else if (mode_ == MapMode::Clay)
            active_map = ctx.resource_clay.data();
        else if (mode_ == MapMode::Fertility)
            active_map = ctx.resource_fertility.data();
        
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
                    for (int y = 0; y < WORLD_WIDTH; ++y)
                    {
                        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
                        for (int x = 0; x < WORLD_WIDTH; ++x)
                        {
                            const std::size_t idx = static_cast<std::size_t>(y) * WORLD_WIDTH + static_cast<std::size_t>(x);
                            const int v = active_map ? static_cast<int>(active_map[idx]) : 0;
                            std::uint8_t r, g, b, a;
                            if (v <= 0) { 
                                a = 0; r = 0; g = 0; b = 0; 
                            }
                            else {
                                a = static_cast<std::uint8_t>(v);
                                if (mode_ == MapMode::Iron) {
                                    r = 0; g = 0; b = static_cast<std::uint8_t>(v);
                                } else if (mode_ == MapMode::Clay) {
                                    r = static_cast<std::uint8_t>(v); g = 0; b = 0;
                                } else {
                                    r = 0; g = static_cast<std::uint8_t>(v); b = 0;
                                }
                            }
                            row[x] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
                        }
                    }
                    SDL_UnlockTexture(resource_texture_);
                }
            }
        }

        if (resource_texture_)
        {
            SDL_SetTextureBlendMode(resource_texture_, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(ctx.renderer, resource_texture_, nullptr, &ui);
        }
    }
    
    if (mode_ == MapMode::World)
        render_text(ctx, "MAP", 20, 20, 100, 32, {200, 200, 200, 255});
    else if (mode_ == MapMode::Iron)
        render_text(ctx, "IRON", 20, 20, 100, 32, {200, 200, 200, 255});
    else if (mode_ == MapMode::Clay)
        render_text(ctx, "CLAY", 20, 20, 100, 32, {200, 200, 200, 255});
    else if (mode_ == MapMode::Fertility)
        render_text(ctx, "FERTILITY", 20, 20, 100, 32, {200, 200, 200, 255});
    else if (mode_ == MapMode::Politics)
        render_text(ctx, "POLITICS", 20, 20, 100, 32, {200, 200, 200, 255});
    
    render_text(ctx, "seed: " + std::to_string(ctx.seed), 20, 60, 200, 24, {150, 150, 150, 255});
}

void MapState::render_politics_map(GameContext& ctx, const SDL_Rect& ui) const noexcept
{
    SDL_Texture* politics_texture = SDL_CreateTexture(ctx.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WORLD_WIDTH, WORLD_WIDTH);
    if (!politics_texture) return;
    
    void* texPixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(politics_texture, nullptr, &texPixels, &pitch) != 0) {
        SDL_DestroyTexture(politics_texture);
        return;
    }
    
    for (int y = 0; y < WORLD_WIDTH; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(texPixels) + y * pitch);
        for (int x = 0; x < WORLD_WIDTH; ++x) {
            const TilePosition tile_pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            std::uint8_t owner_id = ctx.owner[tile_pos];
            const FactionID owner = static_cast<FactionID>(owner_id);
            
            std::uint8_t r = 50, g = 50, b = 50;
            
            if (ctx.world_manager) {
                const auto* faction = ctx.world_manager->politics.get_faction(owner);
                if (faction) {
                    r = faction->R;
                    g = faction->G;
                    b = faction->B;
                } else {
                    if (owner == FactionID::Neutral) { r = 25; g = 75; b = 155; }
                    else { r = 34; g = 139; b = 34; }
                }
            }
            
            const std::uint32_t a = (ctx.relief[tile_pos] == TerrainType::Water) ? 200 : 255;
            row[x] = (static_cast<std::uint32_t>(a) << 24) | 
                    (static_cast<std::uint32_t>(r) << 16) | 
                    (static_cast<std::uint32_t>(g) << 8) | 
                    static_cast<std::uint32_t>(b);
        }
    }
    
    SDL_UnlockTexture(politics_texture);
    SDL_RenderCopy(ctx.renderer, politics_texture, nullptr, &ui);
    SDL_DestroyTexture(politics_texture);
}

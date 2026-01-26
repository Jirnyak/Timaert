#include "states/map_state.h"
#include "sokol_time.h"
#include "rendering/tile_view.h"
#include "rendering/renderer.h"
#include "rendering/texture_manager.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/politics.h"
#include "core/tile_map.h"
#include "core/game_context.h"
#include <cstddef>
#include <string>
#include <vector>

MapState::~MapState() {
    map_texture_.destroy();
}

void MapState::handle_event(GameContext& ctx, TextureManager& /*textures*/) {
    // Event handling now done via Sokol callbacks
    (void)ctx;
}

void MapState::update(GameContext& ctx, TextureManager& /*textures*/) {
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

void MapState::rebuild_map_texture(GameContext& ctx) {
    constexpr int map_size = WORLD_WIDTH;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(map_size * map_size * 4));
    
    for (int y = 0; y < map_size; ++y) {
        for (int x = 0; x < map_size; ++x) {
            TilePosition pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            const auto& mp = ctx.world_map.at(pos);
            const std::size_t idx = static_cast<std::size_t>((y * map_size + x) * 4);
            pixels[idx + 0] = mp.R;
            pixels[idx + 1] = mp.G;
            pixels[idx + 2] = mp.B;
            pixels[idx + 3] = 255;
        }
    }
    
    if (map_texture_.valid()) {
        update_texture(map_texture_, pixels.data(), map_size, map_size);
    } else {
        map_texture_ = create_texture_from_pixels(pixels.data(), map_size, map_size);
    }
    texture_dirty_ = false;
}

void MapState::render(GameContext& ctx, TextureManager& /*textures*/) {
    ui_clear_black();

    // Rebuild texture if needed
    if (texture_dirty_ || !map_texture_.valid()) {
        rebuild_map_texture(ctx);
    }

    const int size = std::min(ctx.window_width, ctx.window_height);
    Rect ui = centered_rect(ctx,
                            size,
                            size,
                            static_cast<int>(ctx.map_offset_x),
                            static_cast<int>(ctx.map_offset_y));

    if (mode_ == MapMode::World && map_texture_.valid()) {
        render_texture(map_texture_, ui);
    } else if (mode_ == MapMode::Politics) {
        render_politics_map(ctx, ui);
    } else {
        // Fallback solid color
        render_fill_rect(ui, ui_color("#203050"));
    }

    // Mode label
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    const int font_size = static_cast<int>(18 * scale);
    const int padding = static_cast<int>(10 * scale);
    
    const char* mode_label = "World Map";
    switch (mode_) {
        case MapMode::Politics: mode_label = "Politics"; break;
        case MapMode::Iron: mode_label = "Iron Resources"; break;
        case MapMode::Clay: mode_label = "Clay Resources"; break;
        case MapMode::Fertility: mode_label = "Fertility"; break;
        default: break;
    }
    render_text(ctx, mode_label, padding, padding, static_cast<int>(150 * scale), font_size, {255, 255, 255, 255});
    render_text(ctx, "[ Press M/ESC to close ]", padding, ctx.window_height - padding - font_size, static_cast<int>(200 * scale), font_size, {150, 150, 150, 255});

    render_draw_rect(ui, ui_color("#FFFFFF"));
}

void MapState::render_politics_map(GameContext& ctx, const Rect& ui) const noexcept {
    // Simplified politics map rendering
    render_fill_rect(ui, ui_color("#304050"));
    (void)ctx;
}

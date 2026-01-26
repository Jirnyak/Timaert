#include "states/map_state.h"
#include "sokol_time.h"
#include "rendering/tile_view.h"
#include "rendering/renderer.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/politics.h"
#include "core/tile_map.h"
#include <cstddef>
#include <string>

MapState::~MapState() {
    // Texture cleanup handled by Texture::destroy()
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

void MapState::render(GameContext& ctx, TextureManager& /*textures*/) {
    ui_clear_black();

    const int size = ctx.window_height;
    Rect ui = centered_rect(ctx,
                            size,
                            size,
                            static_cast<int>(ctx.map_offset_x),
                            static_cast<int>(ctx.map_offset_y));

    if (mode_ == MapMode::World) {
        // World map rendering - TODO: implement with Sokol
        render_fill_rect(ui, ui_color("#203050"));
    } else if (mode_ == MapMode::Politics) {
        render_politics_map(ctx, ui);
    } else {
        // Resource map rendering - TODO: implement with Sokol
        render_fill_rect(ui, ui_color("#102030"));
    }

    // Mode label
    const char* mode_label = "World";
    switch (mode_) {
        case MapMode::Politics: mode_label = "Politics"; break;
        case MapMode::Iron: mode_label = "Iron"; break;
        case MapMode::Clay: mode_label = "Clay"; break;
        case MapMode::Fertility: mode_label = "Fertility"; break;
        default: break;
    }
    (void)mode_label; // TODO: render text

    render_draw_rect(ui, ui_color("#FFFFFF"));
}

void MapState::render_politics_map(GameContext& ctx, const Rect& ui) const noexcept {
    // Simplified politics map rendering
    render_fill_rect(ui, ui_color("#304050"));
    (void)ctx;
}

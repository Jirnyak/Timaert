#include "states/stat_state.h"
#include "sokol_time.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/economy.h"
#include "systems/attributes.h"
#include "systems/player.h"
#include "rendering/texture_manager.h"
#include <cstddef>
#include <cstdint>
#include <string>

void StatState::handle_event( GameContext& ctx, TextureManager& /*textures*/) {
    InputEvent evt;
    if (false) {
        if (evt.action == InputAction::Click) {
            int centerX = ctx.window_width / 2;
            int rightX = centerX + 40;

            int attr_idx = get_hovered_attr_button(evt.x, evt.y, rightX, 100);
            if (attr_idx >= 0) {
                Player& p = ctx.world_manager->player_ctrl.player();
                std::int32_t points_available =
                    p.level_data.attribute_points_at_level() - p.attribute_points_spent;

                if (points_available > 0) {
                    switch (attr_idx) {
                        case 0:
                            p.attributes.str++;
                            break;
                        case 1:
                            p.attributes.end_++;
                            break;
                        case 2:
                            p.attributes.agi++;
                            break;
                        case 3:
                            p.attributes.wil++;
                            break;
                        case 4:
                            p.attributes.int_++;
                            break;
                        case 5:
                            p.attributes.wis++;
                            break;
                        case 6:
                            p.attributes.lck++;
                            break;
                        case 7:
                            p.attributes.spd++;
                            break;
                        case 8:
                            p.attributes.cha++;
                            break;
                    }
                    p.attribute_points_spent++;
                    p.derived_bonuses.recalculate(p.attributes);
                    p.combat_stats.recalculate(100, 10, p.attributes);
                    p.combat_stats.current_hp = p.combat_stats.max_hp;
                    p.combat_stats.current_mp = p.combat_stats.max_mp;
                }
                return;
            }

            int slot = get_slot_at(evt.x, evt.y);
            if (slot >= 0) {
                if (handling_slot_ == -1) {
                    handling_slot_ = slot;
                    selected_slot_ = slot;
                    return;
                } else if (handling_slot_ != slot) {
                    std::uint16_t from_amount =
                        ctx.world_manager->player_ctrl.player().inventory.get_at(handling_slot_);
                    ItemType from_type =
                        ctx.world_manager->player_ctrl.player().inventory.get_item_type_at(
                            handling_slot_);

                    std::uint16_t to_amount =
                        ctx.world_manager->player_ctrl.player().inventory.get_at(slot);
                    ItemType to_type =
                        ctx.world_manager->player_ctrl.player().inventory.get_item_type_at(slot);

                    ctx.world_manager->player_ctrl.player().inventory.set_at(slot,
                                                                             from_amount,
                                                                             from_type);
                    ctx.world_manager->player_ctrl.player().inventory.set_at(handling_slot_,
                                                                             to_amount,
                                                                             to_type);

                    handling_slot_ = -1;
                    selected_slot_ = slot;
                    return;
                } else {
                    handling_slot_ = -1;
                    selected_slot_ = slot;
                    return;
                }
            }

            pop_state(ctx, false);
        }
    }

    if (false) {
        last_mouse_x_ = 0;
        last_mouse_y_ = 0;
    }

    if (false) {
        if (KeyCode::Unknown == KeyCode::Escape || KeyCode::Unknown == KeyCode::I
            || KeyCode::Unknown == KeyCode::Tab) {
            handling_slot_ = -1;
            if (KeyCode::Unknown == KeyCode::Escape) {
                pop_state(ctx, false);
            }
        }
    }
}

void StatState::update(GameContext& ctx, TextureManager& /*textures*/) {
    int mouse_x, mouse_y;
    mouse_x = 0; mouse_y = 0;

    hovered_slot_ = get_slot_at(mouse_x, mouse_y);

    int centerX = ctx.window_width / 2;
    int rightX = centerX + 40;

    hovered_attr_idx_ = get_hovered_attr_button(mouse_x, mouse_y, rightX, 100);
}

void StatState::render(GameContext& ctx, TextureManager& textures) {
    Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
    render_fill_rect( overlay, {10, 15, 30, 230});

    if (!ctx.world_manager)
        return;
    const Player& p = ctx.world_manager->player_ctrl.player();

    // DPI-aware scaling (baseline: 720p height)
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    const int padding = static_cast<int>(15 * scale);
    const int font_title = static_cast<int>(32 * scale);
    const int font_section = static_cast<int>(16 * scale);
    const int font_normal = static_cast<int>(18 * scale);
    const int line_height = static_cast<int>(24 * scale);
    const int section_gap = static_cast<int>(12 * scale);

    int centerX = ctx.window_width / 2;
    render_text(ctx, "CHARACTER STATUS", centerX - static_cast<int>(120 * scale), padding, static_cast<int>(240 * scale), font_title, {255, 255, 255, 255});
    int rightX = centerX + static_cast<int>(30 * scale);
    int ry = padding + font_title + padding;

    auto draw_stat = [&](const std::string& label, int val, int max, Color color) {
        std::string text = label + ": " + std::to_string(val) + " / " + std::to_string(max);
        render_text(ctx, text, rightX, ry, static_cast<int>(160 * scale), font_normal, color);
        ry += line_height;
    };

    render_text(ctx, "--- Vitals ---", rightX, ry, static_cast<int>(100 * scale), font_section, {150, 150, 150, 255});
    ry += line_height;
    draw_stat("Health", p.combat_stats.current_hp, p.combat_stats.max_hp, {255, 100, 100, 255});
    draw_stat("MP", p.combat_stats.current_mp, p.combat_stats.max_mp, {100, 150, 255, 255});
    draw_stat("Lust", p.lust, p.max_lust, {255, 182, 193, 255});

    ry += section_gap;

    render_text(ctx, "--- Level & Experience ---", rightX, ry, static_cast<int>(180 * scale), font_section, {150, 150, 150, 255});
    ry += line_height;
    std::string lvl_text = "Level: " + std::to_string(p.level_data.level);
    render_text(ctx, lvl_text, rightX, ry, static_cast<int>(150 * scale), font_normal, {200, 200, 100, 255});
    ry += line_height + section_gap;

    std::string exp_text = "EXP: " + std::to_string(p.level_data.exp) + " / "
                           + std::to_string(p.level_data.exp_to_next);
    render_text(ctx, exp_text, rightX, ry, static_cast<int>(180 * scale), font_normal, {200, 200, 150, 255});
    ry += line_height + section_gap;

    render_text(ctx, "--- Attributes ---", rightX, ry, static_cast<int>(150 * scale), font_section, {150, 150, 150, 255});
    ry += line_height;

    std::int32_t points_available =
        p.level_data.attribute_points_at_level() - p.attribute_points_spent;
    std::string points_text = "Points: " + std::to_string(points_available);
    Color points_color =
        points_available > 0 ? Color{100, 255, 100, 255} : Color{150, 150, 150, 255};
    render_text(ctx, points_text, rightX, ry, static_cast<int>(120 * scale), font_normal, points_color);
    ry += line_height;

    const char* attr_names[] = {"STR", "END", "AGI", "WIL", "INT", "WIS", "LCK", "SPD", "CHA"};
    const std::uint8_t* attr_values[] = {&p.attributes.str,
                                         &p.attributes.end_,
                                         &p.attributes.agi,
                                         &p.attributes.wil,
                                         &p.attributes.int_,
                                         &p.attributes.wis,
                                         &p.attributes.lck,
                                         &p.attributes.spd,
                                         &p.attributes.cha};

    const int attr_row_height = static_cast<int>(22 * scale);
    const int btn_w = static_cast<int>(26 * scale);
    const int btn_h = static_cast<int>(18 * scale);
    
    for (int i = 0; i < 9; ++i) {
        std::string attr_text = attr_names[i] + std::string(": ") + std::to_string(*attr_values[i]);

        Color attr_color = {200, 200, 200, 255};
        if (hovered_attr_idx_ == i && points_available > 0) {
            attr_color = {255, 255, 100, 255};
        }

        render_text(ctx, attr_text, rightX, ry, static_cast<int>(80 * scale), font_normal, attr_color);

        if (points_available > 0) {
            int button_x = rightX + static_cast<int>(85 * scale);
            int button_y = ry;
            Rect button_rect = {button_x, button_y, btn_w, btn_h};

            Color button_bg = {50, 100, 150, 200};
            Color button_border = {100, 150, 200, 255};

            if (hovered_attr_idx_ == i) {
                button_bg = {100, 150, 200, 255};
                button_border = {150, 200, 255, 255};
            }

            render_fill_rect( button_rect, button_bg);
            render_draw_rect( button_rect, button_border);
            render_text(ctx, "+", button_x + btn_w / 4, button_y + 1, btn_w / 2, btn_h - 2, {255, 255, 255, 255});
        }

        ry += attr_row_height;
    }

    ry += section_gap;
    render_text(ctx, "--- Reputation ---", rightX, ry, static_cast<int>(130 * scale), font_section, {150, 150, 150, 255});
    ry += line_height;

    auto draw_rep = [&](const std::string& name, FactionID fid) {
        int val = p.reputation[static_cast<size_t>(fid)];
        Color color = {200, 200, 200, 255};
        if (val > 20)
            color = {100, 255, 100, 255};
        if (val < -20)
            color = {255, 100, 100, 255};

        std::string text = name + ": " + (val > 0 ? "+" : "") + std::to_string(val);
        render_text(ctx, text, rightX, ry, static_cast<int>(150 * scale), font_normal, color);
        ry += line_height;
    };

    draw_rep("Faction1", FactionID::Faction1);
    draw_rep("Faction2", FactionID::Faction2);
    draw_rep("Wilderness", FactionID::Wilderness);

    // Scaled inventory grid
    const int cell_size = static_cast<int>(28 * scale);
    const int grid_x = padding;
    const int grid_y = padding + font_title + padding;
    const int grid_cols = 16;
    const int grid_rows = std::min(16, (ctx.window_height - grid_y - static_cast<int>(60 * scale)) / cell_size);
    
    const char* grid_label = handling_slot_ == -1
                                 ? "--- Inventory Grid - Click to pick item ---"
                                 : "--- Inventory Grid - Click destination cell ---";
    render_text(ctx, grid_label, grid_x, grid_y - static_cast<int>(25 * scale), static_cast<int>(350 * scale), font_section, {150, 150, 150, 255});

    Rect grid_bg = {grid_x, grid_y, grid_cols * cell_size, grid_rows * cell_size};
    render_fill_rect( grid_bg, {20, 25, 40, 220});
    render_draw_rect( grid_bg, {80, 120, 160, 255});

    for (int row = 0; row < grid_rows; ++row) {
        for (int col = 0; col < grid_cols; ++col) {
            const int slot_idx = row * grid_cols + col;
            const std::uint16_t amount = p.inventory.get_at(slot_idx);

            const int cell_x = grid_x + col * cell_size;
            const int cell_y = grid_y + row * cell_size;

            Rect cell_rect = {cell_x, cell_y, cell_size, cell_size};

            Color bg_color;
            Color border_color;

            if (slot_idx == handling_slot_) {
                bg_color = {220, 100, 100, 255};
                border_color = {255, 50, 50, 255};
            } else if (slot_idx == selected_slot_) {
                bg_color = {100, 150, 220, 255};
                border_color = {255, 200, 100, 255};
            } else if (slot_idx == hovered_slot_) {
                bg_color = {70, 120, 180, 255};
                border_color = {150, 200, 255, 255};
            } else {
                bg_color = {30, 45, 70, 200};
                border_color = {70, 100, 140, 255};
            }

            render_fill_rect( cell_rect, bg_color);
            render_draw_rect( cell_rect, border_color);

            if (amount > 0) {
                ItemType item_type = p.inventory.get_item_type_at(slot_idx);
                const Texture& item_texture = textures.item(item_type);
                if (item_texture.valid()) {
                    int inset = static_cast<int>(3 * scale);
                    Rect dst_rect = {cell_x + inset, cell_y + inset, cell_size - inset * 2, cell_size - inset * 2};
                    render_texture(item_texture, dst_rect);
                }

                std::string count_str = std::to_string(amount);
                Color text_color = (slot_idx == handling_slot_ || slot_idx == selected_slot_
                                        || slot_idx == hovered_slot_)
                                           ? Color{255, 255, 255, 255}
                                           : Color{200, 200, 50, 255};
                int count_font = static_cast<int>(12 * scale);
                render_text(ctx, count_str, cell_x + cell_size - count_font - 2, cell_y + cell_size - count_font - 2, count_font + 4, count_font, text_color);
            }
        }
    }

    int info_y = grid_y + grid_rows * cell_size + static_cast<int>(10 * scale);
    if (handling_slot_ >= 0 && handling_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(handling_slot_);
        std::string info = "HANDLING Slot: " + std::to_string(handling_slot_) + " | Amount: "
                           + std::to_string(amount) + " | Click destination or press ESC";
        render_text(ctx, info, grid_x, info_y, static_cast<int>(500 * scale), font_normal, {255, 150, 100, 255});
    } else if (selected_slot_ >= 0 && selected_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(selected_slot_);
        std::string info = "Selected Slot: " + std::to_string(selected_slot_)
                           + " | Amount: " + std::to_string(amount);
        render_text(ctx, info, grid_x, info_y, static_cast<int>(350 * scale), font_normal, {200, 200, 150, 255});
    }

    render_text(ctx,
                "[ Press ESC/I to close ]",
                centerX - static_cast<int>(80 * scale),
                ctx.window_height - static_cast<int>(30 * scale),
                static_cast<int>(160 * scale),
                font_section,
                {100, 100, 100, 255});
}

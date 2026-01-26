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

    int centerX = ctx.window_width / 2;
    render_text(ctx, "CHARACTER STATUS", centerX - 150, 40, 300, 40, {255, 255, 255, 255});
    int rightX = centerX + 40;
    int ry = 100;

    auto draw_stat = [&](const std::string& label, int val, int max, Color color) {
        std::string text = label + ": " + std::to_string(val) + " / " + std::to_string(max);
        render_text(ctx, text, rightX, ry, 180, 22, color);
        ry += 28;
    };

    render_text(ctx, "--- Vitals ---", rightX, ry, 120, 18, {150, 150, 150, 255});
    ry += 25;
    draw_stat("Health", p.combat_stats.current_hp, p.combat_stats.max_hp, {255, 100, 100, 255});
    draw_stat("MP", p.combat_stats.current_mp, p.combat_stats.max_mp, {100, 150, 255, 255});
    draw_stat("Lust", p.lust, p.max_lust, {255, 182, 193, 255});

    ry += 15;

    render_text(ctx, "--- Level & Experience ---", rightX, ry, 200, 20, {150, 150, 150, 255});
    ry += 25;
    std::string lvl_text = "Level: " + std::to_string(p.level_data.level);
    render_text(ctx, lvl_text, rightX, ry, 180, 25, {200, 200, 100, 255});
    ry += 35;

    std::string exp_text = "EXP: " + std::to_string(p.level_data.exp) + " / "
                           + std::to_string(p.level_data.exp_to_next);
    render_text(ctx, exp_text, rightX, ry, 200, 25, {200, 200, 150, 255});
    ry += 40;

    render_text(ctx, "--- Attributes ---", rightX, ry, 180, 20, {150, 150, 150, 255});
    ry += 30;

    std::int32_t points_available =
        p.level_data.attribute_points_at_level() - p.attribute_points_spent;
    std::string points_text = "Points: " + std::to_string(points_available);
    Color points_color =
        points_available > 0 ? Color{100, 255, 100, 255} : Color{150, 150, 150, 255};
    render_text(ctx, points_text, rightX, ry, 150, 20, points_color);
    ry += 28;

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

    for (int i = 0; i < 9; ++i) {
        std::string attr_text = attr_names[i] + std::string(": ") + std::to_string(*attr_values[i]);

        Color attr_color = {200, 200, 200, 255};
        if (hovered_attr_idx_ == i && points_available > 0) {
            attr_color = {255, 255, 100, 255};
        }

        render_text(ctx, attr_text, rightX, ry, 100, 20, attr_color);

        if (points_available > 0) {
            int button_x = rightX + 100;
            int button_y = ry - 2;
            Rect button_rect = {button_x, button_y, 30, 20};

            Color button_bg = {50, 100, 150, 200};
            Color button_border = {100, 150, 200, 255};

            if (hovered_attr_idx_ == i) {
                button_bg = {100, 150, 200, 255};
                button_border = {150, 200, 255, 255};
            }

            render_fill_rect( button_rect, button_bg);
            render_draw_rect( button_rect, button_border);
            render_text(ctx, "+", button_x + 8, button_y + 2, 15, 16, {255, 255, 255, 255});
        }

        ry += 26;
    }

    ry += 10;
    render_text(ctx, "--- Reputation ---", rightX, ry, 150, 20, {150, 150, 150, 255});
    ry += 28;

    auto draw_rep = [&](const std::string& name, FactionID fid) {
        int val = p.reputation[static_cast<size_t>(fid)];
        Color color = {200, 200, 200, 255};
        if (val > 20)
            color = {100, 255, 100, 255};
        if (val < -20)
            color = {255, 100, 100, 255};

        std::string text = name + ": " + (val > 0 ? "+" : "") + std::to_string(val);
        render_text(ctx, text, rightX, ry, 180, 25, color);
        ry += 35;
    };

    draw_rep("Faction1", FactionID::Faction1);
    draw_rep("Faction2", FactionID::Faction2);
    draw_rep("Wilderness", FactionID::Wilderness);

    const char* grid_label = handling_slot_ == -1
                                 ? "--- Inventory Grid (16x16) - Click to pick item ---"
                                 : "--- Inventory Grid (16x16) - Click destination cell ---";
    render_text(ctx, grid_label, GRID_START_X, GRID_START_Y - 35, 400, 20, {150, 150, 150, 255});

    Rect grid_bg = {GRID_START_X, GRID_START_Y, GRID_COLS * CELL_SIZE, GRID_ROWS * CELL_SIZE};
    render_fill_rect( grid_bg, {20, 25, 40, 220});
    render_draw_rect( grid_bg, {80, 120, 160, 255});

    for (int row = 0; row < GRID_ROWS; ++row) {
        for (int col = 0; col < GRID_COLS; ++col) {
            const int slot_idx = row * GRID_COLS + col;
            const std::uint16_t amount = p.inventory.get_at(slot_idx);

            const int cell_x = GRID_START_X + col * CELL_SIZE;
            const int cell_y = GRID_START_Y + row * CELL_SIZE;

            Rect cell_rect = {cell_x, cell_y, CELL_SIZE, CELL_SIZE};

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
                    Rect src_rect = {16, 16, 16, 16};
                    Rect dst_rect = {cell_x + 4, cell_y + 4, CELL_SIZE - 8, CELL_SIZE - 8};
                    // SDL_RenderCopy(nullptr, item_texture, &src_rect, &dst_rect);
                }

                std::string count_str = std::to_string(amount);
                Color text_color = (slot_idx == handling_slot_ || slot_idx == selected_slot_
                                        || slot_idx == hovered_slot_)
                                           ? Color{255, 255, 255, 255}
                                           : Color{200, 200, 50, 255};
                render_text(ctx, count_str, cell_x + 28, cell_y + 28, 20, 16, text_color);
            }
        }
    }

    int info_y = GRID_START_Y + GRID_ROWS * CELL_SIZE + 15;
    if (handling_slot_ >= 0 && handling_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(handling_slot_);
        std::string info = "HANDLING Slot: " + std::to_string(handling_slot_) + " | Amount: "
                           + std::to_string(amount) + " | Click destination or press ESC";
        render_text(ctx, info, GRID_START_X, info_y, 600, 25, {255, 150, 100, 255});
    } else if (selected_slot_ >= 0 && selected_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(selected_slot_);
        std::string info = "Selected Slot: " + std::to_string(selected_slot_)
                           + " | Amount: " + std::to_string(amount);
        render_text(ctx, info, GRID_START_X, info_y, 400, 25, {200, 200, 150, 255});
    }

    render_text(ctx,
                "[ Press ESC/I to close ]",
                centerX - 100,
                ctx.window_height - 40,
                200,
                20,
                {100, 100, 100, 255});
}

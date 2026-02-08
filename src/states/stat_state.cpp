#include "states/stat_state.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <algorithm>

#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/economy.h"
#include "systems/attributes.h"
#include "systems/skills.h"
#include "systems/player.h"
#include "rendering/texture_manager.h"
#include "core/gfx_types.h"
#include "rendering/renderer.h"

void StatState::update(GameContext& ctx, TextureManager& /*textures*/) {
    // Update hover states based on current mouse position
    hovered_slot_ = get_slot_at(ctx, ctx.curs_x, ctx.curs_y);
    hovered_attr_idx_ = get_hovered_attr_button(ctx, ctx.curs_x, ctx.curs_y);
    hovered_skill_idx_ = get_hovered_skill_button(ctx, ctx.curs_x, ctx.curs_y);

    // Handle mouse clicks for attribute buttons and inventory
    if (ctx.click_event) {
        // Check attribute button clicks
        int const attr_idx = get_hovered_attr_button(ctx, ctx.pick_x, ctx.pick_y);
        if (attr_idx >= 0) {
            Player& p = ctx.world_manager->player_ctrl.player();
            std::int32_t const points_available =
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
                    default:
                        break;  // Invalid attribute index
                }
                p.attribute_points_spent++;
                p.derived_bonuses.recalculate(p.attributes);
                p.combat_stats.recalculate(100, 10, 100, p.attributes);
                p.combat_stats.current_hp = p.combat_stats.max_hp;
                p.combat_stats.current_mp = p.combat_stats.max_mp;
                p.combat_stats.current_sp = p.combat_stats.max_sp;
            }
            return;
        }
        
        // Check skill button clicks
        int const skill_idx = get_hovered_skill_button(ctx, ctx.pick_x, ctx.pick_y);
        if (skill_idx >= 0) {
            Player& p = ctx.world_manager->player_ctrl.player();
            std::int32_t const skill_points_available = p.level_data.level - p.skill_ranks.total_ranks();
            
            if (skill_points_available > 0) {
                SkillID skill_to_increase = SkillID::Bodybuilding;
                switch (skill_idx) {
                    case 0:
                        skill_to_increase = SkillID::Bodybuilding;
                        break;
                    case 1:
                        skill_to_increase = SkillID::Travel;
                        break;
                    case 2:
                        skill_to_increase = SkillID::Fighter;
                        break;
                    default:
                        break;
                }
                p.skill_ranks.increase_rank(skill_to_increase, 1);
                // Recalculate combat stats to apply skill bonuses
                p.combat_stats.recalculate(100, 10, 100, p.attributes);
                p.combat_stats.current_hp = p.combat_stats.max_hp;
                p.combat_stats.current_mp = p.combat_stats.max_mp;
                p.combat_stats.current_sp = p.combat_stats.max_sp;
            }
            return;
        }

        // Check inventory slot clicks
        int const slot = get_slot_at(ctx, ctx.pick_x, ctx.pick_y);
        if (slot >= 0) {
            if (handling_slot_ == -1) {
                handling_slot_ = slot;
                selected_slot_ = slot;
                return;
            }
            
            if (handling_slot_ != slot) {
                // Swap items between slots
                std::uint16_t const from_amount =
                    ctx.world_manager->player_ctrl.player().inventory.get_at(handling_slot_);
                ItemType const from_type =
                    ctx.world_manager->player_ctrl.player().inventory.get_item_type_at(
                        handling_slot_);
                std::uint16_t const to_amount =
                    ctx.world_manager->player_ctrl.player().inventory.get_at(slot);
                ItemType const to_type =
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
            }

            // Clicking the same slot again - just select it
            handling_slot_ = -1;
            selected_slot_ = slot;
            return;
        }
    }
}

void StatState::render(GameContext& ctx, TextureManager& textures) {
    Rect const overlay = {0, 0, ctx.window_width, ctx.window_height};
    render_fill_rect(overlay, {10, 15, 30, 230});

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

    int const centerX = ctx.window_width / 2;
    const int title_height = font_title + static_cast<int>(10 * scale);
    render_text(ctx,
                "CHARACTER STATUS",
                centerX - static_cast<int>(120 * scale),
                padding,
                static_cast<int>(240 * scale),
                title_height,
                {255, 255, 255, 255},
                font_title);
    
    // Three-column layout
    const int grid_width = GRID_COLS * static_cast<int>(28 * scale);
    int middleX = padding + grid_width + static_cast<int>(30 * scale);
    int rightX = middleX + static_cast<int>(220 * scale);
    int my = padding + font_title + padding;  // middle column Y
    int ry = padding + font_title + padding;  // right column Y

    // === MIDDLE COLUMN: Vitals, Level, Attributes ===
    auto draw_stat = [&](const std::string& label, int val, int max, Color color, int x, int& y) {
        std::string const text = label + ": " + std::to_string(val) + " / " + std::to_string(max);
        const int stat_height = font_normal + static_cast<int>(4 * scale);
        render_text(ctx, text, x, y, static_cast<int>(160 * scale), stat_height, color, font_normal);
        y += line_height;
    };

    const int section_height = font_section + static_cast<int>(4 * scale);
    render_text(ctx,
                "--- Vitals ---",
                middleX,
                my,
                static_cast<int>(100 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);
    my += line_height;
    draw_stat("Health", p.combat_stats.current_hp, p.combat_stats.max_hp, {255, 100, 100, 255}, middleX, my);
    draw_stat("MP", p.combat_stats.current_mp, p.combat_stats.max_mp, {100, 150, 255, 255}, middleX, my);
    draw_stat("SP", p.combat_stats.current_sp, p.combat_stats.max_sp, {100, 255, 150, 255}, middleX, my);

    my += section_gap;

    render_text(ctx,
                "--- Level & Experience ---",
                middleX,
                my,
                static_cast<int>(180 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);
    my += line_height;
    std::string const lvl_text = "Level: " + std::to_string(p.level_data.level);
    const int text_height = font_normal + static_cast<int>(4 * scale);
    render_text(ctx,
                lvl_text,
                middleX,
                my,
                static_cast<int>(150 * scale),
                text_height,
                {200, 200, 100, 255},
                font_normal);
    my += line_height;

    std::string const exp_text = "EXP: " + std::to_string(p.level_data.exp) + " / "
                                 + std::to_string(p.level_data.exp_to_next);
    render_text(ctx,
                exp_text,
                middleX,
                my,
                static_cast<int>(180 * scale),
                text_height,
                {200, 200, 150, 255},
                font_normal);
    my += line_height + section_gap;

    render_text(ctx,
                "--- Attributes ---",
                middleX,
                my,
                static_cast<int>(150 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);
    my += line_height;

    std::int32_t const points_available =
        p.level_data.attribute_points_at_level() - p.attribute_points_spent;
    std::string const points_text = "Points: " + std::to_string(points_available);
    Color const points_color =
        points_available > 0 ? Color{100, 255, 100, 255} : Color{150, 150, 150, 255};
    render_text(ctx,
                points_text,
                middleX,
                my,
                static_cast<int>(120 * scale),
                text_height,
                points_color,
                font_normal);
    my += line_height;

    const char* const attr_names[] =
        {"STR", "END", "AGI", "WIL", "INT", "WIS", "LCK", "SPD", "CHA"};
    const std::uint8_t* const attr_values[] = {&p.attributes.str,
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
        std::string const attr_text =
            attr_names[i] + std::string(": ") + std::to_string(*attr_values[i]);

        Color attr_color = {200, 200, 200, 255};
        if (hovered_attr_idx_ == i && points_available > 0) {
            attr_color = {255, 255, 100, 255};
        }

        render_text(ctx,
                    attr_text,
                    middleX,
                    my,
                    static_cast<int>(80 * scale),
                    font_normal + static_cast<int>(4 * scale),
                    attr_color,
                    font_normal);

        if (points_available > 0) {
            int const button_x = middleX + static_cast<int>(85 * scale);
            int const button_y = my;
            Rect const button_rect = {button_x, button_y, btn_w, btn_h};

            Color button_bg = {50, 100, 150, 200};
            Color button_border = {100, 150, 200, 255};

            if (hovered_attr_idx_ == i) {
                button_bg = {100, 150, 200, 255};
                button_border = {150, 200, 255, 255};
            }

            render_fill_rect(button_rect, button_bg);
            render_draw_rect(button_rect, button_border);
            render_text(ctx,
                        "+",
                        button_x + btn_w / 4,
                        button_y + 1,
                        btn_w / 2,
                        btn_h - 2,
                        {255, 255, 255, 255},
                        font_normal);
        }

        my += attr_row_height;
    }

    // === RIGHT COLUMN: Derived Bonuses, Skills, Reputation ===
    // === RIGHT COLUMN: Derived Bonuses, Skills, Reputation ===
    render_text(ctx,
                "--- Derived Bonuses ---",
                rightX,
                ry,
                static_cast<int>(150 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);
    ry += line_height;

    auto draw_bonus = [&](const std::string& label, float value, bool is_percentage = true) {
        std::string text;
        if (is_percentage) {
            int pct = static_cast<int>((value - 1.0f) * 100.0f);
            text = label + ": " + (pct >= 0 ? "+" : "") + std::to_string(pct) + "%";
        } else {
            text = label + ": " + std::to_string(static_cast<int>(value));
        }
        const int bonus_height = font_normal + static_cast<int>(4 * scale);
        render_text(ctx, text, rightX, ry, static_cast<int>(180 * scale), bonus_height, {180, 180, 220, 255}, font_normal);
        ry += line_height;
    };

    draw_bonus("Phys Dmg", p.derived_bonuses.phys_damage_mult);
    draw_bonus("Spell Dmg", p.derived_bonuses.spell_damage_mult);
    draw_bonus("Move Speed", p.derived_bonuses.move_speed_mult);
    draw_bonus("EXP Gain", p.derived_bonuses.exp_mult);
    draw_bonus("Trade Disc", p.derived_bonuses.trade_discount);
    draw_bonus("Relation", static_cast<float>(p.derived_bonuses.relation_bonus), false);

    ry += section_gap;
    
    // Skills section
    render_text(ctx,
                "--- Skills ---",
                rightX,
                ry,
                static_cast<int>(130 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);
    ry += line_height;
    
    std::int32_t const skill_points_available = p.level_data.level - p.skill_ranks.total_ranks();
    std::string const skill_points_text = "Skill Points: " + std::to_string(skill_points_available);
    Color const skill_points_color =
        skill_points_available > 0 ? Color{100, 255, 100, 255} : Color{150, 150, 150, 255};
    render_text(ctx,
                skill_points_text,
                rightX,
                ry,
                static_cast<int>(150 * scale),
                text_height,
                skill_points_color,
                font_normal);
    ry += line_height;
    
    // Display each skill with buttons
    const int skill_row_height = static_cast<int>(22 * scale);
    const SkillID skills[] = {SkillID::Bodybuilding, SkillID::Travel, SkillID::Fighter};
    
    for (int i = 0; i < 3; ++i) {
        SkillInfo const info = get_skill_info(skills[i]);
        std::int32_t const rank = p.skill_ranks.get_rank(skills[i]);
        std::string const skill_text = std::string(info.name) + ": " + std::to_string(rank);
        
        Color skill_color = rank > 0 ? Color{200, 200, 100, 255} : Color{150, 150, 150, 255};
        if (hovered_skill_idx_ == i && skill_points_available > 0) {
            skill_color = {255, 255, 100, 255};
        }
        
        render_text(ctx,
                    skill_text,
                    rightX,
                    ry,
                    static_cast<int>(130 * scale),
                    font_normal + static_cast<int>(4 * scale),
                    skill_color,
                    font_normal);
        
        // Draw + button if skill points available
        if (skill_points_available > 0) {
            int const button_x = rightX + static_cast<int>(135 * scale);
            int const button_y = ry;
            Rect const button_rect = {button_x, button_y, btn_w, btn_h};
            
            Color button_bg = {50, 100, 150, 200};
            Color button_border = {100, 150, 200, 255};
            
            if (hovered_skill_idx_ == i) {
                button_bg = {100, 150, 200, 255};
                button_border = {150, 200, 255, 255};
            }
            
            render_fill_rect(button_rect, button_bg);
            render_draw_rect(button_rect, button_border);
            render_text(ctx,
                        "+",
                        button_x + btn_w / 4,
                        button_y + 1,
                        btn_w / 2,
                        btn_h - 2,
                        {255, 255, 255, 255},
                        font_normal);
        }
        
        ry += skill_row_height;
    }
    
    ry += section_gap;
    
    // Reputation section
    render_text(ctx,
                "--- Reputation ---",
                rightX,
                ry,
                static_cast<int>(130 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);
    ry += line_height;

    auto draw_rep = [&](const std::string& name, FactionID fid) {
        int const val = p.reputation[static_cast<size_t>(fid)];
        Color color = {200, 200, 200, 255};
        if (val > 20)
            color = {100, 255, 100, 255};
        if (val < -20)
            color = {255, 100, 100, 255};

        std::string const text = name + ": " + (val > 0 ? "+" : "") + std::to_string(val);
        const int rep_height = font_normal + static_cast<int>(4 * scale);
        render_text(ctx, text, rightX, ry, static_cast<int>(150 * scale), rep_height, color, font_normal);
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
    const int grid_rows =
        std::min(16, (ctx.window_height - grid_y - static_cast<int>(60 * scale)) / cell_size);

    const char* grid_label = handling_slot_ == -1
                                 ? "--- Inventory Grid - Click to pick item ---"
                                 : "--- Inventory Grid - Click destination cell ---";
    render_text(ctx,
                grid_label,
                grid_x,
                grid_y - static_cast<int>(25 * scale),
                static_cast<int>(350 * scale),
                section_height,
                {150, 150, 150, 255},
                font_section);

    Rect const grid_bg = {grid_x, grid_y, grid_cols * cell_size, grid_rows * cell_size};
    render_fill_rect(grid_bg, {20, 25, 40, 220});
    render_draw_rect(grid_bg, {80, 120, 160, 255});

    for (int row = 0; row < grid_rows; ++row) {
        for (int col = 0; col < grid_cols; ++col) {
            const int slot_idx = row * grid_cols + col;
            const std::uint16_t amount = p.inventory.get_at(slot_idx);

            const int cell_x = grid_x + col * cell_size;
            const int cell_y = grid_y + row * cell_size;

            Rect const cell_rect = {cell_x, cell_y, cell_size, cell_size};

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

            render_fill_rect(cell_rect, bg_color);
            render_draw_rect(cell_rect, border_color);

            if (amount > 0) {
                ItemType const item_type = p.inventory.get_item_type_at(slot_idx);
                const Texture& item_texture = textures.item(item_type);
                if (item_texture.valid()) {
                    int const inset = static_cast<int>(3 * scale);
                    Rect const dst_rect = {cell_x + inset,
                                           cell_y + inset,
                                           cell_size - inset * 2,
                                           cell_size - inset * 2};
                    render_texture(item_texture, dst_rect);
                }

                std::string const count_str = std::to_string(amount);
                Color const text_color = (slot_idx == handling_slot_ || slot_idx == selected_slot_
                                          || slot_idx == hovered_slot_)
                                             ? Color{255, 255, 255, 255}
                                             : Color{200, 200, 50, 255};
                int const count_font = static_cast<int>(12 * scale);
                const int count_height = count_font + 4;
                render_text(ctx,
                            count_str,
                            cell_x + cell_size - count_font - 2,
                            cell_y + cell_size - count_font - 2,
                            count_font + 4,
                            count_height,
                            text_color,
                            count_font);
            }
        }
    }

    int const info_y = grid_y + grid_rows * cell_size + static_cast<int>(10 * scale);
    if (handling_slot_ >= 0 && handling_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(handling_slot_);
        std::string const info = "HANDLING Slot: " + std::to_string(handling_slot_) + " | Amount: "
                                 + std::to_string(amount) + " | Click destination or press ESC";
        const int info_height = font_normal + static_cast<int>(4 * scale);
        render_text(ctx,
                    info,
                    grid_x,
                    info_y,
                    static_cast<int>(500 * scale),
                    info_height,
                    {255, 150, 100, 255},
                    font_normal);
    } else if (selected_slot_ >= 0 && selected_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(selected_slot_);
        std::string const info = "Selected Slot: " + std::to_string(selected_slot_)
                                 + " | Amount: " + std::to_string(amount);
        const int info_height = font_normal + static_cast<int>(4 * scale);
        render_text(ctx,
                    info,
                    grid_x,
                    info_y,
                    static_cast<int>(350 * scale),
                    info_height,
                    {200, 200, 150, 255},
                    font_normal);
    }
    // Draw Item Tooltip
    if (hovered_slot_ >= 0 && hovered_slot_ < 256) {
        const std::uint16_t amount = p.inventory.get_at(hovered_slot_);
        if (amount > 0) {
            ItemType const type = p.inventory.get_item_type_at(hovered_slot_);
            const auto& meta = ITEM_DATABASE[static_cast<size_t>(type)];
            
            std::string tooltip = std::string(meta.name);
            // Optional: Add stats or description here if available
            // tooltip += "\nPrice: " + std::to_string(meta.base_price);

            const int tt_font_size = static_cast<int>(14 * scale);
            Point const size = get_text_renderer()->measure(tooltip, tt_font_size);
            const int tt_padding = 8;
            const int tt_w = size.x + tt_padding * 2;
            const int tt_h = size.y + tt_padding * 2;
            
            // Position near cursor but keep on screen
            int tt_x = ctx.curs_x + 15;
            int tt_y = ctx.curs_y + 15;
            
            if (tt_x + tt_w > ctx.window_width) tt_x -= (tt_w + 20);
            if (tt_y + tt_h > ctx.window_height) tt_y -= (tt_h + 20);

            render_fill_rect(static_cast<float>(tt_x), static_cast<float>(tt_y), 
                             static_cast<float>(tt_w), static_cast<float>(tt_h), 
                             ui_color("#101010F0"));
            render_draw_rect(static_cast<float>(tt_x), static_cast<float>(tt_y), 
                             static_cast<float>(tt_w), static_cast<float>(tt_h), 
                             ui_color("#FFFFFF80"));
            
            render_text(ctx, tooltip, tt_x + tt_padding, tt_y + tt_padding, 
                        size.x, size.y, {255, 255, 255, 255}, tt_font_size);
        }
    }
    render_text(ctx,
                "[ Press ESC/I to close ]",
                centerX - static_cast<int>(80 * scale),
                ctx.window_height - static_cast<int>(30 * scale),
                static_cast<int>(160 * scale),
                section_height,
                {100, 100, 100, 255},
                font_section);
}

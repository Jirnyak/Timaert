#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include "systems/world_manager.h"
#include "systems/economy.h"
#include <string>

class StatState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Stat; }
    [[nodiscard]] bool is_overlay() const noexcept override { return true; }

private:
    InputManager input_manager_;
    int selected_slot_ = -1;
    int hovered_slot_ = -1;
    int handling_slot_ = -1;  // Slot being dragged/handled (-1 = not handling)
    int last_mouse_x_ = -1;
    int last_mouse_y_ = -1;
    
    // Inventory grid layout constants
    static constexpr int GRID_COLS = 16;
    static constexpr int GRID_ROWS = 16;
    static constexpr int CELL_SIZE = 32;  // Компактный размер ячейки
    static constexpr int GRID_START_X = 30;
    static constexpr int GRID_START_Y = 100; // Поднимаем вровень со статами
    
    // Attribute buttons
    int hovered_attr_idx_ = -1;  // -1 = none, 0-8 = attribute index
    static constexpr int ATTR_BUTTON_WIDTH = 140;
    static constexpr int ATTR_BUTTON_HEIGHT = 24;
    
    bool is_mouse_over_grid(int mouse_x, int mouse_y) const noexcept
    {
        return mouse_x >= GRID_START_X && mouse_x < GRID_START_X + GRID_COLS * CELL_SIZE &&
               mouse_y >= GRID_START_Y && mouse_y < GRID_START_Y + GRID_ROWS * CELL_SIZE;
    }
    
    int get_slot_at(int mouse_x, int mouse_y) const noexcept
    {
        if (!is_mouse_over_grid(mouse_x, mouse_y)) return -1;
        int col = (mouse_x - GRID_START_X) / CELL_SIZE;
        int row = (mouse_y - GRID_START_Y) / CELL_SIZE;
        return row * GRID_COLS + col;
    }
    
    // Check if mouse is over an attribute increase button
    // Returns 0-8 for attribute index, -1 if not over any button
    // Calculates attribute starting y position to match render()
    int get_hovered_attr_button(int mouse_x, int mouse_y, int base_x, int base_y) const noexcept
    {
        const int attr_start_x = base_x;
        const int attr_start_y = base_y;
        const int row_height = 26;
        int attr_y_offset = 25 + (3 * 28) + 15 + 35 + 40 + 30 + 28;
        int attr_y_start = attr_start_y + attr_y_offset;
        
        for (int i = 0; i < 9; ++i)
        {
            int button_x = attr_start_x + 100;  // Position of the + button
            int button_y = attr_y_start + i * row_height;
            
            if (mouse_x >= button_x && mouse_x < button_x + 30 &&
                mouse_y >= button_y && mouse_y < button_y + 20)
            {
                return i;
            }
        }
        return -1;
    }

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/) override
    {
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Click)
            {
                // Calculate rightX to match render function
                int centerX = ctx.window_width / 2;
                int rightX = centerX + 40;
                
                // Check if clicking on an attribute button
                int attr_idx = get_hovered_attr_button(evt.x, evt.y, rightX, 100);
                if (attr_idx >= 0)
                {
                    // Try to increase attribute
                    Player& p = ctx.world_manager->player_ctrl.player();
                    std::int32_t points_available = p.level_data.attribute_points_at_level() - p.attribute_points_spent;
                    
                    if (points_available > 0)
                    {
                        // Increase the attribute
                        switch (attr_idx)
                        {
                            case 0: p.attributes.str++; break;
                            case 1: p.attributes.end_++; break;
                            case 2: p.attributes.agi++; break;
                            case 3: p.attributes.wil++; break;
                            case 4: p.attributes.int_++; break;
                            case 5: p.attributes.wis++; break;
                            case 6: p.attributes.lck++; break;
                            case 7: p.attributes.spd++; break;
                            case 8: p.attributes.cha++; break;
                        }
                        p.attribute_points_spent++;
                        p.derived_bonuses.recalculate(p.attributes);
                        // Recalculate combat stats with base HP/MP values (100, 10)
                        p.combat_stats.recalculate(100, 10, p.attributes);
                        // Sync current HP/MP to max (heal to full when allocating points)
                        p.combat_stats.current_hp = p.combat_stats.max_hp;
                        p.combat_stats.current_mp = p.combat_stats.max_mp;
                    }
                    return;
                }
                
                int slot = get_slot_at(evt.x, evt.y);
                if (slot >= 0)
                {
                    if (handling_slot_ == -1)
                    {
                        // No item being handled - start handling this slot
                        handling_slot_ = slot;
                        selected_slot_ = slot;
                        return;
                    }
                    else if (handling_slot_ != slot)
                    {
                        // Item being handled - move to new slot
                        // Swap items between slots
                        std::uint16_t from_amount = ctx.world_manager->player_ctrl.player().inventory.get_at(handling_slot_);
                        ItemType from_type = ctx.world_manager->player_ctrl.player().inventory.get_item_type_at(handling_slot_);
                        
                        std::uint16_t to_amount = ctx.world_manager->player_ctrl.player().inventory.get_at(slot);
                        ItemType to_type = ctx.world_manager->player_ctrl.player().inventory.get_item_type_at(slot);
                        
                        // Move from handling_slot to slot
                        ctx.world_manager->player_ctrl.player().inventory.set_at(slot, from_amount, from_type);
                        // Move what was in slot to handling_slot
                        ctx.world_manager->player_ctrl.player().inventory.set_at(handling_slot_, to_amount, to_type);
                        
                        handling_slot_ = -1;
                        selected_slot_ = slot;
                        return;
                    }
                    else
                    {
                        // Clicked same slot - deselect handling
                        handling_slot_ = -1;
                        selected_slot_ = slot;
                        return;
                    }
                }
                
                // Click outside grid closes menu
                pop_state(ctx, false);
            }
        }
        
        // Track mouse motion for hovering
        if (event.type == SDL_MOUSEMOTION)
        {
            last_mouse_x_ = event.motion.x;
            last_mouse_y_ = event.motion.y;
        }
        
        if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_i || event.key.keysym.sym == SDLK_TAB)
            {
                // Exit handling mode without moving
                handling_slot_ = -1;
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    pop_state(ctx, false);
                }
            }
        }
    }

    void update(GameContext& ctx, TextureManager& /*textures*/) override
    {
        // Update hovered slot based on current mouse position
        int mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        
        hovered_slot_ = get_slot_at(mouse_x, mouse_y);
        
        // Calculate rightX to match render function
        int centerX = ctx.window_width / 2;
        int rightX = centerX + 40;
        
        hovered_attr_idx_ = get_hovered_attr_button(mouse_x, mouse_y, rightX, 100);
    }

    void render(GameContext& ctx, TextureManager& textures) override
    {
        // 1. Фон (затемнение мира)
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, {10, 15, 30, 230});

        if (!ctx.world_manager) return;
        const Player& p = ctx.world_manager->player_ctrl.player();

        // 2. Заголовок
        int centerX = ctx.window_width / 2;
        render_text(ctx, "CHARACTER STATUS", centerX - 150, 40, 300, 40, {255, 255, 255, 255});
        // 3. Текстовая колонка (Справа)
        int rightX = centerX + 40;
        int ry = 100; // Стартовая точка для всей текстовой колонки

        auto draw_stat = [&](const std::string& label, int val, int max, SDL_Color color) {
            std::string text = label + ": " + std::to_string(val) + " / " + std::to_string(max);
            render_text(ctx, text, rightX, ry, 180, 22, color);
            ry += 28;
        };

        render_text(ctx, "--- Vitals ---", rightX, ry, 120, 18, {150, 150, 150, 255});
        ry += 25;
        draw_stat("Health", p.combat_stats.current_hp, p.combat_stats.max_hp, {255, 100, 100, 255});
        draw_stat("MP", p.combat_stats.current_mp, p.combat_stats.max_mp, {100, 150, 255, 255});
        draw_stat("Lust", p.lust, p.max_lust, {255, 182, 193, 255});
        
        ry += 15; // Отступ перед уровнем

        // 4. Уровень и опыт
        int ry = 120;
        int rightX = centerX + 40;
        render_text(ctx, "--- Level & Experience ---", rightX, ry - 30, 200, 20, {150, 150, 150, 255});
        
        std::string lvl_text = "Level: " + std::to_string(p.level_data.level);
        render_text(ctx, lvl_text, rightX, ry, 180, 25, {200, 200, 100, 255});
        ry += 35;
        
        std::string exp_text = "EXP: " + std::to_string(p.level_data.exp) + " / " + std::to_string(p.level_data.exp_to_next);
        render_text(ctx, exp_text, rightX, ry, 200, 25, {200, 200, 150, 255});
        ry += 40;

        // 5. Attributes section with allocation buttons
        render_text(ctx, "--- Attributes ---", rightX, ry, 180, 20, {150, 150, 150, 255});
        ry += 30;
        
        std::int32_t points_available = p.level_data.attribute_points_at_level() - p.attribute_points_spent;
        std::string points_text = "Points: " + std::to_string(points_available);
        SDL_Color points_color = points_available > 0 ? SDL_Color{100, 255, 100, 255} : SDL_Color{150, 150, 150, 255};
        render_text(ctx, points_text, rightX, ry, 150, 20, points_color);
        ry += 28;
        
        // Attribute names and current values
        const char* attr_names[] = {"STR", "END", "AGI", "WIL", "INT", "WIS", "LCK", "SPD", "CHA"};
        const std::uint8_t* attr_values[] = {
            &p.attributes.str, &p.attributes.end_, &p.attributes.agi, &p.attributes.wil,
            &p.attributes.int_, &p.attributes.wis, &p.attributes.lck, &p.attributes.spd, &p.attributes.cha
        };
        
        for (int i = 0; i < 9; ++i)
        {
            std::string attr_text = attr_names[i] + std::string(": ") + std::to_string(*attr_values[i]);
            
            // Color based on hover state
            SDL_Color attr_color = {200, 200, 200, 255};
            if (hovered_attr_idx_ == i && points_available > 0)
            {
                attr_color = {255, 255, 100, 255};  // Highlight when hoverable
            }
            
            render_text(ctx, attr_text, rightX, ry, 100, 20, attr_color);
            
            // Draw + button if points available
            if (points_available > 0)
            {
                int button_x = rightX + 100;
                int button_y = ry - 2;
                SDL_Rect button_rect = {button_x, button_y, 30, 20};
                
                SDL_Color button_bg = {50, 100, 150, 200};
                SDL_Color button_border = {100, 150, 200, 255};
                
                if (hovered_attr_idx_ == i)
                {
                    button_bg = {100, 150, 200, 255};
                    button_border = {150, 200, 255, 255};
                }
                
                ui_fill_rect(ctx.renderer, button_rect, button_bg);
                ui_draw_rect(ctx.renderer, button_rect, button_border);
                render_text(ctx, "+", button_x + 8, button_y + 2, 15, 16, {255, 255, 255, 255});
            }
            
            ry += 26;
        }
        
        // 6. Репутация (переместилась ниже атрибутов)
        ry += 10;
        render_text(ctx, "--- Reputation ---", rightX, ry, 150, 20, {150, 150, 150, 255});
        ry += 28;
        
        auto draw_rep = [&](const std::string& name, FactionID fid) {
            int val = p.reputation[static_cast<size_t>(fid)];
            SDL_Color color = {200, 200, 200, 255};
            if (val > 20) color = {100, 255, 100, 255};
            if (val < -20) color = {255, 100, 100, 255};
            
            std::string text = name + ": " + (val > 0 ? "+" : "") + std::to_string(val);
            render_text(ctx, text, rightX, ry, 180, 25, color);
            ry += 35;
        };

        draw_rep("Faction1", FactionID::Faction1);
        draw_rep("Faction2", FactionID::Faction2);
        draw_rep("Wilderness", FactionID::Wilderness);

        // 7. Инвентарь как 16x16 сетка (интерактивная)
        const char* grid_label = handling_slot_ == -1 ? "--- Inventory Grid (16x16) - Click to pick item ---" : "--- Inventory Grid (16x16) - Click destination cell ---";
        render_text(ctx, grid_label, GRID_START_X, GRID_START_Y - 35, 400, 20, {150, 150, 150, 255});
        
        // Draw grid background
        SDL_Rect grid_bg = {GRID_START_X, GRID_START_Y, GRID_COLS * CELL_SIZE, GRID_ROWS * CELL_SIZE};
        ui_fill_rect(ctx.renderer, grid_bg, {20, 25, 40, 220});
        ui_draw_rect(ctx.renderer, grid_bg, {80, 120, 160, 255});
        
        // Draw each cell in the grid
        for (int row = 0; row < GRID_ROWS; ++row)
        {
            for (int col = 0; col < GRID_COLS; ++col)
            {
                const int slot_idx = row * GRID_COLS + col;
                const std::uint16_t amount = p.inventory.get_at(slot_idx);
                
                const int cell_x = GRID_START_X + col * CELL_SIZE;
                const int cell_y = GRID_START_Y + row * CELL_SIZE;
                
                SDL_Rect cell_rect = {cell_x, cell_y, CELL_SIZE, CELL_SIZE};
                
                // Determine cell color based on state
                SDL_Color bg_color;
                SDL_Color border_color;
                
                if (slot_idx == handling_slot_)
                {
                    // Item being handled: bright red/pink with thick border
                    bg_color = {220, 100, 100, 255};
                    border_color = {255, 50, 50, 255};
                }
                else if (slot_idx == selected_slot_)
                {
                    // Selected: bright blue with orange border
                    bg_color = {100, 150, 220, 255};
                    border_color = {255, 200, 100, 255};
                }
                else if (slot_idx == hovered_slot_)
                {
                    // Hovered: lighter blue
                    bg_color = {70, 120, 180, 255};
                    border_color = {150, 200, 255, 255};
                }
                else
                {
                    // Normal: dark blue
                    bg_color = {30, 45, 70, 200};
                    border_color = {70, 100, 140, 255};
                }
                
                ui_fill_rect(ctx.renderer, cell_rect, bg_color);
                ui_draw_rect(ctx.renderer, cell_rect, border_color);
                
                // Draw item sprite if present
                if (amount > 0)
                {
                    ItemType item_type = p.inventory.get_item_type_at(slot_idx);
                    SDL_Texture* item_texture = textures.item(item_type);
                    if (item_texture)
                    {
                        // coins.png is 48x48 with 16x16 center tile - draw the center 16x16
                        SDL_Rect src_rect = {16, 16, 16, 16};
                        SDL_Rect dst_rect = {cell_x + 4, cell_y + 4, CELL_SIZE - 8, CELL_SIZE - 8};
                        SDL_RenderCopy(ctx.renderer, item_texture, &src_rect, &dst_rect);
                    }
                    
                    // Draw amount in corner
                    std::string count_str = std::to_string(amount);
                    SDL_Color text_color = (slot_idx == handling_slot_ || slot_idx == selected_slot_ || slot_idx == hovered_slot_) 
                        ? SDL_Color{255, 255, 255, 255} 
                        : SDL_Color{200, 200, 50, 255};
                    render_text(ctx, count_str, cell_x + 28, cell_y + 28, 20, 16, text_color);
                }
            }
        }
        
        // Show selected slot info or handling info
        int info_y = GRID_START_Y + GRID_ROWS * CELL_SIZE + 15;
        if (handling_slot_ >= 0 && handling_slot_ < 256)
        {
            const std::uint16_t amount = p.inventory.get_at(handling_slot_);
            std::string info = "HANDLING Slot: " + std::to_string(handling_slot_) + " | Amount: " + std::to_string(amount) + " | Click destination or press ESC";
            render_text(ctx, info, GRID_START_X, info_y, 600, 25, {255, 150, 100, 255});
        }
        else if (selected_slot_ >= 0 && selected_slot_ < 256)
        {
            const std::uint16_t amount = p.inventory.get_at(selected_slot_);
            std::string info = "Selected Slot: " + std::to_string(selected_slot_) + " | Amount: " + std::to_string(amount);
            render_text(ctx, info, GRID_START_X, info_y, 400, 25, {200, 200, 150, 255});
        }

        render_text(ctx, "[ Press ESC/I to close ]", centerX - 100, ctx.window_height - 40, 200, 20, {100, 100, 100, 255});
    }
};

inline StateRegistrar<StatState> register_stat_state_{GameMode::Stat};

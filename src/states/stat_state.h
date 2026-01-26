#pragma once

#include "core/game_state.h"
#include "ui/ui_events.h"

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
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& textures) override;
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;
};

inline StateRegistrar<StatState> register_stat_state_{GameMode::Stat};

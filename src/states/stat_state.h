#pragma once

#include "core/game_state.h"
#include "ui/ui_events.h"
#include "core/game_context.h"
#include "core/types.h"

class StatState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Stat;
    }
    [[nodiscard]] bool is_overlay() const noexcept override {
        return true;
    }

private:
    InputManager input_manager_;
    int selected_slot_ = -1;
    int hovered_slot_ = -1;
    int handling_slot_ = -1;  // Slot being dragged/handled (-1 = not handling)

    // Inventory grid layout constants (base values for 720p)
    static constexpr int GRID_COLS = 16;
    static constexpr int GRID_ROWS_BASE = 16;
    static constexpr int CELL_SIZE_BASE = 28;
    static constexpr int PADDING_BASE = 15;
    static constexpr int FONT_TITLE_BASE = 32;

    // Attribute buttons
    int hovered_attr_idx_ = -1;  // -1 = none, 0-8 = attribute index

    // Calculate DPI scale factor
    [[nodiscard]] static float get_scale(const GameContext& ctx) noexcept {
        return std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    }

    // Calculate grid parameters with DPI scaling
    struct GridParams {
        int cell_size;
        int grid_x;
        int grid_y;
        int grid_rows;
    };

    [[nodiscard]] static GridParams calc_grid_params(const GameContext& ctx) noexcept {
        const float scale = get_scale(ctx);
        GridParams p{};
        p.cell_size = static_cast<int>(CELL_SIZE_BASE * scale);
        const int padding = static_cast<int>(PADDING_BASE * scale);
        const int font_title = static_cast<int>(FONT_TITLE_BASE * scale);
        p.grid_x = padding;
        p.grid_y = padding + font_title + padding;
        p.grid_rows = std::min(16, (ctx.window_height - p.grid_y - static_cast<int>(60 * scale)) / p.cell_size);
        return p;
    }

    [[nodiscard]] static int get_slot_at(const GameContext& ctx, int mouse_x, int mouse_y) noexcept {
        const GridParams gp = calc_grid_params(ctx);
        if (mouse_x < gp.grid_x || mouse_x >= gp.grid_x + GRID_COLS * gp.cell_size
            || mouse_y < gp.grid_y || mouse_y >= gp.grid_y + gp.grid_rows * gp.cell_size) {
            return -1;
        }
        const int col = (mouse_x - gp.grid_x) / gp.cell_size;
        const int row = (mouse_y - gp.grid_y) / gp.cell_size;
        if (row >= gp.grid_rows || col >= GRID_COLS)
            return -1;
        return row * GRID_COLS + col;
    }

    // Check if mouse is over an attribute increase button
    // Returns 0-8 for attribute index, -1 if not over any button
    [[nodiscard]] static int get_hovered_attr_button(const GameContext& ctx, int mouse_x, int mouse_y) noexcept {
        const float scale = get_scale(ctx);
        const int padding = static_cast<int>(PADDING_BASE * scale);
        const int font_title = static_cast<int>(FONT_TITLE_BASE * scale);
        const int line_height = static_cast<int>(24 * scale);
        const int section_gap = static_cast<int>(12 * scale);
        const int attr_row_height = static_cast<int>(22 * scale);
        const int btn_w = static_cast<int>(26 * scale);
        const int btn_h = static_cast<int>(18 * scale);

        const int centerX = ctx.window_width / 2;
        const int rightX = centerX + static_cast<int>(30 * scale);
        int ry = padding + font_title + padding;

        // Skip: vitals (3 lines + header), section_gap, level (2 lines + header), attributes header, points line
        ry += line_height;  // --- Vitals ---
        ry += line_height * 3;  // Health, MP, SP
        ry += section_gap;
        ry += line_height;  // --- Level & Experience ---
        ry += line_height + section_gap;  // Level
        ry += line_height + section_gap;  // EXP
        ry += line_height;  // --- Attributes ---
        ry += line_height;  // Points

        // Now ry is at first attribute row
        for (int i = 0; i < 9; ++i) {
            const int button_x = rightX + static_cast<int>(85 * scale);
            const int button_y = ry + i * attr_row_height;

            if (mouse_x >= button_x && mouse_x < button_x + btn_w
                && mouse_y >= button_y && mouse_y < button_y + btn_h) {
                return i;
            }
        }
        return -1;
    }

public:
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;
};

inline StateRegistrar<StatState> register_stat_state_{GameMode::Stat};

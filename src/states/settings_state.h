#pragma once

#include "core/game_state.h"
#include "rendering/renderer.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include <string>
#include <charconv>

class SettingsState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Settings;
    }

private:
    enum class SettingsAction : std::uint8_t { None, StartGame, Back };
    SettingsAction pending_action_ = SettingsAction::None;
    InputManager input_manager_;
    std::string seed_input_;

    void apply_seed(GameContext& ctx) {
        ctx.seed_input = seed_input_;
        if (!seed_input_.empty()) {
            const char* begin = seed_input_.data();
            const char* end = begin + seed_input_.size();
            std::uint32_t parsed = 0;
            const auto result = std::from_chars(begin, end, parsed);
            if (result.ec == std::errc() && result.ptr == end) {
                ctx.seed = parsed;
                return;
            }
        }
        ctx.seed = std::random_device{}();
    }

public:
    void update(GameContext& ctx, TextureManager& /*textures*/) override;

    void render(GameContext& ctx, TextureManager& textures) override {
        render_texture(textures.bg(0), {0, 0, ctx.window_width, ctx.window_height});

        // Scale factor based on window size (baseline: 720p height)
        const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
        const int title_font = static_cast<int>(40 * scale);
        const int label_font = static_cast<int>(30 * scale);
        const int small_font = static_cast<int>(25 * scale);
        const int btn_small = static_cast<int>(40 * scale);
        const int btn_large_w = static_cast<int>(160 * scale);
        const int btn_large_h = static_cast<int>(50 * scale);
        
        // Layout positions - all computed dynamically
        const int center_x = ctx.window_width / 2;
        const int center_y = ctx.window_height / 2;
        const int label_width = static_cast<int>(150 * scale);
        const int value_width = static_cast<int>(100 * scale);
        const int content_width = label_width + value_width + btn_small * 2 + static_cast<int>(40 * scale);
        const int content_start_x = center_x - content_width / 2;

        // Title
        render_text(ctx,
                    "Map Generation Settings",
                    center_x - static_cast<int>(200 * scale),
                    static_cast<int>(50 * scale),
                    static_cast<int>(400 * scale),
                    title_font,
                    {255, 200, 100, 255});

        // Seed row
        const int seed_row_y = center_y - static_cast<int>(140 * scale);
        render_text(ctx, "Seed:", content_start_x, seed_row_y, label_width, label_font, {200, 200, 200, 255});
        Rect seed_box = {content_start_x + label_width + static_cast<int>(10 * scale), seed_row_y, 
                         static_cast<int>(200 * scale), static_cast<int>(35 * scale)};
        render_fill_rect(seed_box, {50, 50, 60, 255});
        render_draw_rect(seed_box, {100, 150, 200, 255});
        render_text(ctx, seed_input_.empty() ? "(random)" : seed_input_,
                    seed_box.x + static_cast<int>(10 * scale), seed_row_y + static_cast<int>(5 * scale),
                    static_cast<int>(180 * scale), small_font, {150, 200, 255, 255});

        // Continents row
        const int cont_row_y = center_y - static_cast<int>(50 * scale);
        Rect cont_minus = {content_start_x, cont_row_y, btn_small, btn_small};
        render_draw_panel(cont_minus, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, "-", cont_minus.x + btn_small/3, cont_minus.y + btn_small/6, btn_small, label_font, {255, 255, 255, 255});
        
        render_text(ctx, "Continents:", content_start_x + btn_small + static_cast<int>(15 * scale), cont_row_y + static_cast<int>(5 * scale),
                    label_width, label_font, {200, 200, 200, 255});
        render_text(ctx, std::to_string(ctx.num_continents) + " / 10",
                    content_start_x + btn_small + label_width + static_cast<int>(25 * scale), cont_row_y + static_cast<int>(5 * scale),
                    value_width, label_font, {255, 255, 100, 255});
        
        Rect cont_plus = {content_start_x + content_width - btn_small, cont_row_y, btn_small, btn_small};
        render_draw_panel(cont_plus, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, "+", cont_plus.x + btn_small/3, cont_plus.y + btn_small/6, btn_small, label_font, {255, 255, 255, 255});

        // Water row
        const int water_row_y = center_y + static_cast<int>(30 * scale);
        Rect water_minus = {content_start_x, water_row_y, btn_small, btn_small};
        render_draw_panel(water_minus, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, "-", water_minus.x + btn_small/3, water_minus.y + btn_small/6, btn_small, label_font, {255, 255, 255, 255});
        
        render_text(ctx, "Water:", content_start_x + btn_small + static_cast<int>(15 * scale), water_row_y + static_cast<int>(5 * scale),
                    label_width, label_font, {200, 200, 200, 255});
        render_text(ctx, std::to_string(ctx.water_amount) + " / 10",
                    content_start_x + btn_small + label_width + static_cast<int>(25 * scale), water_row_y + static_cast<int>(5 * scale),
                    value_width, label_font, {100, 150, 255, 255});
        
        Rect water_plus = {content_start_x + content_width - btn_small, water_row_y, btn_small, btn_small};
        render_draw_panel(water_plus, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, "+", water_plus.x + btn_small/3, water_plus.y + btn_small/6, btn_small, label_font, {255, 255, 255, 255});

        // Generate button
        const int gen_btn_y = center_y + static_cast<int>(120 * scale);
        Rect gen_btn = {center_x - btn_large_w / 2, gen_btn_y, btn_large_w, btn_large_h};
        render_draw_panel(gen_btn, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, "Generate", gen_btn.x + static_cast<int>(30 * scale), gen_btn.y + static_cast<int>(10 * scale),
                    btn_large_w - static_cast<int>(60 * scale), label_font, {255, 255, 255, 255});

        // Back button
        const int back_btn_y = center_y + static_cast<int>(185 * scale);
        Rect back_btn = {center_x - btn_large_w / 2, back_btn_y, btn_large_w, btn_large_h};
        render_draw_panel(back_btn, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, "Back", back_btn.x + static_cast<int>(50 * scale), back_btn.y + static_cast<int>(10 * scale),
                    btn_large_w - static_cast<int>(100 * scale), label_font, {255, 255, 255, 255});

        // Handle button clicks
        if (ctx.picked) {
            if (ui_point_in_rect(ctx.pick_x, ctx.pick_y, cont_minus)) {
                if (ctx.num_continents > 0) ctx.num_continents--;
            } else if (ui_point_in_rect(ctx.pick_x, ctx.pick_y, cont_plus)) {
                if (ctx.num_continents < 10) ctx.num_continents++;
            } else if (ui_point_in_rect(ctx.pick_x, ctx.pick_y, water_minus)) {
                if (ctx.water_amount > 0) ctx.water_amount--;
            } else if (ui_point_in_rect(ctx.pick_x, ctx.pick_y, water_plus)) {
                if (ctx.water_amount < 10) ctx.water_amount++;
            } else if (ui_point_in_rect(ctx.pick_x, ctx.pick_y, gen_btn)) {
                apply_seed(ctx);
                pending_action_ = SettingsAction::StartGame;
            } else if (ui_point_in_rect(ctx.pick_x, ctx.pick_y, back_btn)) {
                pending_action_ = SettingsAction::Back;
            }
            ctx.picked = false;
        }

        // Instructions
        render_text(ctx,
                    "Type seed number and press Enter. Tap +/- to adjust settings.",
                    center_x - static_cast<int>(350 * scale),
                    ctx.window_height - static_cast<int>(60 * scale),
                    static_cast<int>(700 * scale),
                    label_font,
                    {150, 150, 150, 255});
    }
};

inline StateRegistrar<SettingsState> register_settings_state_{GameMode::Settings};

inline void SettingsState::update(GameContext& ctx, TextureManager& /*textures*/) {
    if (pending_action_ == SettingsAction::None)
        return;

    switch (pending_action_) {
        case SettingsAction::StartGame:
            clear_states(ctx, false);
            push_state(ctx, StateRegistry::instance().create(GameMode::Gen));
            break;
        case SettingsAction::Back:
            clear_states(ctx, false);
            push_state(ctx, StateRegistry::instance().create(GameMode::Menu));
            break;
        default:
            break;
    }
    pending_action_ = SettingsAction::None;
}

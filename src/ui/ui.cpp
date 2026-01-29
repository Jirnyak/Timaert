#include "ui/ui.h"

#include "rendering/renderer.h"
#include "rendering/text_renderer.h"

enum class RaIcon : std::uint16_t;

namespace {

// Global text renderer pointer (set by main.cpp)
TextRenderer* g_text_renderer = nullptr;

}  // namespace

void set_text_renderer(TextRenderer* renderer) {
    g_text_renderer = renderer;
}

TextRenderer* get_text_renderer() {
    return g_text_renderer;
}

void render_text(GameContext& /*ctx*/,
                 std::string_view text,
                 int x,
                 int y,
                 int width,
                 int height,
                 const Color& color,
                 int font_size) {
    if (text.empty() || !g_text_renderer)
        return;
    // Flush any pending texture batch before fontstash draws
    render_flush_batch();
    g_text_renderer->draw(text,
                          static_cast<float>(x),
                          static_cast<float>(y),
                          static_cast<float>(width),
                          static_cast<float>(height),
                          color,
                          font_size);
}

void render_icon(RaIcon icon, int x, int y, int size, const Color& color, int font_size) {
    if (!g_text_renderer)
        return;
    // Flush any pending texture batch before fontstash draws
    render_flush_batch();
    g_text_renderer->draw_icon(icon,
                               static_cast<float>(x),
                               static_cast<float>(y),
                               size,
                               color,
                               font_size);
}

void UIButtonGroup::render(GameContext& ctx) const {
    const bool input_enabled = ctx.ui_input_enabled;

    for (const auto& btn : buttons_) {
        if (input_enabled) {
            ctx.ui_hit_test.add(btn.rect);
        }
        const bool active = btn.is_active && btn.is_active();
        const bool hovered = input_enabled && btn.contains(ctx.curs_x, ctx.curs_y);

        Color fill = ui_color("#0F3460B4");
        if (active) {
            fill = ui_color("#16C79ADC");
        } else if (btn.pressed) {
            fill = ui_color("#11999EDC");
        } else if (hovered) {
            fill = ui_color("#1F6DB0D4");
        }
        const Color border = ui_color("#16C79A");
        render_draw_panel(btn.rect, fill, border);

        // Draw icon if present
        if (btn.icon.has_value()) {
            const Color icon_color = ui_color("#FFFFFF");
            const int icon_size = std::max(16, btn.rect.h * 2 / 3);
            const int icon_x = btn.rect.x + (btn.rect.w - icon_size) / 2;
            const int icon_y = btn.rect.y + (btn.rect.h - icon_size) / 2;
            render_icon(btn.icon.value(), icon_x, icon_y, icon_size, icon_color, icon_size);
        } else if (!btn.label.empty()) {
            const Color text_color = ui_color("#FFFFFF");
            const int font_size = std::max(12, btn.rect.h / 2);
            // Use actual text measurement for centering
            int text_width = 0;
            if (g_text_renderer) {
                Point const size = g_text_renderer->measure(btn.label, font_size);
                text_width = size.x;
            }
            const int text_x = btn.rect.x + (btn.rect.w - text_width) / 2;
            // Vertical center: center the font_size height in button
            const int text_y = btn.rect.y + (btn.rect.h - font_size) / 2;
            const int text_height = font_size + 4;
            render_text(ctx, btn.label, text_x, text_y, btn.rect.w, text_height, text_color, font_size);
        }
    }
}

void ui_init_speed_buttons(UIButtonGroup& group, GameContext& ctx, int fast_speed) {
    const UiButtonLayout layout = ui_default_button_layout(ctx);
    group.clear();
    group.add(UIButton{{layout.speed_start_x, layout.speed_y, layout.btn_size, layout.btn_size},
                       "||",
                       [&ctx]() {
                           ctx.set_paused(true);
                           ctx.set_speed(1);
                       },
                       [&ctx]() { return ctx.is_paused(); }});
    group.add(UIButton{{layout.speed_start_x + layout.btn_size + layout.margin,
                        layout.speed_y,
                        layout.btn_size,
                        layout.btn_size},
                       ">",
                       [&ctx]() {
                           ctx.set_paused(false);
                           ctx.set_speed(1);
                       },
                       [&ctx]() { return !ctx.is_paused() && ctx.speed() == 1; }});
    group.add(UIButton{{layout.speed_start_x + (layout.btn_size + layout.margin) * 2,
                        layout.speed_y,
                        layout.btn_size,
                        layout.btn_size},
                       ">>",
                       [&ctx, fast_speed]() {
                           ctx.set_paused(false);
                           ctx.set_speed(fast_speed);
                       },
                       [&ctx]() { return !ctx.is_paused() && ctx.speed() > 1; }});
}

void ui_init_move_buttons(UIButtonGroup& group,
                          const GameContext& ctx,
                          std::function<void()> on_up,
                          std::function<void()> on_left,
                          std::function<void()> on_center,
                          std::function<void()> on_right,
                          std::function<void()> on_down,
                          std::string center_label) {
    const UiButtonLayout layout = ui_default_button_layout(ctx);
    group.clear();
    group.add(UIButton{{layout.move_start_x + layout.btn_size + layout.margin,
                        layout.move_start_y,
                        layout.btn_size,
                        layout.btn_size},
                       "^",
                       std::move(on_up)});
    group.add(UIButton{{layout.move_start_x,
                        layout.move_start_y + layout.btn_size + layout.margin,
                        layout.btn_size,
                        layout.btn_size},
                       "<",
                       std::move(on_left)});
    group.add(UIButton{{layout.move_start_x + layout.btn_size + layout.margin,
                        layout.move_start_y + layout.btn_size + layout.margin,
                        layout.btn_size,
                        layout.btn_size},
                       std::move(center_label),
                       std::move(on_center)});
    group.add(UIButton{{layout.move_start_x + (layout.btn_size + layout.margin) * 2,
                        layout.move_start_y + layout.btn_size + layout.margin,
                        layout.btn_size,
                        layout.btn_size},
                       ">",
                       std::move(on_right)});
    group.add(UIButton{{layout.move_start_x + layout.btn_size + layout.margin,
                        layout.move_start_y + (layout.btn_size + layout.margin) * 2,
                        layout.btn_size,
                        layout.btn_size},
                       "v",
                       std::move(on_down)});
}

void MenuButtonList::render_and_handle(GameContext& ctx,
                                       int center_x,
                                       int start_y,
                                       int btn_width,
                                       int btn_height,
                                       int spacing,
                                       int cursor_x,
                                       int cursor_y,
                                       int pick_x,
                                       int pick_y,
                                       bool& picked) {
    const bool input_enabled = ctx.ui_input_enabled;
    int box_y = start_y;

    for (auto& item : items_) {
        Rect ui{};
        ui.w = btn_width;
        ui.h = btn_height;
        ui.x = center_x - ui.w / 2;
        ui.y = box_y;

        const bool disabled = item.is_disabled && item.is_disabled();

        if (input_enabled && !disabled) {
            ctx.ui_hit_test.add(ui);
        }

        const bool hovered = input_enabled && !disabled && ui_point_in_rect(cursor_x, cursor_y, ui);
        const bool touch_hit = input_enabled && !disabled && picked && ui_point_in_rect(pick_x, pick_y, ui);

        Color fill = disabled ? ui_color("#1A1A2080") : ui_color("#0F3460DC");
        if (hovered || touch_hit) {
            fill = ui_color("#16C79A");

            if (picked && touch_hit) {
                picked = false;
                if (item.on_click) {
                    item.on_click();
                }
            }
        }

        const Color border = disabled ? ui_color("#404050") : ui_color("#16C79A");
        render_draw_panel(ui, fill, border);

        // Calculate sizes for icon and text
        const Color icon_color = disabled ? ui_color("#606070") : ui_color("#FFFFFF");
        const Color text_color = disabled ? ui_color("#606070") : ui_color("#FFFFFF");
        const int font_size = std::max(14, btn_height / 2);
        const int icon_size = std::max(16, btn_height * 2 / 3);
        const int icon_text_gap = 12;  // Gap between icon and text
        
        // Measure text width
        int text_width = 0;
        if (!item.label.empty() && g_text_renderer) {
            Point const text_size = g_text_renderer->measure(item.label, font_size);
            text_width = text_size.x;
        }
        
        // Calculate total content width and center position
        int content_width = 0;
        if (item.icon.has_value() && !item.label.empty()) {
            content_width = icon_size + icon_text_gap + text_width;
        } else if (item.icon.has_value()) {
            content_width = icon_size;
        } else {
            content_width = text_width;
        }
        
        const int content_x = ui.x + (btn_width - content_width) / 2;
        
        // Draw icon if present
        if (item.icon.has_value()) {
            const int icon_x = content_x;
            const int icon_y = ui.y + (btn_height - icon_size) / 2;
            render_icon(item.icon.value(), icon_x, icon_y, icon_size, icon_color, icon_size);
        }

        // Draw label text
        if (!item.label.empty()) {
            int text_x = content_x;
            if (item.icon.has_value()) {
                text_x += icon_size + icon_text_gap;
            }
            const int text_y = ui.y + (btn_height - font_size) / 2;
            const int text_height = font_size + 4;
            render_text(ctx, item.label, text_x, text_y, btn_width - 40, text_height, text_color, font_size);
        }

        box_y += btn_height + spacing;
    }

    if (input_enabled && picked) {
        picked = false;
    }
}

bool inputbox(GameContext& /*ctx*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/, std::span<char> output, int /*type*/) {
    // Inputbox functionality removed - requires reimplementation with Sokol
    std::fill(output.begin(), output.end(), '\0');
    return false;
}

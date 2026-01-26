#pragma once

// UI utilities using Sokol rendering
// Replaces SDL-based ui.h

#include "core/game_context.h"
#include "core/gfx_types.h"
#include "rendering/renderer.h"
#include "rendering/ra_icon.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::size_t INPUT_BUFFER_SIZE = 64;

// ui_color is now in renderer.h - re-export for compatibility
using ::ui_color;
using ::ui_point_in_rect;
using ::ui_centered_rect;

inline void ui_clear_black() noexcept {
    render_clear(ui_color("#000000"));
}

inline void ui_fill_rect(const Rect& rect, const Color& color) noexcept {
    render_fill_rect(rect, color);
}

inline void ui_draw_rect(const Rect& rect, const Color& color) noexcept {
    render_draw_rect(rect, color);
}

inline void ui_draw_panel(const Rect& rect, const Color& fill, const Color& border) noexcept {
    render_draw_panel(rect, fill, border);
}

class TextRenderer;

// Global text renderer accessor (defined in ui.cpp)
TextRenderer* get_text_renderer();
void set_text_renderer(TextRenderer* renderer);

void render_text(GameContext& ctx,
                 std::string_view text,
                 int x,
                 int y,
                 int width,
                 int height,
                 const Color& color,
                 int font_size = 0);

void render_icon(RaIcon icon, int x, int y, int size, const Color& color, int font_size = 0);

struct UIButton {
    Rect rect{};
    std::string label;
    std::optional<RaIcon> icon{};
    std::function<void()> on_click;
    std::function<bool()> is_active;
    bool pressed = false;

    UIButton() = default;

    UIButton(Rect r,
             std::string lbl,
             std::function<void()> click,
             std::function<bool()> active = nullptr,
             std::optional<RaIcon> icn = std::nullopt)
        : rect(r), label(std::move(lbl)), icon(icn), on_click(std::move(click)),
          is_active(std::move(active)) {}

    [[nodiscard]] bool contains(int px, int py) const noexcept {
        return px >= rect.x && px < rect.x + rect.w && py >= rect.y && py < rect.y + rect.h;
    }
};

class UIButtonGroup {
private:
    std::vector<UIButton> buttons_;

public:
    void add(UIButton btn) {
        buttons_.push_back(std::move(btn));
    }

    void clear() {
        buttons_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return buttons_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return buttons_.size();
    }

    [[nodiscard]] bool contains(int px, int py) const noexcept {
        for (const auto& btn : buttons_) {
            if (btn.contains(px, py)) {
                return true;
            }
        }
        return false;
    }

    bool handle_press(int px, int py) {
        for (auto& btn : buttons_) {
            if (btn.contains(px, py)) {
                if (btn.on_click) {
                    btn.on_click();
                }
                // Don't keep pressed state - it's a click, not a hold
                return true;
            }
        }
        return false;
    }

    void reset_pressed() {
        for (auto& btn : buttons_) {
            btn.pressed = false;
        }
    }

    void render(GameContext& ctx) const;

    [[nodiscard]] std::vector<UIButton>& buttons() noexcept {
        return buttons_;
    }
    [[nodiscard]] const std::vector<UIButton>& buttons() const noexcept {
        return buttons_;
    }
};

struct UiButtonLayout {
    int btn_size = 0;
    int margin = 0;
    int speed_start_x = 0;
    int speed_y = 0;
    int move_start_x = 0;
    int move_start_y = 0;
};

[[nodiscard]] inline UiButtonLayout ui_default_button_layout(const GameContext& ctx) noexcept {
    UiButtonLayout layout{};
    layout.btn_size = std::min(ctx.window_width, ctx.window_height) / 10;
    layout.margin = layout.btn_size / 4;
    layout.speed_start_x = ctx.window_width - (layout.btn_size + layout.margin) * 3;
    layout.speed_y = ctx.window_height - layout.btn_size - layout.margin;
    layout.move_start_x = layout.margin;
    layout.move_start_y = ctx.window_height - layout.btn_size * 3 - layout.margin * 4;
    return layout;
}

void ui_init_speed_buttons(UIButtonGroup& group, GameContext& ctx, int fast_speed);

void ui_init_move_buttons(UIButtonGroup& group,
                          const GameContext& ctx,
                          std::function<void()> on_up,
                          std::function<void()> on_left,
                          std::function<void()> on_center,
                          std::function<void()> on_right,
                          std::function<void()> on_down,
                          std::string center_label = "o");

struct MenuItem {
    std::string label;
    std::optional<RaIcon> icon{};
    std::function<void()> on_click;

    MenuItem(std::string lbl, std::function<void()> click, std::optional<RaIcon> icn = std::nullopt)
        : label(std::move(lbl)), icon(icn), on_click(std::move(click)) {}
};

class MenuButtonList {
private:
    std::vector<MenuItem> items_;

public:
    void add(MenuItem item) {
        items_.push_back(std::move(item));
    }

    void clear() {
        items_.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return items_.size();
    }

    void render_and_handle(GameContext& ctx,
                           int center_x,
                           int start_y,
                           int btn_width,
                           int btn_height,
                           int spacing,
                           int cursor_x,
                           int cursor_y,
                           int pick_x,
                           int pick_y,
                           bool& picked);
};

[[nodiscard]] bool
inputbox(GameContext& ctx, int x, int y, int w, int h, std::span<char> output, int type = 0);

#pragma once

#include "core/game_context.h"
#include "rendering/ra_icon.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::size_t INPUT_BUFFER_SIZE = 64;

[[nodiscard]] constexpr inline uint8_t hex_digit(char c) noexcept {
    return (c >= '0' && c <= '9')   ? (c - '0')
           : (c >= 'a' && c <= 'f') ? (c - 'a' + 10)
           : (c >= 'A' && c <= 'F') ? (c - 'A' + 10)
                                    : 0;
}

[[nodiscard]] constexpr inline uint8_t hex_byte(char hi, char lo) noexcept {
    return (hex_digit(hi) << 4) | hex_digit(lo);
}

[[nodiscard]] constexpr inline SDL_Color ui_color(const char* s) noexcept {
    // Expect: "#RRGGBB" or "#RRGGBBAA"

    const uint8_t r = hex_byte(s[1], s[2]);
    const uint8_t g = hex_byte(s[3], s[4]);
    const uint8_t b = hex_byte(s[5], s[6]);

    const uint8_t a = (s[7] && s[8]) ? hex_byte(s[7], s[8]) : 255;

    return SDL_Color{r, g, b, a};
}

inline void ui_set_blend(SDL_Renderer* renderer, SDL_BlendMode mode) noexcept {
    if (!renderer)
        return;
    SDL_SetRenderDrawBlendMode(renderer, mode);
}

inline void ui_clear(SDL_Renderer* renderer, const SDL_Color& color) noexcept {
    if (!renderer)
        return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer);
}

inline void ui_clear_black(SDL_Renderer* renderer) noexcept {
    ui_clear(renderer, ui_color("#000000"));
}

inline void ui_fill_rect(SDL_Renderer* renderer,
                         const SDL_Rect& rect,
                         const SDL_Color& color,
                         SDL_BlendMode blend = SDL_BLENDMODE_BLEND) noexcept {
    if (!renderer)
        return;
    SDL_SetRenderDrawBlendMode(renderer, blend);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

inline void ui_draw_rect(SDL_Renderer* renderer,
                         const SDL_Rect& rect,
                         const SDL_Color& color,
                         SDL_BlendMode blend = SDL_BLENDMODE_BLEND) noexcept {
    if (!renderer)
        return;
    SDL_SetRenderDrawBlendMode(renderer, blend);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

inline void ui_draw_panel(SDL_Renderer* renderer,
                          const SDL_Rect& rect,
                          const SDL_Color& fill,
                          const SDL_Color& border,
                          SDL_BlendMode blend = SDL_BLENDMODE_BLEND) noexcept {
    ui_fill_rect(renderer, rect, fill, blend);
    ui_draw_rect(renderer, rect, border, blend);
}

[[nodiscard]] inline bool ui_point_in_rect(int x, int y, const SDL_Rect& rect) noexcept {
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

[[nodiscard]] inline SDL_Rect ui_centered_rect(int window_w, int window_h, int w, int h) noexcept {
    return SDL_Rect{window_w / 2 - w / 2, window_h / 2 - h / 2, w, h};
}

inline void render_text(GameContext& ctx,
                        std::string_view text,
                        int x,
                        int y,
                        int width,
                        int height,
                        const SDL_Color& color,
                        int font_size = 0) {
    if (!ctx.renderer || text.empty())
        return;
    ctx.text_renderer.set_renderer(ctx.renderer);
    ctx.text_renderer.draw(text, x, y, width, height, color, font_size);
}

struct UIButton {
    SDL_Rect rect{};
    std::string label;
    std::optional<RaIcon> icon{};
    std::function<void()> on_click;
    std::function<bool()> is_active;  // Optional: returns true if button should show as "active"
    bool pressed = false;

    UIButton() = default;

    UIButton(SDL_Rect r,
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
                btn.pressed = true;
                if (btn.on_click) {
                    btn.on_click();
                }
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

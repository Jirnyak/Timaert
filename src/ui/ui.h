#pragma once

#include "core/game_context.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <vector>

inline constexpr std::size_t INPUT_BUFFER_SIZE = 64;

[[nodiscard]] constexpr inline uint8_t hex_digit(char c) noexcept {
    return (c >= '0' && c <= '9') ? (c - '0') :
           (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
           (c >= 'A' && c <= 'F') ? (c - 'A' + 10) :
           0;
}

[[nodiscard]] constexpr inline uint8_t hex_byte(char hi, char lo) noexcept {
    return (hex_digit(hi) << 4) | hex_digit(lo);
}

[[nodiscard]] constexpr inline SDL_Color ui_color(const char* s) noexcept {
    // Expect: "#RRGGBB" or "#RRGGBBAA"

    const uint8_t r = hex_byte(s[1], s[2]);
    const uint8_t g = hex_byte(s[3], s[4]);
    const uint8_t b = hex_byte(s[5], s[6]);

    const uint8_t a = (s[7] && s[8])
        ? hex_byte(s[7], s[8])
        : 255;

    return SDL_Color{ r, g, b, a };
}

inline void ui_set_blend(SDL_Renderer* renderer, SDL_BlendMode mode) noexcept
{
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, mode);
}

inline void ui_clear(SDL_Renderer* renderer, const SDL_Color& color) noexcept
{
    if (!renderer) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer);
}

inline void ui_fill_rect(SDL_Renderer* renderer, const SDL_Rect& rect, const SDL_Color& color,
                         SDL_BlendMode blend = SDL_BLENDMODE_BLEND) noexcept
{
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, blend);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

inline void ui_draw_rect(SDL_Renderer* renderer, const SDL_Rect& rect, const SDL_Color& color,
                         SDL_BlendMode blend = SDL_BLENDMODE_BLEND) noexcept
{
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, blend);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &rect);
}

inline void ui_draw_panel(SDL_Renderer* renderer, const SDL_Rect& rect, const SDL_Color& fill,
                          const SDL_Color& border, SDL_BlendMode blend = SDL_BLENDMODE_BLEND) noexcept
{
    ui_fill_rect(renderer, rect, fill, blend);
    ui_draw_rect(renderer, rect, border, blend);
}

[[nodiscard]] inline bool ui_point_in_rect(int x, int y, const SDL_Rect& rect) noexcept
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

[[nodiscard]] inline SDL_Rect ui_centered_rect(int window_w, int window_h, int w, int h) noexcept
{
    return SDL_Rect{window_w / 2 - w / 2, window_h / 2 - h / 2, w, h};
}

inline void render_text(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
                        int x, int y, int width, int height, const SDL_Color& color)
{
    if (!renderer || !font || text.empty()) return;

    SDLSurfacePtr surface{TTF_RenderText_Solid(font, text.c_str(), color)};
    if (!surface) return;

    SDLTexturePtr texture{SDL_CreateTextureFromSurface(renderer, surface.get())};
    if (!texture) return;

    SDL_Rect rect = { x, y, width, height };
    SDL_RenderCopy(renderer, texture.get(), nullptr, &rect);
}

struct UIButton {
    SDL_Rect rect{};
    std::string label;
    std::function<void()> on_click;
    std::function<bool()> is_active;  // Optional: returns true if button should show as "active"
    bool pressed = false;

    UIButton() = default;

    UIButton(SDL_Rect r, std::string lbl, std::function<void()> click,
             std::function<bool()> active = nullptr)
        : rect(r)
        , label(std::move(lbl))
        , on_click(std::move(click))
        , is_active(std::move(active))
    {}

    [[nodiscard]] bool contains(int px, int py) const noexcept {
        return px >= rect.x && px < rect.x + rect.w &&
               py >= rect.y && py < rect.y + rect.h;
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

    void render(SDL_Renderer* renderer, TTF_Font* font) const {
        if (!renderer) return;

        for (const auto& btn : buttons_) {
            const bool active = btn.is_active && btn.is_active();

            SDL_Color fill = ui_color("#0F3460B4");
            if (active) {
                fill = ui_color("#16C79ADC");
            } else if (btn.pressed) {
                fill = ui_color("#11999EDC");
            }
            const SDL_Color border = ui_color("#16C79A");
            ui_draw_panel(renderer, btn.rect, fill, border);

            if (!btn.label.empty() && font) {
                const int text_w = static_cast<int>(btn.label.size()) * btn.rect.h / 3;
                const int text_h = btn.rect.h / 2;
                const int text_x = btn.rect.x + (btn.rect.w - text_w) / 2;
                const int text_y = btn.rect.y + (btn.rect.h - text_h) / 2;
                render_text(renderer, font, btn.label, text_x, text_y, text_w, text_h, ui_color("#FFFFFF"));
            }
        }
    }

    [[nodiscard]] std::vector<UIButton>& buttons() noexcept { return buttons_; }
    [[nodiscard]] const std::vector<UIButton>& buttons() const noexcept { return buttons_; }
};

struct MenuItem {
    std::string label;
    std::function<void()> on_click;

    MenuItem(std::string lbl, std::function<void()> click)
        : label(std::move(lbl)), on_click(std::move(click)) {}
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

    void render_and_handle(SDL_Renderer* renderer, TTF_Font* font,
                           int center_x, int start_y, int btn_width, int btn_height, int spacing,
                           int cursor_x, int cursor_y, int pick_x, int pick_y, bool& picked) {
        if (!renderer) return;

        int box_y = start_y;

        for (auto& item : items_) {
            SDL_Rect ui{};
            ui.w = btn_width;
            ui.h = btn_height;
            ui.x = center_x - ui.w / 2;
            ui.y = box_y;

            const bool hovered = ui_point_in_rect(cursor_x, cursor_y, ui);
            const bool touch_hit = picked && ui_point_in_rect(pick_x, pick_y, ui);

            SDL_Color fill = ui_color("#0F3460DC");
            if (hovered || touch_hit) {
                fill = ui_color("#16C79A");

                if (picked && touch_hit) {
                    picked = false;
                    if (item.on_click) {
                        item.on_click();
                    }
                }
            }

            const SDL_Color border = ui_color("#16C79A");
            ui_draw_panel(renderer, ui, fill, border);

            if (font && !item.label.empty()) {
                render_text(renderer, font, item.label,
                            ui.x + ui.w / 4, ui.y + ui.h / 4,
                            ui.w / 2, ui.h / 2,
                            ui_color("#FFFFFF"));
            }

            box_y += btn_height + spacing;
        }

        if (picked) {
            picked = false;
        }
    }
};

[[nodiscard]] inline bool inputbox(SDL_Renderer* renderer, TTF_Font* font,
                                   int x, int y, int w, int h,
                                   std::span<char> output, int type = 0)
{
    if (!renderer || !font || output.empty()) return false;

    SDL_Event e;
    bool done = false;
    std::string inputStr;
    inputStr.reserve(output.size());

    SDL_StartTextInput();

    const SDL_Rect boxRect = { x, y, w, h };

    std::fill(output.begin(), output.end(), '\0');

    while (!done)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
            {
                SDL_StopTextInput();
                return false;
            }
            else if (e.type == SDL_KEYDOWN)
            {
                if (e.key.keysym.sym == SDLK_BACKSPACE && !inputStr.empty())
                {
                    inputStr.pop_back();
                }
                else if (e.key.keysym.sym == SDLK_RETURN ||
                         e.key.keysym.sym == SDLK_KP_ENTER)
                {
                    done = true;
                }
            }
            else if (e.type == SDL_TEXTINPUT)
            {
                const char c = e.text.text[0];

                if (inputStr.size() < output.size() - 1)
                {
                    if (type == 1)
                    {
                        if (std::isdigit(static_cast<unsigned char>(c)))
                            inputStr += c;
                    }
                    else
                    {
                        inputStr += c;
                    }
                }
            }
        }

        ui_draw_rect(renderer, boxRect, ui_color("#FFFFFF"), SDL_BLENDMODE_NONE);

        if (!inputStr.empty())
        {
            constexpr SDL_Color color = { 255, 255, 255, 255 };
            SDLSurfacePtr surface{TTF_RenderText_Solid(font, inputStr.c_str(), color)};
            if (surface)
            {
                SDLTexturePtr texture{SDL_CreateTextureFromSurface(renderer, surface.get())};
                if (texture)
                {
                    SDL_Rect textRect = { x + 5, y + 5, surface->w, surface->h };
                    SDL_RenderCopy(renderer, texture.get(), nullptr, &textRect);
                }
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    SDL_StopTextInput();

    const std::size_t len = std::min(inputStr.size(), output.size() - 1);
    std::copy_n(inputStr.c_str(), len, output.data());
    output[len] = '\0';

    return true;
}

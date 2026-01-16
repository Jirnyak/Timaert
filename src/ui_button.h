#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>
#include <vector>
#include <string>
#include <cstring>

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
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        
        for (const auto& btn : buttons_) {
            const bool active = btn.is_active && btn.is_active();
            
            if (active) {
                SDL_SetRenderDrawColor(renderer, 22, 199, 154, 220);
            } else if (btn.pressed) {
                SDL_SetRenderDrawColor(renderer, 17, 153, 158, 220);
            } else {
                SDL_SetRenderDrawColor(renderer, 15, 52, 96, 180);
            }
            SDL_RenderFillRect(renderer, &btn.rect);
            
            SDL_SetRenderDrawColor(renderer, 22, 199, 154, 255);
            SDL_RenderDrawRect(renderer, &btn.rect);
            
            if (!btn.label.empty() && font) {
                const int text_w = static_cast<int>(btn.label.size()) * btn.rect.h / 3;
                const int text_h = btn.rect.h / 2;
                const int text_x = btn.rect.x + (btn.rect.w - text_w) / 2;
                const int text_y = btn.rect.y + (btn.rect.h - text_h) / 2;
                
                SDL_Color color = {255, 255, 255, 255};
                SDL_Surface* surface = TTF_RenderText_Solid(font, btn.label.c_str(), color);
                if (surface) {
                    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                    if (texture) {
                        SDL_Rect dest = {text_x, text_y, text_w, text_h};
                        SDL_RenderCopy(renderer, texture, nullptr, &dest);
                        SDL_DestroyTexture(texture);
                    }
                    SDL_FreeSurface(surface);
                }
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
        int box_y = start_y;
        
        for (auto& item : items_) {
            SDL_Rect ui{};
            ui.w = btn_width;
            ui.h = btn_height;
            ui.x = center_x - ui.w / 2;
            ui.y = box_y;
            
            const bool hovered = (cursor_x > ui.x && cursor_x < ui.x + ui.w && 
                                  cursor_y > ui.y && cursor_y < ui.y + ui.h);
            
            const bool touch_hit = picked && 
                                   (pick_x > ui.x && pick_x < ui.x + ui.w && 
                                    pick_y > ui.y && pick_y < ui.y + ui.h);
            
            if (hovered || touch_hit) {
                SDL_SetRenderDrawColor(renderer, 22, 199, 154, 255);
                
                if (picked && touch_hit) {
                    picked = false;
                    if (item.on_click) {
                        item.on_click();
                    }
                }
            } else {
                SDL_SetRenderDrawColor(renderer, 15, 52, 96, 220);
            }
            
            SDL_RenderFillRect(renderer, &ui);
            
            SDL_SetRenderDrawColor(renderer, 22, 199, 154, 255);
            SDL_RenderDrawRect(renderer, &ui);
            
            if (font && !item.label.empty()) {
                SDL_Color color = {255, 255, 255, 255};
                SDL_Surface* surface = TTF_RenderText_Solid(font, item.label.c_str(), color);
                if (surface) {
                    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                    if (texture) {
                        SDL_Rect dest = {ui.x + ui.w / 4, ui.y + ui.h / 4, ui.w / 2, ui.h / 2};
                        SDL_RenderCopy(renderer, texture, nullptr, &dest);
                        SDL_DestroyTexture(texture);
                    }
                    SDL_FreeSurface(surface);
                }
            }
            
            box_y += btn_height + spacing;
        }
        
        if (picked) {
            picked = false;
        }
    }
};

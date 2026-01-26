#include "ui/ui.h"
#include <SDL_events.h>

void UIButtonGroup::render(GameContext& ctx) const {
    if (!ctx.renderer)
        return;

    ctx.text_renderer.set_renderer(ctx.renderer);
    const bool input_enabled = ctx.ui_input_enabled;

    for (const auto& btn : buttons_) {
        if (input_enabled) {
            ctx.ui_hit_test.add(btn.rect);
        }
        const bool active = btn.is_active && btn.is_active();
        const bool hovered = input_enabled && btn.contains(ctx.curs_x, ctx.curs_y);

        SDL_Color fill = ui_color("#0F3460B4");
        if (active) {
            fill = ui_color("#16C79ADC");
        } else if (btn.pressed) {
            fill = ui_color("#11999EDC");
        } else if (hovered) {
            fill = ui_color("#1F6DB0D4");
        }
        const SDL_Color border = ui_color("#16C79A");
        ui_draw_panel(ctx.renderer, btn.rect, fill, border);

        if (!btn.label.empty() || btn.icon) {
            const SDL_Color text_color = ui_color("#FFFFFF");
            const int font_size = std::max(12, btn.rect.h / 2);
            const int icon_size = std::max(12, btn.rect.h * 3 / 5);
            const int icon_pad = std::max(6, btn.rect.h / 6);
            const SDL_Point text_size = btn.label.empty()
                                            ? SDL_Point{0, 0}
                                            : ctx.text_renderer.measure(btn.label, font_size);

            if (btn.icon && btn.label.empty()) {
                ctx.text_renderer.draw_icon(*btn.icon,
                                            btn.rect.x + (btn.rect.w - icon_size) / 2,
                                            btn.rect.y + (btn.rect.h - icon_size) / 2,
                                            icon_size,
                                            text_color);
                continue;
            }

            int content_width = text_size.x;
            if (btn.icon) {
                content_width += icon_size + icon_pad;
            }
            const int start_x = btn.rect.x + (btn.rect.w - content_width) / 2;
            const int icon_x = start_x;
            const int text_x = btn.icon ? (start_x + icon_size + icon_pad) : start_x;
            const int text_y = btn.rect.y + (btn.rect.h - text_size.y) / 2;

            if (btn.icon) {
                ctx.text_renderer.draw_icon(*btn.icon,
                                            icon_x,
                                            btn.rect.y + (btn.rect.h - icon_size) / 2,
                                            icon_size,
                                            text_color);
            }

            if (!btn.label.empty()) {
                render_text(ctx,
                            btn.label,
                            text_x,
                            text_y,
                            text_size.x,
                            text_size.y,
                            text_color,
                            font_size);
            }
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
    if (!ctx.renderer)
        return;

    ctx.text_renderer.set_renderer(ctx.renderer);
    const bool input_enabled = ctx.ui_input_enabled;

    int box_y = start_y;

    for (auto& item : items_) {
        SDL_Rect ui{};
        ui.w = btn_width;
        ui.h = btn_height;
        ui.x = center_x - ui.w / 2;
        ui.y = box_y;

        if (input_enabled) {
            ctx.ui_hit_test.add(ui);
        }

        const bool hovered = input_enabled && ui_point_in_rect(cursor_x, cursor_y, ui);
        const bool touch_hit = input_enabled && picked && ui_point_in_rect(pick_x, pick_y, ui);

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
        ui_draw_panel(ctx.renderer, ui, fill, border);

        if (!item.label.empty() || item.icon) {
            const SDL_Color text_color = ui_color("#FFFFFF");
            const int font_size = std::max(12, ui.h / 2);
            const int icon_size = std::max(12, ui.h * 3 / 5);
            const int icon_pad = std::max(6, ui.h / 6);
            const SDL_Point text_size = item.label.empty()
                                            ? SDL_Point{0, 0}
                                            : ctx.text_renderer.measure(item.label, font_size);

            if (item.icon && item.label.empty()) {
                ctx.text_renderer.draw_icon(*item.icon,
                                            ui.x + (ui.w - icon_size) / 2,
                                            ui.y + (ui.h - icon_size) / 2,
                                            icon_size,
                                            text_color);
            } else {
                int content_width = text_size.x;
                if (item.icon) {
                    content_width += icon_size + icon_pad;
                }
                const int start_x = ui.x + (ui.w - content_width) / 2;
                const int icon_x = start_x;
                const int text_x = item.icon ? (start_x + icon_size + icon_pad) : start_x;
                const int text_y = ui.y + (ui.h - text_size.y) / 2;

                if (item.icon) {
                    ctx.text_renderer.draw_icon(*item.icon,
                                                icon_x,
                                                ui.y + (ui.h - icon_size) / 2,
                                                icon_size,
                                                text_color);
                }

                if (!item.label.empty()) {
                    render_text(ctx,
                                item.label,
                                text_x,
                                text_y,
                                text_size.x,
                                text_size.y,
                                text_color,
                                font_size);
                }
            }
        }

        box_y += btn_height + spacing;
    }

    if (input_enabled && picked) {
        picked = false;
    }
}

bool inputbox(GameContext& ctx, int x, int y, int w, int h, std::span<char> output, int type) {
    if (!ctx.renderer || output.empty())
        return false;

    SDL_Event e;
    bool done = false;
    std::string inputStr;
    inputStr.reserve(output.size());

    SDL_StartTextInput();

    const SDL_Rect boxRect = {x, y, w, h};

    std::fill(output.begin(), output.end(), '\0');

    while (!done) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                SDL_StopTextInput();
                return false;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_BACKSPACE && !inputStr.empty()) {
                    inputStr.pop_back();
                } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                    done = true;
                }
            } else if (e.type == SDL_TEXTINPUT) {
                const char c = e.text.text[0];

                if (inputStr.size() < output.size() - 1) {
                    if (type == 1) {
                        if (std::isdigit(static_cast<unsigned char>(c)))
                            inputStr += c;
                    } else {
                        inputStr += c;
                    }
                }
            }
        }

        ui_draw_rect(ctx.renderer, boxRect, ui_color("#FFFFFF"), SDL_BLENDMODE_NONE);

        if (!inputStr.empty()) {
            constexpr SDL_Color color = {255, 255, 255, 255};
            const int font_size = std::max(12, h - 10);
            const SDL_Point text_size = ctx.text_renderer.measure(inputStr, font_size);
            render_text(ctx, inputStr, x + 5, y + 5, text_size.x, text_size.y, color, font_size);
        }

        SDL_RenderPresent(ctx.renderer);
        SDL_Delay(10);
    }

    SDL_StopTextInput();

    const std::size_t len = std::min(inputStr.size(), output.size() - 1);
    std::copy_n(inputStr.c_str(), len, output.data());
    output[len] = '\0';

    return true;
}

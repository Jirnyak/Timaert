#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include <charconv>
#include <string>

class SettingsState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Settings; }

private:
    enum class Focus : uint8_t {
        Seed,
        Continents,
        Water,
        Confirm,
        Back,
        None
    };

    Focus focus_ = Focus::Seed;
    InputManager input_manager_;
    std::string seed_buffer_;
    bool settings_initialized_ = false;

    void init_settings(GameContext& ctx) {
        seed_buffer_ = ctx.seed_input;
        settings_initialized_ = true;
    }

    void render_option(GameContext& ctx, const std::string& label, int value, int y, Focus current_focus, int option_width) {
        const int label_x = ctx.window_width / 2 - 250;
        const int value_x = ctx.window_width / 2 + 50;

        // Render label
        render_text(ctx, label, label_x, y, 200, 30, {255, 255, 255, 255});

        // Render value box
        SDL_Rect value_rect = {value_x, y, option_width, 30};
        SDL_Color border_color = (current_focus != Focus::None) ? ui_color("#16C79A") : ui_color("#0F3460");
        ui_draw_panel(ctx.renderer, value_rect, ui_color("#0B1D2A"), border_color);

        render_text(ctx, std::to_string(value), value_x + 10, y + 3, option_width - 20, 24, {255, 255, 255, 255});

        // Render arrow hints
        if (current_focus != Focus::None) {
            render_text(ctx, "< >", value_x + option_width + 20, y + 3, 60, 24, {150, 150, 150, 255});
        }
    }

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!settings_initialized_) init_settings(ctx);

        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
                case SDLK_UP:
                    if (focus_ == Focus::Continents) focus_ = Focus::Seed;
                    else if (focus_ == Focus::Water) focus_ = Focus::Continents;
                    else if (focus_ == Focus::Confirm) focus_ = Focus::Water;
                    else if (focus_ == Focus::Back) focus_ = Focus::Confirm;
                    else focus_ = Focus::Back;
                    break;

                case SDLK_DOWN:
                    if (focus_ == Focus::Seed) focus_ = Focus::Continents;
                    else if (focus_ == Focus::Continents) focus_ = Focus::Water;
                    else if (focus_ == Focus::Water) focus_ = Focus::Confirm;
                    else if (focus_ == Focus::Confirm) focus_ = Focus::Back;
                    else focus_ = Focus::Seed;
                    break;

                case SDLK_LEFT:
                    if (focus_ == Focus::Continents && ctx.num_continents > 0) ctx.num_continents--;
                    else if (focus_ == Focus::Water && ctx.water_amount > 0) ctx.water_amount--;
                    break;

                case SDLK_RIGHT:
                    if (focus_ == Focus::Continents && ctx.num_continents < 6) ctx.num_continents++;
                    else if (focus_ == Focus::Water && ctx.water_amount < 6) ctx.water_amount++;
                    break;

                case SDLK_BACKSPACE:
                    if (focus_ == Focus::Seed && !seed_buffer_.empty()) {
                        seed_buffer_.pop_back();
                    }
                    break;

                case SDLK_RETURN:
                    if (focus_ == Focus::Seed || focus_ == Focus::Continents || focus_ == Focus::Water) {
                        // Do nothing, just allow navigation
                    } else if (focus_ == Focus::Confirm) {
                        // Apply settings and go to generation
                        ctx.seed_input = seed_buffer_;
                        if (!seed_buffer_.empty()) {
                            const char* begin = seed_buffer_.data();
                            const char* end = begin + seed_buffer_.size();
                            std::uint32_t parsed = 0;
                            const auto result = std::from_chars(begin, end, parsed);
                            if (result.ec == std::errc() && result.ptr == end) {
                                ctx.seed = parsed;
                            } else {
                                ctx.seed = std::random_device{}();
                            }
                        } else {
                            ctx.seed = std::random_device{}();
                        }
                        enter_gen(ctx);
                    } else if (focus_ == Focus::Back) {
                        // Return to menu
                        enter_menu(ctx);
                    }
                    break;

                case SDLK_ESCAPE:
                    enter_menu(ctx);
                    break;

                default:
                    // Handle numeric input for seed
                    if (focus_ == Focus::Seed && event.key.keysym.sym >= SDLK_0 && event.key.keysym.sym <= SDLK_9) {
                        if (seed_buffer_.length() < 10) {
                            seed_buffer_ += static_cast<char>('0' + (event.key.keysym.sym - SDLK_0));
                        }
                    }
                    break;
            }
        }
    }

    void update(GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
    }

    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        SDL_Rect bg = textures.tile_background();
        SDL_RenderCopy(ctx.renderer, textures.bg(0), nullptr, &bg);

        // Title
        render_text(ctx, "Map Generation Settings", ctx.window_width / 2 - 200, 50, 400, 40, {255, 200, 100, 255});

        // Seed input
        int y = 130;
        const int seed_input_width = 200;
        const int label_x = ctx.window_width / 2 - 250;
        const int input_x = ctx.window_width / 2 + 50;

        render_text(ctx, "Seed (random if empty):", label_x, y, 200, 30, {255, 255, 255, 255});

        SDL_Rect seed_rect = {input_x, y, seed_input_width, 30};
        SDL_Color seed_border = (focus_ == Focus::Seed) ? ui_color("#16C79A") : ui_color("#0F3460");
        ui_draw_panel(ctx.renderer, seed_rect, ui_color("#0B1D2A"), seed_border);

        std::string display_seed = seed_buffer_.empty() ? "random" : seed_buffer_;
        render_text(ctx, display_seed, input_x + 10, y + 3, seed_input_width - 20, 24, {200, 200, 200, 255});

        // Continents
        y += 80;
        render_option(ctx, "Continents:", ctx.num_continents, y, focus_, 100);

        // Water amount
        y += 80;
        render_option(ctx, "Water Amount:", ctx.water_amount, y, focus_, 100);

        // Buttons
        y += 100;

        // Confirm button
        SDL_Rect confirm_rect = {ctx.window_width / 2 - 100, y, 90, 35};
        SDL_Color confirm_border = (focus_ == Focus::Confirm) ? ui_color("#16C79A") : ui_color("#0F3460");
        ui_draw_panel(ctx.renderer, confirm_rect, ui_color("#0B1D2A"), confirm_border);
        render_text(ctx, "Generate", confirm_rect.x + 5, confirm_rect.y + 5, confirm_rect.w - 10, confirm_rect.h - 10, {255, 255, 255, 255});

        // Back button
        SDL_Rect back_rect = {ctx.window_width / 2 + 20, y, 80, 35};
        SDL_Color back_border = (focus_ == Focus::Back) ? ui_color("#16C79A") : ui_color("#0F3460");
        ui_draw_panel(ctx.renderer, back_rect, ui_color("#0B1D2A"), back_border);
        render_text(ctx, "Back", back_rect.x + 15, back_rect.y + 5, back_rect.w - 30, back_rect.h - 10, {255, 255, 255, 255});

        // Help text
        render_text(ctx, "Use arrow keys to navigate and adjust values. Enter to confirm.", 
                    ctx.window_width / 2 - 350, ctx.window_height - 60, 700, 30, {150, 150, 150, 255});
    }
};

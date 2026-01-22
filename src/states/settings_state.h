#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include <string>
#include <charconv>

class SettingsState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Settings; }

private:
    UIButtonGroup buttons_;
    bool buttons_initialized_ = false;
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

    void init_buttons(GameContext& ctx) {
        buttons_.clear();
        
        // Continents decrease
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 200, ctx.window_height / 2 - 80, 40, 40},
            "-",
            [&ctx]() {
                if (ctx.num_continents > 0) ctx.num_continents--;
            }
        });
        
        // Continents increase
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 + 160, ctx.window_height / 2 - 80, 40, 40},
            "+",
            [&ctx]() {
                if (ctx.num_continents < 10) ctx.num_continents++;
            }
        });
        
        // Water decrease
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 200, ctx.window_height / 2, 40, 40},
            "-",
            [&ctx]() {
                if (ctx.water_amount > 0) ctx.water_amount--;
            }
        });
        
        // Water increase
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 + 160, ctx.window_height / 2, 40, 40},
            "+",
            [&ctx]() {
                if (ctx.water_amount < 10) ctx.water_amount++;
            }
        });
        
        // Generate button
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 70, ctx.window_height / 2 + 100, 140, 45},
            "Generate",
            [this, &ctx]() {
                apply_seed(ctx);
                pending_action_ = SettingsAction::StartGame;
            }
        });
        
        // Back button
        buttons_.add(UIButton{
            SDL_Rect{ctx.window_width / 2 - 70, ctx.window_height / 2 + 160, 140, 45},
            "Back",
            [this]() { pending_action_ = SettingsAction::Back; }
        });
        
        buttons_initialized_ = true;
    }

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!buttons_initialized_) init_buttons(ctx);
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Press)
            {
                buttons_.handle_press(evt.x, evt.y);
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                pending_action_ = SettingsAction::Back;
            }
            else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                if (!seed_input_.empty()) {
                    seed_input_.pop_back();
                }
            }
            else if (event.key.keysym.sym == SDLK_RETURN) {
                apply_seed(ctx);
                pending_action_ = SettingsAction::StartGame;
            }
            else if (event.key.keysym.sym >= SDLK_0 && event.key.keysym.sym <= SDLK_9) {
                if (seed_input_.length() < 10) {
                    seed_input_ += static_cast<char>('0' + (event.key.keysym.sym - SDLK_0));
                }
            }
            else {
                handle_fullscreen_key(ctx, event.key.keysym.sym);
            }
        }
    }

    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override;

    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        SDL_Rect bg = textures.tile_background();
        SDL_RenderCopy(ctx.renderer, textures.bg(0), nullptr, &bg);

        // Title
        render_text(ctx, "Map Generation Settings", ctx.window_width / 2 - 200, 50, 400, 40, {255, 200, 100, 255});

        // Seed input label
        render_text(ctx, "Seed:", ctx.window_width / 2 - 200, ctx.window_height / 2 - 160, 150, 30, {200, 200, 200, 255});
        
        // Seed input box
        SDL_Rect seed_box = {ctx.window_width / 2 - 50, ctx.window_height / 2 - 160, 200, 35};
        SDL_SetRenderDrawColor(ctx.renderer, 50, 50, 60, 255);
        SDL_RenderFillRect(ctx.renderer, &seed_box);
        SDL_SetRenderDrawColor(ctx.renderer, 100, 150, 200, 255);
        SDL_RenderDrawRect(ctx.renderer, &seed_box);
        
        // Seed input text
        render_text(ctx, seed_input_.empty() ? "(random)" : seed_input_, ctx.window_width / 2 - 40, ctx.window_height / 2 - 155, 180, 25, {150, 200, 255, 255});

        // Continents label and value
        render_text(ctx, "Continents:", ctx.window_width / 2 - 200, ctx.window_height / 2 - 95, 150, 30, {200, 200, 200, 255});
        render_text(ctx, std::to_string(ctx.num_continents) + " / 10", ctx.window_width / 2 - 50, ctx.window_height / 2 - 95, 150, 30, {255, 255, 100, 255});

        // Water label and value
        render_text(ctx, "Water:", ctx.window_width / 2 - 200, ctx.window_height / 2 - 15, 150, 30, {200, 200, 200, 255});
        render_text(ctx, std::to_string(ctx.water_amount) + " / 10", ctx.window_width / 2 - 50, ctx.window_height / 2 - 15, 150, 30, {100, 150, 255, 255});

        // Render buttons
        buttons_.render(ctx);

        // Instructions
        render_text(ctx, "Type seed number and press Enter. Tap +/- to adjust settings.", 
                    ctx.window_width / 2 - 350, ctx.window_height - 60, 700, 30, {150, 150, 150, 255});
    }
};

inline StateRegistrar<SettingsState> register_settings_state_{GameMode::Settings};

inline void SettingsState::update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/)
{
    if (pending_action_ == SettingsAction::None) return;
    
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

#pragma once

#include "core/game_state.h"
#include "rendering/renderer.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include "rendering/ra_icon.h"
#include "systems/save_game.h"

class MenuState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Menu;
    }

private:
    MenuButtonList menu_;
    UIButtonGroup corner_buttons_;
    bool menu_initialized_ = false;
    bool save_exists_ = false;
    InputManager input_manager_;

    enum class MenuAction : std::uint8_t { None, NewGame, Settings, Labyrinth, Load, Exit };
    MenuAction pending_action_ = MenuAction::None;

    void init_menu() {
        menu_.clear();

        menu_.add(MenuItem{"New Game",
                           [this]() { pending_action_ = MenuAction::NewGame; },
                           RaIcon::Flower});
        menu_.add(MenuItem{"Settings",
                           [this]() { pending_action_ = MenuAction::Settings; },
                           RaIcon::Tower});
        menu_.add(MenuItem{"Labyrinth",
                           [this]() { pending_action_ = MenuAction::Labyrinth; },
                           RaIcon::Tower});
        menu_.add(MenuItem{"Load",
                           [this]() { pending_action_ = MenuAction::Load; },
                           RaIcon::Load,
                           [this]() { return !save_exists_; }});
#ifndef __EMSCRIPTEN__
        menu_.add(
            MenuItem{"Exit", [this]() { pending_action_ = MenuAction::Exit; }, RaIcon::Reverse});
#endif

        menu_initialized_ = true;
    }

    void init_corner_buttons(GameContext& ctx) {
        corner_buttons_.clear();
        const int btn_size = std::max(24, std::min(ctx.window_width, ctx.window_height) / 14);
        const int margin = std::max(8, btn_size / 3);
        const Rect rect{ctx.window_width - btn_size - margin, margin, btn_size, btn_size};
        corner_buttons_.add(UIButton{rect,
                                     "",
                                     [&ctx]() { ctx.sound_manager.toggle_mute(); },
                                     [&ctx]() { return ctx.sound_manager.is_muted(); },
                                     RaIcon::Bell});
    }

    void process_pending_action(GameContext& ctx) {
        const MenuAction action = pending_action_;
        pending_action_ = MenuAction::None;
        
        switch (action) {
            case MenuAction::NewGame:
                replace_state(ctx, StateRegistry::instance().create(GameMode::Gen), false);
                break;
            case MenuAction::Settings:
                replace_state(ctx, StateRegistry::instance().create(GameMode::Settings));
                break;
            case MenuAction::Labyrinth:
                replace_state(ctx, StateRegistry::instance().create(GameMode::Labyrinth), false);
                break;
            case MenuAction::Load:
                replace_state(ctx, StateRegistry::instance().create(GameMode::Load), false);
                break;
            case MenuAction::Exit:
                ctx.quit = true;
                break;
            default:
                break;
        }
    }

public:
    void update(GameContext& ctx, TextureManager& /*textures*/) override {
        if (pending_action_ != MenuAction::None) {
            process_pending_action(ctx);
        }
    }

    void render(GameContext& ctx, TextureManager& textures) override {
        render_texture(textures.bg(0), {0, 0, ctx.window_width, ctx.window_height});

        if (!menu_initialized_) {
            save_exists_ = save_game::save_exists(ctx);
            init_menu();
        }
        init_corner_buttons(ctx);
        // Handle clicks on corner buttons
        if (ctx.picked) {
            corner_buttons_.handle_press(ctx.pick_x, ctx.pick_y);
        }
        corner_buttons_.render(ctx);

        menu_.render_and_handle(ctx,
                                ctx.window_width / 2,
                                ctx.window_height / 3,
                                ctx.window_width / 3,
                                ctx.window_height / 10,
                                20,
                                ctx.curs_x,
                                ctx.curs_y,
                                ctx.pick_x,
                                ctx.pick_y,
                                ctx.picked);
    }
};

inline StateRegistrar<MenuState> register_menu_state_{GameMode::Menu};

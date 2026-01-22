#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class MenuState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Menu; }

private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;
    InputManager input_manager_;
    
    enum class MenuAction : std::uint8_t { None, NewGame, Settings, Labyrinth, Load, Exit };
    MenuAction pending_action_ = MenuAction::None;
    
    void init_menu() {
        menu_.clear();
        
        menu_.add(MenuItem{"New Game", [this]() { pending_action_ = MenuAction::NewGame; }, RaIcon::Flower});
        menu_.add(MenuItem{"Settings", [this]() { pending_action_ = MenuAction::Settings; }, RaIcon::Tower});
        menu_.add(MenuItem{"Labyrinth", [this]() { pending_action_ = MenuAction::Labyrinth; }, RaIcon::Tower});
        menu_.add(MenuItem{"Load", [this]() { pending_action_ = MenuAction::Load; }, RaIcon::Load});
#ifndef __EMSCRIPTEN__
        menu_.add(MenuItem{"Exit", [this]() { pending_action_ = MenuAction::Exit; }, RaIcon::Reverse});
#endif
        
        menu_initialized_ = true;
    }
    
    void process_pending_action(GameContext& ctx) {
        switch (pending_action_) {
            case MenuAction::NewGame:
                clear_states(ctx, false);
                push_state(ctx, StateRegistry::instance().create(GameMode::Gen));
                break;
            case MenuAction::Settings:
                replace_state(ctx, StateRegistry::instance().create(GameMode::Settings));
                break;
            case MenuAction::Labyrinth:
                clear_states(ctx, false);
                push_state(ctx, StateRegistry::instance().create(GameMode::Labyrinth));
                break;
            case MenuAction::Load:
                clear_states(ctx, false);
                push_state(ctx, StateRegistry::instance().create(GameMode::Load));
                break;
            case MenuAction::Exit:
                ctx.quit = true;
                break;
            default:
                break;
        }
        pending_action_ = MenuAction::None;
    }
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (!menu_initialized_) init_menu();
        
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Press)
            {
                set_pick(ctx, evt.x, evt.y);
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            handle_fullscreen_key(ctx, event.key.keysym.sym);
        }
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (pending_action_ != MenuAction::None) {
            process_pending_action(ctx);
        }
    }
    
    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        SDL_Rect bg = textures.tile_background();
        SDL_RenderCopy(ctx.renderer, textures.bg(0), nullptr, &bg);

        menu_.render_and_handle(
            ctx,
            ctx.window_width / 2, ctx.window_height / 3,
            ctx.window_width / 3, ctx.window_height / 10, 20,
            ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked
        );
    }
};

inline StateRegistrar<MenuState> register_menu_state_{GameMode::Menu};

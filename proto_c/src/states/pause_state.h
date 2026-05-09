#pragma once

#include "core/game_state.h"
#include "systems/save_game.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class LoadState;
class MenuState;
class WorldManager;

class PauseState : public GameState
{
public:
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Pause; }
    [[nodiscard]] bool is_overlay() const noexcept override { return true; }

private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;
    InputManager input_manager_;
    
    enum class PauseAction : std::uint8_t { None, Resume, Save, Load, MainMenu, Exit };
    PauseAction pending_action_ = PauseAction::None;

    void init_menu() {
        menu_.clear();
        
        menu_.add(MenuItem{"Resume", [this]() { pending_action_ = PauseAction::Resume; }, RaIcon::Forward});
        menu_.add(MenuItem{"Save", [this]() { pending_action_ = PauseAction::Save; }, RaIcon::Save});
        menu_.add(MenuItem{"Load", [this]() { pending_action_ = PauseAction::Load; }, RaIcon::Load});
        menu_.add(MenuItem{"To main menu", [this]() { pending_action_ = PauseAction::MainMenu; }, RaIcon::CastleEmblem});
#ifndef __EMSCRIPTEN__
        menu_.add(MenuItem{"Exit", [this]() { pending_action_ = PauseAction::Exit; }, RaIcon::Reverse});
#endif
        
        menu_initialized_ = true;
    }
    
public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/) override
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
        else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
        {
            pending_action_ = PauseAction::Resume;
        }
    }
    
    void update(GameContext& ctx, TextureManager& /*textures*/) override;
    
    
    void render(GameContext& ctx, TextureManager& /*textures*/) override
    {
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, ui_color("#000000B4"));

        const int title_h = ctx.window_height / 12;
        const std::string title = "PAUSED";
        render_text(ctx, title, 
                    ctx.window_width / 2 - static_cast<int>(title.size()) * title_h / 4, 
                    ctx.window_height / 6, 
                    static_cast<int>(title.size()) * title_h / 2, title_h, 
                    {255, 255, 255, 255});

        menu_.render_and_handle(
            ctx,
            ctx.window_width / 2, ctx.window_height / 3,
            ctx.window_width / 3, ctx.window_height / 12, 15,
            ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked
        );
    }
};

inline StateRegistrar<PauseState> register_pause_state_{GameMode::Pause};

inline void PauseState::update(GameContext& ctx, TextureManager& /*textures*/)
{
    if (pending_action_ == PauseAction::None) return;
    
    switch (pending_action_) {
        case PauseAction::Resume:
            pop_state(ctx, false);
            break;
        case PauseAction::Save:
            if (ctx.world_manager) {
                (void)save_game::write_save(ctx, *ctx.world_manager);
            }
            break;
        case PauseAction::Load:
            clear_states(ctx, false);
            push_state(ctx, StateRegistry::instance().create(GameMode::Load));
            break;
        case PauseAction::MainMenu:
            clear_states(ctx, false);
            push_state(ctx, StateRegistry::instance().create(GameMode::Menu));
            break;
        case PauseAction::Exit:
            if (ctx.world_manager) {
                (void)save_game::write_save(ctx, *ctx.world_manager);
            }
            ctx.quit = true;
            break;
        default:
            break;
    }
    pending_action_ = PauseAction::None;
}

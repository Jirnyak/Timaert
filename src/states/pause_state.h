#pragma once

#include "core/game_state.h"
#include "rendering/renderer.h"
#include "systems/save_game.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

class LoadState;
class MenuState;
class WorldManager;

class PauseState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Pause;
    }
    [[nodiscard]] bool is_overlay() const noexcept override {
        return true;
    }

private:
    MenuButtonList menu_;
    bool menu_initialized_ = false;
    InputManager input_manager_;

    enum class PauseAction : std::uint8_t { None, Resume, Save, Load, MainMenu, Exit };
    PauseAction pending_action_ = PauseAction::None;

    void init_menu() {
        menu_.clear();

        menu_.add(MenuItem{"Resume",
                           [this]() { pending_action_ = PauseAction::Resume; },
                           RaIcon::Forward});
        menu_.add(
            MenuItem{"Save", [this]() { pending_action_ = PauseAction::Save; }, RaIcon::Save});
        menu_.add(
            MenuItem{"Load", [this]() { pending_action_ = PauseAction::Load; }, RaIcon::Load});
        menu_.add(MenuItem{"To main menu",
                           [this]() { pending_action_ = PauseAction::MainMenu; },
                           RaIcon::CastleEmblem});
#ifndef __EMSCRIPTEN__
        menu_.add(
            MenuItem{"Exit", [this]() { pending_action_ = PauseAction::Exit; }, RaIcon::Reverse});
#endif

        menu_initialized_ = true;
    }

public:
    void handle_event(GameContext& ctx, TextureManager& /*textures*/) override {
        // Event handling now done via Sokol callbacks
        (void)ctx;
    }

    void update(GameContext& ctx, TextureManager& /*textures*/) override;

    void render(GameContext& ctx, TextureManager& /*textures*/) override {
        if (!menu_initialized_) {
            init_menu();
        }
        Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        render_fill_rect(overlay, ui_color("#000000B4"));

        const int title_h = ctx.window_height / 12;
        const std::string title = "PAUSED";
        render_text(ctx,
                    title,
                    ctx.window_width / 2 - static_cast<int>(title.size()) * title_h / 4,
                    ctx.window_height / 6,
                    static_cast<int>(title.size()) * title_h / 2,
                    title_h,
                    {255, 255, 255, 255});

        // Scale spacing with window size
        const int spacing = std::max(15, ctx.window_height / 50);
        menu_.render_and_handle(ctx,
                                ctx.window_width / 2,
                                ctx.window_height / 3,
                                ctx.window_width / 3,
                                ctx.window_height / 12,
                                spacing,
                                ctx.curs_x,
                                ctx.curs_y,
                                ctx.pick_x,
                                ctx.pick_y,
                                ctx.picked);
    }
};

inline StateRegistrar<PauseState> register_pause_state_{GameMode::Pause};

inline void PauseState::update(GameContext& ctx, TextureManager& /*textures*/) {
    if (pending_action_ == PauseAction::None)
        return;

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

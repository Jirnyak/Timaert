#pragma once

#include "core/game_state.h"
#include "core/binary_io.h"
#include "ui/ui.h"
#include "systems/random_events.h"
#include "ui/ui_events.h"

class EventState : public GameState
{
public:
    static constexpr std::int32_t kRandomEvent = -2;
    
    explicit EventState(std::int32_t event_id = kRandomEvent) : event_id_(event_id) {}
    
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Event; }
    [[nodiscard]] bool is_overlay() const noexcept override { return true; }
    [[nodiscard]] std::int32_t event_id() const noexcept { return event_id_; }
    
    // Serialization - save/load event_id
    void save_state(BinaryWriter& writer) const override {
        writer.write(event_id_);
    }
    
    void load_state(BinaryReader& reader) override {
        event_id_ = reader.read<std::int32_t>();
    }

private:
    std::int32_t event_id_ = -1;
    MenuButtonList choice_buttons_;
    bool ui_initialized_ = false;
    InputManager input_manager_;

    void init_ui(GameContext& /*ctx*/)
    {
        choice_buttons_.clear();
        if (event_id_ == -1) return;

        const auto& event_data = get_random_event_data(event_id_);
        
        for (const auto& choice : event_data.choices)
        {
            choice_buttons_.add(MenuItem{choice.text, [choice, this]() {
                pending_choice_ = &choice;
            }});
        }
        
        ui_initialized_ = true;
    }
    
    const EventChoice* pending_choice_ = nullptr;

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/) override
    {
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Press)
            {
                set_pick(ctx, evt.x, evt.y);
            }
        }
    }

    void update(GameContext& ctx, TextureManager& /*textures*/) override
    {
        if (pending_choice_) {
            pending_choice_->action(ctx);
            pending_choice_ = nullptr;
            pop_state(ctx, false);
            return;
        }
        
        if (event_id_ == -1) {
            pop_state(ctx, false);
            return;
        }

        if (event_id_ == kRandomEvent) {
            int count = get_random_event_count();
            if (count > 0) {
                event_id_ = static_cast<std::int32_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(count - 1)));
            } else {
                pop_state(ctx, false);
                return;
            }
        }

        if (!ui_initialized_ && event_id_ != -1) {
            init_ui(ctx);
        }
    }

    void render(GameContext& ctx, TextureManager& /*textures*/) override
    {
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, {0, 0, 0, 180});

        if (event_id_ == -1) return;
        const auto& event_data = get_random_event_data(event_id_);

        // Центрированное окно события
        int panel_w = std::min(600, ctx.window_width - 40);
        int panel_h = std::min(400, ctx.window_height - 40);
        SDL_Rect panel = ui_centered_rect(ctx.window_width, ctx.window_height, panel_w, panel_h);
        
        ui_draw_panel(ctx.renderer, panel, ui_color("#1A1A2E"), ui_color("#16C79A"));

        // Заголовок
        render_text(ctx, event_data.title, 
                    panel.x + 20, panel.y + 20, panel_w - 40, 30, {255, 255, 255, 255});

        // Описание (упрощенный вывод текста без переноса строк пока)
        render_text(ctx, event_data.description, 
                    panel.x + 20, panel.y + 70, panel_w - 40, 20, {200, 200, 200, 255});

        // Кнопки выбора
        choice_buttons_.render_and_handle(
            ctx,
            ctx.window_width / 2, panel.y + panel_h - (static_cast<int>(choice_buttons_.size()) * 50),
            panel_w - 80, 40, 10,
            ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked
        );
    }
};

inline StateRegistrar<EventState> register_event_state_{GameMode::Event};

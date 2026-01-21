#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "systems/random_events.h"
#include "ui/ui_events.h"

class EventState : public GameState
{
private:
    MenuButtonList choice_buttons_;
    bool ui_initialized_ = false;
    int last_event_id_ = -1;
    InputManager input_manager_;

    void init_ui(GameContext& ctx)
    {
        choice_buttons_.clear();
        if (ctx.active_event_id == -1) return;

        const auto& event_data = get_random_event_data(ctx.active_event_id);
        
        for (const auto& choice : event_data.choices)
        {
            // Захватываем choice по значению для безопасности в лямбде
            choice_buttons_.add(MenuItem{choice.text, [choice, &ctx, this]() {
                choice.action(ctx);
                ctx.active_event_id = -1;
                enter_game(ctx);
                ui_initialized_ = false; // Сброс для следующего события
            }});
        }
        
        last_event_id_ = ctx.active_event_id;
        ui_initialized_ = true;
    }

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
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

    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        // Если WorldManager запросил случайное событие (-2)
        if (ctx.active_event_id == -2) {
            int count = get_random_event_count();
            if (count > 0) {
                ctx.active_event_id = static_cast<std::int32_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(count - 1)));
            } else {
                ctx.active_event_id = -1;
                enter_game(ctx, false); // Отмена, если событий нет
            }
        }

        if (ctx.active_event_id != last_event_id_) {
            ui_initialized_ = false;
        }

        if (!ui_initialized_ && ctx.active_event_id != -1) {
            init_ui(ctx);
        }
    }

    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        // Затемнение фона (рисуем поверх последнего кадра игры)
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, {0, 0, 0, 180});

        if (ctx.active_event_id == -1) return;
        const auto& event_data = get_random_event_data(ctx.active_event_id);

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

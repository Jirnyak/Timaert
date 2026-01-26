#pragma once

#include <SDL_events.h>
#include <SDL_keycode.h>
#include <SDL_mouse.h>
#include <cmath>
#include "core/game_context.h"

enum class InputAction {
    None,
    Press,    // Нажатие (Mouse Down / Finger Down)
    Release,  // Отпускание без гарантии клика
    Click,    // Короткое нажатие (Tap) без смещения
    Drag,     // Перемещение с зажатием
    Zoom      // Колесо мыши или щипок
};

struct InputEvent {
    InputAction action = InputAction::None;
    int x = 0;  // Экранные координаты (с учетом масштаба)
    int y = 0;
    int dx = 0;         // Смещение по X для Drag
    int dy = 0;         // Смещение по Y для Drag
    float zoom = 1.0f;  // Множитель зума (например, 1.2 или 0.8)
    bool handled = false;
};

class InputManager {
private:
    bool is_pressed_ = false;
    bool is_dragging_ = false;
    int start_x_ = 0;
    int start_y_ = 0;
    int last_x_ = 0;
    int last_y_ = 0;

    // Пороги для определения свайпа/драга (в пикселях)
    static constexpr int kDragThreshold = 10;
    static constexpr int kFingerDragThreshold = 20;

public:
    InputManager() = default;

    // Сброс состояния (вызывать при смене режимов игры)
    void reset() {
        is_pressed_ = false;
        is_dragging_ = false;
        start_x_ = 0;
        start_y_ = 0;
        last_x_ = 0;
        last_y_ = 0;
    }

    // Возвращает true, если событие было обработано как значимое действие
    bool process_event(const SDL_Event& event, const GameContext& ctx, InputEvent& out) {
        out = InputEvent{};
        out.action = InputAction::None;

        switch (event.type) {
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    out.x = to_render_x(ctx, event.button.x);
                    out.y = to_render_y(ctx, event.button.y);

                    is_pressed_ = true;
                    is_dragging_ = false;
                    start_x_ = out.x;
                    start_y_ = out.y;
                    last_x_ = out.x;
                    last_y_ = out.y;

                    out.action = InputAction::Press;
                    return true;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    out.x = to_render_x(ctx, event.button.x);
                    out.y = to_render_y(ctx, event.button.y);
                    is_pressed_ = false;

                    const int dist = std::abs(out.x - start_x_) + std::abs(out.y - start_y_);
                    // Если не было драга и смещение маленькое - считаем это кликом
                    if (!is_dragging_ && dist < kDragThreshold) {
                        out.action = InputAction::Click;
                    } else {
                        out.action = InputAction::Release;
                    }
                    is_dragging_ = false;
                    return true;
                }
                break;

            case SDL_MOUSEMOTION: {
                out.x = to_render_x(ctx, event.motion.x);
                out.y = to_render_y(ctx, event.motion.y);

                if (is_pressed_) {
                    out.dx = out.x - last_x_;
                    out.dy = out.y - last_y_;
                    last_x_ = out.x;
                    last_y_ = out.y;

                    const int dist = std::abs(out.x - start_x_) + std::abs(out.y - start_y_);
                    if (dist >= kDragThreshold || is_dragging_) {
                        is_dragging_ = true;
                        out.action = InputAction::Drag;
                        return true;
                    }
                }
            } break;

            case SDL_FINGERDOWN: {
                // Для тача координаты приходят в диапазоне 0.0 - 1.0, масштабируем к логическим
                // размерам окна
                out.x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
                out.y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));

                is_pressed_ = true;
                is_dragging_ = false;
                start_x_ = out.x;
                start_y_ = out.y;
                last_x_ = out.x;
                last_y_ = out.y;

                out.action = InputAction::Press;
                return true;
            } break;

            case SDL_FINGERUP: {
                out.x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
                out.y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));
                is_pressed_ = false;

                const int dist = std::abs(out.x - start_x_) + std::abs(out.y - start_y_);
                if (!is_dragging_ && dist < kFingerDragThreshold) {
                    out.action = InputAction::Click;
                } else {
                    out.action = InputAction::Release;
                }
                is_dragging_ = false;
                return true;
            } break;

            case SDL_FINGERMOTION: {
                out.x = static_cast<int>(event.tfinger.x * static_cast<float>(ctx.window_width));
                out.y = static_cast<int>(event.tfinger.y * static_cast<float>(ctx.window_height));

                if (is_pressed_) {
                    out.dx = out.x - last_x_;
                    out.dy = out.y - last_y_;
                    last_x_ = out.x;
                    last_y_ = out.y;

                    const int dist = std::abs(out.x - start_x_) + std::abs(out.y - start_y_);
                    if (dist >= kFingerDragThreshold || is_dragging_) {
                        is_dragging_ = true;
                        out.action = InputAction::Drag;
                        return true;
                    }
                }
            } break;

            case SDL_MOUSEWHEEL:
                out.action = InputAction::Zoom;
                if (event.wheel.y > 0)
                    out.zoom = 1.2f;
                else if (event.wheel.y < 0)
                    out.zoom = 1.0f / 1.2f;
                return true;

            case SDL_MULTIGESTURE:
                if (std::abs(event.mgesture.dDist) > 0.002f) {
                    out.action = InputAction::Zoom;
                    // dDist - это дельта расстояния между пальцами, умножаем для чувствительности
                    out.zoom = 1.0f + event.mgesture.dDist * 5.0f;
                    return true;
                }
                break;
        }

        return false;
    }
};

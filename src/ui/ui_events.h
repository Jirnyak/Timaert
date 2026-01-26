#pragma once

#include <cmath>
#include "core/game_context.h"
#include "core/gfx_types.h"

enum class InputAction {
    None,
    Press,
    Release,
    Click,
    Drag,
    Zoom
};

struct InputEvent {
    InputAction action = InputAction::None;
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    float zoom = 1.0f;
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

    static constexpr int kDragThreshold = 10;
    static constexpr int kFingerDragThreshold = 20;

public:
    InputManager() = default;

    void reset() {
        is_pressed_ = false;
        is_dragging_ = false;
        start_x_ = 0;
        start_y_ = 0;
        last_x_ = 0;
        last_y_ = 0;
    }

    void on_mouse_down(int x, int y, InputEvent& out) {
        out = InputEvent{};
        out.x = x;
        out.y = y;
        is_pressed_ = true;
        is_dragging_ = false;
        start_x_ = x;
        start_y_ = y;
        last_x_ = x;
        last_y_ = y;
        out.action = InputAction::Press;
    }

    void on_mouse_up(int x, int y, InputEvent& out) {
        out = InputEvent{};
        out.x = x;
        out.y = y;
        is_pressed_ = false;

        const int dist = std::abs(x - start_x_) + std::abs(y - start_y_);
        if (!is_dragging_ && dist < kDragThreshold) {
            out.action = InputAction::Click;
        } else {
            out.action = InputAction::Release;
        }
        is_dragging_ = false;
    }

    bool on_mouse_move(int x, int y, InputEvent& out) {
        out = InputEvent{};
        out.x = x;
        out.y = y;

        if (is_pressed_) {
            out.dx = x - last_x_;
            out.dy = y - last_y_;
            last_x_ = x;
            last_y_ = y;

            const int dist = std::abs(x - start_x_) + std::abs(y - start_y_);
            if (dist >= kDragThreshold || is_dragging_) {
                is_dragging_ = true;
                out.action = InputAction::Drag;
                return true;
            }
        }
        return false;
    }

    void on_scroll(float delta, InputEvent& out) {
        out = InputEvent{};
        out.action = InputAction::Zoom;
        if (delta > 0)
            out.zoom = 1.2f;
        else if (delta < 0)
            out.zoom = 1.0f / 1.2f;
    }

    [[nodiscard]] bool is_pressed() const noexcept { return is_pressed_; }
    [[nodiscard]] bool is_dragging() const noexcept { return is_dragging_; }
};

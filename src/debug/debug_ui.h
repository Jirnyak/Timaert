#pragma once

#ifdef SAMOSBOR_DEBUG_UI

#include <entt/entity/entity.hpp>

namespace debug {
class SystemProfiler;
}
namespace ecs {
class World;
}
struct GameContext;

namespace debug {

class DebugUI {
public:
    void init();
    void shutdown();

    void render();

    void toggle_visibility() {
        visible_ = !visible_;
    }
    [[nodiscard]] bool is_visible() const {
        return visible_;
    }
    [[nodiscard]] bool wants_input() const;

    void set_profiler(SystemProfiler* profiler) {
        profiler_ = profiler;
    }
    void set_ecs_world(ecs::World* world) {
        ecs_world_ = world;
    }
    void set_game_context(GameContext* ctx) {
        game_ctx_ = ctx;
    }

    void set_fps(float fps) {
        // Smooth FPS display with exponential moving average
        constexpr float alpha = 0.05f;  // Lower = smoother
        if (display_fps_ <= 0.0f) {
            display_fps_ = fps;
        } else {
            display_fps_ = alpha * fps + (1.0f - alpha) * display_fps_;
        }
        current_fps_ = fps;
    }
    void set_frame_time(float ms) {
        // Smooth frame time display
        constexpr float alpha = 0.05f;
        if (display_frame_time_ms_ <= 0.0f) {
            display_frame_time_ms_ = ms;
        } else {
            display_frame_time_ms_ = alpha * ms + (1.0f - alpha) * display_frame_time_ms_;
        }
        frame_time_ms_ = ms;
    }

private:
    bool initialized_ = false;
    bool visible_ = false;

    SystemProfiler* profiler_ = nullptr;
    ecs::World* ecs_world_ = nullptr;
    GameContext* game_ctx_ = nullptr;

    float current_fps_ = 0.0f;
    float frame_time_ms_ = 0.0f;
    float display_fps_ = 0.0f;
    float display_frame_time_ms_ = 0.0f;

    bool show_profiler_ = true;
    bool show_entity_inspector_ = true;
    bool show_ecs_stats_ = true;
    bool show_game_state_ = true;
    entt::entity selected_entity_ = entt::null;

    int entity_type_filter_ = 0;
    char entity_search_[64]{};

    void render_main_menu_bar();
    void render_profiler_window();
    void render_entity_inspector_window();
    void render_ecs_stats_window();
    void render_game_state_window();
    void render_entity_details(entt::entity entity);
};

DebugUI& get_debug_ui();

}  // namespace debug

#else  // !SAMOSBOR_DEBUG_UI

namespace debug {

class DebugUI {
public:
    void init() {}
    void shutdown() {}
    void render() {}
    void toggle_visibility() {}
    [[nodiscard]] bool is_visible() const {
        return false;
    }
    [[nodiscard]] bool wants_input() const {
        return false;
    }
    void set_profiler(void*) {}
    void set_ecs_world(void*) {}
    void set_game_context(void*) {}
    void set_fps(float) {}
    void set_frame_time(float) {}
};

inline DebugUI& get_debug_ui() {
    static DebugUI stub;
    return stub;
}

}  // namespace debug

#endif  // SAMOSBOR_DEBUG_UI

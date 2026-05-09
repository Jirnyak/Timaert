#pragma once

#ifdef SAMOSBOR_DEBUG_UI

#include "debug/profiler.h"
#include "ecs/world.h"
#include "core/game_context.h"
#include <SDL.h>

namespace debug {

class DebugUI {
public:
    void init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();
    
    void process_event(SDL_Event& event);
    void new_frame();
    void render();
    
    void toggle_visibility() { visible_ = !visible_; }
    [[nodiscard]] bool is_visible() const { return visible_; }
    [[nodiscard]] bool wants_input() const;
    
    void set_profiler(SystemProfiler* profiler) { profiler_ = profiler; }
    void set_ecs_world(ecs::World* world) { ecs_world_ = world; }
    void set_game_context(GameContext* ctx) { game_ctx_ = ctx; }
    
    void set_fps(float fps) { current_fps_ = fps; }
    void set_frame_time(float ms) { frame_time_ms_ = ms; }

private:
    bool initialized_ = false;
    bool visible_ = false;
    
    SDL_Renderer* renderer_ = nullptr;
    SystemProfiler* profiler_ = nullptr;
    ecs::World* ecs_world_ = nullptr;
    GameContext* game_ctx_ = nullptr;
    
    float current_fps_ = 0.0f;
    float frame_time_ms_ = 0.0f;
    
    // UI State
    bool show_profiler_ = true;
    bool show_entity_inspector_ = true;
    bool show_ecs_stats_ = true;
    bool show_game_state_ = true;
    entt::entity selected_entity_ = entt::null;
    
    // Filter state for entity list
    int entity_type_filter_ = 0; // 0=All, 1=NPCs, 2=Trees, 3=Player
    char entity_search_[64]{};
    
    void render_main_menu_bar();
    void render_profiler_window();
    void render_entity_inspector_window();
    void render_ecs_stats_window();
    void render_game_state_window();
    void render_entity_details(entt::entity entity);
};

// Global debug UI instance (only exists when SAMOSBOR_DEBUG_UI is defined)
DebugUI& get_debug_ui();

} // namespace debug

#else // !SAMOSBOR_DEBUG_UI

// Stub implementation when debug UI is disabled
namespace debug {

class DebugUI {
public:
    void init(void*, void*) {}
    void shutdown() {}
    void process_event(void*) {}
    void new_frame() {}
    void render() {}
    void toggle_visibility() {}
    [[nodiscard]] bool is_visible() const { return false; }
    [[nodiscard]] bool wants_input() const { return false; }
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

} // namespace debug

#endif // SAMOSBOR_DEBUG_UI

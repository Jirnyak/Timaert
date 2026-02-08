// Samosbor - Sokol-based main entry point
// Replaces SDL2 main loop with Sokol application lifecycle

#include <algorithm>
#include <cstdint>
#include <memory>
#include <print>
#include <string>
#include <vector>
#include <cstdlib>

#ifndef __EMSCRIPTEN__
#include <limits.h>
#include <unistd.h>
#endif

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "sokol_log.h"
#include "sokol_glue.h"
#include "core/game_state.h"
#include "core/gfx_types.h"
#include "core/types.h"
#include "entt/entt.hpp"
#include "rendering/sound_manager.h"
#include "ui/ui.h"

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

#include "core/game_context.h"
#include "rendering/texture_manager.h"
#include "rendering/text_renderer.h"

#ifndef __EMSCRIPTEN__
// Get the directory containing the executable
static std::string get_executable_dir() {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        return "";  // Fallback to current directory
    }
    exe_path[len] = '\0';
    
    // Remove the executable name to get the directory
    std::string path(exe_path);
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
        return path.substr(0, last_slash);
    }
    return "";  // Fallback
}
#endif

#include "rendering/renderer.h"
#include "systems/world_manager.h"
#include "debug/profiler.h"

#ifdef SAMOSBOR_DEBUG_UI
    #include "sokol_imgui.h"
    #include "debug/debug_ui.h"
#endif

// Include all states to register them via StateRegistrar
#include "states/menu_state.h"  // IWYU pragma: keep
#include "states/gen_state.h"  // IWYU pragma: keep
#include "states/load_state.h"  // IWYU pragma: keep
#include "states/play_state.h"  // IWYU pragma: keep
#include "states/pause_state.h"  // IWYU pragma: keep
#include "states/map_state.h"  // IWYU pragma: keep
#include "states/stat_state.h"  // IWYU pragma: keep
#include "states/labyrinth_state.h"  // IWYU pragma: keep
#include "states/event_state.h"  // IWYU pragma: keep
#include "states/battle_state.h"  // IWYU pragma: keep
#include "states/interaction_state.h"  // IWYU pragma: keep

namespace {

struct AppState {
    GameContext ctx;
    std::unique_ptr<TextureManager> textures;
    std::unique_ptr<TextRenderer> text_renderer;
    std::unique_ptr<WorldManager> world_manager;
    debug::SystemProfiler profiler;
    
    std::uint64_t last_time = 0;
    double frame_time_ms = 16.0;
    bool initialized = false;
};

AppState g_app;

void init_graphics() {
    // Initialize Sokol graphics
    sg_desc gfx_desc{};
    gfx_desc.environment = sglue_environment();
    gfx_desc.logger.func = slog_func;
    sg_setup(&gfx_desc);

    // Initialize renderer (sokol_gl, pipelines, samplers)
    renderer_init();

    // Initialize sokol_time
    stm_setup();

#ifdef SAMOSBOR_DEBUG_UI
    // Initialize sokol_imgui
    simgui_desc_t simgui_desc{};
    simgui_desc.max_vertices = 65536 * 4;
    simgui_setup(&simgui_desc);
#endif
}

void shutdown_graphics() {
#ifdef SAMOSBOR_DEBUG_UI
    simgui_shutdown();
#endif
    renderer_shutdown();
    sg_shutdown();
}


void update_window_metrics() {
    // Get DPI scale - on high-DPI displays this is > 1.0
    const float dpi_scale = sapp_dpi_scale();
    g_app.ctx.dpi_scale = dpi_scale;

    // Use framebuffer size for rendering
    g_app.ctx.window_width = sapp_width();
    g_app.ctx.window_height = sapp_height();
    g_app.ctx.screen_center_x = g_app.ctx.window_width / 2;
    g_app.ctx.screen_center_y = g_app.ctx.window_height / 2;
    
    if (g_app.textures) {
        g_app.textures->set_tile_background_size(g_app.ctx.window_width, g_app.ctx.window_height);
    }
}

}  // namespace

namespace {

void init_cb() {
    std::println("Samosbor starting...");
    
    init_graphics();

#ifdef __EMSCRIPTEN__
    em_init_persistent_fs();
#endif

    // Set up base path
#ifndef __EMSCRIPTEN__
    // For desktop, use executable directory to find assets
    g_app.ctx.base_path = get_executable_dir();
#endif

    // Initialize window metrics
    update_window_metrics();

    // Initialize text renderer
    g_app.text_renderer = std::make_unique<TextRenderer>();
    const std::string font_path = resolve_path(g_app.ctx, "assets/fonts/Roboto-Black.ttf");
    const std::string icon_font_path = resolve_path(g_app.ctx, "assets/fonts/rpgawesome-webfont.ttf");
    g_app.text_renderer->initialize(font_path, 20, icon_font_path);
    (void)g_app.text_renderer->preload(20);
    (void)g_app.text_renderer->preload_icon(20);

    // Set global text renderer for UI
    set_text_renderer(g_app.text_renderer.get());

    // Initialize ECS world
    g_app.ctx.init_world();

    // Initialize textures
    g_app.textures = std::make_unique<TextureManager>();
    g_app.textures->init(g_app.ctx.window_width, g_app.ctx.window_height, g_app.ctx);

    // Load background music
    const std::string music_path = resolve_path(g_app.ctx, "assets/sound/15-dungeon-suno.mp3");
    if (!g_app.ctx.sound_manager.load_background_music(music_path)) {
        std::println("Failed to load background music");
    } else {
        g_app.ctx.sound_manager.play_background_music(-1);
    }

    // Initialize world manager
    g_app.world_manager = std::make_unique<WorldManager>();
    g_app.ctx.world_manager = g_app.world_manager.get();

#ifdef SAMOSBOR_DEBUG_UI
    // Initialize debug UI
    debug::get_debug_ui().init();
#endif

    // Initialize state stack with Menu state
    push_state(g_app.ctx, std::make_unique<MenuState>());

    g_app.last_time = stm_now();
    g_app.initialized = true;
    g_app.ctx.redraw_requested = true;  // Ensure first frame renders
    
    std::println("Samosbor initialized successfully");
}

void frame_cb() {
    if (!g_app.initialized)
        return;

    // Calculate frame time and FPS
    const std::uint64_t now = stm_now();
    const double dt_sec = stm_sec(stm_diff(now, g_app.last_time));
    
#ifdef __EMSCRIPTEN__
    // Emscripten: limit to 60 FPS by skipping frames that come too early
    constexpr double kTargetFrameTime = 1.0 / 60.0;
    static double accumulated_time = 0.0;
    accumulated_time += dt_sec;
    g_app.last_time = now;
    
    if (accumulated_time < kTargetFrameTime) {
        return;  // Skip this frame, don't render
    }
    // Use accumulated time for game logic, then reset
    const double clamped_dt = std::min(accumulated_time, 0.1);
    g_app.frame_time_ms = clamped_dt * 1000.0;
    accumulated_time = 0.0;
#else
    g_app.last_time = now;
    // Clamp dt to avoid large jumps (e.g., after tab switch or debugger pause)
    const double clamped_dt = std::min(dt_sec, 0.1);  // Max 100ms per frame
    g_app.frame_time_ms = clamped_dt * 1000.0;
#endif
    
    // FPS logging every second
    static double fps_accumulator = 0.0;
    static int fps_frame_count = 0;
    static double fps_min = 1000.0;
    fps_accumulator += dt_sec;
    fps_frame_count++;
    if (dt_sec > 0.0) {
        const double instant_fps = 1.0 / dt_sec;
        fps_min = std::min(instant_fps, fps_min);
    }
    if (fps_accumulator >= 1.0) {
        const double avg_fps = static_cast<double>(fps_frame_count) / fps_accumulator;
        std::println("FPS: avg={:.1f} min={:.1f} tiles={}x{}", avg_fps, fps_min, 
                     g_app.ctx.window_width / 16, g_app.ctx.window_height / 16);
        fps_accumulator = 0.0;
        fps_frame_count = 0;
        fps_min = 1000.0;
    }

    // Update window metrics if changed
    if (sapp_width() != g_app.ctx.window_width || sapp_height() != g_app.ctx.window_height) {
        update_window_metrics();
        g_app.ctx.window_dirty = true;
        g_app.ctx.redraw_requested = true;
    }

    // Begin pass with clear
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};  // Black
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    // Begin frame for sokol_gl
    renderer_begin_frame(g_app.ctx.window_width, g_app.ctx.window_height, sapp_dpi_scale());

    // Begin frame for text renderer
    if (g_app.text_renderer) {
        g_app.text_renderer->begin_frame(g_app.ctx.window_width, g_app.ctx.window_height, sapp_dpi_scale());
    }

#ifdef SAMOSBOR_DEBUG_UI
    // Begin ImGui frame
    simgui_frame_desc_t simgui_frame_desc{};
    simgui_frame_desc.width = sapp_width();
    simgui_frame_desc.height = sapp_height();
    simgui_frame_desc.delta_time = sapp_frame_duration();
    simgui_frame_desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&simgui_frame_desc);
#endif

    // Update and render game state
    g_app.ctx.ui_hit_test.begin_frame();
    g_app.ctx.ui_input_enabled = true;

    GameState* current = current_state(g_app.ctx);
    if (current) {
        // For overlay states, render underlying state first
        if (current->is_overlay() && g_app.ctx.redraw_requested) {
            const bool was_picked = g_app.ctx.picked;
            const int pick_x = g_app.ctx.pick_x;
            const int pick_y = g_app.ctx.pick_y;
            g_app.ctx.ui_input_enabled = false;

            if (g_app.ctx.state_stack.size() > 1) {
                GameState* underlying = g_app.ctx.state_stack[g_app.ctx.state_stack.size() - 2].get();
                if (underlying) {
                    underlying->render(g_app.ctx, *g_app.textures);
                }
            }

            g_app.ctx.ui_input_enabled = true;
            g_app.ctx.picked = was_picked;
            g_app.ctx.pick_x = pick_x;
            g_app.ctx.pick_y = pick_y;
        }

        g_app.profiler.begin("Update");
        current->update(g_app.ctx, *g_app.textures);
        g_app.profiler.end("Update");

        // Re-fetch current state in case it changed during update
        current = current_state(g_app.ctx);
        if (current) {
            g_app.profiler.begin("Render");
            current->render(g_app.ctx, *g_app.textures);
            g_app.profiler.end("Render");
        }
    }

    // Clear one-frame click flags after processing
    g_app.ctx.picked = false;
    g_app.ctx.click_event = false;
    
    g_app.ctx.ui_hit_test.commit_if_dirty();
    // Always request redraw for continuous rendering
    g_app.ctx.redraw_requested = true;

    // End sokol_gl frame (draws our primitives and textures)
    renderer_end_frame();

    // Flush text renderer after sgl_draw - sfons_flush has its own sgl_draw call
    if (g_app.text_renderer) {
        g_app.text_renderer->flush();
    }

#ifdef SAMOSBOR_DEBUG_UI
    // Render debug UI
    auto& debug_ui = debug::get_debug_ui();
    if (debug_ui.is_visible()) {
        double const fps = g_app.frame_time_ms > 0 ? 1000.0 / g_app.frame_time_ms : 0.0;
        debug_ui.set_fps(static_cast<float>(fps));
        debug_ui.set_frame_time(static_cast<float>(g_app.frame_time_ms));
        debug_ui.set_profiler(&g_app.profiler);
        debug_ui.set_ecs_world(g_app.ctx.ecs_world.get());
        debug_ui.set_game_context(&g_app.ctx);
        debug_ui.render();
    }
    simgui_render();
#endif

    sg_end_pass();
    sg_commit();

    // Handle quit
    if (g_app.ctx.quit) {
        sapp_quit();
    }
}

void cleanup_cb() {
    std::println("Samosbor shutting down...");

#ifdef SAMOSBOR_DEBUG_UI
    debug::get_debug_ui().shutdown();
#endif

    g_app.textures.reset();
    g_app.text_renderer.reset();
    g_app.world_manager.reset();

    shutdown_graphics();
    
    std::println("Samosbor closed");
}

void event_cb(const sapp_event* ev) {
    if (!g_app.initialized)
        return;

#ifdef SAMOSBOR_DEBUG_UI
    // Toggle debug UI with F3 or backtick - check BEFORE passing to ImGui
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (ev->key_code == SAPP_KEYCODE_F3 || ev->key_code == SAPP_KEYCODE_GRAVE_ACCENT) {
            debug::get_debug_ui().toggle_visibility();
            g_app.ctx.redraw_requested = true;
            return;  // Don't pass toggle key to game
        }
    }

    // Pass events to ImGui
    simgui_handle_event(ev);
    
    // If debug UI wants input, skip game processing for input events
    auto& debug_ui = debug::get_debug_ui();
    if (debug_ui.wants_input()) {
        if (ev->type == SAPP_EVENTTYPE_KEY_DOWN || ev->type == SAPP_EVENTTYPE_KEY_UP ||
            ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_UP ||
            ev->type == SAPP_EVENTTYPE_MOUSE_MOVE || ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
            g_app.ctx.redraw_requested = true;
            return;
        }
    }
#endif

    // Update mouse position
    if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE || 
        ev->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
        ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
        // Mouse coordinates are in window points, matching sapp_width/height()
        g_app.ctx.curs_x = static_cast<int>(ev->mouse_x);
        g_app.ctx.curs_y = static_cast<int>(ev->mouse_y);
    }

    // Convert Sokol event to game event and process
    // This is a simplified version - the full implementation would need
    // to properly convert all event types and route them to game states
    
    GameState const* current = current_state(g_app.ctx);
    if (!current)
        return;

    // Handle keyboard events
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN) {
        sapp_keycode const key = ev->key_code;
        
        // Handle global keys
        switch (key) {
            case SAPP_KEYCODE_ESCAPE:
                if (current_game_mode(g_app.ctx) == GameMode::Game) {
                    push_state(g_app.ctx, StateRegistry::instance().create(GameMode::Pause));
                } else if (current_game_mode(g_app.ctx) == GameMode::Labyrinth) {
                    if (g_app.ctx.state_stack.size() > 1) {
                        pop_state(g_app.ctx);
                    } else {
                        replace_state(g_app.ctx, StateRegistry::instance().create(GameMode::Menu));
                    }
                } else if (current_game_mode(g_app.ctx) != GameMode::Menu) {
                    // Only pop if not on Menu and won't leave stack empty
                    if (g_app.ctx.state_stack.size() > 1) {
                        pop_state(g_app.ctx);
                    }
                }
                break;
            case SAPP_KEYCODE_SPACE:
                g_app.ctx.paused = !g_app.ctx.paused;
                break;
            case SAPP_KEYCODE_M:
                if (current_game_mode(g_app.ctx) == GameMode::Game) {
                    push_state(g_app.ctx, StateRegistry::instance().create(GameMode::Map));
                }
                break;
            case SAPP_KEYCODE_I:
            case SAPP_KEYCODE_TAB:
                if (current_game_mode(g_app.ctx) == GameMode::Game) {
                    push_state(g_app.ctx, StateRegistry::instance().create(GameMode::Stat));
                }
                break;
            // Arrow keys for player movement
            case SAPP_KEYCODE_UP:
                g_app.ctx.key_up = true;
                break;
            case SAPP_KEYCODE_DOWN:
                g_app.ctx.key_down = true;
                break;
            case SAPP_KEYCODE_LEFT:
                g_app.ctx.key_left = true;
                break;
            case SAPP_KEYCODE_RIGHT:
                g_app.ctx.key_right = true;
                break;
            default:
                break;
        }
        g_app.ctx.redraw_requested = true;
    }
    
    // Handle key up events
    if (ev->type == SAPP_EVENTTYPE_KEY_UP) {
        sapp_keycode const key = ev->key_code;
        switch (key) {
            case SAPP_KEYCODE_UP:
                g_app.ctx.key_up = false;
                break;
            case SAPP_KEYCODE_DOWN:
                g_app.ctx.key_down = false;
                break;
            case SAPP_KEYCODE_LEFT:
                g_app.ctx.key_left = false;
                break;
            case SAPP_KEYCODE_RIGHT:
                g_app.ctx.key_right = false;
                break;
            default:
                break;
        }
    }

    // Handle touch events with pinch zoom support
    if (ev->type == SAPP_EVENTTYPE_TOUCHES_BEGAN) {
        if (ev->num_touches == 1) {
            const int x = static_cast<int>(ev->touches[0].pos_x);
            const int y = static_cast<int>(ev->touches[0].pos_y);
            g_app.ctx.mouse_pressed = true;
            g_app.ctx.drag_start_x = x;
            g_app.ctx.drag_start_y = y;
            g_app.ctx.drag_last_x = x;
            g_app.ctx.drag_last_y = y;
            g_app.ctx.map_dragging = false;
            g_app.ctx.pressed_button_rect = g_app.ctx.ui_hit_test.get_rect_at(x, y);
            g_app.ctx.pinch_active = false;
        } else if (ev->num_touches >= 2) {
            // Start pinch zoom
            const float dx = ev->touches[1].pos_x - ev->touches[0].pos_x;
            const float dy = ev->touches[1].pos_y - ev->touches[0].pos_y;
            g_app.ctx.pinch_start_dist = std::sqrt(dx * dx + dy * dy);
            g_app.ctx.pinch_start_zoom = g_app.ctx.target_zoom;
            g_app.ctx.pinch_active = true;
            g_app.ctx.map_dragging = false;  // Cancel any drag
        }
        g_app.ctx.redraw_requested = true;
    }
    
    if (ev->type == SAPP_EVENTTYPE_TOUCHES_MOVED) {
        if (g_app.ctx.pinch_active && ev->num_touches >= 2) {
            // Handle pinch zoom
            const float dx = ev->touches[1].pos_x - ev->touches[0].pos_x;
            const float dy = ev->touches[1].pos_y - ev->touches[0].pos_y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (g_app.ctx.pinch_start_dist > 10.0f) {
                const float scale = dist / g_app.ctx.pinch_start_dist;
                g_app.ctx.target_zoom = g_app.ctx.pinch_start_zoom * scale;
                g_app.ctx.target_zoom = std::max(g_app.ctx.target_zoom, g_app.ctx.min_zoom);
                g_app.ctx.target_zoom = std::min(g_app.ctx.target_zoom, g_app.ctx.max_zoom);
            }
        } else if (ev->num_touches == 1 && g_app.ctx.mouse_pressed && !g_app.ctx.pinch_active) {
            // Single finger drag/pan - only if not started on a button
            const int x = static_cast<int>(ev->touches[0].pos_x);
            const int y = static_cast<int>(ev->touches[0].pos_y);
            const int dx = x - g_app.ctx.drag_last_x;
            const int dy = y - g_app.ctx.drag_last_y;
            const int total_dist = std::abs(x - g_app.ctx.drag_start_x) + std::abs(y - g_app.ctx.drag_start_y);
            const bool started_on_button = g_app.ctx.pressed_button_rect.w > 0;
            
            if ((total_dist >= 10 || g_app.ctx.map_dragging) && !started_on_button) {
                g_app.ctx.map_dragging = true;
                if (current_game_mode(g_app.ctx) == GameMode::Game || 
                    current_game_mode(g_app.ctx) == GameMode::Labyrinth) {
                    g_app.ctx.map_offset_x += static_cast<float>(dx) / g_app.ctx.zoom;
                    g_app.ctx.map_offset_y += static_cast<float>(dy) / g_app.ctx.zoom;
                    g_app.ctx.velocity_x = static_cast<float>(dx) / g_app.ctx.zoom;
                    g_app.ctx.velocity_y = static_cast<float>(dy) / g_app.ctx.zoom;
                }
            }
            g_app.ctx.drag_last_x = x;
            g_app.ctx.drag_last_y = y;
        }
        g_app.ctx.redraw_requested = true;
    }
    
    if (ev->type == SAPP_EVENTTYPE_TOUCHES_ENDED || ev->type == SAPP_EVENTTYPE_TOUCHES_CANCELLED) {
        if (g_app.ctx.pinch_active) {
            g_app.ctx.pinch_active = false;
        } else if (g_app.ctx.mouse_pressed && ev->num_touches > 0) {
            const int x = static_cast<int>(ev->touches[0].pos_x);
            const int y = static_cast<int>(ev->touches[0].pos_y);
            const Rect& btn = g_app.ctx.pressed_button_rect;
            const int total_dist = std::abs(x - g_app.ctx.drag_start_x) + std::abs(y - g_app.ctx.drag_start_y);
            
            // Click if: started on button and released on SAME button, OR didn't drag and not on any button
            const bool on_same_button = btn.w > 0 && 
                x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h;
            const bool simple_click = !g_app.ctx.map_dragging && total_dist < 10 && btn.w == 0;
            
            if (on_same_button || simple_click) {
                g_app.ctx.pick_x = x;
                g_app.ctx.pick_y = y;
                g_app.ctx.picked = true;
                g_app.ctx.click_event = true;
            }
        }
        g_app.ctx.mouse_pressed = false;
        g_app.ctx.map_dragging = false;
        g_app.ctx.pressed_button_rect = {0, 0, 0, 0};
        g_app.ctx.redraw_requested = true;
    }

    // Handle mouse events
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) {
        const int x = static_cast<int>(ev->mouse_x);
        const int y = static_cast<int>(ev->mouse_y);
        g_app.ctx.mouse_pressed = true;
        g_app.ctx.drag_start_x = x;
        g_app.ctx.drag_start_y = y;
        g_app.ctx.drag_last_x = x;
        g_app.ctx.drag_last_y = y;
        g_app.ctx.map_dragging = false;
        g_app.ctx.pressed_button_rect = g_app.ctx.ui_hit_test.get_rect_at(x, y);
        g_app.ctx.redraw_requested = true;
    }
    
    // Handle drag (mouse move while pressed)
    if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE && g_app.ctx.mouse_pressed) {
        const int x = static_cast<int>(ev->mouse_x);
        const int y = static_cast<int>(ev->mouse_y);
        const int dx = x - g_app.ctx.drag_last_x;
        const int dy = y - g_app.ctx.drag_last_y;
        const int total_dist = std::abs(x - g_app.ctx.drag_start_x) + std::abs(y - g_app.ctx.drag_start_y);
        const bool started_on_button = g_app.ctx.pressed_button_rect.w > 0;
        
        // Start dragging only if NOT started on a button and moved enough
        if ((total_dist >= 10 || g_app.ctx.map_dragging) && !started_on_button) {
            g_app.ctx.map_dragging = true;
            // Apply drag to map offset (scaled by zoom)
            if (current_game_mode(g_app.ctx) == GameMode::Game || 
                current_game_mode(g_app.ctx) == GameMode::Labyrinth) {
                g_app.ctx.map_offset_x += static_cast<float>(dx) / g_app.ctx.zoom;
                g_app.ctx.map_offset_y += static_cast<float>(dy) / g_app.ctx.zoom;
                g_app.ctx.velocity_x = static_cast<float>(dx) / g_app.ctx.zoom;
                g_app.ctx.velocity_y = static_cast<float>(dy) / g_app.ctx.zoom;
            }
        }
        g_app.ctx.drag_last_x = x;
        g_app.ctx.drag_last_y = y;
        g_app.ctx.redraw_requested = true;
    }
    
    if (ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
        const int x = static_cast<int>(ev->mouse_x);
        const int y = static_cast<int>(ev->mouse_y);
        const Rect& btn = g_app.ctx.pressed_button_rect;
        const int total_dist = std::abs(x - g_app.ctx.drag_start_x) + std::abs(y - g_app.ctx.drag_start_y);
        
        // Click if: started on button and released on SAME button, OR didn't drag and not on any button
        const bool on_same_button = btn.w > 0 && 
            x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h;
        const bool simple_click = !g_app.ctx.map_dragging && total_dist < 10 && btn.w == 0;
        
        if (on_same_button || simple_click) {
            g_app.ctx.pick_x = x;
            g_app.ctx.pick_y = y;
            g_app.ctx.picked = true;
            g_app.ctx.click_event = true;
        }
        g_app.ctx.mouse_pressed = false;
        g_app.ctx.map_dragging = false;
        g_app.ctx.pressed_button_rect = {0, 0, 0, 0};
        g_app.ctx.redraw_requested = true;
    }

    // Handle window resize
    if (ev->type == SAPP_EVENTTYPE_RESIZED) {
        update_window_metrics();
        g_app.ctx.window_dirty = true;
        g_app.ctx.redraw_requested = true;
    }

    // Handle mouse wheel for zoom
    if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        g_app.ctx.target_zoom *= (ev->scroll_y > 0) ? 1.1f : 0.9f;
        g_app.ctx.target_zoom = std::max(g_app.ctx.target_zoom, g_app.ctx.min_zoom);
        g_app.ctx.target_zoom = std::min(g_app.ctx.target_zoom, g_app.ctx.max_zoom);
        g_app.ctx.redraw_requested = true;
    }
}

}  // namespace

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    sapp_desc desc{};
    desc.init_cb = init_cb;
    desc.frame_cb = frame_cb;
    desc.cleanup_cb = cleanup_cb;
    desc.event_cb = event_cb;
    desc.width = 1280;
    desc.height = 720;
    desc.window_title = "Samosbor";
    desc.icon.sokol_default = true;
    desc.logger.func = slog_func;
    desc.high_dpi = true;
    desc.sample_count = 1;  // Disable MSAA for pixel art
    desc.swap_interval = 1;  // Enable vsync for 60 FPS limit

#ifdef __EMSCRIPTEN__
    desc.html5.canvas_selector = "#canvas";
#endif

    return desc;
}

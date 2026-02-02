#ifdef SAMOSBOR_DEBUG_UI

#include "debug/debug_ui.h"
#include "debug/profiler.h"
#include "core/game_context.h"
#include "core/game_state.h"
#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/entity.h"
#include "ecs/components/player.h"
#include "ecs/components/npc.h"
#include "core/types.h"

#include <imgui.h>
#include "sokol_app.h"
#include "sokol_imgui.h"

#include <cstdio>
#include <cstring>

namespace debug {

namespace {

DebugUI g_debug_ui;

}  // namespace

DebugUI& get_debug_ui() {
    return g_debug_ui;
}

void DebugUI::init() {
    if (initialized_)
        return;

    // ImGui context is already created by sokol_imgui in main
    // Just set up our style preferences
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.94f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.3f, 0.3f, 0.4f, 0.6f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.4f, 0.4f, 0.5f, 0.8f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.3f, 0.7f, 0.3f, 1.0f);

    initialized_ = true;
}

void DebugUI::shutdown() {
    if (!initialized_)
        return;
    // sokol_imgui handles cleanup
    initialized_ = false;
}

void DebugUI::render() {
    if (!initialized_ || !visible_)
        return;

    render_main_menu_bar();

    if (show_profiler_)
        render_profiler_window();
    if (show_entity_inspector_)
        render_entity_inspector_window();
    if (show_ecs_stats_)
        render_ecs_stats_window();
    if (show_game_state_)
        render_game_state_window();
}

bool DebugUI::wants_input() const {
    if (!initialized_ || !visible_)
        return false;
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void DebugUI::render_main_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Profiler", "F4", &show_profiler_);
            ImGui::MenuItem("Entity Inspector", "F5", &show_entity_inspector_);
            ImGui::MenuItem("ECS Stats", "F6", &show_ecs_stats_);
            ImGui::MenuItem("Game State", "F7", &show_game_state_);
            ImGui::EndMenu();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "FPS: %.1f", display_fps_);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Frame: %.2f ms", display_frame_time_ms_);

        if (profiler_) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                               "Systems: %.2f ms",
                               profiler_->total_frame_time_us() / 1000.0);
        }

        ImGui::EndMainMenuBar();
    }
}

void DebugUI::render_profiler_window() {
    if (!profiler_)
        return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("System Profiler", &show_profiler_)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Reset Stats")) {
        profiler_->reset();
    }

    ImGui::SameLine();
    ImGui::Text("Total: %.3f ms", profiler_->total_frame_time_us() / 1000.0);

    ImGui::Separator();

    ImGui::Columns(5, "profiler_columns");
    ImGui::SetColumnWidth(0, 150);
    ImGui::SetColumnWidth(1, 70);
    ImGui::SetColumnWidth(2, 70);
    ImGui::SetColumnWidth(3, 70);
    ImGui::SetColumnWidth(4, 70);

    ImGui::Text("System");
    ImGui::NextColumn();
    ImGui::Text("Current");
    ImGui::NextColumn();
    ImGui::Text("Avg");
    ImGui::NextColumn();
    ImGui::Text("Min");
    ImGui::NextColumn();
    ImGui::Text("Max");
    ImGui::NextColumn();
    ImGui::Separator();

    const auto* timings = profiler_->timings();
    std::size_t const count = profiler_->timing_count();

    for (std::size_t i = 0; i < count; ++i) {
        const auto& t = timings[i];

        ImVec4 color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
        if (t.time_us > 1000)
            color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
        if (t.time_us > 5000)
            color = ImVec4(1.0f, 0.5f, 0.3f, 1.0f);
        if (t.time_us > 10000)
            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::TextColored(color, "%s", t.name);
        ImGui::NextColumn();

        ImGui::Text("%.1f", t.time_us);
        ImGui::NextColumn();

        ImGui::Text("%.1f", t.avg_time_us);
        ImGui::NextColumn();

        ImGui::Text("%.1f", t.min_time_us < 1e8 ? t.min_time_us : 0.0);
        ImGui::NextColumn();

        ImGui::Text("%.1f", t.max_time_us);
        ImGui::NextColumn();
    }

    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Text("Time Distribution:");

    for (std::size_t i = 0; i < count; ++i) {
        const auto& t = timings[i];
        float const fraction = static_cast<float>(t.time_us / 16666.0);
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), t.name);
    }

    ImGui::End();
}

void DebugUI::render_entity_inspector_window() {
    if (!ecs_world_)
        return;

    ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entity Inspector", &show_entity_inspector_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Filter:");
    ImGui::SameLine();
    const char* const filter_items[] = {"All", "NPCs", "Trees", "Player"};
    ImGui::SetNextItemWidth(100);
    ImGui::Combo("##type_filter", &entity_type_filter_, filter_items, 4);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##search", entity_search_, sizeof(entity_search_));

    ImGui::Separator();

    ImGui::BeginChild("EntityList", ImVec2(0, 200), true);

    auto& registry = ecs_world_->registry;

    auto process_entity = [&](entt::entity entity, const char* label) {
        if (entity_search_[0] != '\0') {
            if (std::strstr(label, entity_search_) == nullptr)
                return;
        }

        char full_label[128];
        std::snprintf(full_label, sizeof(full_label), "%s [%u]", label, static_cast<unsigned>(entity));

        bool const is_selected = (selected_entity_ == entity);
        if (ImGui::Selectable(full_label, is_selected)) {
            selected_entity_ = entity;
        }
    };

    if (entity_type_filter_ == 0 || entity_type_filter_ == 3) {
        auto player_view = ecs_world_->registry.view<ecs::PlayerTag, ecs::Active>();
        for (auto entity : player_view) {
            process_entity(entity, "Player");
        }
    }

    if (entity_type_filter_ == 0 || entity_type_filter_ == 1) {
        auto npc_view = ecs_world_->view<ecs::NPCTag, ecs::Active>();
        for (auto entity : npc_view) {
            const auto& tag = npc_view.get<ecs::NPCTag>(entity);
            process_entity(entity, npc_type_name(tag.type));
        }
    }

    if (entity_type_filter_ == 0 || entity_type_filter_ == 2) {
        auto obj_view = ecs_world_->view<ecs::ObjectSprite, ecs::Active>();
        for (auto entity : obj_view) {
            const auto& sprite = obj_view.get<ecs::ObjectSprite>(entity);
            const char* obj_name = (sprite.type == ObjectType::Tree) ? "Tree" : "Object";
            process_entity(entity, obj_name);
        }
    }

    ImGui::EndChild();

    ImGui::Separator();

    ImGui::Text("Selected Entity Details:");
    ImGui::BeginChild("EntityDetails", ImVec2(0, 0), true);

    if (selected_entity_ != entt::null && registry.valid(selected_entity_)) {
        render_entity_details(selected_entity_);
    } else {
        ImGui::TextDisabled("No entity selected");
    }

    ImGui::EndChild();

    ImGui::End();
}

void DebugUI::render_entity_details(entt::entity entity) {
    auto& registry = ecs_world_->registry;

    ImGui::Text("Entity ID: %u", static_cast<unsigned>(entity));
    ImGui::Separator();

    if (registry.all_of<ecs::Position>(entity)) {
        auto& pos = registry.get<ecs::Position>(entity);
        ImGui::Text("Position: (%d, %d)", pos.tile.x, pos.tile.y);
    }

    if (registry.all_of<ecs::VisualPos>(entity)) {
        auto& vis = registry.get<ecs::VisualPos>(entity);
        ImGui::Text("Visual Pos: (%.2f, %.2f)", vis.x, vis.y);
    }

    if (registry.all_of<ecs::Health>(entity)) {
        auto& hp = registry.get<ecs::Health>(entity);
        ImGui::Text("Health: %d / %d", hp.current, hp.max);

        float const ratio = hp.ratio();
        ImVec4 bar_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        if (ratio < 0.5f)
            bar_color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
        if (ratio < 0.25f)
            bar_color = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
        ImGui::ProgressBar(ratio, ImVec2(-1, 0), "HP");
        ImGui::PopStyleColor();
    }

    if (registry.all_of<ecs::Speed>(entity)) {
        auto& speed = registry.get<ecs::Speed>(entity);
        ImGui::Text("Speed: %.2f (progress: %.1f)", speed.base, speed.progress);
    }

    if (registry.all_of<ecs::NPCTag>(entity)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "NPC Components:");

        auto& tag = registry.get<ecs::NPCTag>(entity);
        ImGui::Text("Type: %s", npc_type_name(tag.type));

        if (registry.all_of<ecs::AIBehavior>(entity)) {
            auto& ai = registry.get<ecs::AIBehavior>(entity);
            const char* state_str = "Unknown";
            switch (ai.state) {
                case NPCState::Idle: state_str = "Idle"; break;
                case NPCState::Wandering: state_str = "Wandering"; break;
                case NPCState::Traveling: state_str = "Traveling"; break;
                case NPCState::Returning: state_str = "Returning"; break;
                case NPCState::Fleeing: state_str = "Fleeing"; break;
                case NPCState::Raiding: state_str = "Raiding"; break;
                case NPCState::Trading: state_str = "Trading"; break;
                case NPCState::Cutting: state_str = "Cutting"; break;
                case NPCState::Dead: state_str = "Dead"; break;
                default: break;
            }
            ImGui::Text("AI State: %s", state_str);
            ImGui::Text("Idle Timer: %d", ai.idle_timer);
            ImGui::Text("Action Timer: %d", ai.action_timer);
        }
        
        // Display RPG attributes if available
        if (registry.all_of<Attributes>(entity)) {
            auto& attrs = registry.get<Attributes>(entity);
            ImGui::Separator();
            ImGui::Text("Attributes:");
            ImGui::Text("STR:%d END:%d AGI:%d", attrs.str, attrs.end_, attrs.agi);
            ImGui::Text("WIL:%d INT:%d WIS:%d", attrs.wil, attrs.int_, attrs.wis);
            ImGui::Text("LCK:%d SPD:%d CHA:%d", attrs.lck, attrs.spd, attrs.cha);
        }
        
        // Display level data if available
        if (registry.all_of<LevelData>(entity)) {
            auto& lvl = registry.get<LevelData>(entity);
            ImGui::Text("Level: %d (EXP: %d/%d)", lvl.level, lvl.exp, lvl.exp_to_next);
        }
    }

    ImGui::Separator();
    ImGui::Text("Tags:");
    if (registry.all_of<ecs::Active>(entity))
        ImGui::BulletText("Active");
    if (registry.all_of<ecs::Dead>(entity))
        ImGui::BulletText("Dead");
    if (registry.all_of<ecs::PlayerTag>(entity))
        ImGui::BulletText("PlayerTag");
}

void DebugUI::render_ecs_stats_window() {
    if (!ecs_world_)
        return;

    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ECS Statistics", &show_ecs_stats_)) {
        ImGui::End();
        return;
    }

    auto& registry = ecs_world_->registry;

    std::size_t const total_entities = registry.storage<entt::entity>().size();

    std::size_t active_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::Active>())
        active_count++;

    std::size_t npc_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::NPCTag, ecs::Active>())
        npc_count++;

    std::size_t tree_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::ObjectSprite, ecs::Active>())
        tree_count++;

    std::size_t dead_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::Dead>())
        dead_count++;

    ImGui::Text("Total Entities: %zu", total_entities);
    ImGui::Text("Active Entities: %zu", active_count);
    ImGui::Text("Dead (pending cleanup): %zu", dead_count);

    ImGui::Separator();
    ImGui::Text("By Type:");
    ImGui::BulletText("NPCs: %zu", npc_count);
    ImGui::BulletText("Objects (trees): %zu", tree_count);

    ImGui::End();
}

void DebugUI::render_game_state_window() {
    if (!game_ctx_)
        return;

    ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Game State", &show_game_state_)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Window: %dx%d", game_ctx_->window_width, game_ctx_->window_height);
    ImGui::Text("Game Speed: %d", game_ctx_->game_speed);
    ImGui::Text("Paused: %s", game_ctx_->paused ? "Yes" : "No");
    ImGui::Text("Hour: %llu", static_cast<unsigned long long>(game_ctx_->hour));

    ImGui::Separator();
    ImGui::Text("Camera:");
    ImGui::BulletText("Position: (%d, %d)", game_ctx_->pos_cam.x, game_ctx_->pos_cam.y);
    ImGui::BulletText("Zoom: %.2f -> %.2f", game_ctx_->zoom, game_ctx_->target_zoom);
    ImGui::BulletText("Offset: (%.1f, %.1f)", game_ctx_->map_offset_x, game_ctx_->map_offset_y);
    
    ImGui::Spacing();
    ImGui::Text("Go to position:");
    ImGui::PushItemWidth(80);
    ImGui::InputInt("X##goto", &camera_goto_x_, 0, 0);
    ImGui::SameLine();
    ImGui::InputInt("Y##goto", &camera_goto_y_, 0, 0);
    ImGui::PopItemWidth();
    if (ImGui::Button("Move Camera")) {
        game_ctx_->pos_cam.x = static_cast<std::uint16_t>(std::max(0, camera_goto_x_));
        game_ctx_->pos_cam.y = static_cast<std::uint16_t>(std::max(0, camera_goto_y_));
    }

    ImGui::Separator();
    ImGui::Text("Input:");
    ImGui::BulletText("Cursor: (%d, %d)", game_ctx_->curs_x, game_ctx_->curs_y);
    ImGui::BulletText("Pick: (%d, %d) [%s]",
                      game_ctx_->pick_x,
                      game_ctx_->pick_y,
                      game_ctx_->picked ? "active" : "inactive");

    ImGui::Separator();
    ImGui::Text("State Stack: %zu states", game_ctx_->state_stack.size());
    for (std::size_t i = 0; i < game_ctx_->state_stack.size(); ++i) {
        auto* state = game_ctx_->state_stack[i].get();
        const char* mode_str = "Unknown";
        switch (state->mode()) {
            case GameMode::Menu: mode_str = "Menu"; break;
            case GameMode::Game: mode_str = "Game"; break;
            case GameMode::Gen: mode_str = "Gen"; break;
            case GameMode::Fight: mode_str = "Fight"; break;
            case GameMode::Interaction: mode_str = "Interaction"; break;
            case GameMode::Event: mode_str = "Event"; break;
            case GameMode::Pause: mode_str = "Pause"; break;
            case GameMode::Stat: mode_str = "Stat"; break;
            case GameMode::Map: mode_str = "Map"; break;
            case GameMode::Load: mode_str = "Load"; break;
            case GameMode::Labyrinth: mode_str = "Labyrinth"; break;
            case GameMode::Settings: mode_str = "Settings (removed)"; break;
            case GameMode::Exit: mode_str = "Exit"; break;
            default: break;
        }
        ImGui::BulletText("[%zu] %s%s", i, mode_str, state->is_overlay() ? " (overlay)" : "");
    }

    ImGui::End();
}

}  // namespace debug

#endif  // SAMOSBOR_DEBUG_UI

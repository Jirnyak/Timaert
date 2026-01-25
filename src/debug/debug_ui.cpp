#ifdef SAMOSBOR_DEBUG_UI

#include "debug/debug_ui.h"
#include "ecs/components/entity.h"
#include "ecs/components/npc.h"
#include "ecs/components/player.h"
#include "core/types.h"
#include "core/game_state.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <cstdio>

namespace debug {

static DebugUI g_debug_ui;

DebugUI& get_debug_ui() {
    return g_debug_ui;
}

void DebugUI::init(SDL_Window* window, [[maybe_unused]] SDL_Renderer* renderer) {
    if (initialized_) return;
    
    renderer_ = renderer;
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Dark theme with custom accent
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
    
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    
    initialized_ = true;
    SDL_Log("DEBUG_UI: Initialized (visible=%d)", visible_);
}

void DebugUI::shutdown() {
    if (!initialized_) return;
    
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    
    initialized_ = false;
}

void DebugUI::process_event(SDL_Event& event) {
    if (!initialized_) return;
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void DebugUI::new_frame() {
    if (!initialized_ || !visible_) return;
    
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::render() {
    if (!initialized_ || !visible_) return;
    
    render_main_menu_bar();
    
    if (show_profiler_) render_profiler_window();
    if (show_entity_inspector_) render_entity_inspector_window();
    if (show_ecs_stats_) render_ecs_stats_window();
    if (show_game_state_) render_game_state_window();
    
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
}

bool DebugUI::wants_input() const {
    if (!initialized_ || !visible_) return false;
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
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "FPS: %.1f", current_fps_);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Frame: %.2f ms", frame_time_ms_);
        
        if (profiler_) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Systems: %.2f ms", 
                              profiler_->total_frame_time_us() / 1000.0);
        }
        
        ImGui::EndMainMenuBar();
    }
}

void DebugUI::render_profiler_window() {
    if (!profiler_) return;
    
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
    
    // Column headers
    ImGui::Columns(5, "profiler_columns");
    ImGui::SetColumnWidth(0, 150);
    ImGui::SetColumnWidth(1, 70);
    ImGui::SetColumnWidth(2, 70);
    ImGui::SetColumnWidth(3, 70);
    ImGui::SetColumnWidth(4, 70);
    
    ImGui::Text("System"); ImGui::NextColumn();
    ImGui::Text("Current"); ImGui::NextColumn();
    ImGui::Text("Avg"); ImGui::NextColumn();
    ImGui::Text("Min"); ImGui::NextColumn();
    ImGui::Text("Max"); ImGui::NextColumn();
    ImGui::Separator();
    
    const auto* timings = profiler_->timings();
    std::size_t count = profiler_->timing_count();
    
    for (std::size_t i = 0; i < count; ++i) {
        const auto& t = timings[i];
        
        // Color code based on time
        ImVec4 color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f); // Green
        if (t.time_us > 1000) color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f); // Yellow
        if (t.time_us > 5000) color = ImVec4(1.0f, 0.5f, 0.3f, 1.0f); // Orange
        if (t.time_us > 10000) color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
        
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
    
    // Visual bar chart
    ImGui::Separator();
    ImGui::Text("Time Distribution (µs):");
    
    for (std::size_t i = 0; i < count; ++i) {
        const auto& t = timings[i];
        float fraction = static_cast<float>(t.time_us / 16666.0); // Relative to 60fps frame budget
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), t.name);
    }
    
    ImGui::End();
}

void DebugUI::render_entity_inspector_window() {
    if (!ecs_world_) return;
    
    ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Entity Inspector", &show_entity_inspector_)) {
        ImGui::End();
        return;
    }
    
    // Filter controls
    ImGui::Text("Filter:");
    ImGui::SameLine();
    const char* filter_items[] = { "All", "NPCs", "Trees", "Player" };
    ImGui::SetNextItemWidth(100);
    ImGui::Combo("##type_filter", &entity_type_filter_, filter_items, 4);
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##search", entity_search_, sizeof(entity_search_));
    
    ImGui::Separator();
    
    // Entity list
    ImGui::BeginChild("EntityList", ImVec2(0, 200), true);
    
    auto& registry = ecs_world_->registry;
    
    auto process_entity = [&](entt::entity entity, const char* label) {
        // Search filter
        if (entity_search_[0] != '\0') {
            if (std::strstr(label, entity_search_) == nullptr) return;
        }
        
        char full_label[128];
        std::snprintf(full_label, sizeof(full_label), "%s [%u]", label, 
                      static_cast<unsigned>(entity));
        
        bool is_selected = (selected_entity_ == entity);
        if (ImGui::Selectable(full_label, is_selected)) {
            selected_entity_ = entity;
        }
    };
    
    // Display entities based on filter
    if (entity_type_filter_ == 0 || entity_type_filter_ == 3) {
        // Player
        auto player_view = ecs_world_->registry.view<ecs::PlayerTag, ecs::Active>();
        for (auto entity : player_view) {
            process_entity(entity, "Player");
        }
    }
    
    if (entity_type_filter_ == 0 || entity_type_filter_ == 1) {
        // NPCs
        auto npc_view = ecs_world_->view<ecs::NPCTag, ecs::Active>();
        for (auto entity : npc_view) {
            const auto& tag = npc_view.get<ecs::NPCTag>(entity);
            process_entity(entity, npc_type_name(tag.type));
        }
    }
    
    if (entity_type_filter_ == 0 || entity_type_filter_ == 2) {
        // Trees/Objects
        auto obj_view = ecs_world_->view<ecs::ObjectSprite, ecs::Active>();
        for (auto entity : obj_view) {
            const auto& sprite = obj_view.get<ecs::ObjectSprite>(entity);
            const char* obj_name = (sprite.type == ObjectType::Tree) ? "Tree" : "Object";
            process_entity(entity, obj_name);
        }
    }
    
    ImGui::EndChild();
    
    ImGui::Separator();
    
    // Entity details
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
    
    // Position
    if (registry.all_of<ecs::Position>(entity)) {
        auto& pos = registry.get<ecs::Position>(entity);
        ImGui::Text("Position: (%d, %d)", pos.tile.x, pos.tile.y);
    }
    
    // Visual position
    if (registry.all_of<ecs::VisualPos>(entity)) {
        auto& vis = registry.get<ecs::VisualPos>(entity);
        ImGui::Text("Visual Pos: (%.2f, %.2f)", vis.x, vis.y);
    }
    
    // Health
    if (registry.all_of<ecs::Health>(entity)) {
        auto& hp = registry.get<ecs::Health>(entity);
        ImGui::Text("Health: %d / %d", hp.current, hp.max);
        
        float ratio = hp.ratio();
        ImVec4 bar_color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        if (ratio < 0.5f) bar_color = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
        if (ratio < 0.25f) bar_color = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
        
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
        ImGui::ProgressBar(ratio, ImVec2(-1, 0), "HP");
        ImGui::PopStyleColor();
    }
    
    // Speed
    if (registry.all_of<ecs::Speed>(entity)) {
        auto& speed = registry.get<ecs::Speed>(entity);
        ImGui::Text("Speed: %.2f (progress: %.1f)", speed.base, speed.progress);
    }
    
    // NPC specific
    if (registry.all_of<ecs::NPCTag>(entity)) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "NPC Components:");
        
        auto& tag = registry.get<ecs::NPCTag>(entity);
        const char* type_str = "Unknown";
        switch (tag.type) {
            case NPCType::Peasant: type_str = "Peasant"; break;
            case NPCType::Woodcutter: type_str = "Woodcutter"; break;
            case NPCType::Merchant: type_str = "Merchant"; break;
            case NPCType::Bandit: type_str = "Bandit"; break;
            case NPCType::Guard: type_str = "Guard"; break;
            case NPCType::Caravan: type_str = "Caravan"; break;
            case NPCType::Witch: type_str = "Witch"; break;
            default: break;
        }
        ImGui::Text("Type: %s", type_str);
        
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
        
        if (registry.all_of<ecs::CharacterInfo>(entity)) {
            auto& info = registry.get<ecs::CharacterInfo>(entity);
            if (info.name[0] != '\0') {
                ImGui::Text("Name: %s", info.name);
            }
            if (info.personality[0] != '\0') {
                ImGui::Text("Personality: %s", info.personality);
            }
        }
        
        if (registry.all_of<ecs::CombatStats>(entity)) {
            auto& combat = registry.get<ecs::CombatStats>(entity);
            ImGui::Text("Will: %d / %d", combat.will, combat.max_will);
            ImGui::Text("Lust: %d / %d", combat.lust, combat.max_lust);
        }
        
        if (registry.all_of<ecs::SettlementLink>(entity)) {
            auto& link = registry.get<ecs::SettlementLink>(entity);
            ImGui::Text("Home Settlement: %d", link.home_idx);
            ImGui::Text("Target Settlement: %d", link.target_idx);
        }
    }
    
    // Faction
    if (registry.all_of<ecs::FactionMember>(entity)) {
        auto& faction = registry.get<ecs::FactionMember>(entity);
        const char* faction_str = "Neutral";
        switch (faction.faction) {
            case FactionID::Faction1: faction_str = "Kingdom"; break;
            case FactionID::Faction2: faction_str = "Outlaws"; break;
            case FactionID::Neutral: faction_str = "Neutral"; break;
            default: break;
        }
        ImGui::Text("Faction: %s", faction_str);
    }
    
    // Tags
    ImGui::Separator();
    ImGui::Text("Tags:");
    if (registry.all_of<ecs::Active>(entity)) ImGui::BulletText("Active");
    if (registry.all_of<ecs::Dead>(entity)) ImGui::BulletText("Dead");
    if (registry.all_of<ecs::PlayerTag>(entity)) ImGui::BulletText("PlayerTag");
    if (registry.all_of<ecs::SpecialNPC>(entity)) ImGui::BulletText("Special");
    if (registry.all_of<ecs::PeasantTag>(entity)) ImGui::BulletText("PeasantTag");
    if (registry.all_of<ecs::WoodcutterTag>(entity)) ImGui::BulletText("WoodcutterTag");
    if (registry.all_of<ecs::MerchantTag>(entity)) ImGui::BulletText("MerchantTag");
    if (registry.all_of<ecs::BanditTag>(entity)) ImGui::BulletText("BanditTag");
    if (registry.all_of<ecs::GuardTag>(entity)) ImGui::BulletText("GuardTag");
    if (registry.all_of<ecs::CaravanTag>(entity)) ImGui::BulletText("CaravanTag");
}

void DebugUI::render_ecs_stats_window() {
    if (!ecs_world_) return;
    
    ImGui::SetNextWindowSize(ImVec2(280, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ECS Statistics", &show_ecs_stats_)) {
        ImGui::End();
        return;
    }
    
    auto& registry = ecs_world_->registry;
    
    // Count entities by type
    std::size_t total_entities = registry.storage<entt::entity>().size();
    
    // Count by iterating views (size_hint not available for all view types)
    std::size_t active_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::Active>()) active_count++;
    
    std::size_t npc_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::NPCTag, ecs::Active>()) npc_count++;
    
    std::size_t tree_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::ObjectSprite, ecs::Active>()) tree_count++;
    
    std::size_t dead_count = 0;
    for ([[maybe_unused]] auto _ : ecs_world_->view<ecs::Dead>()) dead_count++;
    
    ImGui::Text("Total Entities: %zu", total_entities);
    ImGui::Text("Active Entities: %zu", active_count);
    ImGui::Text("Dead (pending cleanup): %zu", dead_count);
    
    ImGui::Separator();
    ImGui::Text("By Type:");
    ImGui::BulletText("NPCs: %zu", npc_count);
    ImGui::BulletText("Objects (trees): %zu", tree_count);
    
    // NPC breakdown
    if (npc_count > 0) {
        ImGui::Separator();
        ImGui::Text("NPC Breakdown:");
        
        auto count_type = [&](NPCType type) -> std::size_t {
            std::size_t count = 0;
            auto view = ecs_world_->view<ecs::NPCTag, ecs::Active>();
            for (auto e : view) {
                const auto& tag = view.get<ecs::NPCTag>(e);
                if (tag.type == type) count++;
            }
            return count;
        };
        
        ImGui::BulletText("Peasants: %zu", count_type(NPCType::Peasant));
        ImGui::BulletText("Woodcutters: %zu", count_type(NPCType::Woodcutter));
        ImGui::BulletText("Merchants: %zu", count_type(NPCType::Merchant));
        ImGui::BulletText("Caravans: %zu", count_type(NPCType::Caravan));
        ImGui::BulletText("Guards: %zu", count_type(NPCType::Guard));
        ImGui::BulletText("Bandits: %zu", count_type(NPCType::Bandit));
    }
    
    ImGui::End();
}

void DebugUI::render_game_state_window() {
    if (!game_ctx_) return;
    
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
    
    ImGui::Separator();
    ImGui::Text("Input:");
    ImGui::BulletText("Cursor: (%d, %d)", game_ctx_->curs_x, game_ctx_->curs_y);
    ImGui::BulletText("Pick: (%d, %d) [%s]", game_ctx_->pick_x, game_ctx_->pick_y,
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
            case GameMode::Settings: mode_str = "Settings"; break;
            case GameMode::Exit: mode_str = "Exit"; break;
            default: break;
        }
        ImGui::BulletText("[%zu] %s%s", i, mode_str, 
                          state->is_overlay() ? " (overlay)" : "");
    }
    
    ImGui::End();
}

} // namespace debug

#endif // SAMOSBOR_DEBUG_UI

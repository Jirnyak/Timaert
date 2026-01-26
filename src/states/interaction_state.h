#pragma once

#include "core/game_state.h"
#include "core/binary_io.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "ui/ui_events.h"
#include "ecs/components/core.h"
#include "ecs/entity_ref.h"
#include <string>

class InteractionState : public GameState
{
public:
    explicit InteractionState() = default;
    
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Interaction; }
    
    void save_state(BinaryWriter& writer) const override {
        writer.write(static_cast<std::uint8_t>(npc_type_));
        writer.write_string(npc_name_);
        writer.write(static_cast<std::uint8_t>(npc_faction_));
    }
    
    void load_state(BinaryReader& reader) override {
        npc_type_ = static_cast<NPCType>(reader.read<std::uint8_t>());
        reader.read_string(npc_name_);
        npc_faction_ = static_cast<FactionID>(reader.read<std::uint8_t>());
        loaded_from_save_ = true;
    }

private:
    ecs::EntityRef npc_ref_;
    bool loaded_from_save_ = false;
    bool interaction_started_ = false;  // Prevent re-initialization
    
    // Cached NPC data for rendering
    NPCType npc_type_ = NPCType::Peasant;
    std::string npc_name_ = "Unknown";
    FactionID npc_faction_ = FactionID::Neutral;
    
    [[nodiscard]] bool has_npc() const { 
        return !npc_ref_.is_null() || loaded_from_save_; 
    }
    
    [[nodiscard]] bool is_npc_valid() const {
        if (loaded_from_save_) return true;  // Using cached data
        if (!npc_ref_.valid()) return false;
        return !npc_ref_.has<ecs::Dead>();
    }
    
    [[nodiscard]] entt::entity npc_entity() const {
        return npc_ref_.get();
    }
    
    // UI state
    std::string dialogue_message_;
    bool showing_trade_ = false;
    bool showing_quest_msg_ = false;
    
    [[nodiscard]] bool is_npc_hostile(GameContext& ctx) const
    {
        if (!ctx.world_manager) return false;
        const Player& p = ctx.world_manager->player_ctrl.player();
        
        // Check reputation - if < 0, NPC is hostile
        std::int32_t rep = p.reputation[static_cast<std::size_t>(npc_faction_)];
        return rep < 0;
    }
    
    // UI
    MenuButtonList interaction_menu_;
    UIButtonGroup pause_buttons_;
    bool ui_initialized_ = false;
    bool pause_buttons_initialized_ = false;
    int last_buttons_width_ = -1;
    int last_buttons_height_ = -1;
    
    enum class InteractionAction : std::uint8_t { 
        None, Talk, Trade, Quest, Fight, Leave 
    };
    InteractionAction pending_action_ = InteractionAction::None;
    
    InputManager input_manager_;

    void init_pause_buttons(GameContext& ctx);
    void init_ui(GameContext& ctx);
    void process_pending_action(GameContext& ctx);
    void handle_talk(GameContext& ctx);
    void handle_trade(GameContext& ctx);
    void handle_quest(GameContext& ctx);
    void handle_fight(GameContext& ctx);
    void start_interaction_ecs(entt::entity entity, GameContext& ctx);

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& textures) override;
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;
    void render_trade_ui(GameContext& ctx, TextureManager& textures);
    void render_inventory_grid(GameContext& ctx, TextureManager& textures, const Inventory& inv, 
                               int start_x, int start_y, int cell_size, int cols, int rows);
};

inline StateRegistrar<InteractionState> register_interaction_state_{GameMode::Interaction};

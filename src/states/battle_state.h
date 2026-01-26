#pragma once

#include "core/game_state.h"
#include "core/game_context.h"
#include "core/binary_io.h"
#include "ui/ui.h"
#include "systems/skills.h"
#include "ui/ui_events.h"
#include "ecs/entity_ref.h"
#include "ecs/components/core.h"
#include <string>

struct Player;

class BattleState : public GameState
{
public:
    explicit BattleState() = default;
    
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Fight; }
    
    void save_state(BinaryWriter& writer) const override {
        writer.write(static_cast<std::uint8_t>(enemy_type_));
        writer.write_string(enemy_name_);
        
        // Cached enemy stats
        writer.write(enemy_life_);
        writer.write(enemy_max_life_);
        writer.write(enemy_will_);
        writer.write(enemy_max_will_);
        
        // Battle progress
        writer.write(turn_timer_);
        writer.write(static_cast<std::uint8_t>(player_turn_ ? 1 : 0));
        writer.write(static_cast<std::uint8_t>(battle_ended_ ? 1 : 0));
        writer.write(static_cast<std::uint8_t>(player_won_ ? 1 : 0));
        writer.write(static_cast<std::uint8_t>(npc_surrendered_ ? 1 : 0));
        writer.write(escape_attempts_);
        writer.write(escape_focus_);
        
        // Log message
        writer.write_string(log_message_);
    }
    
    void load_state(BinaryReader& reader) override {
        enemy_type_ = static_cast<NPCType>(reader.read<std::uint8_t>());
        reader.read_string(enemy_name_);
        
        // Cached enemy stats
        enemy_life_ = reader.read<std::int32_t>();
        enemy_max_life_ = reader.read<std::int32_t>();
        enemy_will_ = reader.read<std::int32_t>();
        enemy_max_will_ = reader.read<std::int32_t>();
        
        // Battle progress
        turn_timer_ = reader.read<int>();
        player_turn_ = reader.read<std::uint8_t>() != 0;
        battle_ended_ = reader.read<std::uint8_t>() != 0;
        player_won_ = reader.read<std::uint8_t>() != 0;
        npc_surrendered_ = reader.read<std::uint8_t>() != 0;
        escape_attempts_ = reader.read<int>();
        escape_focus_ = reader.read<int>();
        
        // Log message
        reader.read_string(log_message_);
        
        // Mark as loaded from save - battle continues with cached data only
        loaded_from_save_ = true;
    }

private:
    ecs::EntityRef enemy_ref_;
    bool loaded_from_save_ = false;  // Battle restored from save file
    GameContext* ctx_ = nullptr;  // Context pointer for lambdas
    
    // Cached enemy data for rendering
    NPCType enemy_type_ = NPCType::Bandit;
    std::string enemy_name_ = "Enemy";
    std::int32_t enemy_life_ = 100;
    std::int32_t enemy_max_life_ = 100;
    std::int32_t enemy_will_ = 50;
    std::int32_t enemy_max_will_ = 50;
    
    [[nodiscard]] bool has_enemy() const { 
        return !enemy_ref_.is_null() || loaded_from_save_; 
    }
    
    [[nodiscard]] bool is_enemy_valid() const {
        if (loaded_from_save_) return true;  // Using cached data
        if (!enemy_ref_.valid()) return false;
        return !enemy_ref_.has<ecs::Dead>();
    }
    
    [[nodiscard]] entt::entity enemy_entity() const {
        return enemy_ref_.get();
    }
    
    // UI
    MenuButtonList skill_buttons_;
    MenuButtonList system_buttons_; 
    MenuButtonList mercy_buttons_; // Кнопки после сдачи врага
    bool ui_initialized_ = false;
    std::string log_message_ = "Battle started!";
    UIButtonGroup pause_buttons_;
    bool pause_buttons_initialized_ = false;
    int last_buttons_width_ = -1;
    int last_buttons_height_ = -1;
    SkillID pending_skill_ = SkillID::Punch;
    bool skill_pending_ = false;
    bool escape_pending_ = false;
    bool pause_pending_ = false;

    void request_skill(SkillID sid) { pending_skill_ = sid; skill_pending_ = true; }
    void request_escape() { escape_pending_ = true; }
    void request_pause() { pause_pending_ = true; }
    
    // Logic
    int turn_timer_ = 0; 
    bool player_turn_ = true;
    bool battle_ended_ = false;
    bool player_won_ = false;
    bool npc_surrendered_ = false; // Флаг капитуляции врага
    int escape_attempts_ = 0;
    int escape_focus_ = 0;

    InputManager input_manager_;

    void init_pause_buttons(GameContext& ctx);

    static constexpr int kEscapeMaxAttempts = 3;
    static constexpr int kEscapeFocusMax = 40;

    int compute_escape_chance(const Player& p) const;
    void attempt_escape(GameContext& ctx);
    void update_system_buttons(GameContext& ctx);

    void init_ui(GameContext& ctx);
    void execute_player_move(GameContext& ctx, SkillID sid, const Skill& info);
    void execute_enemy_move(GameContext& ctx);
    void apply_skill_effect(GameContext& ctx, const Skill& skill, bool player_source);
    void check_win_condition(GameContext& ctx);
    void end_battle(bool victory);
    void start_battle_ecs(entt::entity entity, GameContext& ctx);
    

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& textures) override;
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;

    void draw_bars(GameContext& ctx, int x, int y, int hp, int max_hp, int will, int max_will, const std::string& label);
};

inline StateRegistrar<BattleState> register_battle_state_{GameMode::Fight};

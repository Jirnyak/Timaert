#include "states/battle_state.h"

#include <entt/entt.hpp>
#include <cstdlib>
#include <optional>

#include "systems/world_manager.h"
#include "systems/player.h"
#include "systems/attributes.h"
#include "systems/character_templates.h"
#include "systems/economy.h"
#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/npc.h"
#include "rendering/ra_icon.h"
#include "rendering/texture_manager.h"
#include "core/gfx_types.h"
#include "rendering/renderer.h"

void BattleState::init_pause_buttons(GameContext& ctx) {
    const UiButtonLayout layout = ui_default_button_layout(ctx);
    const int btn_size = layout.btn_size;
    const int margin = layout.margin;
    pause_buttons_.clear();
    pause_buttons_.add(UIButton{{margin, layout.speed_y, btn_size, btn_size},
                                "",
                                [this]() { request_pause(); },
                                nullptr,
                                RaIcon::Cog});
    pause_buttons_initialized_ = true;
    last_buttons_width_ = ctx.window_width;
    last_buttons_height_ = ctx.window_height;
}

int BattleState::compute_escape_chance(const Player& p) const {
    if (!has_enemy())
        return 0;

    const int enemy_hp_pct = (enemy_max_life_ > 0) ? (enemy_life_ * 100 / enemy_max_life_) : 0;

    int chance = 20;
    chance += p.level_data.level * 2;  // Higher level = better escape
    chance += (100 - enemy_hp_pct) / 3;
    chance += escape_focus_;

    chance = std::clamp(chance, 5, 90);
    return chance;
}

void BattleState::attempt_escape(GameContext& ctx) {
    if (!ctx.world_manager || !has_enemy() || battle_ended_)
        return;
    Player& p = ctx.world_manager->player_ctrl.player();

    if (escape_attempts_ >= kEscapeMaxAttempts) {
        log_message_ = "You drop your weapon and yield.";
        end_battle(false);
        return;
    }

    const int chance = compute_escape_chance(p);
    const int roll = rand() % 100;
    if (roll < chance) {
        log_message_ = "You break away and vanish into the shadows!";
        end_battle(false);
        return;
    }

    escape_attempts_++;
    escape_focus_ = std::min(kEscapeFocusMax, escape_focus_ + 12);

    log_message_ = "Flee failed (" + std::to_string(chance) + "%).";
    if (escape_attempts_ >= kEscapeMaxAttempts) {
        log_message_ += " Cornered!";
    }

    player_turn_ = false;
    turn_timer_ = 45;
    check_win_condition(ctx);
}

void BattleState::update_system_buttons(GameContext& ctx) {
    system_buttons_.clear();
    if (!ctx.world_manager || !has_enemy())
        return;

    const bool give_up = escape_attempts_ >= kEscapeMaxAttempts;
    
    system_buttons_.add(MenuItem{
        give_up ? "Give Up" : "Flee",
        [this]() {
            if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                request_escape();
            }
        },
        give_up ? RaIcon::XMark : RaIcon::ShoePrints
    });
}

void BattleState::init_ui(GameContext& ctx) {
    skill_buttons_.clear();
    system_buttons_.clear();

    if (!ctx.world_manager)
        return;
    Player const& p_mutable = ctx.world_manager->player_ctrl.player();
    const Player& p = p_mutable;

    // Skill buttons
    for (size_t i = 0; i < (size_t)p.skill_count; ++i) {
        SkillID const sid = p.skills[i];
        SkillInfo const info = get_skill_info(sid);
        std::string const label{info.name};
        
        skill_buttons_.add(MenuItem{label, [this, sid]() {
            if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                request_skill(sid);
            }
        }});
    }

    update_system_buttons(ctx);
    ui_initialized_ = true;
}

void BattleState::execute_player_move(GameContext& ctx, SkillID /*sid*/, const SkillInfo& info) {
    if (!has_enemy())
        return;

    apply_skill_effect(ctx, info, true);
    player_turn_ = false;
    turn_timer_ = 60;
}

void BattleState::execute_enemy_move(GameContext& ctx) {
    if (!has_enemy())
        return;

    SkillInfo selected_skill = {"Attack", "A basic strike."};

    if (auto* ss = enemy_ref_.try_get<ecs::SkillSet>()) {
        if (ss->count > 0) {
            int const idx = rand() % ss->count;
            selected_skill = get_skill_info(ss->skills[idx]);
        }
    }

    apply_skill_effect(ctx, selected_skill, false);
    log_message_ = enemy_name_ + " uses " + std::string(selected_skill.name) + "!";

    player_turn_ = true;
}

void BattleState::apply_skill_effect(GameContext& ctx, const SkillInfo& skill, bool player_source) {
    if (!ctx.world_manager)
        return;
    Player& p = ctx.world_manager->player_ctrl.player();

    // Basic attack damage (simplified - no skill power field anymore)
    int base_power = 10;  // Default base attack power
    int final_damage = base_power;

    if (player_source) {
        // Player attacking enemy
        // Get enemy attributes for dodge/crit calculations
        std::int32_t enemy_agi = 1;
        std::int32_t enemy_lck = 1;
        if (auto* enemy_attrs = enemy_ref_.try_get<ecs::AttributesComponent>()) {
            enemy_agi = enemy_attrs->data.agi;
            enemy_lck = enemy_attrs->data.lck;
        }

        // Calculate dodge chance: enemy dodges based on AGI difference
        const float dodge_chance = calc_dodge_chance(enemy_agi, p.attributes.agi);
        const float dodge_roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        
        if (dodge_roll < dodge_chance) {
            log_message_ = "Enemy dodged!";
            return;  // Attack missed
        }

        // Calculate crit chance: player crits based on LCK difference
        const float crit_chance = calc_crit_chance(p.attributes.lck, enemy_lck);
        const float crit_roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        const bool is_crit = crit_roll < crit_chance;

        // Apply player's attribute bonuses (physical attack)
        final_damage = static_cast<int>(base_power * p.derived_bonuses.phys_damage_mult);
        if (is_crit) {
            final_damage = static_cast<int>(final_damage * 2.0f);  // 2x damage on crit
            log_message_ = "Critical hit!";
        }
        if (auto* h = enemy_ref_.try_get<ecs::Health>()) {
            h->current -= final_damage;
            enemy_life_ = h->current;
        } else {
            enemy_life_ -= final_damage;
        }
    } else {
        // Enemy attacking player
        // Get enemy attributes for damage calculation
        float enemy_dmg_mult = 1.0f;
        std::int32_t enemy_agi = 1;
        std::int32_t enemy_lck = 1;
        
        if (auto* enemy_attrs = enemy_ref_.try_get<ecs::AttributesComponent>()) {
            enemy_agi = enemy_attrs->data.agi;
            enemy_lck = enemy_attrs->data.lck;
        }
        
        if (auto* enemy_bonuses = enemy_ref_.try_get<ecs::DerivedBonusesComponent>()) {
            enemy_dmg_mult = enemy_bonuses->data.phys_damage_mult;  // Use physical damage
        }

        // Calculate dodge chance: player dodges based on AGI difference
        const float dodge_chance = calc_dodge_chance(p.attributes.agi, enemy_agi);
        const float dodge_roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        
        if (dodge_roll < dodge_chance) {
            log_message_ = "You dodged!";
            return;  // Attack missed
        }

        // Calculate crit chance: enemy crits based on LCK difference
        const float crit_chance = calc_crit_chance(enemy_lck, p.attributes.lck);
        const float crit_roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        const bool is_crit = crit_roll < crit_chance;
        
        // Apply enemy attack (physical damage)
        final_damage = static_cast<int>(base_power * enemy_dmg_mult);
        if (is_crit) {
            final_damage = static_cast<int>(final_damage * 2.0f);
            log_message_ = "Enemy critical hit!";
        }
        p.combat_stats.current_hp -= final_damage;
    }

    check_win_condition(ctx);
}

void BattleState::check_win_condition(GameContext& ctx) {
    if (!ctx.world_manager)
        return;
    Player const& p = ctx.world_manager->player_ctrl.player();

    int const e_life = enemy_life_;

    if (e_life <= 0) {
        log_message_ = "Victory! Enemy defeated.";
        end_battle(true);
    } else if (p.combat_stats.current_hp <= 0) {
        log_message_ = "Defeat... You passed out.";
        end_battle(false);
    }
}

void BattleState::end_battle(bool victory) {
    battle_ended_ = true;
    player_won_ = victory;
    turn_timer_ = 0;
}

void BattleState::start_battle_ecs(entt::entity entity, GameContext& ctx) {
    if (!ctx.ecs_world)
        return;
    auto& registry = ctx.ecs_world->registry;

    ctx_ = &ctx;
    enemy_ref_.assign(registry, entity);
    player_turn_ = true;
    battle_ended_ = false;
    player_won_ = false;
    turn_timer_ = 0;
    escape_attempts_ = 0;
    escape_focus_ = 0;

    std::string enemy_name = "Enemy";
    std::string type_name = "Enemy";
    if (registry.all_of<ecs::NPCTag>(entity)) {
        auto& tag = registry.get<ecs::NPCTag>(entity);
        switch (tag.type) {
            case NPCType::Bandit:
                type_name = "Bandit";
                enemy_name = "Bandit";
                break;
            case NPCType::Witch:
                type_name = "Witch";
                enemy_name = "Witch";
                break;
            case NPCType::Caravan:
                type_name = "Caravan";
                enemy_name = "Caravan Guard";
                break;
            case NPCType::Merchant:
                type_name = "Merchant";
                enemy_name = "Merchant";
                break;
            case NPCType::Guard:
                type_name = "Guard";
                enemy_name = "Guard";
                break;
            case NPCType::Peasant:
                type_name = "Peasant";
                enemy_name = "Peasant";
                break;
            default:
                break;
        }
    }
    if (registry.all_of<ecs::CharacterInfo>(entity)) {
        auto& info = registry.get<ecs::CharacterInfo>(entity);
        if (info.name[0] != '\0')
            enemy_name = info.name;
    }

    enemy_name_ = enemy_name;
    if (registry.all_of<ecs::NPCTag>(entity)) {
        enemy_type_ = registry.get<ecs::NPCTag>(entity).type;
    }
    if (registry.all_of<ecs::Health>(entity)) {
        auto& health = registry.get<ecs::Health>(entity);
        enemy_life_ = health.current;
        enemy_max_life_ = health.max;
    }
    if (registry.all_of<ecs::LevelDataComponent>(entity)) {
        auto& level = registry.get<ecs::LevelDataComponent>(entity);
        enemy_level_ = level.data.level;
    } else {
        enemy_level_ = 1;  // Default level
    }
    
    // Get difficulty modifier from character template
    const CharacterTemplate* tmpl = get_character_template(enemy_type_);
    enemy_difficulty_ = tmpl ? tmpl->difficulty_modifier : 1.0f;

    log_message_ = "Battle start!";

    ctx.picked = false;
    ctx.battle_target_entity = entt::null;
    init_ui(ctx);
    init_pause_buttons(ctx);
}

void BattleState::update(GameContext& ctx, TextureManager& /*textures*/) {
    // Exit immediately when battle ends
    if (battle_ended_) {
        if (player_won_) {
            // Award EXP using RPG formula: EXP = 10 * enemy_level * difficulty_modifier
            if (ctx.world_manager) {
                Player& p = ctx.world_manager->player_ctrl.player();
                
                // Calculate EXP reward using cached difficulty modifier
                const std::int32_t exp_reward = static_cast<std::int32_t>(
                    10 * enemy_level_ * enemy_difficulty_);
                
                // Award EXP
                p.level_data.exp += exp_reward;
                
                // Check for level up
                while (p.level_data.exp >= p.level_data.exp_to_next) {
                    p.level_data.exp -= p.level_data.exp_to_next;
                    p.level_data.level++;
                    p.level_data.exp_to_next = LevelData::calc_exp_for_level(p.level_data.level);
                    // Note: Player will see level up in stat screen
                }
            }
            
            // Mark enemy as dead
            if (enemy_ref_.valid()) {
                ctx.ecs_world->mark_dead(enemy_ref_.get());
            }
        }
        pop_state(ctx, false);
        return;
    }

    if (pause_pending_) {
        pause_pending_ = false;
        if (current_game_mode(ctx) != GameMode::Pause)
            push_state(ctx, StateRegistry::instance().create(GameMode::Pause));
    }
    if (escape_pending_) {
        escape_pending_ = false;
        attempt_escape(ctx);
    }
    if (skill_pending_) {
        skill_pending_ = false;
        const SkillInfo info = get_skill_info(pending_skill_);
        execute_player_move(ctx, pending_skill_, info);
    }

    if (!pause_buttons_initialized_ || last_buttons_width_ != ctx.window_width
        || last_buttons_height_ != ctx.window_height) {
        init_pause_buttons(ctx);
    }

    if (loaded_from_save_ && !ui_initialized_) {
        init_ui(ctx);
    } else if (enemy_ref_.is_null() && !loaded_from_save_
               && ctx.battle_target_entity != entt::null) {
        if (ctx.ecs_world && ctx.ecs_world->registry.valid(ctx.battle_target_entity)) {
            start_battle_ecs(ctx.battle_target_entity, ctx);
        } else {
            ctx.battle_target_entity = entt::null;
            pop_state(ctx, false);
        }
    }

    ctx.redraw_requested = true;

    if (turn_timer_ > 0) {
        turn_timer_--;
        if (turn_timer_ == 0 && !player_turn_ && !battle_ended_) {
            execute_enemy_move(ctx);
        }
    }
}

void BattleState::render(GameContext& ctx, TextureManager& textures) {
    Rect const overlay = {0, 0, ctx.window_width, ctx.window_height};
    render_fill_rect( overlay, ui_color("#050510FF"));

    // Exit immediately if battle ended - don't render anything
    if (battle_ended_)
        return;

    if (!ctx.world_manager || !has_enemy())
        return;
    
    // Sync enemy HP from ECS component before rendering
    if (auto* h = enemy_ref_.try_get<ecs::Health>()) {
        enemy_life_ = h->current;
        enemy_max_life_ = h->max;
    }
    
    // Prevent rendering if window is too small to avoid coordinate errors
    if (ctx.window_width < 100 || ctx.window_height < 100)
        return;
    
    const Player& p = ctx.world_manager->player_ctrl.player();

    // Scale factor based on window size (baseline: 720p height)
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    const int padding = static_cast<int>(15 * scale);
    
    // Button sizing
    const int btn_height = std::max(static_cast<int>(40 * scale), ctx.window_height / 16);
    const int btn_spacing = static_cast<int>(8 * scale);
    const int bar_width = static_cast<int>(180 * scale);
    
    // Calculate button zone height first (6 buttons total)
    const int num_buttons = 6;
    const int total_buttons_height = num_buttons * btn_height + (num_buttons - 1) * btn_spacing;
    const int buttons_zone_top = ctx.window_height - padding - total_buttons_height;
    
    // Health bars at top
    const int bars_y = padding * 2;
    draw_bars(ctx, padding, bars_y, p.combat_stats.current_hp, p.combat_stats.max_hp, "", scale);
    draw_bars(ctx,
              ctx.window_width - bar_width - padding,
              bars_y,
              enemy_life_,
              enemy_max_life_,
              "",
              scale);
    
    // Sprite zone: between bars and buttons
    const int bars_height = static_cast<int>(80 * scale);  // Approximate bar height
    const int sprite_zone_top = bars_y + bars_height + padding;
    const int sprite_zone_bottom = buttons_zone_top - padding;
    const int sprite_zone_height = sprite_zone_bottom - sprite_zone_top;
    
    // Calculate sprite size to fit in zone
    int sprite_size = std::min({
        sprite_zone_height,
        ctx.window_width - padding * 2,
        static_cast<int>(180 * scale)
    });
    sprite_size = std::max(sprite_size, static_cast<int>(60 * scale));
    
    // Center sprite in its zone
    const int sprite_x = (ctx.window_width - sprite_size) / 2;
    const int sprite_y = sprite_zone_top + (sprite_zone_height - sprite_size) / 2;
    
    // Get sprite index
    NPCType const etype = enemy_type_;
    size_t s_idx = static_cast<size_t>(ObjectType::Bandit);
    switch (etype) {
        case NPCType::Peasant: s_idx = static_cast<size_t>(ObjectType::Peasant); break;
        case NPCType::Woodcutter: s_idx = static_cast<size_t>(ObjectType::Woodcutter); break;
        case NPCType::Guard: s_idx = static_cast<size_t>(ObjectType::Guard); break;
        case NPCType::Merchant: s_idx = static_cast<size_t>(ObjectType::Merchant); break;
        case NPCType::Witch: s_idx = static_cast<size_t>(ObjectType::Witch); break;
        case NPCType::Caravan: s_idx = static_cast<size_t>(ObjectType::Caravan); break;
        default: break;
    }

    // Render enemy sprite
    Rect const enemy_rect = {sprite_x, sprite_y, sprite_size, sprite_size};
    render_texture(textures.sprite(s_idx), enemy_rect);

    if (!battle_ended_ && player_turn_ && turn_timer_ <= 0) {
        // Debug: print before rendering buttons
        printf("[BATTLE] About to render buttons\n");
        
        bool main_picked = ctx.picked;
        bool system_picked = ctx.picked;

        update_system_buttons(ctx);
        
        printf("[BATTLE] Updated system buttons, about to calculate positions\n");
        
        // System buttons at bottom (1 button)
        const int system_num_buttons = 1;
        const int btn_width = std::min(ctx.window_width / 3, static_cast<int>(280 * scale));
        const int system_total_height = system_num_buttons * btn_height + (system_num_buttons - 1) * btn_spacing;
        const int system_buttons_y = ctx.window_height - padding - system_total_height;
        
        printf("[BATTLE] btn_width=%d btn_height=%d system_buttons_y=%d\n", btn_width, btn_height, system_buttons_y);
        
        // Safety check: skip rendering if positions are invalid
        if (btn_width < 100 || btn_height < 30 || system_buttons_y < 100 || 
            system_buttons_y > ctx.window_height - 50) {
            printf("[BATTLE] Invalid button dimensions, skipping render\n");
            return;
        }
        
        // Main skill buttons above system buttons (4 buttons)
        const int main_num_buttons = 4;
        const int main_total_height = main_num_buttons * btn_height + (main_num_buttons - 1) * btn_spacing;
        const int main_buttons_y = system_buttons_y - btn_spacing - main_total_height;
        
        printf("[BATTLE] main_buttons_y=%d\n", main_buttons_y);

        printf("[BATTLE] Rendering skill buttons\n");
        skill_buttons_.render_and_handle(ctx,
                                         ctx.window_width / 2,
                                         main_buttons_y,
                                         btn_width,
                                         btn_height,
                                         btn_spacing,
                                         ctx.curs_x,
                                         ctx.curs_y,
                                         ctx.pick_x,
                                         ctx.pick_y,
                                         main_picked);

        printf("[BATTLE] About to render system buttons\n");
        system_buttons_.render_and_handle(ctx,
                                          ctx.window_width / 2,
                                          system_buttons_y,
                                          btn_width,
                                          btn_height,
                                          btn_spacing,
                                          ctx.curs_x,
                                          ctx.curs_y,
                                          ctx.pick_x,
                                          ctx.pick_y,
                                          system_picked);

        printf("[BATTLE] Finished rendering all buttons\n");
        
        if (ctx.picked) {
            ctx.picked = false;
        }
    }
}

void BattleState::draw_bars(GameContext& ctx,
                            int x,
                            int y,
                            int hp,
                            int max_hp,
                            const std::string& /*label*/,
                            float scale) {
    // HP bar
    int const bar_w = static_cast<int>(200 * scale);
    int const bar_h = static_cast<int>(12 * scale);

    // Background (empty bar)
    render_fill_rect( {x, y, bar_w, bar_h}, ui_color("#330000FF"));
    
    // Foreground (filled bar) - clamp HP to [0, max_hp] for display only
    if (max_hp > 0) {
        int const display_hp = std::clamp(hp, 0, max_hp);
        if (display_hp > 0) {
            int const fill = static_cast<int>((static_cast<float>(display_hp) / max_hp) * bar_w);
            render_fill_rect( {x, y, fill, bar_h}, ui_color("#FF0000FF"));
        }
    }
    
    // HP text under bar: "current/max"
    const std::string hp_text = std::to_string(std::max(0, hp)) + "/" + std::to_string(max_hp);
    const int text_y = y + bar_h + static_cast<int>(4 * scale);
    const int text_height = static_cast<int>(14 * scale);
    render_text(ctx, hp_text, x, text_y, bar_w, text_height, ui_color("#FFFFFFFF"), static_cast<int>(12 * scale));
}

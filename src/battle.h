#pragma once

#include "game_state.h"
#include "ui.h"
#include "world_manager.h"
#include "skills.h"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

class BattleState : public GameState
{
private:
    WorldManager* world_manager_ = nullptr;
    NPC* enemy_ = nullptr;
    
    // UI
    MenuButtonList skill_buttons_;
    MenuButtonList system_buttons_; 
    bool ui_initialized_ = false;
    std::string log_message_ = "Battle started!";
    
    // Logic
    int turn_timer_ = 0; 
    bool player_turn_ = true;
    bool battle_ended_ = false;
    bool player_won_ = false;

    void init_ui(const GameContext& ctx)
    {
        skill_buttons_.clear();
        system_buttons_.clear();
        
        if (!world_manager_) return;
        const Player& p = world_manager_->player_ctrl.player();

        int btn_w = 200;
        int btn_h = 40;
        int start_x = ctx.window_width / 2 - btn_w / 2;
        int start_y = ctx.window_height / 2; 
        
        // Создаем кнопки для скиллов игрока
        for (size_t i = 0; i < p.skill_count; ++i)
        {
            SkillID sid = p.skills[i];
            // ВАЖНО: берем имя сразу, а логику поиска данных выносим ВНУТРЬ лямбды
            std::string s_name = get_skill_info(sid).name;
            
            skill_buttons_.add(MenuItem{s_name, [this, sid]() {
                if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                    // Ищем данные скилла только в момент нажатия!
                    const Skill& info = get_skill_info(sid);
                    execute_player_move(sid, info);
                }
            }});
        }
        
        system_buttons_.add(MenuItem{"Run / Give Up", [this]() {
            if (!battle_ended_) {
                if (rand() % 2 == 0) {
                    log_message_ = "You ran away safely!";
                    end_battle(false); 
                } else {
                    log_message_ = "Can't escape!";
                    turn_timer_ = 45;
                    player_turn_ = false;
                }
            }
        }});
        
        ui_initialized_ = true;
    }

    void execute_player_move(SkillID /*sid*/, const Skill& info)
    {
        if (!enemy_) return;
        apply_skill_effect(info, true); 
        log_message_ = "You used " + info.name + "!";
        player_turn_ = false;
        turn_timer_ = 60; 
    }

    void execute_enemy_move()
    {
        if (!enemy_) return;
        if (enemy_->skill_count > 0) {
            int idx = rand() % enemy_->skill_count;
            SkillID sid = enemy_->skills[idx];
            const Skill& info = get_skill_info(sid);
            apply_skill_effect(info, false); 
            log_message_ = "Enemy used " + info.name + "!";
        } else {
            world_manager_->player_ctrl.player().life -= 1;
            log_message_ = "Enemy struggles!";
        }
        player_turn_ = true;
    }

    void apply_skill_effect(const Skill& skill, bool player_source)
    {
        Player& p = world_manager_->player_ctrl.player();
        NPC* npc = enemy_;
        int power = skill.power; 
        
        if (player_source) {
            switch (skill.type) {
                case SkillType::Physical: case SkillType::Magic: npc->life -= power; break;
                case SkillType::Lust: npc->will -= power; npc->lust += power / 2; break;
                case SkillType::Heal: p.life = std::min(p.life + power, p.max_life); break;
                default: break;
            }
        } else {
            switch (skill.type) {
                case SkillType::Physical: case SkillType::Magic: p.life -= power; break;
                case SkillType::Lust: p.will -= power; p.lust += power / 2; break;
                case SkillType::Heal: npc->life = std::min(npc->life + power, npc->max_life); break;
                default: break;
            }
        }
        check_win_condition();
    }

    void check_win_condition()
    {
        Player& p = world_manager_->player_ctrl.player();
        if (enemy_->life <= 0 || enemy_->will <= 0) {
            log_message_ = (enemy_->life <= 0) ? "Victory!" : "Enemy Submitted!";
            end_battle(true);
        } else if (p.life <= 0 || p.will <= 0) {
            log_message_ = "Defeat...";
            end_battle(false);
        }
    }

    void end_battle(bool victory)
    {
        battle_ended_ = true;
        player_won_ = victory;
        turn_timer_ = 90; 
    }

public:
    void set_world_manager(WorldManager* wm) { world_manager_ = wm; }
    
    void start_battle(NPC* enemy, GameContext& ctx)
    {
        enemy_ = enemy;
        player_turn_ = true;
        battle_ended_ = false;
        player_won_ = false;
        log_message_ = "Encounter: " + std::string(enemy->is_special ? "Special" : "Normal");
        turn_timer_ = 0;
        ctx.picked = false; // Сброс клика
        init_ui(ctx); 
    }

    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (battle_ended_ && turn_timer_ <= 0) {
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_FINGERDOWN || event.type == SDL_KEYDOWN) {
                if (player_won_ && enemy_) world_manager_->npcs.despawn(enemy_);
                ctx.game_mod = GameMode::Game;
                ctx.picked = false;
                enemy_ = nullptr;
            }
            return;
        }

        if (player_turn_ && !battle_ended_) {
            if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_FINGERDOWN) {
                ctx.pick_x = ctx.curs_x;
                ctx.pick_y = ctx.curs_y;
                ctx.picked = true;
            }
        }
    }

    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        // Проверяем триггер начала боя
        if (ctx.battle_target_id != -1) {
            if (world_manager_) {
                NPC* target = world_manager_->npcs.get_by_id(ctx.battle_target_id);
                if (target) start_battle(target, ctx);
                else ctx.game_mod = GameMode::Game;
            }
            ctx.battle_target_id = -1; 
        }

        // Принудительно просим движок рисовать каждый кадр боя (для анимаций и таймеров)
        ctx.redraw_requested = true;

        if (turn_timer_ > 0) {
            turn_timer_--;
            if (turn_timer_ == 0 && !player_turn_ && !battle_ended_) {
                execute_enemy_move();
            }
        }
    }

    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, ui_color("#050510FF"));

        if (!world_manager_ || !enemy_) return;
        const Player& p = world_manager_->player_ctrl.player();

        // Рисуем врага
        SDL_Rect enemy_rect = ui_centered_rect(ctx.window_width, ctx.window_height, 200, 200);
        enemy_rect.y -= 100;
        size_t s_idx = (size_t)ObjectType::Bandit;
        if (enemy_->type == NPCType::Peasant) s_idx = (size_t)ObjectType::Peasant;
        if (enemy_->type == NPCType::Guard) s_idx = (size_t)ObjectType::Guard;
        SDL_RenderCopy(ctx.renderer, textures.sprite(s_idx), nullptr, &enemy_rect);

        // Полоски HP/Will
        draw_bars(ctx, 40, ctx.window_height - 100, p.life, p.max_life, p.will, p.max_will, "Player");
        draw_bars(ctx, ctx.window_width - 240, ctx.window_height - 100, enemy_->life, enemy_->max_life, enemy_->will, enemy_->max_will, "Enemy");

        render_text(ctx.renderer, ctx.font.get(), log_message_, ctx.window_width / 2 - 200, 30, 400, 30, {255, 255, 255, 255});

        if (!battle_ended_ && player_turn_) {
             skill_buttons_.render_and_handle(ctx.renderer, ctx.font.get(), ctx.window_width / 2, ctx.window_height / 2 + 50, 240, 40, 10, ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked);
             system_buttons_.render_and_handle(ctx.renderer, ctx.font.get(), ctx.window_width / 2, ctx.window_height - 50, 240, 40, 10, ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, ctx.picked);
        }
        
        if (battle_ended_ && turn_timer_ <= 0) {
             render_text(ctx.renderer, ctx.font.get(), "[ Click to Continue ]", ctx.window_width / 2 - 150, ctx.window_height / 2 + 100, 300, 30, {255, 255, 0, 255});
        }
    }

    void draw_bars(const GameContext& ctx, int x, int y, int hp, int max_hp, int will, int max_will, const std::string& label)
    {
        render_text(ctx.renderer, ctx.font.get(), label, x, y - 25, 100, 20, {255,255,255,255});
        int bar_w = 200;
        int bar_h = 12;
        ui_fill_rect(ctx.renderer, {x, y, bar_w, bar_h}, ui_color("#330000FF"));
        if (max_hp > 0) ui_fill_rect(ctx.renderer, {x, y, (int)((float)std::max(0, hp) / max_hp * bar_w), bar_h}, ui_color("#FF0000FF"));
        ui_fill_rect(ctx.renderer, {x, y + 15, bar_w, bar_h}, ui_color("#000033FF"));
        if (max_will > 0) ui_fill_rect(ctx.renderer, {x, y + 15, (int)((float)std::max(0, will) / max_will * bar_w), bar_h}, ui_color("#FF00FFFF"));
    }
};

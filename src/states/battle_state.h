#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "systems/skills.h"
#include "ui/ui_events.h"
#include <string>
#include <algorithm>
#include <cstdio>

class BattleState : public GameState
{
public:
    explicit BattleState(std::int32_t target_id = -1) : target_id_(target_id) {}
    
    [[nodiscard]] GameMode mode() const noexcept override { return GameMode::Fight; }
    [[nodiscard]] std::int32_t target_id() const noexcept { return target_id_; }

private:
    std::int32_t target_id_ = -1;
    NPC* enemy_ = nullptr;
    
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

    void init_pause_buttons(GameContext& ctx)
    {
        const UiButtonLayout layout = ui_default_button_layout(ctx);
        const int btn_size = layout.btn_size;
        const int margin = layout.margin;
        pause_buttons_.clear();
        pause_buttons_.add(UIButton{
            {margin, layout.speed_y, btn_size, btn_size},
            "",
            [this]() { request_pause(); },
            nullptr,
            RaIcon::Cog
        });
        pause_buttons_initialized_ = true;
        last_buttons_width_ = ctx.window_width;
        last_buttons_height_ = ctx.window_height;
    }

    static constexpr int kEscapeMaxAttempts = 3;
    static constexpr int kEscapeFocusMax = 40;

    int compute_escape_chance(const Player& p) const
    {
        if (!enemy_) return 0;

        const int will_pct = (p.max_will > 0) ? (p.will * 100 / p.max_will) : 0;
        const int enemy_hp_pct = (enemy_->combat_stats.max_hp > 0) ? (enemy_->combat_stats.current_hp * 100 / enemy_->combat_stats.max_hp) : 0;

        int chance = 20;
        chance += will_pct / 4;
        chance += (100 - enemy_hp_pct) / 3;
        chance += escape_focus_;

        const std::string trait = enemy_->personality;
        if (trait == "Aggressive" || trait == "Merciless") chance -= 10;
        if (trait == "Fearful" || trait == "Calm" || trait == "Flirty") chance += 10;

        chance = std::clamp(chance, 5, 90);
        return chance;
    }

    void attempt_escape(GameContext& ctx)
    {
        if (!ctx.world_manager || !enemy_ || battle_ended_) return;
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

        const int will_loss = 6 + escape_attempts_ * 3;
        p.will = std::max(0, p.will - will_loss);

        log_message_ = "You try to flee (" + std::to_string(chance) + "%) but are blocked. Will -" +
                       std::to_string(will_loss) + ".";
        if (escape_attempts_ >= kEscapeMaxAttempts) {
            log_message_ += " You are cornered. Next time you must give up.";
        }

        player_turn_ = false;
        turn_timer_ = 45;
        check_win_condition(ctx);
    }

    void update_system_buttons(GameContext& ctx)
    {
        system_buttons_.clear();
        if (!ctx.world_manager || !enemy_) return;

        const bool give_up = escape_attempts_ >= kEscapeMaxAttempts;
        const std::string label = give_up
            ? "Give Up"
            : "Run (" + std::to_string(compute_escape_chance(ctx.world_manager->player_ctrl.player())) + "%)";
        const RaIcon icon = give_up ? RaIcon::XMark : RaIcon::ShoePrints;

        system_buttons_.add(MenuItem{label, [this]() {
            if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                request_escape();
            }
        }, icon});
    }

    void init_ui(GameContext& ctx)
    {

        skill_buttons_.clear();
        system_buttons_.clear();
        mercy_buttons_.clear();
        
        if (!ctx.world_manager) return;
        Player& p_mutable = ctx.world_manager->player_ctrl.player();
        const Player& p = p_mutable;

        // --- Кнопка переговоров ---
        skill_buttons_.add(MenuItem{"Talk", [this]() {
            if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                if (npc_surrendered_) {
                    log_message_ = "They are listening now.";
                } else {
                    log_message_ = std::string(enemy_->name) + " ignores your words!";
                    turn_timer_ = 30;
                    player_turn_ = false; // Попытка поговорить тратит ход
                }
            }
        }, RaIcon::SpeechBubble});

        // --- Кнопки пощады (появляются при npc_surrendered_) ---
        mercy_buttons_.add(MenuItem{"Spare (Mercy)", [this, &p_mutable]() {
            log_message_ = "You spared " + std::string(enemy_->name) + ".";
            p_mutable.reputation[static_cast<size_t>(enemy_->faction)] += 5;
            end_battle(true);
        }, RaIcon::Hearts});

        mercy_buttons_.add(MenuItem{"Loot (Rob)", [this, &p_mutable]() {
            int gold = static_cast<int>(enemy_->inventory.get_capital());
            p_mutable.inventory.add_capital(gold);
            p_mutable.reputation[static_cast<size_t>(enemy_->faction)] -= 10;
            log_message_ = "You robbed " + std::string(enemy_->name) + " for " + std::to_string(gold) + " gold.";
            enemy_->inventory.set_capital(0);
            end_battle(true);
        }, RaIcon::GoldBar});

        mercy_buttons_.add(MenuItem{"Abuse (Theater)", [this]() {
            log_message_ = "You humiliate your opponent. [Scene Placeholder]";
            end_battle(true);
        }, RaIcon::Skull});
        
        // ИСПРАВЛЕНИЕ: Удалена строка повторного объявления 'p'
        // const Player& p = ctx.world_manager->player_ctrl.player(); 

        // --- ИСПРАВЛЕНИЕ: Безопасный захват переменных ---
        // Создаем кнопки для скиллов игрока
        for (size_t i = 0; i < (size_t)p.skill_count; ++i)
        {
            SkillID sid = p.skills[i];
            // Получаем имя сразу, чтобы передать строку в кнопку
            std::string s_name = get_skill_info(sid).name;
            
            // Захватываем 'sid' по значению ([this, sid]), а не по ссылке!
            // Это предотвращает обращение к мусору при клике.
            skill_buttons_.add(MenuItem{s_name, [this, sid]() {
                if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                    request_skill(sid);
                }
            }});
        }
        
        update_system_buttons(ctx);
        
        ui_initialized_ = true;
    }

    void execute_player_move(GameContext& ctx, SkillID /*sid*/, const Skill& info)
    {
        if (!enemy_) return;
        
        apply_skill_effect(ctx, info, true); 
        log_message_ = "You used " + info.name + "!";
        
        player_turn_ = false;
        turn_timer_ = 60; // Задержка 1 секунда (при 60 FPS)
    }

    void execute_enemy_move(GameContext& ctx)
    {
        if (!enemy_) return;

        // Генерация реплики на основе характера
        std::string shout = "...";
        std::string trait = enemy_->personality;
        if (trait == "Aggressive") shout = "I'll crush you!";
        else if (trait == "Arrogant") shout = "You're pathetic.";
        else if (trait == "Fearful") shout = "Stay back! I'm warning you!";
        else if (trait == "Merciless") shout = "Your life ends here.";
        else if (trait == "Flirty") shout = "Don't you want to play instead?";
        else if (trait == "Calm") shout = "Let's finish this quickly.";
        
        if (enemy_->skill_count > 0) {
            int idx = rand() % enemy_->skill_count;
            SkillID sid = enemy_->skills[idx];
            const Skill& info = get_skill_info(sid);
            
            apply_skill_effect(ctx, info, false); 
            log_message_ = std::string(enemy_->name) + ": \"" + shout + "\" (Used " + info.name + ")";
        } else {
            // Фолбэк, если у врага нет скиллов
            ctx.world_manager->player_ctrl.player().combat_stats.current_hp -= 1;
            log_message_ = "Enemy struggles!";
        }
        
        player_turn_ = true;
    }

    void apply_skill_effect(GameContext& ctx, const Skill& skill, bool player_source)
    {
        if (!ctx.world_manager) return;
        Player& p = ctx.world_manager->player_ctrl.player();
        NPC* npc = enemy_;
        
        int power = skill.power; 
        
        if (player_source) {
            // Игрок -> NPC
            switch (skill.type) {
                case SkillType::Physical:
                case SkillType::Magic:
                    npc->combat_stats.current_hp -= power;
                    break;
                case SkillType::Lust:
                    npc->will -= power;
                    npc->lust += power / 2;
                    break;
                case SkillType::Heal:
                    p.combat_stats.current_hp = std::min(p.combat_stats.current_hp + power, p.combat_stats.max_hp);
                    break;
                default: break;
            }
        } else {
            // NPC -> Игрок
            switch (skill.type) {
                case SkillType::Physical:
                case SkillType::Magic:
                    p.combat_stats.current_hp -= power;
                    break;
                case SkillType::Lust:
                    p.will -= power;
                    p.lust += power / 2;
                    break;
                case SkillType::Heal:
                    npc->combat_stats.current_hp = std::min(npc->combat_stats.current_hp + power, npc->combat_stats.max_hp);
                    break;
                default: break;
            }
        }
        
        check_win_condition(ctx);
    }

    void check_win_condition(GameContext& ctx)
    {
        if (!ctx.world_manager) return;
        Player& p = ctx.world_manager->player_ctrl.player();
        std::string trait = enemy_->personality;
        
        // Логика капитуляции (Surrender)
        bool should_surrender = false;
        if (enemy_->will <= 20) should_surrender = true; // Сломлена воля
        if (trait == "Fearful" && enemy_->combat_stats.current_hp < 40) should_surrender = true; // Трусливый сдается при 40% HP
        if (trait == "Calm" && enemy_->combat_stats.current_hp < 15) should_surrender = true;    // Спокойный — при 15%
        
        if (should_surrender && !npc_surrendered_ && enemy_->combat_stats.current_hp > 0) {
            npc_surrendered_ = true;
            log_message_ = std::string(enemy_->name) + " drops weapon: \"Wait! I surrender!\"";
            // Бой не заканчивается сразу, давая игроку выбор через UI (в следующем шаге)
            return; 
        }

        if (enemy_->combat_stats.current_hp <= 0) {
            log_message_ = "Victory! " + std::string(enemy_->name) + " has fallen.";
            end_battle(true);
        }
        else if (enemy_->will <= 0) {
            log_message_ = "Victory! Enemy Submitted.";
            end_battle(true);
        }
        else if (p.combat_stats.current_hp <= 0) {
            log_message_ = "Defeat... You passed out.";
            end_battle(false);
        }
        else if (p.will <= 0) {
            log_message_ = "Defeat... Broken by lust.";
            end_battle(false);
        }
    }

    void end_battle(bool victory)
    {
        battle_ended_ = true;
        player_won_ = victory;
        turn_timer_ = 120; // Пауза перед выходом
    }

public:
    void start_battle(NPC* enemy, GameContext& ctx)
    {
        // Лог для отладки
        SDL_Log("!!! BattleState: Starting battle with ID %d", enemy->id);
        
        enemy_ = enemy;
        
        // Recalculate enemy combat stats based on attributes
        enemy_->combat_stats.recalculate(100, 10, enemy_->attributes);
        enemy_->combat_stats.current_hp = enemy_->combat_stats.max_hp;
        enemy_->combat_stats.current_mp = enemy_->combat_stats.max_mp;
        
        player_turn_ = true;
        battle_ended_ = false;
        player_won_ = false;
        npc_surrendered_ = false;
        turn_timer_ = 0;
        escape_attempts_ = 0;
        escape_focus_ = 0;
        
        std::string type_name = "Enemy";
        if (enemy->is_special) type_name = "Special Target";
        log_message_ = std::string(enemy->name) + " approaches! (" + type_name + ")";
        
        ctx.picked = false; 
        ctx.battle_target_id = -1;
        ctx.active_battle_id = enemy->id;
        init_ui(ctx); 
        init_pause_buttons(ctx);
    }

    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        InputEvent evt;
        const bool input_processed = input_manager_.process_event(event, ctx, evt);

        // 1. Выход из боя (по клику в конце)
        if (battle_ended_ && turn_timer_ <= 0) {
            bool trigger_exit = false;

            if (input_processed && (evt.action == InputAction::Press || evt.action == InputAction::Click)) {
                trigger_exit = true;
            }
            if (event.type == SDL_KEYDOWN) {
                trigger_exit = true;
            }

            if (trigger_exit) {
                if (player_won_ && enemy_) {
                    ctx.world_manager->npcs.despawn(enemy_);
                }
                
                ctx.active_battle_id = -1;
                pop_state(ctx);
                enemy_ = nullptr;
                ctx.redraw_requested = true;
            }
            return;
        }

        // 2. Ввод игрока (клики по кнопкам)
        if (player_turn_ && !battle_ended_) {
            // Используем Press для отзывчивости интерфейса (как было в оригинале с MOUSEBUTTONDOWN)
            if (input_processed && evt.action == InputAction::Press) {
                if (!pause_buttons_initialized_ ||
                    last_buttons_width_ != ctx.window_width ||
                    last_buttons_height_ != ctx.window_height) {
                    init_pause_buttons(ctx);
                }
                if (pause_buttons_.handle_press(evt.x, evt.y)) {
                    return;
                }
                set_pick(ctx, evt.x, evt.y);
            }
        }
    }

    void update(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        if (target_id_ == -1 && ctx.battle_target_id != -1) {
            target_id_ = ctx.battle_target_id;
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
            const Skill& info = get_skill_info(pending_skill_);
            execute_player_move(ctx, pending_skill_, info);
        }
        
        // Проверка триггера (если бой запустился извне)
        if (!pause_buttons_initialized_ ||
            last_buttons_width_ != ctx.window_width ||
            last_buttons_height_ != ctx.window_height) {
            init_pause_buttons(ctx);
        }

        if (!enemy_ && target_id_ != -1) {
            if (ctx.world_manager) {
                NPC* target = ctx.world_manager->npcs.get_by_id(target_id_);
                if (target) {
                    start_battle(target, ctx);
                } else {
                    ctx.active_battle_id = -1;
                    pop_state(ctx, false);
                }
            }
        }

        // ВАЖНО: В бою всегда обновляем экран, чтобы видеть анимации и изменения HP
        ctx.redraw_requested = true;

        if (turn_timer_ > 0) {
            turn_timer_--;
            if (turn_timer_ == 0 && !player_turn_ && !battle_ended_) {
                execute_enemy_move(ctx);
            }
        }
    }

    void render(GameContext& ctx, TextureManager& textures, EntityManager& /*entities*/) override
    {
        // 1. Фон
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, ui_color("#050510FF"));

        if (!ctx.world_manager || !enemy_) return;
        const Player& p = ctx.world_manager->player_ctrl.player();

        // 2. Спрайт врага
        int sprite_size = 256;
        if (sprite_size > ctx.window_width) sprite_size = ctx.window_width - 40;
        
        SDL_Rect enemy_rect = ui_centered_rect(ctx.window_width, ctx.window_height, sprite_size, sprite_size);
        enemy_rect.y -= 80;
        
        size_t s_idx = (size_t)ObjectType::Bandit;
        if (enemy_->type == NPCType::Peasant) s_idx = (size_t)ObjectType::Peasant;
        if (enemy_->type == NPCType::Woodcutter) s_idx = (size_t)ObjectType::Woodcutter;
        if (enemy_->type == NPCType::Guard) s_idx = (size_t)ObjectType::Guard;
        if (enemy_->type == NPCType::Merchant) s_idx = (size_t)ObjectType::Merchant;
        
        SDL_RenderCopy(ctx.renderer, textures.sprite(s_idx), nullptr, &enemy_rect);

        // 3. Статы
        draw_bars(ctx, 20, ctx.window_height - 350, p.combat_stats.current_hp, p.combat_stats.max_hp, p.will, p.max_will, "Player");
        draw_bars(ctx, ctx.window_width - 220, ctx.window_height - 350, enemy_->combat_stats.current_hp, enemy_->combat_stats.max_hp, enemy_->will, enemy_->max_will, enemy_->name);

        // 4. Лог
        render_text(ctx, log_message_, 
                    ctx.window_width / 2 - 200, 40, 400, 30, {255, 255, 255, 255});

        // 5. Кнопки (отрисовка и обработка)
        if (!battle_ended_ && player_turn_) {
            bool main_picked = ctx.picked;
            bool system_picked = ctx.picked;

            if (npc_surrendered_) {
                mercy_buttons_.render_and_handle(
                    ctx,
                    ctx.window_width / 2, ctx.window_height - 300,
                    240, 40, 10,
                    ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, main_picked
                );
            } else {
                skill_buttons_.render_and_handle(
                    ctx,
                    ctx.window_width / 2, ctx.window_height - 300,
                    240, 40, 10,
                    ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, main_picked
                );
            }
             
             update_system_buttons(ctx);
             system_buttons_.render_and_handle(
                ctx,
                ctx.window_width / 2, ctx.window_height - 60,
                240, 40, 10,
                ctx.curs_x, ctx.curs_y, ctx.pick_x, ctx.pick_y, system_picked
            );

            if (ctx.picked) {
                ctx.picked = false;
            }
        }
        
        if (battle_ended_ && turn_timer_ <= 0) {
             render_text(ctx, "[ Tap to Continue ]", 
                    ctx.window_width / 2 - 150, ctx.window_height - 100, 
                    300, 30, {255, 255, 0, 255});
        }

        if (pause_buttons_initialized_) {
            pause_buttons_.render(ctx);
        }
    }

    void draw_bars(GameContext& ctx, int x, int y, int hp, int max_hp, int will, int max_will, const std::string& label)
    {
        render_text(ctx, label, x, y - 25, 100, 20, {255,255,255,255});
        
        int bar_w = 200;
        int bar_h = 12;
        
        // HP Bar
        ui_fill_rect(ctx.renderer, {x, y, bar_w, bar_h}, ui_color("#330000FF"));
        if (max_hp > 0) {
            int fill = (int)((float)std::max(0, hp) / max_hp * bar_w);
            ui_fill_rect(ctx.renderer, {x, y, fill, bar_h}, ui_color("#FF0000FF"));
        }
        
        // Will Bar
        ui_fill_rect(ctx.renderer, {x, y + 15, bar_w, bar_h}, ui_color("#300030FF"));
        if (max_will > 0) {
            int fill = (int)((float)std::max(0, will) / max_will * bar_w);
            ui_fill_rect(ctx.renderer, {x, y + 15, fill, bar_h}, ui_color("#FF69B4FF"));
        }
    }
};

inline StateRegistrar<BattleState> register_battle_state_{GameMode::Fight};

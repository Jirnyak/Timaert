#include "states/battle_state.h"
#include "systems/world_manager.h"
#include "systems/player.h"
#include "systems/attributes.h"
#include "systems/economy.h"
#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/npc.h"
#include "rendering/ra_icon.h"
#include "rendering/texture_manager.h"
#include <SDL_rect.h>
#include <SDL_render.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <entt/entt.hpp>

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

    const int will_pct = (p.max_will > 0) ? (p.will * 100 / p.max_will) : 0;

    const int enemy_hp_pct = (enemy_max_life_ > 0) ? (enemy_life_ * 100 / enemy_max_life_) : 0;

    int chance = 20;
    chance += will_pct / 4;
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

    const int will_loss = 6 + escape_attempts_ * 3;
    p.will = std::max(0, p.will - will_loss);

    log_message_ = "You try to flee (" + std::to_string(chance) + "%) but are blocked. Will -"
                   + std::to_string(will_loss) + ".";
    if (escape_attempts_ >= kEscapeMaxAttempts) {
        log_message_ += " You are cornered. Next time you must give up.";
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
    const std::string label =
        give_up
            ? "Give Up"
            : "Run ("
                  + std::to_string(compute_escape_chance(ctx.world_manager->player_ctrl.player()))
                  + "%)";
    const RaIcon icon = give_up ? RaIcon::XMark : RaIcon::ShoePrints;

    system_buttons_.add(MenuItem{label,
                                 [this]() {
                                     if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                                         request_escape();
                                     }
                                 },
                                 icon});
}

void BattleState::init_ui(GameContext& ctx) {
    skill_buttons_.clear();
    system_buttons_.clear();
    mercy_buttons_.clear();

    if (!ctx.world_manager)
        return;
    Player& p_mutable = ctx.world_manager->player_ctrl.player();
    const Player& p = p_mutable;

    mercy_buttons_.add(MenuItem{
        "Spare (Mercy)",
        [this]() {
            if (!ctx_ || !ctx_->world_manager)
                return;
            Player& p = ctx_->world_manager->player_ctrl.player();
            log_message_ =
                "You show mercy. " + enemy_name_ + " flees in tears, grateful for her life.";
            p.reputation[static_cast<size_t>(
                enemy_type_ == NPCType::Bandit ? FactionID::Faction2 : FactionID::Faction1)] += 15;
            end_battle(true);
        },
        RaIcon::Hearts});

    mercy_buttons_.add(MenuItem{"Loot (Rob)",
                                [this]() {
                                    if (!ctx_ || !ctx_->world_manager)
                                        return;
                                    Player& p = ctx_->world_manager->player_ctrl.player();
                                    int gold = 30 + (rand() % 70);
                                    p.inventory.add_capital(gold);

                                    ItemType loot_item = static_cast<ItemType>(1 + (rand() % 5));
                                    p.inventory.add(loot_item, 1);

                                    log_message_ =
                                        "You robbed " + enemy_name_ + " for " + std::to_string(gold)
                                        + "g and her "
                                        + ITEM_DATABASE[static_cast<size_t>(loot_item)].name + ".";
                                    p.reputation[static_cast<size_t>(FactionID::Faction1)] -= 10;
                                    end_battle(true);
                                },
                                RaIcon::GoldBar});

    mercy_buttons_.add(MenuItem{
        "Abuse (Theater)",
        [this]() {
            if (!ctx_ || !ctx_->world_manager)
                return;
            Player& p = ctx_->world_manager->player_ctrl.player();
            const int roll = static_cast<int>(random_u32_inclusive(ctx_->rng, 9));
            std::string desc;
            ItemType reward = ItemType::Rags;
            int lust_gain = 40;

            if (enemy_type_ == NPCType::Bandit) {
                static const char* b_scenes[] = {
                    "You force the bandit girl to the dirt, binding her wrists tight with her own "
                    "belt. She glares up, face flushed with a mix of rage and sudden heat.",
                    "With slow, deliberate movements, you undo each buckle of her leather armor. "
                    "She shivers as the cold air hits her bared skin, her defiance melting into a "
                    "whimper.",
                    "You claim a rough, dominant kiss to silence her curses. Her body goes weak in "
                    "your arms as you start to strip away the rags she calls clothes.",
                    "You use your dagger to carefully shred her tunic into ribbons. She watches, "
                    "breathless, as her modesty is taken away piece by piece until she stands "
                    "fully exposed.",
                    "The bandit girl gasps as you explore the curves of her body with possessive "
                    "hands, marking her as your prize before taking her mask and gear.",
                    "You command her to dance nude in the moonlight at swordpoint. She obeys with "
                    "trembling legs, her eyes never leaving yours as you claim her boots.",
                    "She yields completely, offering her body in exchange for her life. You take "
                    "her dignity and her leather armor, leaving her shivering in the grass.",
                    "You fix a makeshift collar around her neck, forcing her to follow you on all "
                    "fours for a moment before taking her clothes as your trophy.",
                    "You pin her against a tree, your bodies pressed tight. She moans softly as "
                    "you strip her to the waist, savoring her total submission.",
                    "The fight is gone from her eyes. You slowly undress her, taking every scrap "
                    "of silk and leather she owns, leaving her with nothing but a deep blush."};
                desc = b_scenes[roll];
                reward = (roll % 2 == 0) ? ItemType::BanditMask : ItemType::LeatherArmor;
                lust_gain = 50;
            } else if (enemy_type_ == NPCType::Guard) {
                static const char* g_scenes[] = {
                    "The guard girl gasps as you undo the heavy buckles of her breastplate. 'This "
                    "is against regulations!' she moans, her face flushing crimson as you expose "
                    "her undershirt.",
                    "You remove her iron helm, revealing a face full of pride that quickly melts "
                    "into submission as you start to unlace her military tunic.",
                    "You use her own cloak to bind her hands above her head. She shivers, her "
                    "breath coming in short hitches as you claim her armor as your trophy.",
                    "Her uniform is ripped open, exposing her skin to the cold air. The once-stern "
                    "defender now whimpers, unable to meet your dominant gaze.",
                    "You force her to stand at attention while you slowly undress her. Each piece "
                    "of equipment hitting the floor sounds like a crack in her discipline.",
                    "The heavy boots are removed, leaving her vulnerable and bare-footed. She "
                    "trembles as your hands find the laces of her leather leggings.",
                    "You pin the guard against the city wall, your bodies pressed tight. Her heart "
                    "races against your chest as you strip her of her rank and her clothes.",
                    "She tries to maintain a stoic face, but her knees go weak as you slide her "
                    "greaves off, exposing her shapely legs to the moonlight.",
                    "You claim a dominant kiss, tasting her surrender. Her resolve breaks "
                    "completely as you shred her official tabard into rags.",
                    "The fight is gone. You leave the proud warrior shivering in nothing but her "
                    "blushing skin, taking her sword and her dignity."};
                desc = g_scenes[roll];
                reward = (roll % 2 == 0) ? ItemType::IronHelmet : ItemType::LeatherArmor;
                lust_gain = 45;
            } else if (enemy_type_ == NPCType::Witch) {
                static const char* w_scenes[] = {
                    "The witch's magic fails as you bind her wrists with silk. She glares with "
                    "burning eyes, but her breath hitches as you reach for her ritual robes.",
                    "You slowly unravel her dark vestments, piece by piece. Strange runes on her "
                    "skin glow faintly as they are exposed to your touch.",
                    "She whispers a curse that turns into a moan as you strip her to the waist. "
                    "Her mystical superiority is replaced by raw, trembling vulnerability.",
                    "You take her staff and use it to pin her down. She whimpers as you carefully "
                    "shred her silken dress, savoring the look of defeat on her face.",
                    "The air is thick with tension as you remove her arcane circlet. You explore "
                    "her curves with possessive hands, marking the sorceress as your own.",
                    "She begs you to stop, but her body betrays her, arching into your touch as "
                    "her heavy robes fall to the dirt.",
                    "You find her hidden potions and ritual knife tucked away in her garter. She "
                    "blushes deeply as you claim both her secrets and her modesty.",
                    "Bound and exposed, the witch can only watch as you savor her beauty. Her "
                    "magical aura is gone, replaced by a deep, enticing flush.",
                    "You force her to recite a 'submission' spell while you strip her bare. Her "
                    "voice trembles as much as her exposed body.",
                    "The moon witnesses her total exposure. You leave the witch shivering amidst "
                    "her shredded robes, taking her mystic jewelry as loot."};
                desc = w_scenes[roll];
                reward = (roll % 2 == 0) ? ItemType::RitualKnife : ItemType::MagicDust;
                lust_gain = 60;
            } else {
                static const char* m_scenes[] = {
                    "The merchant girl tries to offer her body to save her gold. You accept the "
                    "'payment', savoring her desperate beauty before stripping her fine clothes "
                    "anyway.",
                    "You pin her against her own trade cart. She gasps as your hands find the silk "
                    "ribbons of her bodice, unraveling her modest layers with slow, possessive "
                    "care.",
                    "She begs for mercy with a deep blush. You respond by systematically removing "
                    "her fine stockings and shoes, leaving her shivering and bare-footed in the "
                    "dirt.",
                    "You use your blade to tease the laces of her dress until they snap. She "
                    "watches in wide-eyed surrender as the fabric slides down, exposing her to "
                    "your hunger.",
                    "The girl trembles as you explore the warmth of her skin. Her breath hitches "
                    "in a soft moan when you claim her expensive silk scarf as a trophy of her "
                    "defeat.",
                    "You force her into a submissive pose, enjoying the view of her flushed skin "
                    "before taking every scrap of her clothing to sell later.",
                    "She offers a heavy purse to be let go. You take the gold, then lean in to "
                    "claim a dominant kiss while unfastening her tunic with practiced ease.",
                    "Bound with her own silk ribbons, she can only watch as you admire her fully "
                    "exposed form. Her pride is gone, replaced by a lingering, heated gaze.",
                    "You slowly undress the simple peasant girl, her skin warm and smelling of "
                    "wild flowers. She shivers as you remove her rough tunic, revealing her raw, "
                    "natural beauty.",
                    "You claim her as the rightful prize of the road. She stands breathless and "
                    "blushing crimson as you claim every layer of her modesty for yourself, "
                    "savoring her submission."};
                desc = m_scenes[roll];
                reward = (roll % 2 == 0) ? ItemType::SilkScarf : ItemType::PeasantClothes;
                lust_gain = 40;
            }

            log_message_ = desc;
            p.lust += lust_gain;
            p.inventory.add(reward, 1);
            p.reputation[static_cast<size_t>(FactionID::Wilderness)] -= 15;

            npc_surrendered_ = false;
            battle_ended_ = true;
            player_won_ = true;
            turn_timer_ = 360;
        },
        RaIcon::Skull});

    for (size_t i = 0; i < (size_t)p.skill_count; ++i) {
        SkillID sid = p.skills[i];
        std::string s_name = get_skill_info(sid).name;

        skill_buttons_.add(MenuItem{s_name, [this, sid]() {
                                        if (player_turn_ && !battle_ended_ && turn_timer_ <= 0) {
                                            request_skill(sid);
                                        }
                                    }});
    }

    update_system_buttons(ctx);

    ui_initialized_ = true;
}

void BattleState::execute_player_move(GameContext& ctx, SkillID /*sid*/, const Skill& info) {
    if (!has_enemy())
        return;

    apply_skill_effect(ctx, info, true);
    log_message_ = "You used " + info.name + "!";

    player_turn_ = false;
    turn_timer_ = 60;
}

void BattleState::execute_enemy_move(GameContext& ctx) {
    if (!has_enemy())
        return;

    const Skill& info = get_skill_info(SkillID::Punch);
    apply_skill_effect(ctx, info, false);
    log_message_ = enemy_name_ + " attacks! (Used " + info.name + ")";

    player_turn_ = true;
}

void BattleState::apply_skill_effect(GameContext& ctx, const Skill& skill, bool player_source) {
    if (!ctx.world_manager)
        return;
    Player& p = ctx.world_manager->player_ctrl.player();

    int power = skill.power;

    if (player_source) {
        switch (skill.type) {
            case SkillType::Physical:
            case SkillType::Magic:
                if (auto* h = enemy_ref_.try_get<ecs::Health>()) {
                    h->current -= power;
                    enemy_life_ = h->current;
                } else {
                    enemy_life_ -= power;
                }
                break;
            case SkillType::Lust:
                if (auto* stats = enemy_ref_.try_get<ecs::CombatStats>()) {
                    stats->will -= power;
                    stats->lust += power / 2;
                    enemy_will_ = stats->will;
                } else {
                    enemy_will_ -= power;
                }
                break;
            case SkillType::Heal:
                p.combat_stats.current_hp =
                    std::min(p.combat_stats.current_hp + power, p.combat_stats.max_hp);
                break;
            default:
                break;
        }
    } else {
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
                if (auto* h = enemy_ref_.try_get<ecs::Health>()) {
                    h->current = std::min(h->current + power, h->max);
                    enemy_life_ = h->current;
                } else {
                    enemy_life_ = std::min(enemy_life_ + power, enemy_max_life_);
                }
                break;
            default:
                break;
        }
    }

    check_win_condition(ctx);
}

void BattleState::check_win_condition(GameContext& ctx) {
    if (!ctx.world_manager)
        return;
    Player& p = ctx.world_manager->player_ctrl.player();

    int e_life = enemy_life_;
    int e_will = enemy_will_;

    bool should_surrender = false;
    if (e_will <= 20)
        should_surrender = true;
    if (e_life < 40)
        should_surrender = true;

    if (should_surrender && !npc_surrendered_ && e_life > 0) {
        npc_surrendered_ = true;
        log_message_ = enemy_name_ + " drops weapon: \"Wait! I surrender!\"";
        return;
    }

    if (e_life <= 0) {
        log_message_ = "Victory! " + enemy_name_ + " has fallen.";
        end_battle(true);
    } else if (e_will <= 0) {
        log_message_ = "Victory! Enemy Submitted.";
        end_battle(true);
    } else if (p.combat_stats.current_hp <= 0) {
        log_message_ = "Defeat... You passed out.";
        end_battle(false);
    } else if (p.will <= 0) {
        log_message_ = "Defeat... Broken by lust.";
        end_battle(false);
    }
}

void BattleState::end_battle(bool victory) {
    battle_ended_ = true;
    player_won_ = victory;
    turn_timer_ = 120;
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
    npc_surrendered_ = false;
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
    if (registry.all_of<ecs::CombatStats>(entity)) {
        auto& stats = registry.get<ecs::CombatStats>(entity);
        enemy_will_ = stats.will;
        enemy_max_will_ = stats.max_will;
    }

    log_message_ = enemy_name + " approaches! (" + type_name + ")";

    ctx.picked = false;
    ctx.battle_target_entity = entt::null;
    init_ui(ctx);
    init_pause_buttons(ctx);
}

void BattleState::handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/) {
    InputEvent evt;
    const bool input_processed = input_manager_.process_event(event, ctx, evt);

    if (battle_ended_ && turn_timer_ <= 0) {
        bool trigger_exit = false;

        if (input_processed
            && (evt.action == InputAction::Press || evt.action == InputAction::Click)) {
            trigger_exit = true;
        }
        if (event.type == SDL_KEYDOWN) {
            trigger_exit = true;
        }

        if (trigger_exit) {
            if (player_won_) {
                if (enemy_ref_.valid()) {
                    ctx.ecs_world->mark_dead(enemy_ref_.get());
                }
            }

            pop_state(ctx, false);
        }
        return;
    }

    if (player_turn_ && !battle_ended_) {
        if (input_processed && evt.action == InputAction::Press) {
            if (!pause_buttons_initialized_ || last_buttons_width_ != ctx.window_width
                || last_buttons_height_ != ctx.window_height) {
                init_pause_buttons(ctx);
            }
            if (pause_buttons_.handle_press(evt.x, evt.y)) {
                return;
            }
            set_pick(ctx, evt.x, evt.y);
        }
    }
}

void BattleState::update(GameContext& ctx, TextureManager& /*textures*/) {
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
    SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
    ui_fill_rect(ctx.renderer, overlay, ui_color("#050510FF"));

    if (!ctx.world_manager || !has_enemy())
        return;
    const Player& p = ctx.world_manager->player_ctrl.player();

    int sprite_size = 256;
    if (sprite_size > ctx.window_width)
        sprite_size = ctx.window_width - 40;

    SDL_Rect enemy_rect =
        ui_centered_rect(ctx.window_width, ctx.window_height, sprite_size, sprite_size);
    enemy_rect.y -= 80;

    NPCType etype = enemy_type_;
    size_t s_idx = (size_t)ObjectType::Bandit;
    if (etype == NPCType::Peasant)
        s_idx = (size_t)ObjectType::Peasant;
    if (etype == NPCType::Woodcutter)
        s_idx = (size_t)ObjectType::Woodcutter;
    if (etype == NPCType::Guard)
        s_idx = (size_t)ObjectType::Guard;
    if (etype == NPCType::Merchant)
        s_idx = (size_t)ObjectType::Merchant;
    if (etype == NPCType::Witch)
        s_idx = (size_t)ObjectType::Witch;
    if (etype == NPCType::Caravan)
        s_idx = (size_t)ObjectType::Caravan;

    SDL_RenderCopy(ctx.renderer, textures.sprite(s_idx), nullptr, &enemy_rect);

    draw_bars(ctx, 20, ctx.window_height - 350, p.life, p.max_life, p.will, p.max_will, "Player");
    draw_bars(ctx,
              ctx.window_width - 220,
              ctx.window_height - 350,
              enemy_life_,
              enemy_max_life_,
              enemy_will_,
              enemy_max_will_,
              enemy_name_.c_str());

    render_text(ctx, log_message_, ctx.window_width / 2 - 200, 40, 400, 30, {255, 255, 255, 255});

    if (!battle_ended_ && player_turn_) {
        bool main_picked = ctx.picked;
        bool system_picked = ctx.picked;

        if (npc_surrendered_) {
            mercy_buttons_.render_and_handle(ctx,
                                             ctx.window_width / 2,
                                             ctx.window_height - 300,
                                             240,
                                             40,
                                             10,
                                             ctx.curs_x,
                                             ctx.curs_y,
                                             ctx.pick_x,
                                             ctx.pick_y,
                                             main_picked);
        } else {
            skill_buttons_.render_and_handle(ctx,
                                             ctx.window_width / 2,
                                             ctx.window_height - 300,
                                             240,
                                             40,
                                             10,
                                             ctx.curs_x,
                                             ctx.curs_y,
                                             ctx.pick_x,
                                             ctx.pick_y,
                                             main_picked);
        }

        update_system_buttons(ctx);
        system_buttons_.render_and_handle(ctx,
                                          ctx.window_width / 2,
                                          ctx.window_height - 60,
                                          240,
                                          40,
                                          10,
                                          ctx.curs_x,
                                          ctx.curs_y,
                                          ctx.pick_x,
                                          ctx.pick_y,
                                          system_picked);

        if (ctx.picked) {
            ctx.picked = false;
        }
    }

    if (battle_ended_ && turn_timer_ <= 0) {
        render_text(ctx,
                    "[ Tap to Continue ]",
                    ctx.window_width / 2 - 150,
                    ctx.window_height - 100,
                    300,
                    30,
                    {255, 255, 0, 255});
    }

    if (pause_buttons_initialized_) {
        pause_buttons_.render(ctx);
    }
}

void BattleState::draw_bars(GameContext& ctx,
                            int x,
                            int y,
                            int hp,
                            int max_hp,
                            int will,
                            int max_will,
                            const std::string& label) {
    render_text(ctx, label, x, y - 25, 100, 20, {255, 255, 255, 255});

    int bar_w = 200;
    int bar_h = 12;

    ui_fill_rect(ctx.renderer, {x, y, bar_w, bar_h}, ui_color("#330000FF"));
    if (max_hp > 0) {
        int fill = (int)((float)std::max(0, hp) / max_hp * bar_w);
        ui_fill_rect(ctx.renderer, {x, y, fill, bar_h}, ui_color("#FF0000FF"));
    }

    ui_fill_rect(ctx.renderer, {x, y + 15, bar_w, bar_h}, ui_color("#300030FF"));
    if (max_will > 0) {
        int fill = (int)((float)std::max(0, will) / max_will * bar_w);
        ui_fill_rect(ctx.renderer, {x, y + 15, fill, bar_h}, ui_color("#FF69B4FF"));
    }
}

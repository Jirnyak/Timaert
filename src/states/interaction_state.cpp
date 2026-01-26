#include "states/interaction_state.h"
#include "ecs/components/npc.h"
#include "ecs/world.h"
#include "rendering/ra_icon.h"
#include "rendering/texture_manager.h"
#include "systems/economy.h"
#include <SDL_keycode.h>
#include <SDL_rect.h>
#include <SDL_render.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <entt/entt.hpp>

void InteractionState::init_pause_buttons(GameContext& ctx) {
    const UiButtonLayout layout = ui_default_button_layout(ctx);
    const int btn_size = layout.btn_size;
    const int margin = layout.margin;
    pause_buttons_.clear();
    pause_buttons_.add(UIButton{{margin, layout.speed_y, btn_size, btn_size},
                                "",
                                [this]() { pending_action_ = InteractionAction::Leave; },
                                nullptr,
                                RaIcon::Reverse});
    pause_buttons_initialized_ = true;
    last_buttons_width_ = ctx.window_width;
    last_buttons_height_ = ctx.window_height;
}

void InteractionState::init_ui(GameContext& ctx) {
    interaction_menu_.clear();

    if (!ctx.world_manager)
        return;

    interaction_menu_.add(MenuItem{"Talk",
                                   [this]() { pending_action_ = InteractionAction::Talk; },
                                   RaIcon::SpeechBubble});

    interaction_menu_.add(MenuItem{"Trade",
                                   [this]() { pending_action_ = InteractionAction::Trade; },
                                   RaIcon::GoldBar});

    interaction_menu_.add(MenuItem{"Quest",
                                   [this]() { pending_action_ = InteractionAction::Quest; },
                                   RaIcon::ScrollUnfurled});

    interaction_menu_.add(MenuItem{"Fight",
                                   [this]() { pending_action_ = InteractionAction::Fight; },
                                   RaIcon::CrossedSwords});

    interaction_menu_.add(MenuItem{"Leave",
                                   [this]() { pending_action_ = InteractionAction::Leave; },
                                   RaIcon::Reverse});

    ui_initialized_ = true;
}

void InteractionState::process_pending_action(GameContext& ctx) {
    dialogue_message_.clear();
    showing_quest_msg_ = false;

    switch (pending_action_) {
        case InteractionAction::Talk:
            handle_talk(ctx);
            break;
        case InteractionAction::Trade:
            handle_trade(ctx);
            break;
        case InteractionAction::Quest:
            handle_quest(ctx);
            break;
        case InteractionAction::Fight:
            handle_fight(ctx);
            break;
        case InteractionAction::Leave:
            pop_state(ctx, false);
            break;
        default:
            break;
    }
    pending_action_ = InteractionAction::None;
}

void InteractionState::handle_talk(GameContext& ctx) {
    if (!has_npc())
        return;

    if (is_npc_hostile(ctx)) {
        dialogue_message_ = npc_name_ + " says: \"I will kill you!\"";
        handle_fight(ctx);
        return;
    }

    switch (npc_type_) {
        case NPCType::Peasant:
            dialogue_message_ = npc_name_ + " says: \"Good day, traveler. These lands are harsh.\"";
            break;
        case NPCType::Merchant:
            dialogue_message_ =
                npc_name_ + " says: \"Looking for goods? I have the finest wares!\"";
            break;
        case NPCType::Guard:
            dialogue_message_ = npc_name_ + " says: \"Keep the peace, citizen.\"";
            break;
        case NPCType::Bandit:
            dialogue_message_ = npc_name_ + " says: \"Your coin or your life!\"";
            break;
        case NPCType::Witch:
            dialogue_message_ = npc_name_ + " says: \"The spirits whisper of your coming...\"";
            break;
        case NPCType::Woodcutter:
            dialogue_message_ = npc_name_ + " says: \"Honest work keeps one warm in winter.\"";
            break;
        case NPCType::Caravan:
            dialogue_message_ =
                npc_name_ + " says: \"Safe travels, friend. The roads are dangerous.\"";
            break;
        default:
            dialogue_message_ = npc_name_ + " says: \"Greetings, traveler.\"";
            break;
    }
}

void InteractionState::handle_trade(GameContext& ctx) {
    if (!has_npc() || !ctx.world_manager)
        return;

    if (is_npc_hostile(ctx)) {
        dialogue_message_ = npc_name_ + " says: \"I will kill you!\"";
        handle_fight(ctx);
        return;
    }

    showing_trade_ = !showing_trade_;
    dialogue_message_.clear();
    showing_quest_msg_ = false;
}

void InteractionState::handle_quest(GameContext& ctx) {
    if (!has_npc())
        return;

    if (is_npc_hostile(ctx)) {
        dialogue_message_ = npc_name_ + " says: \"I will kill you!\"";
        handle_fight(ctx);
        return;
    }

    showing_quest_msg_ = true;
    showing_trade_ = false;
    dialogue_message_.clear();
}

void InteractionState::handle_fight(GameContext& ctx) {
    if (!has_npc())
        return;

    if (!npc_ref_.is_null()) {
        ctx.battle_target_entity = npc_ref_.get();
    }

    pop_state(ctx, false);
    push_state(ctx, StateRegistry::instance().create(GameMode::Fight));
}

void InteractionState::start_interaction_ecs(entt::entity entity, GameContext& ctx) {
    if (!ctx.ecs_world)
        return;
    auto& registry = ctx.ecs_world->registry;

    npc_ref_.assign(registry, entity);
    interaction_started_ = true;

    npc_name_ = "NPC";
    npc_type_ = NPCType::Peasant;

    if (registry.all_of<ecs::NPCTag>(entity)) {
        auto& tag = registry.get<ecs::NPCTag>(entity);
        npc_type_ = tag.type;
        npc_name_ = npc_type_name(tag.type);
    }
    if (registry.all_of<ecs::CharacterInfo>(entity)) {
        auto& info = registry.get<ecs::CharacterInfo>(entity);
        if (info.name[0] != '\0')
            npc_name_ = info.name;
    }
    if (registry.all_of<ecs::FactionMember>(entity)) {
        npc_faction_ = registry.get<ecs::FactionMember>(entity).faction;
    }

    ctx.picked = false;
    init_ui(ctx);
    init_pause_buttons(ctx);
}

void InteractionState::handle_event(SDL_Event& event,
                                    GameContext& ctx,
                                    TextureManager& /*textures*/) {
    InputEvent evt;
    const bool input_processed = input_manager_.process_event(event, ctx, evt);

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

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
            pop_state(ctx, false);
        }
    }
}

void InteractionState::update(GameContext& ctx, TextureManager& /*textures*/) {
    if (interaction_started_ && !npc_ref_.is_null()) {
        if (!npc_ref_.valid() || npc_ref_.has<ecs::Dead>()) {
            pop_state(ctx, false);
            return;
        }
    }

    if (pending_action_ != InteractionAction::None) {
        process_pending_action(ctx);
        return;
    }

    if (!pause_buttons_initialized_ || last_buttons_width_ != ctx.window_width
        || last_buttons_height_ != ctx.window_height) {
        init_pause_buttons(ctx);
    }

    if (loaded_from_save_ && !ui_initialized_) {
        init_ui(ctx);
        interaction_started_ = true;
    } else if (!interaction_started_ && !loaded_from_save_
               && ctx.battle_target_entity != entt::null) {
        if (ctx.ecs_world && ctx.ecs_world->registry.valid(ctx.battle_target_entity)) {
            start_interaction_ecs(ctx.battle_target_entity, ctx);
        } else {
            ctx.battle_target_entity = entt::null;
            pop_state(ctx, false);
        }
    }

    ctx.redraw_requested = true;
}

void InteractionState::render(GameContext& ctx, TextureManager& textures) {
    SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
    ui_fill_rect(ctx.renderer, overlay, ui_color("#050510FF"));

    if (!has_npc())
        return;
    if (!ctx.world_manager)
        return;

    if (showing_trade_) {
        render_trade_ui(ctx, textures);
        if (pause_buttons_initialized_) {
            pause_buttons_.render(ctx);
        }
        return;
    }

    int sprite_size = 256;
    if (sprite_size > ctx.window_width)
        sprite_size = ctx.window_width - 40;

    SDL_Rect npc_rect =
        ui_centered_rect(ctx.window_width, ctx.window_height, sprite_size, sprite_size);
    npc_rect.y -= 80;

    NPCType etype = npc_type_;
    size_t s_idx = (size_t)ObjectType::Peasant;
    if (etype == NPCType::Bandit)
        s_idx = (size_t)ObjectType::Bandit;
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

    SDL_RenderCopy(ctx.renderer, textures.sprite(s_idx), nullptr, &npc_rect);

    std::string display_name = npc_name_;
    render_text(ctx, display_name, ctx.window_width / 2 - 150, 40, 300, 30, {255, 255, 255, 255});

    if (!dialogue_message_.empty()) {
        int msg_y = ctx.window_height / 2 - 50;
        int panel_w = std::min(600, ctx.window_width - 80);
        SDL_Rect msg_panel = {ctx.window_width / 2 - panel_w / 2, msg_y, panel_w, 80};
        ui_draw_panel(ctx.renderer, msg_panel, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx,
                    dialogue_message_,
                    msg_panel.x + 20,
                    msg_panel.y + 25,
                    panel_w - 40,
                    25,
                    {255, 255, 255, 255});
    }

    if (showing_quest_msg_) {
        int msg_y = ctx.window_height / 2 - 50;
        int panel_w = std::min(500, ctx.window_width - 80);
        SDL_Rect msg_panel = {ctx.window_width / 2 - panel_w / 2, msg_y, panel_w, 100};
        ui_draw_panel(ctx.renderer, msg_panel, ui_color("#1A1A2E"), ui_color("#FF6B6B"));
        render_text(ctx,
                    "Sorry, quests are not implemented!",
                    msg_panel.x + 20,
                    msg_panel.y + 35,
                    panel_w - 40,
                    25,
                    {255, 200, 200, 255});
    }

    bool menu_picked = ctx.picked;
    interaction_menu_.render_and_handle(ctx,
                                        ctx.window_width / 2,
                                        ctx.window_height - 300,
                                        240,
                                        40,
                                        10,
                                        ctx.curs_x,
                                        ctx.curs_y,
                                        ctx.pick_x,
                                        ctx.pick_y,
                                        menu_picked);

    if (ctx.picked) {
        ctx.picked = false;
    }

    if (pause_buttons_initialized_) {
        pause_buttons_.render(ctx);
    }
}

void InteractionState::render_trade_ui(GameContext& ctx, TextureManager& textures) {
    if (!ctx.world_manager)
        return;
    const Player& p = ctx.world_manager->player_ctrl.player();

    render_text(ctx, "TRADE", ctx.window_width / 2 - 50, 20, 100, 30, {255, 255, 255, 255});

    const Inventory* npc_inv = nullptr;
    if (auto* inv_comp = npc_ref_.try_get<ecs::InventoryComponent>()) {
        npc_inv = &inv_comp->data;
    }

    const int cell_size = 24;
    const int cols = 16;
    const int rows = 8;
    const int margin = 20;
    const int panel_w = cols * cell_size + 40;
    const int panel_h = rows * cell_size + 80;

    int left_x = margin;
    int panel_y = 60;

    SDL_Rect player_panel = {left_x, panel_y, panel_w, panel_h};
    ui_draw_panel(ctx.renderer, player_panel, ui_color("#1A2A3A"), ui_color("#4A9EFF"));
    render_text(ctx, "Your Inventory", left_x + 10, panel_y + 10, 180, 20, {200, 220, 255, 255});

    render_inventory_grid(ctx,
                          textures,
                          p.inventory,
                          left_x + 20,
                          panel_y + 40,
                          cell_size,
                          cols,
                          rows);

    int right_x = ctx.window_width - panel_w - margin;

    SDL_Rect npc_panel = {right_x, panel_y, panel_w, panel_h};
    ui_draw_panel(ctx.renderer, npc_panel, ui_color("#2A1A1A"), ui_color("#FF9E4A"));
    std::string npc_label = npc_name_ + "'s Inventory";
    render_text(ctx, npc_label, right_x + 10, panel_y + 10, 180, 20, {255, 220, 200, 255});

    if (npc_inv) {
        render_inventory_grid(ctx,
                              textures,
                              *npc_inv,
                              right_x + 20,
                              panel_y + 40,
                              cell_size,
                              cols,
                              rows);
    } else {
        render_text(ctx,
                    "No inventory",
                    right_x + panel_w / 2 - 60,
                    panel_y + panel_h / 2,
                    120,
                    20,
                    {150, 150, 150, 255});
    }

    render_text(ctx,
                "[ Trading not yet functional - Press ESC to close ]",
                ctx.window_width / 2 - 250,
                ctx.window_height - 40,
                500,
                20,
                {150, 150, 150, 255});
}

void InteractionState::render_inventory_grid(GameContext& ctx,
                                             TextureManager& textures,
                                             const Inventory& inv,
                                             int start_x,
                                             int start_y,
                                             int cell_size,
                                             int cols,
                                             int rows) {
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int slot_idx = row * cols + col;
            if (slot_idx >= 256)
                break;

            const std::uint16_t amount = inv.get_at(slot_idx);
            const int cell_x = start_x + col * cell_size;
            const int cell_y = start_y + row * cell_size;

            SDL_Rect cell_rect = {cell_x, cell_y, cell_size, cell_size};
            ui_fill_rect(ctx.renderer, cell_rect, {30, 40, 55, 200});
            ui_draw_rect(ctx.renderer, cell_rect, {70, 90, 120, 255});

            if (amount > 0) {
                ItemType item_type = inv.get_item_type_at(slot_idx);
                SDL_Texture* item_texture = textures.item(item_type);
                if (item_texture) {
                    SDL_Rect src_rect = {16, 16, 16, 16};
                    SDL_Rect dst_rect = {cell_x + 2, cell_y + 2, cell_size - 4, cell_size - 4};
                    SDL_RenderCopy(ctx.renderer, item_texture, &src_rect, &dst_rect);
                }

                if (cell_size >= 20) {
                    std::string count_str = std::to_string(amount);
                    render_text(ctx,
                                count_str,
                                cell_x + cell_size - 18,
                                cell_y + cell_size - 14,
                                16,
                                12,
                                {200, 200, 50, 255});
                }
            }
        }
    }
}

#include "states/interaction_state.h"

#include <entt/entt.hpp>
#include <optional>

#include "ecs/components/npc.h"
#include "ecs/world.h"
#include "rendering/ra_icon.h"
#include "rendering/renderer.h"
#include "rendering/texture_manager.h"
#include "systems/economy.h"
#include "core/gfx_types.h"
#include "ecs/components/core.h"

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
    const InteractionAction action = pending_action_;
    pending_action_ = InteractionAction::None;
    
    dialogue_message_.clear();
    showing_quest_msg_ = false;
    
    switch (action) {
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
}

void InteractionState::handle_talk(GameContext& ctx) {
    if (!has_npc())
        return;

    if (is_npc_hostile(ctx)) {
        dialogue_message_ = npc_name_ + " says: \"I will kill you!\"";
        handle_fight(ctx);
        return;
    }

    // Lore-based dialogs from Characters.md
    switch (npc_type_) {
        case NPCType::Peasant: {
            const char* dialogues[] = {
                "Good day, traveler. These lands are harsh. The taxes grow ever higher.",
                "In these lands of Magika, even simple folk know a touch of magic. What brings you to our village?",
                "My children were born under free skies, thanks to the Czar-Peasant. But peace is fragile.",
                "Do you believe in the Path of Light? Or are you one who values Magika's freedom?",
                "Work from dawn to dusk is all I know. Are you a wanderer by choice or by curse?"
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        case NPCType::Merchant: {
            const char* dialogues[] = {
                "Looking for goods? I trade with all - Magika, the Empire, even neutral hands.",
                "The roads grow more dangerous. I hear whispers of war between the kingdoms.",
                "Gold flows best when you stay neutral. I've learned that well.",
                "Do you know of the prophecy? Strange omens appear in the sky these days.",
                "Quality wares, fair prices. I've served every faction and lived to tell it."
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        case NPCType::Guard: {
            const char* dialogues[] = {
                "Keep the peace, citizen. The King's law is absolute here.",
                "Did you hear? Mages are causing trouble again. Stay clear of them.",
                "This post is boring. Wish something exciting would happen.",
                "You look capable. Ever thought about joining the watch?",
                "Report any suspicious magic use immediately. Orders from above are strict."
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        case NPCType::Bandit: {
            const char* dialogues[] = {
                "Your coin or your life! Choose quickly, friend.",
                "The lords take everything. I just take my share back.",
                "You look too strong for an easy robbery. This might get interesting.",
                "Run while you still can. I'm giving you a chance.",
                "Rumors say the Czar-Peasant is coming. Even we bandits are nervous."
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        case NPCType::Witch: {
            const char* dialogues[] = {
                "The spirits whisper of your coming. They say you carry the weight of choices.",
                "Magika fades, child. I taste it in the air. An age is ending.",
                "You seek power? All power has a price. What will you pay?",
                "The dark arts grow bolder. The old protections weaken. Do you feel it?",
                "Come closer. Let me read the threads of fate wound around your soul."
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        case NPCType::Woodcutter: {
            const char* dialogues[] = {
                "Honest work keeps one warm in winter. Better than serving false gods or lying mages.",
                "The forest has her own laws. I respect them. You should too.",
                "Been cutting wood here for thirty years. Seen empires rise and fall from this hillside.",
                "The druids came through last season. They bless the trees now. Strange times.",
                "If you're fleeing the kingdoms, this forest will shelter you... for a price."
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        case NPCType::Caravan: {
            const char* dialogues[] = {
                "Safe travels, friend. The roads grow more dangerous each season.",
                "I trade goods from the free city of Tymert. The best neutral ground in the world.",
                "War comes. The mages sense it. The Empire prepares. Wise traders flee.",
                "You look like you've seen battle. Care to join my guard?",
                "In these times, information is worth more than gold. What have you heard?"
            };
            dialogue_message_ = npc_name_ + " says: \"" + dialogues[rand() % 5] + "\"";
            break;
        }
        default:
            dialogue_message_ = npc_name_ + " says: \"Greetings, traveler. Dark times we live in.\"";
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

    // Lower player's reputation with this faction by 10 when fight is triggered
    if (ctx.world_manager) {
        Player& p = ctx.world_manager->player_ctrl.player();
        std::int32_t& rep = p.reputation[static_cast<std::size_t>(npc_faction_)];
        rep -= 10;
        if (rep < -127) {
            rep = -127;  // Cap at minimum
        }
    }

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


void InteractionState::update(GameContext& ctx, TextureManager& /*textures*/) {
    if (interaction_started_ && !npc_ref_.is_null()) {
        if (!npc_ref_.valid() || npc_ref_.has<ecs::Dead>()) {
            pop_state(ctx, false);
            return;
        }
    }

    // Handle Trade UI clicks
    if (showing_trade_ && ctx.picked) {
        // Check pause buttons (Leave)
        if (pause_buttons_initialized_ && pause_buttons_.handle_press(ctx.pick_x, ctx.pick_y)) {
            ctx.picked = false;
            // Handle leave action logic if needed, usually button callback sets pending_action
        }
        
        if (ctx.picked) { // If button didn't consume click
            handle_trade_click(ctx);
            ctx.picked = false; // Consume click
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

void InteractionState::handle_trade_click(GameContext& ctx) {
    if (!ctx.world_manager || !has_npc())
        return;

    Player& p = ctx.world_manager->player_ctrl.player();
    Inventory* npc_inv = nullptr;
    if (auto* inv_comp = npc_ref_.try_get<ecs::InventoryComponent>()) {
        npc_inv = &inv_comp->data;
    }

    if (!npc_inv) return;

    // Replicate layout calculations from render_trade_ui
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    const int padding = static_cast<int>(15 * scale);
    const int font_title = static_cast<int>(28 * scale);
    const int cell_size = static_cast<int>(24 * scale);
    const int cols = 8;
    // const int rows = std::min(8, (ctx.window_height - static_cast<int>(140 * scale)) / cell_size);
    const int margin = padding;
    const int panel_w = cols * cell_size + static_cast<int>(30 * scale);
    // const int panel_h = rows * cell_size + static_cast<int>(60 * scale);

    int const left_x = margin;
    int const panel_y = padding + font_title + padding;
    int const right_x = ctx.window_width - panel_w - margin;

    int const grid_start_x_player = left_x + static_cast<int>(15 * scale);
    int const grid_start_y = panel_y + static_cast<int>(35 * scale);
    int const grid_start_x_npc = right_x + static_cast<int>(15 * scale);

    const int mx = ctx.pick_x;
    const int my = ctx.pick_y;

    auto get_slot = [&](int start_x, int start_y) -> int {
        if (mx < start_x || my < start_y) return -1;
        int col = (mx - start_x) / cell_size;
        int row = (my - start_y) / cell_size;
        if (col >= cols || col < 0) return -1;
        // Limit rows check if needed, but Inventory is linear
        return row * cols + col;
    };

    // Check Player Grid (Selling)
    int player_slot = get_slot(grid_start_x_player, grid_start_y);
    if (player_slot >= 0 && player_slot < 256) {
        if (player_slot == static_cast<int>(Inventory::COINS_SLOT)) return; // Can't sell gold itself

        const std::uint16_t amount = p.inventory.get_at(player_slot);
        if (amount > 0) {
            ItemType type = p.inventory.get_item_type_at(player_slot);
            int price = static_cast<int>(ITEM_DATABASE[static_cast<size_t>(type)].base_price * 0.5); // Sell at 50%
            if (price < 1) price = 1;

            if (npc_inv->get_capital() >= price) {
                if (npc_inv->add(type, 1)) {
                    p.inventory.remove_capital(0); // Dummy update? No, just remove item
                    // Using set_at to decrease count or clear if 0? 
                    // Inventory doesn't have remove_at(index), only remove(type).
                    // But we know the slot. 
                    // Let's manually decrement for safety or use helper if available.
                    // Inventory::remove uses type search. Here we want specific slot.
                    // Manual decrement:
                    p.inventory.set_at(player_slot, amount - 1, type);
                    
                    p.inventory.add_capital(price);
                    npc_inv->remove_capital(price);
                    // Sound?
                }
            }
        }
        return;
    }

    // Check NPC Grid (Buying)
    int npc_slot = get_slot(grid_start_x_npc, grid_start_y);
    if (npc_slot >= 0 && npc_slot < 256) {
        if (npc_slot == static_cast<int>(Inventory::COINS_SLOT)) return;

        const std::uint16_t amount = npc_inv->get_at(npc_slot);
        if (amount > 0) {
            ItemType type = npc_inv->get_item_type_at(npc_slot);
            int price = ITEM_DATABASE[static_cast<size_t>(type)].base_price; // Buy at 100%

            if (p.inventory.get_capital() >= price) {
                if (p.inventory.add(type, 1)) {
                    npc_inv->set_at(npc_slot, amount - 1, type);
                    
                    npc_inv->add_capital(price);
                    p.inventory.remove_capital(price);
                }
            }
        }
        return;
    }
}


void InteractionState::render(GameContext& ctx, TextureManager& textures) {
    Rect const overlay = {0, 0, ctx.window_width, ctx.window_height};
    render_fill_rect(overlay, ui_color("#050510FF"));

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

    // Scale factor based on window size (baseline: 720p height)
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    
    // Layout: divide screen into zones to prevent overlap
    // Zone 1 (top): NPC name - fixed height
    // Zone 2 (upper): NPC sprite - takes remaining upper space  
    // Zone 3 (middle): Dialogue message - only if present
    // Zone 4 (bottom): Buttons - fixed from bottom
    
    const int padding = static_cast<int>(20 * scale);
    const int title_font = static_cast<int>(28 * scale);
    const int msg_font = static_cast<int>(22 * scale);
    
    // Button zone (bottom) - calculate first to know where sprite zone ends
    const int btn_width = std::min(ctx.window_width / 3, static_cast<int>(300 * scale));
    const int btn_height = std::max(ctx.window_height / 14, static_cast<int>(40 * scale));
    const int btn_spacing = static_cast<int>(10 * scale);
    const int num_buttons = 5;
    const int total_menu_height = num_buttons * btn_height + (num_buttons - 1) * btn_spacing;
    const int menu_y = ctx.window_height - padding - total_menu_height;
    
    // Title zone (top)
    const int title_y = padding;
    const int title_height = title_font + padding;
    
    // Calculate available space for sprite and dialogue
    const int content_top = title_y + title_height + padding;
    const int content_bottom = menu_y - padding;
    const int available_height = content_bottom - content_top;
    
    // Dialogue panel height (if message present)
    const int dialogue_height = (!dialogue_message_.empty() || showing_quest_msg_) 
                                ? static_cast<int>(70 * scale) : 0;
    
    // Sprite gets remaining space (capped)
    int const sprite_available = available_height - dialogue_height - (dialogue_height > 0 ? padding : 0);
    int sprite_size = std::min({
        sprite_available,
        ctx.window_width - padding * 2,
        static_cast<int>(200 * scale)  // Max sprite size
    });
    sprite_size = std::max(sprite_size, static_cast<int>(80 * scale));  // Min sprite size
    
    // Position sprite centered horizontally, in upper content area
    const int sprite_x = (ctx.window_width - sprite_size) / 2;
    const int sprite_y = content_top;
    
    // Get sprite index based on NPC type
    NPCType const etype = npc_type_;
    size_t s_idx = static_cast<size_t>(ObjectType::Peasant);
    switch (etype) {
        case NPCType::Bandit: s_idx = static_cast<size_t>(ObjectType::Bandit); break;
        case NPCType::Woodcutter: s_idx = static_cast<size_t>(ObjectType::Woodcutter); break;
        case NPCType::Guard: s_idx = static_cast<size_t>(ObjectType::Guard); break;
        case NPCType::Merchant: s_idx = static_cast<size_t>(ObjectType::Merchant); break;
        case NPCType::Witch: s_idx = static_cast<size_t>(ObjectType::Witch); break;
        case NPCType::Caravan: s_idx = static_cast<size_t>(ObjectType::Caravan); break;
        default: break;
    }

    // Render NPC sprite
    Rect const npc_rect = {sprite_x, sprite_y, sprite_size, sprite_size};
    render_texture(textures.sprite(s_idx), npc_rect);

    // Render NPC name at top center
    const int title_width = static_cast<int>(300 * scale);
    const int title_x = (ctx.window_width - title_width) / 2;
    const int title_text_height = title_font + static_cast<int>(10 * scale);
    render_text(ctx, npc_name_, title_x, title_y, title_width, title_text_height,
                {255, 255, 255, 255}, title_font);

    // Dialogue message - positioned below sprite
    const int dialogue_y = sprite_y + sprite_size + padding;
    if (!dialogue_message_.empty()) {
        int const panel_w = std::min(static_cast<int>(500 * scale), ctx.window_width - padding * 2);
        int const panel_h = static_cast<int>(60 * scale);
        Rect const msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#16C79A"));
        const int msg_height = msg_font + static_cast<int>(10 * scale);
        render_text(ctx, dialogue_message_,
                    msg_panel.x + padding, msg_panel.y + padding,
                    panel_w - padding * 2, msg_height,
                    {255, 255, 255, 255}, msg_font);
    }

    if (showing_quest_msg_) {
        int const panel_w = std::min(static_cast<int>(400 * scale), ctx.window_width - padding * 2);
        int const panel_h = static_cast<int>(60 * scale);
        Rect const msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#FF6B6B"));
        const int msg_height = msg_font + static_cast<int>(10 * scale);
        render_text(ctx, "Sorry, quests are not implemented!",
                    msg_panel.x + padding, msg_panel.y + padding,
                    panel_w - padding * 2, msg_height,
                    {255, 200, 200, 255}, msg_font);
    }

    // Render menu buttons
    bool menu_picked = ctx.picked;
    interaction_menu_.render_and_handle(ctx,
                                        ctx.window_width / 2,
                                        menu_y,
                                        btn_width,
                                        btn_height,
                                        btn_spacing,
                                        ctx.curs_x,
                                        ctx.curs_y,
                                        ctx.pick_x,
                                        ctx.pick_y,
                                        menu_picked);

    // Handle pause button clicks (bottom-left Leave button)
    if (pause_buttons_initialized_ && ctx.picked) {
        if (pause_buttons_.handle_press(ctx.pick_x, ctx.pick_y)) {
            ctx.picked = false;
        }
    }

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

    // DPI-aware scaling (baseline: 720p height)
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    const int padding = static_cast<int>(15 * scale);
    const int font_title = static_cast<int>(28 * scale);
    const int font_label = static_cast<int>(18 * scale);
    const int font_small = static_cast<int>(14 * scale);

    const int title_height = font_title + static_cast<int>(10 * scale);
    render_text(ctx, "TRADE", ctx.window_width / 2 - static_cast<int>(40 * scale), padding, static_cast<int>(80 * scale), title_height, {255, 255, 255, 255}, font_title);

    const Inventory* npc_inv = nullptr;
    if (auto* inv_comp = npc_ref_.try_get<ecs::InventoryComponent>()) {
        npc_inv = &inv_comp->data;
    }

    const int cell_size = static_cast<int>(24 * scale);
    const int cols = 8;  // Fewer columns to fit better
    const int rows = std::min(8, (ctx.window_height - static_cast<int>(140 * scale)) / cell_size);
    const int margin = padding;
    const int panel_w = cols * cell_size + static_cast<int>(30 * scale);
    const int panel_h = rows * cell_size + static_cast<int>(60 * scale);

    int const left_x = margin;
    int const panel_y = padding + font_title + padding;

    Rect const player_panel = {left_x, panel_y, panel_w, panel_h};
    render_draw_panel( player_panel, ui_color("#1A2A3A"), ui_color("#4A9EFF"));
    const int label_height = font_label + static_cast<int>(5 * scale);
    render_text(ctx, "Your Inventory", left_x + static_cast<int>(8 * scale), panel_y + static_cast<int>(8 * scale), static_cast<int>(150 * scale), label_height, {200, 220, 255, 255}, font_label);

    int player_hover = render_inventory_grid(ctx,
                          textures,
                          p.inventory,
                          left_x + static_cast<int>(15 * scale),
                          panel_y + static_cast<int>(35 * scale),
                          cell_size,
                          cols,
                          rows);

    int const right_x = ctx.window_width - panel_w - margin;

    Rect const npc_panel = {right_x, panel_y, panel_w, panel_h};
    render_draw_panel( npc_panel, ui_color("#2A1A1A"), ui_color("#FF9E4A"));
    std::string const npc_label = npc_name_ + "'s Inventory";
    render_text(ctx, npc_label, right_x + static_cast<int>(8 * scale), panel_y + static_cast<int>(8 * scale), static_cast<int>(150 * scale), label_height, {255, 220, 200, 255}, font_label);

    // NPC Grid
    int npc_hover = -1;
    if (npc_inv) {
        npc_hover = render_inventory_grid(ctx,
                              textures,
                              *npc_inv,
                              right_x + static_cast<int>(15 * scale),
                              panel_y + static_cast<int>(35 * scale),
                              cell_size,
                              cols,
                              rows);
    } else {
        const int no_inv_height = font_label + static_cast<int>(5 * scale);
        render_text(ctx,
                    "No inventory",
                    right_x + panel_w / 2 - static_cast<int>(50 * scale),
                    panel_y + panel_h / 2,
                    static_cast<int>(100 * scale),
                    no_inv_height,
                    {150, 150, 150, 255},
                    font_label);
    }

    const int trading_msg_height = font_small + static_cast<int>(5 * scale);
    render_text(ctx,
                "[ Trading not yet functional - Press ESC to close ]",
                ctx.window_width / 2 - static_cast<int>(200 * scale),
                ctx.window_height - static_cast<int>(30 * scale),
                static_cast<int>(400 * scale),
                trading_msg_height,
                {150, 150, 150, 255},
                font_small);
}
std::string tt_text;
    
    if (player_hover >= 0 && player_hover < 256) {
        const std::uint16_t amt = p.inventory.get_at(player_hover);
        if (amt > 0) {
            ItemType type = p.inventory.get_item_type_at(player_hover);
            int price = static_cast<int>(ITEM_DATABASE[static_cast<size_t>(type)].base_price * 0.5); 
            if (price < 1) price = 1;
            tt_text = "Sell: " + std::string(ITEM_DATABASE[static_cast<size_t>(type)].name) + "\nPrice: " + std::to_string(price);
        }
    } else if (npc_hover >= 0 && npc_hover < 256 && npc_inv) {
        const std::uint16_t amt = npc_inv->get_at(npc_hover);
        if (amt > 0) {
            ItemType type = npc_inv->get_item_type_at(npc_hover);
            int price = ITEM_DATABASE[static_cast<size_t>(type)].base_price;
            tt_text = "Buy: " + std::string(ITEM_DATABASE[static_cast<size_t>(type)].name) + "\nPrice: " + std::to_string(price);
        }
    }

    if (!tt_text.empty() && get_text_renderer()) {
        const int tt_font_size = static_cast<int>(16 * scale);
        Point const size = get_text_renderer()->measure(tt_text, tt_font_size);
        const int tt_padding = 10;
        const int tt_w = size.x + tt_padding * 2;
        const int tt_h = size.y + tt_padding * 2;
        
        int tt_x = ctx.curs_x + 15;
        int tt_y = ctx.curs_y + 15;
        if (tt_x + tt_w > ctx.window_width) tt_x -= (tt_w + 20);
        if (tt_y + tt_h > ctx.window_height) tt_y -= (tt_h + 20);

        render_fill_rect(static_cast<float>(tt_x), static_cast<float>(tt_y), 
                         static_cast<float>(tt_w), static_cast<float>(tt_h), 
                         ui_color("#101010FA"));
        render_draw_rect(static_cast<float>(tt_x), static_cast<float>(tt_y), 
                         static_cast<float>(tt_w), static_cast<float>(tt_h), 
                         ui_color("#FFD70080"));
        
        render_text(ctx, tt_text, tt_x + tt_padding, tt_y + tt_padding, 
                    tt_w, tt_h, {255, 255, 255, 255}, tt_font_size);
    }
}
int InteractionState::render_inventory_grid(GameContext& ctx,
                                             TextureManager& textures,
                                             const Inventory& inv,
                                             int start_x,
                                             int start_y,
                                             int cell_size,
                                             int cols,
                                             int rows) {
    int hovered_idx = -1;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int slot_idx = row * cols + col;
            if (slot_idx >= 256)
                break;

            const std::uint16_t amount = inv.get_at(slot_idx);
            const int cell_x = start_x + col * cell_size;
            const int cell_y = start_y + row * cell_size;

            Rect const cell_rect = {cell_x, cell_y, cell_size, cell_size};
            
            // Check hover
            bool is_hovered = ui_point_in_rect(ctx.curs_x, ctx.curs_y, cell_rect);
            if (is_hovered) {
                hovered_idx = slot_idx;
                render_fill_rect(cell_rect, {60, 70, 90, 200}); // Lighter bg
                render_draw_rect(cell_rect, {200, 200, 100, 255}); // Highlight border
            } else {
                render_fill_rect(cell_rect, {30, 40, 55, 200});
                render_draw_rect(cell_rect, {70, 90, 120, 255});
            }

            if (amount > 0) {
                ItemType const item_type = inv.get_item_type_at(slot_idx);
                const Texture& item_texture = textures.item(item_type);
                if (item_texture.valid()) {
                    Rect const dst_rect = {cell_x + 2, cell_y + 2, cell_size - 4, cell_size - 4};
                    render_texture(item_texture, dst_rect);
                }

                if (cell_size >= 20) {
                    std::string const count_str = std::to_string(amount);
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
    return hovered_idx;
}

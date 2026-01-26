#include "states/interaction_state.h"
#include "sokol_time.h"
#include "ecs/components/npc.h"
#include "ecs/world.h"
#include "rendering/ra_icon.h"
#include "rendering/renderer.h"
#include "rendering/texture_manager.h"
#include "systems/economy.h"
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

void InteractionState::handle_event(GameContext& ctx, TextureManager& /*textures*/) {
    // Event handling now done via Sokol callbacks
    (void)ctx;
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
    Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
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
    int sprite_available = available_height - dialogue_height - (dialogue_height > 0 ? padding : 0);
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
    NPCType etype = npc_type_;
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
    Rect npc_rect = {sprite_x, sprite_y, sprite_size, sprite_size};
    render_texture(textures.sprite(s_idx), npc_rect);

    // Render NPC name at top center
    const int title_width = static_cast<int>(300 * scale);
    const int title_x = (ctx.window_width - title_width) / 2;
    render_text(ctx, npc_name_, title_x, title_y, title_width, title_font, 
                {255, 255, 255, 255}, title_font);

    // Dialogue message - positioned below sprite
    const int dialogue_y = sprite_y + sprite_size + padding;
    if (!dialogue_message_.empty()) {
        int panel_w = std::min(static_cast<int>(500 * scale), ctx.window_width - padding * 2);
        int panel_h = static_cast<int>(60 * scale);
        Rect msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#16C79A"));
        render_text(ctx, dialogue_message_,
                    msg_panel.x + padding, msg_panel.y + padding,
                    panel_w - padding * 2, msg_font,
                    {255, 255, 255, 255}, msg_font);
    }

    if (showing_quest_msg_) {
        int panel_w = std::min(static_cast<int>(400 * scale), ctx.window_width - padding * 2);
        int panel_h = static_cast<int>(60 * scale);
        Rect msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#FF6B6B"));
        render_text(ctx, "Sorry, quests are not implemented!",
                    msg_panel.x + padding, msg_panel.y + padding,
                    panel_w - padding * 2, msg_font,
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

    render_text(ctx, "TRADE", ctx.window_width / 2 - static_cast<int>(40 * scale), padding, static_cast<int>(80 * scale), font_title, {255, 255, 255, 255});

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

    int left_x = margin;
    int panel_y = padding + font_title + padding;

    Rect player_panel = {left_x, panel_y, panel_w, panel_h};
    render_draw_panel( player_panel, ui_color("#1A2A3A"), ui_color("#4A9EFF"));
    render_text(ctx, "Your Inventory", left_x + static_cast<int>(8 * scale), panel_y + static_cast<int>(8 * scale), static_cast<int>(150 * scale), font_label, {200, 220, 255, 255});

    render_inventory_grid(ctx,
                          textures,
                          p.inventory,
                          left_x + static_cast<int>(15 * scale),
                          panel_y + static_cast<int>(35 * scale),
                          cell_size,
                          cols,
                          rows);

    int right_x = ctx.window_width - panel_w - margin;

    Rect npc_panel = {right_x, panel_y, panel_w, panel_h};
    render_draw_panel( npc_panel, ui_color("#2A1A1A"), ui_color("#FF9E4A"));
    std::string npc_label = npc_name_ + "'s Inventory";
    render_text(ctx, npc_label, right_x + static_cast<int>(8 * scale), panel_y + static_cast<int>(8 * scale), static_cast<int>(150 * scale), font_label, {255, 220, 200, 255});

    if (npc_inv) {
        render_inventory_grid(ctx,
                              textures,
                              *npc_inv,
                              right_x + static_cast<int>(15 * scale),
                              panel_y + static_cast<int>(35 * scale),
                              cell_size,
                              cols,
                              rows);
    } else {
        render_text(ctx,
                    "No inventory",
                    right_x + panel_w / 2 - static_cast<int>(50 * scale),
                    panel_y + panel_h / 2,
                    static_cast<int>(100 * scale),
                    font_label,
                    {150, 150, 150, 255});
    }

    render_text(ctx,
                "[ Trading not yet functional - Press ESC to close ]",
                ctx.window_width / 2 - static_cast<int>(200 * scale),
                ctx.window_height - static_cast<int>(30 * scale),
                static_cast<int>(400 * scale),
                font_small,
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

            Rect cell_rect = {cell_x, cell_y, cell_size, cell_size};
            render_fill_rect( cell_rect, {30, 40, 55, 200});
            render_draw_rect( cell_rect, {70, 90, 120, 255});

            if (amount > 0) {
                ItemType item_type = inv.get_item_type_at(slot_idx);
                const Texture& item_texture = textures.item(item_type);
                if (item_texture.valid()) {
                    Rect dst_rect = {cell_x + 2, cell_y + 2, cell_size - 4, cell_size - 4};
                    render_texture(item_texture, dst_rect);
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

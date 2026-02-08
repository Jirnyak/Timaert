#include "states/landmark_state.h"

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

void LandmarkState::init_pause_buttons(GameContext& ctx) {
    const UiButtonLayout layout = ui_default_button_layout(ctx);
    const int btn_size = layout.btn_size;
    const int margin = layout.margin;
    pause_buttons_.clear();
    pause_buttons_.add(UIButton{{margin, layout.speed_y, btn_size, btn_size},
                                "",
                                [this]() { pending_action_ = LandmarkAction::Leave; },
                                nullptr,
                                RaIcon::Reverse});
    pause_buttons_initialized_ = true;
    last_buttons_width_ = ctx.window_width;
    last_buttons_height_ = ctx.window_height;
}

void LandmarkState::init_ui(GameContext& ctx) {
    landmark_menu_.clear();

    if (!ctx.world_manager)
        return;

    landmark_menu_.add(MenuItem{"Enter",
                                   [this]() { pending_action_ = LandmarkAction::Enter; },
                                   RaIcon::Tower});

    landmark_menu_.add(MenuItem{"Trade",
                                   [this]() { pending_action_ = LandmarkAction::Trade; },
                                   RaIcon::GoldBar});

    landmark_menu_.add(MenuItem{"Tavern",
                                   [this]() { pending_action_ = LandmarkAction::Tavern; }});

    landmark_menu_.add(MenuItem{"Leave",
                                   [this]() { pending_action_ = LandmarkAction::Leave; },
                                   RaIcon::Reverse});

    ui_initialized_ = true;
}

void LandmarkState::process_pending_action(GameContext& ctx) {
    const LandmarkAction action = pending_action_;
    pending_action_ = LandmarkAction::None;
    
    dialogue_message_.clear();
    showing_enter_msg_ = false;
    showing_tavern_msg_ = false;
    
    switch (action) {
        case LandmarkAction::Enter:
            handle_enter(ctx);
            break;
        case LandmarkAction::Trade:
            handle_trade(ctx);
            break;
        case LandmarkAction::Tavern:
            handle_tavern(ctx);
            break;
        case LandmarkAction::Leave:
            pop_state(ctx, false);
            break;
        default:
            break;
    }
}

void LandmarkState::handle_enter(GameContext& ctx) {
    if (!has_settlement())
        return;

    showing_enter_msg_ = true;
    dialogue_message_.clear();
    showing_tavern_msg_ = false;
}

void LandmarkState::handle_trade(GameContext& ctx) {
    if (!has_settlement())
        return;

    dialogue_message_ = "Trade interface coming soon!";
    showing_enter_msg_ = false;
    showing_tavern_msg_ = false;
}

void LandmarkState::handle_tavern(GameContext& ctx) {
    if (!has_settlement())
        return;

    showing_tavern_msg_ = true;
    dialogue_message_.clear();
    showing_enter_msg_ = false;
}

void LandmarkState::start_interaction_with_settlement(std::int32_t settlement_id, GameContext& ctx) {
    if (!ctx.world_manager)
        return;

    // Get settlement from LandmarkSystem FIRST
    auto* settlement = ctx.world_manager->landmarks.find_settlement_by_id(settlement_id);
    if (!settlement) {
        // Settlement not found - don't start interaction
        return;
    }

    // Only set state after validation
    settlement_id_ = settlement_id;
    interaction_started_ = true;
    settlement_name_ = settlement->name;
    
    // Map SettlementType to LandmarkType
    switch (settlement->type) {
        case SettlementType::Village: landmark_type_ = LandmarkType::Village; break;
        case SettlementType::Town: landmark_type_ = LandmarkType::Town; break;
        case SettlementType::City: landmark_type_ = LandmarkType::City; break;
        default: landmark_type_ = LandmarkType::None; break;
    }

    ctx.picked = false;
    init_ui(ctx);
    init_pause_buttons(ctx);
}


void LandmarkState::update(GameContext& ctx, TextureManager& /*textures*/) {
    if (pending_action_ != LandmarkAction::None) {
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
               && ctx.landmark_target_id >= 0 && ctx.world_manager) {
        // Validate settlement exists before starting interaction
        if (ctx.world_manager->landmarks.find_settlement_by_id(ctx.landmark_target_id) != nullptr) {
            start_interaction_with_settlement(ctx.landmark_target_id, ctx);
        } else {
            pop_state(ctx, false);
        }
        ctx.landmark_target_id = -1;
    }

    ctx.redraw_requested = true;
}

void LandmarkState::render(GameContext& ctx, TextureManager& textures) {
    Rect const overlay = {0, 0, ctx.window_width, ctx.window_height};
    render_fill_rect(overlay, ui_color("#050510FF"));

    if (!has_settlement())
        return;
    if (!ctx.world_manager)
        return;

    // Scale factor based on window size (baseline: 720p height)
    const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
    
    const int padding = static_cast<int>(20 * scale);
    const int title_font = static_cast<int>(28 * scale);
    const int msg_font = static_cast<int>(22 * scale);
    
    // Button zone (bottom)
    const int btn_width = std::min(ctx.window_width / 3, static_cast<int>(300 * scale));
    const int btn_height = static_cast<int>(50 * scale);  // Fixed height, not dependent on window height
    const int btn_spacing = static_cast<int>(10 * scale);
    const int num_buttons = 4;
    const int total_menu_height = num_buttons * btn_height + (num_buttons - 1) * btn_spacing;
    int menu_y = ctx.window_height - padding - total_menu_height;
    
    // Guard against menu going off-screen (clamp to valid range)
    menu_y = std::max(padding + title_font + padding * 2, std::min(menu_y, ctx.window_height - total_menu_height - padding));
    
    // Title zone (top)
    const int title_y = padding;
    const int title_height = title_font + padding;
    
    // Calculate available space for sprite and dialogue
    const int content_top = title_y + title_height + padding;
    const int content_bottom = menu_y - padding;
    const int available_height = content_bottom - content_top;
    
    // Dialogue panel height (if message present)
    const int dialogue_height = (!dialogue_message_.empty() || showing_enter_msg_ || showing_tavern_msg_) 
                                ? static_cast<int>(70 * scale) : 0;
    
    // Sprite gets remaining space (capped)
    int const sprite_available = available_height - dialogue_height - (dialogue_height > 0 ? padding : 0);
    int sprite_size = std::min({
        sprite_available,
        ctx.window_width - padding * 2,
        static_cast<int>(200 * scale)
    });
    sprite_size = std::max(sprite_size, static_cast<int>(80 * scale));
    
    // Position sprite centered horizontally
    const int sprite_x = (ctx.window_width - sprite_size) / 2;
    const int sprite_y = content_top;
    
    // Get sprite based on landmark type
    size_t s_idx = static_cast<size_t>(ObjectType::Village);
    switch (landmark_type_) {
        case LandmarkType::Village: s_idx = static_cast<size_t>(ObjectType::Village); break;
        case LandmarkType::Town: s_idx = static_cast<size_t>(ObjectType::Town); break;
        case LandmarkType::City: s_idx = static_cast<size_t>(ObjectType::City); break;
        default: s_idx = static_cast<size_t>(ObjectType::Village); break;
    }

    // Render landmark sprite
    Rect const landmark_rect = {sprite_x, sprite_y, sprite_size, sprite_size};
    render_texture(textures.sprite(s_idx), landmark_rect);

    // Render settlement name at top center
    const int title_width = static_cast<int>(300 * scale);
    const int title_x = (ctx.window_width - title_width) / 2;
    const int title_text_height = title_font + static_cast<int>(10 * scale);
    
    // Guard against negative coordinates
    if (title_y >= 0 && title_x >= 0) {
        render_text(ctx, settlement_name_, title_x, title_y, title_width, title_text_height,
                    {255, 255, 255, 255}, title_font);
    }

    // Dialogue message - positioned below sprite
    const int dialogue_y = std::max(0, sprite_y + sprite_size + padding);
    if (!dialogue_message_.empty() && dialogue_y >= 0) {
        int const panel_w = std::min(static_cast<int>(500 * scale), ctx.window_width - padding * 2);
        int const panel_h = static_cast<int>(60 * scale);
        Rect const msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#16C79A"));
        const int msg_height = msg_font + static_cast<int>(10 * scale);
        const int text_y = std::max(0, msg_panel.y + padding);
        render_text(ctx, dialogue_message_,
                    msg_panel.x + padding, text_y,
                    panel_w - padding * 2, msg_height,
                    {255, 255, 255, 255}, msg_font);
    }

    if (showing_enter_msg_ && dialogue_y >= 0) {
        int const panel_w = std::min(static_cast<int>(400 * scale), ctx.window_width - padding * 2);
        int const panel_h = static_cast<int>(60 * scale);
        Rect const msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#FF6B6B"));
        const int msg_height = msg_font + static_cast<int>(10 * scale);
        const int text_y = std::max(0, msg_panel.y + padding);
        render_text(ctx, "Sorry, not implemented yet!",
                    msg_panel.x + padding, text_y,
                    panel_w - padding * 2, msg_height,
                    {255, 200, 200, 255}, msg_font);
    }

    if (showing_tavern_msg_ && dialogue_y >= 0) {
        int const panel_w = std::min(static_cast<int>(400 * scale), ctx.window_width - padding * 2);
        int const panel_h = static_cast<int>(60 * scale);
        Rect const msg_panel = {(ctx.window_width - panel_w) / 2, dialogue_y, panel_w, panel_h};
        render_draw_panel(msg_panel, ui_color("#1A1A2E"), ui_color("#FF6B6B"));
        const int msg_height = msg_font + static_cast<int>(10 * scale);
        const int text_y = std::max(0, msg_panel.y + padding);
        render_text(ctx, "Sorry, tavern not implemented!",
                    msg_panel.x + padding, text_y,
                    panel_w - padding * 2, msg_height,
                    {255, 200, 200, 255}, msg_font);
    }

    // Render menu buttons
    bool menu_picked = ctx.picked;
    
    // Safety check: ensure menu + all buttons fit within window bounds
    const int menu_bottom = menu_y + total_menu_height;
    if (menu_y >= 0 && menu_bottom <= ctx.window_height) {
        landmark_menu_.render_and_handle(ctx,
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
    }
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

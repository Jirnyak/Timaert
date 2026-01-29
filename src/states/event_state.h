#pragma once

#include "core/game_state.h"
#include "core/binary_io.h"
#include "rendering/renderer.h"
#include "ui/ui.h"
#include "systems/random_events.h"
#include "ui/ui_events.h"

class EventState : public GameState {
public:
    static constexpr std::int32_t kRandomEvent = -2;

    explicit EventState(std::int32_t event_id = kRandomEvent) : event_id_(event_id) {}

    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Event;
    }
    [[nodiscard]] bool is_overlay() const noexcept override {
        return true;
    }
    [[nodiscard]] std::int32_t event_id() const noexcept {
        return event_id_;
    }

    // Serialization - save/load event_id
    void save_state(BinaryWriter& writer) const override {
        writer.write(event_id_);
    }

    void load_state(BinaryReader& reader) override {
        event_id_ = reader.read<std::int32_t>();
    }

private:
    std::int32_t event_id_ = -1;
    MenuButtonList choice_buttons_;
    bool ui_initialized_ = false;
    InputManager input_manager_;

    void init_ui(GameContext& /*ctx*/) {
        choice_buttons_.clear();
        if (event_id_ == -1)
            return;

        const auto& event_data = get_random_event_data(event_id_);

        for (const auto& choice : event_data.choices) {
            choice_buttons_.add(
                MenuItem{choice.text, [choice, this]() { pending_choice_ = &choice; }});
        }

        ui_initialized_ = true;
    }

    const EventChoice* pending_choice_ = nullptr;

public:
    void update(GameContext& ctx, TextureManager& /*textures*/) override {
        if (pending_choice_) {
            pending_choice_->action(ctx);
            pending_choice_ = nullptr;
            pop_state(ctx, false);
            return;
        }

        if (event_id_ == -1) {
            pop_state(ctx, false);
            return;
        }

        if (event_id_ == kRandomEvent) {
            const int count = get_random_event_count();
            if (count > 0) {
                event_id_ = static_cast<std::int32_t>(
                    random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(count - 1)));
            } else {
                pop_state(ctx, false);
                return;
            }
        }

        if (!ui_initialized_ && event_id_ != -1) {
            init_ui(ctx);
        }
    }

    void render(GameContext& ctx, TextureManager& /*textures*/) override {
        const Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        render_fill_rect(overlay, {0, 0, 0, 180});

        if (event_id_ == -1)
            return;
        const auto& event_data = get_random_event_data(event_id_);

        // Scale factor based on window size (baseline: 720p height)
        const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
        const int margin = static_cast<int>(30 * scale);
        const int title_font_size = static_cast<int>(32 * scale);
        const int desc_font_size = static_cast<int>(22 * scale);
        // Use window-relative button sizing - same as menu_state
        const int btn_width = ctx.window_width / 3;
        const int btn_height = ctx.window_height / 12;
        const int btn_spacing = static_cast<int>(20 * scale);

        // Calculate panel size based on content
        const int num_buttons = static_cast<int>(choice_buttons_.size());
        const int buttons_total_height = num_buttons * btn_height + (num_buttons - 1) * btn_spacing;
        const int min_panel_h = static_cast<int>(200 * scale) + buttons_total_height + margin * 2;
        
        const int panel_w = std::min(static_cast<int>(700 * scale), ctx.window_width - margin * 2);
        const int panel_h = std::min(std::max(static_cast<int>(450 * scale), min_panel_h), ctx.window_height - margin * 2);
        const Rect panel = ui_centered_rect(ctx.window_width, ctx.window_height, panel_w, panel_h);

        render_draw_panel(panel, ui_color("#1A1A2E"), ui_color("#16C79A"));

        // Заголовок
        const int title_height = title_font_size + static_cast<int>(10 * scale);
        render_text(ctx,
                    event_data.title,
                    panel.x + margin,
                    panel.y + margin,
                    panel_w - margin * 2,
                    title_height,
                    {255, 255, 255, 255},
                    title_font_size);

        // Описание (упрощенный вывод текста без переноса строк пока)
        const int desc_height = desc_font_size + static_cast<int>(10 * scale);
        render_text(ctx,
                    event_data.description,
                    panel.x + margin,
                    panel.y + static_cast<int>(80 * scale),
                    panel_w - margin * 2,
                    desc_height,
                    {200, 200, 200, 255},
                    desc_font_size);

        // Кнопки выбора - position from bottom of panel
        const int buttons_y = panel.y + panel_h - buttons_total_height - margin;
        bool picked = ctx.picked;
        choice_buttons_.render_and_handle(ctx,
                                          ctx.window_width / 2,
                                          buttons_y,
                                          btn_width,
                                          btn_height,
                                          btn_spacing,
                                          ctx.curs_x,
                                          ctx.curs_y,
                                          ctx.pick_x,
                                          ctx.pick_y,
                                          picked);
        if (picked != ctx.picked) {
            ctx.picked = false;  // Button consumed the click
        }
    }
};

inline StateRegistrar<EventState> register_event_state_{GameMode::Event};

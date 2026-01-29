#pragma once

#include "ui/ui.h"
#include "systems/world_manager.h"
#include "ecs/world.h"
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

class HudState {
private:
    int hud_gold_value_ = std::numeric_limits<int>::min();
    int hud_life_value_ = std::numeric_limits<int>::min();
    int hud_max_life_value_ = std::numeric_limits<int>::min();
    int hud_items_value_ = std::numeric_limits<int>::min();
    int hud_settlement_count_ = std::numeric_limits<int>::min();
    int hud_npc_count_ = std::numeric_limits<int>::min();
    TilePosition hud_aim_pos_ = INVALID_POS;
    bool hud_has_aim_ = false;
    std::string hud_settlement_name_;
    std::string hud_gold_text_;
    std::string hud_hp_text_;
    std::string hud_items_text_;
    std::string hud_at_text_;
    std::string hud_aim_text_;
    std::string hud_settlement_count_text_;
    std::string hud_npc_count_text_;
    std::string hud_hover_npc_text_;

public:
    void set_hover_npc_text(const std::string& text) {
        hud_hover_npc_text_ = text;
    }

    void render(GameContext& ctx, WorldManager* world_manager) {
        if (!world_manager)
            return;

        const Player& p = world_manager->player_ctrl.player();
        if (p.active) {
            const int gold_value = static_cast<int>(p.inventory.get_capital());
            if (gold_value != hud_gold_value_) {
                hud_gold_value_ = gold_value;
                hud_gold_text_ = "Gold: " + std::to_string(hud_gold_value_);
            }

            if (p.combat_stats.current_hp != hud_life_value_
                || p.combat_stats.max_hp != hud_max_life_value_) {
                hud_life_value_ = p.combat_stats.current_hp;
                hud_max_life_value_ = p.combat_stats.max_hp;
                hud_hp_text_ = "HP: " + std::to_string(hud_life_value_) + "/"
                               + std::to_string(hud_max_life_value_);
            }

            const int items_value = p.inventory.total_items();
            if (items_value != hud_items_value_) {
                hud_items_value_ = items_value;
                hud_items_text_ = "Items: " + std::to_string(hud_items_value_);
            }

            const Settlement* at_settlement = world_manager->get_settlement_at(p.pos);
            const std::string settlement_name = at_settlement ? at_settlement->name : std::string{};
            if (settlement_name != hud_settlement_name_) {
                hud_settlement_name_ = settlement_name;
                if (at_settlement) {
                    hud_at_text_ = "At: " + hud_settlement_name_;
                } else {
                    hud_at_text_.clear();
                }
            }

            if (p.has_aim() != hud_has_aim_ || p.aim_pos != hud_aim_pos_) {
                hud_has_aim_ = p.has_aim();
                hud_aim_pos_ = p.aim_pos;
                if (hud_has_aim_) {
                    hud_aim_text_ = "Moving to: (" + std::to_string(hud_aim_pos_.x) + ","
                                    + std::to_string(hud_aim_pos_.y) + ")";
                } else {
                    hud_aim_text_.clear();
                }
            }
        }

        const int settlement_count = static_cast<int>(world_manager->landmarks.settlement_count());
        if (settlement_count != hud_settlement_count_) {
            hud_settlement_count_ = settlement_count;
            hud_settlement_count_text_ = "Settlements: " + std::to_string(hud_settlement_count_);
        }

        // Count NPCs from ECS
        int npc_count = 0;
        if (ctx.ecs_world) {
            auto view = ctx.ecs_world->registry.view<ecs::NPCTag, ecs::Active>();
            npc_count = static_cast<int>(view.size_hint());
        }
        if (npc_count != hud_npc_count_) {
            hud_npc_count_ = npc_count;
            hud_npc_count_text_ = "NPCs: " + std::to_string(hud_npc_count_);
        }

        // Scale factor based on window size (baseline: 720p height)
        const float scale = std::max(1.0f, static_cast<float>(ctx.window_height) / 720.0f);
        
        struct HudItem {
            std::string text;
            Color color;
            int height;
        };
        auto text_width = [scale](const std::string& text) {
            return static_cast<int>(text.size() * 10 * scale);
        };

        // --- Расчет времени ---
        const std::uint64_t day_tick = ctx.ticks() % TICKS_PER_DAY;
        const int game_hour = static_cast<int>(day_tick / 1000);
        // ИСПРАВЛЕНИЕ: Явное создание std::string для корректной конкатенации
        const std::string time_str =
            std::string("Time: ") + (game_hour < 10 ? "0" : "") + std::to_string(game_hour) + ":00";
        // ----------------------

        // Scaled font sizes
        const int font_size_large = static_cast<int>(18 * scale);
        const int font_size_small = static_cast<int>(16 * scale);

        std::vector<HudItem> row_one;
        std::vector<HudItem> row_two;

        row_one.push_back({time_str, {200, 200, 255, 255}, font_size_large});  // Добавляем время первым пунктом
        row_one.push_back({hud_gold_text_, {255, 215, 0, 255}, font_size_large});
        row_one.push_back({hud_hp_text_, {255, 100, 100, 255}, font_size_large});
        row_one.push_back({hud_items_text_, {200, 200, 200, 255}, font_size_large});

        if (!hud_at_text_.empty()) {
            row_one.push_back({hud_at_text_, {100, 255, 100, 255}, font_size_large});
        }
        if (hud_has_aim_ && !hud_aim_text_.empty()) {
            row_two.push_back({hud_aim_text_, {150, 150, 255, 255}, font_size_small});
        }
        if (!hud_hover_npc_text_.empty()) {
            row_two.push_back({hud_hover_npc_text_, {200, 220, 255, 255}, font_size_small});
        }
        row_two.push_back({hud_settlement_count_text_, {180, 180, 180, 255}, font_size_small});
        row_two.push_back({hud_npc_count_text_, {180, 180, 180, 255}, font_size_small});

        const int padding = static_cast<int>(8 * scale);
        const int gap = static_cast<int>(8 * scale);
        const int row_gap = static_cast<int>(2 * scale);
        const int row_one_height = static_cast<int>(20 * scale);
        const int row_two_height = static_cast<int>(18 * scale);
        int row_one_width = 0;
        int row_two_width = 0;
        for (const auto& item : row_one) {
            if (!item.text.empty()) {
                row_one_width += text_width(item.text) + gap;
            }
        }
        for (const auto& item : row_two) {
            if (!item.text.empty()) {
                row_two_width += text_width(item.text) + gap;
            }
        }
        if (row_one_width > 0)
            row_one_width -= gap;
        if (row_two_width > 0)
            row_two_width -= gap;
        const int hud_width = std::max(row_one_width, row_two_width);
        const int hud_height = row_one_height + row_two_height + row_gap + padding * 2;
        const int hud_x = 8;
        const int hud_y = 6;

        const Rect hud_bg = {hud_x, hud_y, hud_width + padding * 2, hud_height};
        render_draw_panel(hud_bg, ui_color("#12100CBE"), ui_color("#504632DC"));

        int draw_x = hud_x + padding;
        int draw_y = hud_y + padding;
        for (const auto& item : row_one) {
            if (item.text.empty())
                continue;
            render_text(ctx,
                        item.text,
                        draw_x,
                        draw_y,
                        text_width(item.text),
                        item.height,
                        item.color);
            draw_x += text_width(item.text) + gap;
        }
        draw_x = hud_x + padding;
        draw_y += row_one_height + row_gap;
        for (const auto& item : row_two) {
            if (item.text.empty())
                continue;
            render_text(ctx,
                        item.text,
                        draw_x,
                        draw_y,
                        text_width(item.text),
                        item.height,
                        item.color);
            draw_x += text_width(item.text) + gap;
        }

        // --- Отрисовка Инвентаря (Список справа) ---
        // ИСПРАВЛЕНИЕ: Удалено повторное объявление 'const Player& p', так как она уже объявлена в
        // начале функции
        int inv_y = hud_y + hud_height + static_cast<int>(10 * scale);
        const int inv_x = hud_x;
        const int inv_item_height = static_cast<int>(20 * scale);
        const int inv_font_size = static_cast<int>(16 * scale);

        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i) {
            auto res = static_cast<ResourceType>(i);
            const int amount = p.inventory.get(res);
            if (amount > 0) {
                const std::string res_text =
                    std::string(resource_name(res)) + ": " + std::to_string(amount);

                // Рисуем полупрозрачную подложку для читаемости
                const int w = text_width(res_text);
                const Rect bg = {inv_x, inv_y, w + static_cast<int>(10 * scale), inv_item_height};
                render_fill_rect(bg, ui_color("#00000080"));

                render_text(ctx, res_text, inv_x + static_cast<int>(5 * scale), inv_y + 1, w, inv_item_height, {220, 220, 220, 255}, inv_font_size);
                inv_y += inv_item_height + static_cast<int>(2 * scale);
            }
        }
    }
};

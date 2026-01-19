#pragma once

#include "ui.h"
#include "world_manager.h"
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

class HudState
{
private:
    int hud_gold_value_ = std::numeric_limits<int>::min();
    int hud_life_value_ = std::numeric_limits<int>::min();
    int hud_max_life_value_ = std::numeric_limits<int>::min();
    int hud_items_value_ = std::numeric_limits<int>::min();
    int hud_max_items_value_ = std::numeric_limits<int>::min();
    int hud_settlement_count_ = std::numeric_limits<int>::min();
    int hud_npc_count_ = std::numeric_limits<int>::min();
    int hud_aim_pos_ = std::numeric_limits<int>::min();
    bool hud_has_aim_ = false;
    std::string hud_settlement_name_;
    std::string hud_gold_text_;
    std::string hud_hp_text_;
    std::string hud_items_text_;
    std::string hud_at_text_;
    std::string hud_aim_text_;
    std::string hud_settlement_count_text_;
    std::string hud_npc_count_text_;

public:
    void render(GameContext& ctx, WorldManager* world_manager)
    {
        if (!world_manager) return;

        const Player& p = world_manager->player_ctrl.player();
        if (p.active)
        {
            const int gold_value = static_cast<int>(p.inventory.capital);
            if (gold_value != hud_gold_value_)
            {
                hud_gold_value_ = gold_value;
                hud_gold_text_ = "Gold: " + std::to_string(hud_gold_value_);
            }

            if (p.life != hud_life_value_ || p.max_life != hud_max_life_value_)
            {
                hud_life_value_ = p.life;
                hud_max_life_value_ = p.max_life;
                hud_hp_text_ = "HP: " + std::to_string(hud_life_value_) + "/" + std::to_string(hud_max_life_value_);
            }

            const int items_value = p.inventory.total_items();
            const int max_items_value = p.inventory.max_capacity;
            if (items_value != hud_items_value_ || max_items_value != hud_max_items_value_)
            {
                hud_items_value_ = items_value;
                hud_max_items_value_ = max_items_value;
                hud_items_text_ = "Items: " + std::to_string(hud_items_value_) + "/" + std::to_string(hud_max_items_value_);
            }

            const Settlement* at_settlement = world_manager->get_settlement_at(p.pos);
            const std::string settlement_name = at_settlement ? at_settlement->name : std::string{};
            if (settlement_name != hud_settlement_name_)
            {
                hud_settlement_name_ = settlement_name;
                if (at_settlement)
                {
                    hud_at_text_ = "At: " + hud_settlement_name_;
                }
                else
                {
                    hud_at_text_.clear();
                }
            }

            if (p.has_aim() != hud_has_aim_ || p.aim_pos != hud_aim_pos_)
            {
                hud_has_aim_ = p.has_aim();
                hud_aim_pos_ = p.aim_pos;
                if (hud_has_aim_)
                {
                    hud_aim_text_ = "Moving to: " + std::to_string(hud_aim_pos_);
                }
                else
                {
                    hud_aim_text_.clear();
                }
            }
        }

        const int settlement_count = static_cast<int>(world_manager->landmarks.settlement_count());
        if (settlement_count != hud_settlement_count_)
        {
            hud_settlement_count_ = settlement_count;
            hud_settlement_count_text_ = "Settlements: " + std::to_string(hud_settlement_count_);
        }

        const int npc_count = static_cast<int>(world_manager->npcs.active_count());
        if (npc_count != hud_npc_count_)
        {
            hud_npc_count_ = npc_count;
            hud_npc_count_text_ = "NPCs: " + std::to_string(hud_npc_count_);
        }

        struct HudItem {
            std::string text;
            SDL_Color color;
            int height;
        };
        auto text_width = [](const std::string& text) {
            return static_cast<int>(text.size()) * 10;
        };

        // --- Расчет времени ---
        const std::uint64_t day_tick = ctx.hour % TICKS_PER_DAY;
        const int game_hour = static_cast<int>(day_tick / 1000);
        std::string time_str = "Time: " + (game_hour < 10 ? "0" : "") + std::to_string(game_hour) + ":00";
        // ----------------------

        std::vector<HudItem> row_one;
        std::vector<HudItem> row_two;
        
        row_one.push_back({time_str, {200, 200, 255, 255}, 14}); // Добавляем время первым пунктом
        row_one.push_back({hud_gold_text_, {255, 215, 0, 255}, 14});
        row_one.push_back({hud_hp_text_, {255, 100, 100, 255}, 14});
        row_one.push_back({hud_items_text_, {200, 200, 200, 255}, 14});
        
        if (!hud_at_text_.empty()) {
            row_one.push_back({hud_at_text_, {100, 255, 100, 255}, 14});
        }
        if (hud_has_aim_ && !hud_aim_text_.empty()) {
            row_two.push_back({hud_aim_text_, {150, 150, 255, 255}, 12});
        }
        row_two.push_back({hud_settlement_count_text_, {180, 180, 180, 255}, 12});
        row_two.push_back({hud_npc_count_text_, {180, 180, 180, 255}, 12});

        const int padding = 8;
        const int gap = 12;
        const int row_gap = 4;
        const int row_one_height = 16;
        const int row_two_height = 14;
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
        if (row_one_width > 0) row_one_width -= gap;
        if (row_two_width > 0) row_two_width -= gap;
        const int hud_width = std::max(row_one_width, row_two_width);
        const int hud_height = row_one_height + row_two_height + row_gap + padding * 2;
        const int hud_x = 8;
        const int hud_y = 6;

        SDL_Rect hud_bg = {hud_x, hud_y, hud_width + padding * 2, hud_height};
        ui_draw_panel(ctx.renderer, hud_bg, ui_color("#12100CBE"), ui_color("#504632DC"));

        int draw_x = hud_x + padding;
        int draw_y = hud_y + padding;
        for (const auto& item : row_one) {
            if (item.text.empty()) continue;
            render_text(ctx.renderer, ctx.font.get(), item.text, draw_x, draw_y, text_width(item.text), item.height, item.color);
            draw_x += text_width(item.text) + gap;
        }
        draw_x = hud_x + padding;
        draw_y += row_one_height + row_gap;
        for (const auto& item : row_two) {
            if (item.text.empty()) continue;
            render_text(ctx.renderer, ctx.font.get(), item.text, draw_x, draw_y, text_width(item.text), item.height, item.color);
            draw_x += text_width(item.text) + gap;
        }

        // --- Отрисовка Инвентаря (Список справа) ---
        const Player& p = world_manager->player_ctrl.player();
        int inv_y = hud_y + hud_height + 10;
        int inv_x = hud_x;
        
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i) {
            auto res = static_cast<ResourceType>(i);
            int amount = p.inventory.get(res);
            if (amount > 0) {
                std::string res_text = std::string(resource_name(res)) + ": " + std::to_string(amount);
                
                // Рисуем полупрозрачную подложку для читаемости
                int w = text_width(res_text);
                SDL_Rect bg = {inv_x, inv_y, w + 10, 18};
                ui_fill_rect(ctx.renderer, bg, ui_color("#00000080"));
                
                render_text(ctx.renderer, ctx.font.get(), res_text, inv_x + 5, inv_y + 1, w, 14, {220, 220, 220, 255});
                inv_y += 20;
            }
        }
    }
};

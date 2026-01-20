#pragma once

#include "core/game_state.h"
#include "ui/ui.h"
#include "ui/ui_events.h"
#include "systems/world_manager.h"
#include "systems/economy.h"
#include <string>

class StatState : public GameState
{
private:
    InputManager input_manager_;

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        InputEvent evt;
        if (input_manager_.process_event(event, ctx, evt))
        {
            if (evt.action == InputAction::Click || evt.action == InputAction::Press)
            {
                // Любой клик закрывает меню статов
                ctx.game_mod = GameMode::Game;
            }
        }
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_i || event.key.keysym.sym == SDLK_TAB)
            {
                ctx.game_mod = GameMode::Game;
            }
        }
    }

    void update(GameContext& /*ctx*/, TextureManager& /*textures*/, EntityManager& /*entities*/) override {}

    void render(GameContext& ctx, TextureManager& /*textures*/, EntityManager& /*entities*/) override
    {
        // 1. Фон (затемнение мира)
        SDL_Rect overlay = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, overlay, {10, 15, 30, 230});

        if (!ctx.world_manager) return;
        const Player& p = ctx.world_manager->player_ctrl.player();

        // 2. Заголовок
        int centerX = ctx.window_width / 2;
        render_text(ctx, "CHARACTER STATUS", centerX - 150, 40, 300, 40, {255, 255, 255, 255});

        // 3. Основные статы (Левая колонка)
        int leftX = 60;
        int y = 120;
        auto draw_stat = [&](const std::string& label, int val, int max, SDL_Color color) {
            std::string text = label + ": " + std::to_string(val) + " / " + std::to_string(max);
            render_text(ctx, text, leftX, y, 200, 25, color);
            y += 40;
        };

        render_text(ctx, "--- Vitals ---", leftX, y - 30, 120, 20, {150, 150, 150, 255});
        draw_stat("Health", p.life, p.max_life, {255, 100, 100, 255});
        draw_stat("Willpower", p.will, p.max_will, {255, 100, 255, 255});
        draw_stat("Lust", p.lust, p.max_lust, {255, 182, 193, 255});

        // 4. Репутация (Правая колонка)
        int rightX = centerX + 40;
        int ry = 120;
        render_text(ctx, "--- Reputation ---", rightX, ry - 30, 150, 20, {150, 150, 150, 255});
        
        auto draw_rep = [&](const std::string& name, FactionID fid) {
            int val = p.reputation[static_cast<size_t>(fid)];
            SDL_Color color = {200, 200, 200, 255};
            if (val > 20) color = {100, 255, 100, 255};
            if (val < -20) color = {255, 100, 100, 255};
            
            std::string text = name + ": " + (val > 0 ? "+" : "") + std::to_string(val);
            render_text(ctx, text, rightX, ry, 180, 25, color);
            ry += 35;
        };

        draw_rep("Kingdom", FactionID::Kingdom);
        draw_rep("Outlaws", FactionID::Outlaws);
        draw_rep("Wilderness", FactionID::Wilderness);

        // 5. Нижняя часть: Инвентарь
        int invY = y + 20;
        render_text(ctx, "--- Inventory (Weight: " + std::to_string(p.inventory.total_weight()) + "/" + std::to_string(p.inventory.max_capacity) + ") ---", leftX, invY, 350, 20, {150, 150, 150, 255});
        invY += 30;
        
        int count = 0;
        for (size_t i = 1; i < RESOURCE_COUNT; ++i) {
            int amount = p.inventory.get(static_cast<ResourceType>(i));
            if (amount > 0) {
                std::string item_text = std::string(RESOURCE_DATA[i].name) + " x" + std::to_string(amount);
                render_text(ctx, item_text, leftX + (count % 2) * 250, invY + (count / 2) * 30, 180, 20, {220, 220, 220, 255});
                count++;
            }
        }

        render_text(ctx, "[ Press any key to return ]", centerX - 100, ctx.window_height - 60, 200, 20, {100, 100, 100, 255});
    }
};

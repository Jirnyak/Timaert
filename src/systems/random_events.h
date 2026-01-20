#pragma once

#include <string>
#include <vector>
#include <functional>
#include "core/game_context.h"
#include "systems/world_manager.h"

struct EventChoice {
    std::string text;
    // Описание того, что произойдет (для логики)
    std::function<void(GameContext& ctx)> action;
};

struct RandomEvent {
    std::string title;
    std::string description;
    std::vector<EventChoice> choices;
};

// Простая база данных событий
inline const RandomEvent& get_random_event_data(int id) {
    static const std::vector<RandomEvent> DB = {
        {
            "Mysterious Shrine",
            "You find a crumbling altar covered in moss. It radiates a faint, strange energy.",
            {
                {"Pray for health", [](GameContext& ctx) {
                    if (ctx.world_manager) {
                        auto& p = ctx.world_manager->player_ctrl.player();
                        p.life = std::min(p.life + 20, p.max_life);
                    }
                }},
                {"Search for gold", [](GameContext& ctx) {
                    if (ctx.world_manager) {
                        ctx.world_manager->player_ctrl.player().inventory.capital += 50;
                    }
                }},
                {"Leave it alone", [](GameContext& /*ctx*/) {}}
            }
        },
        {
            "Traveling Merchant",
            "A merchant is resting by the road. He offers to share his supplies for a small donation.",
            {
                {"Pay 100 gold for rest", [](GameContext& ctx) {
                    if (ctx.world_manager) {
                        auto& p = ctx.world_manager->player_ctrl.player();
                        if (p.inventory.capital >= 100) {
                            p.inventory.capital -= 100;
                            p.life = p.max_life;
                            p.will = p.max_will;
                        }
                    }
                }},
                {"Ignore him", [](GameContext& /*ctx*/) {}}
            }
        }
    };

    if (id < 0 || id >= static_cast<int>(DB.size())) return DB[0];
    return DB[id];
}

inline int get_random_event_count() {
    return 2; // Пока у нас 2 события
}

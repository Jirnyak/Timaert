#pragma once

#include <string>
#include <vector>
#include <functional>
#include "core/game_context.h"

struct Player;
enum class NPCType : std::uint8_t;

struct EventChoice {
    std::string text;
    std::function<void(GameContext& ctx)> action;
};

struct RandomEvent {
    std::string title;
    std::string description;
    std::vector<EventChoice> choices;
};

// All defined in random_events.cpp - needs WorldManager/Player/BattleState complete
Player* get_player(GameContext& ctx);
void trigger_fight(GameContext& ctx, NPCType type, const std::string& override_name = "");
const std::vector<RandomEvent>& get_event_db();
const RandomEvent& get_random_event_data(int id);
int get_random_event_count();

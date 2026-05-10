// Quest types — six universal verbs. Mirrors quests/quest-types.ts.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "events/event_types.h"

namespace sm {

enum class QuestCategory : std::uint8_t { Main, Side, Procedural };

enum class ObjectiveKind : std::uint8_t {
    VisitCell, FindLocation, DeliverItems, DestroyNpc, WaitAt, InteractCell,
};

struct Objective {
    ObjectiveKind kind;
    bool completed = false;

    // Generic fields — interpretation depends on kind.
    int   ix = 0, iy = 0;
    int   cellX = 0, cellY = 0;
    int   subX = 0, subY = 0;
    float radius = 0;
    std::string itemId;
    int   quantity = 0;
    int   targetSettlementId = 0;
    int   npcType = 0;
    int   count = 0, killed = 0;
    float zoneRadius = 0;
    int   hoursRequired = 0, hoursWaited = 0;
    std::string action;
};

enum class RewardKind : std::uint8_t { Gold, Xp, Item, Reputation, Event };
struct Reward {
    RewardKind kind;
    int amount = 0;
    std::string itemId;
    std::string faction;
    int delta = 0;
    GameEvent event;
};

struct Quest {
    std::string id;
    std::string title, description;
    QuestCategory category;
    int giverSettlementId = -1;
    std::vector<Objective> objectives;
    std::vector<Reward>    rewards;
    std::vector<GameEvent> onAccept;
    int  expireDay = -1;
    int  difficulty = 1;
};

inline int quest_id_key(const std::string& id) noexcept {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : id) {
        h ^= std::uint32_t(c);
        h *= 16777619u;
    }
    return int(h & 0x7fffffffu);
}

} // namespace sm

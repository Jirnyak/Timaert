#include "content/quests/procedural.h"
#include "core/rng.h"

namespace sm {

static std::string make_quest_id(const Settlement& s, int day, int n) {
    return "q_" + std::to_string(s.id) + "_d" + std::to_string(day) + "_" + std::to_string(n);
}

std::vector<Quest> generate_quests_for_settlement(const Settlement& s,
                                                  const GameState& gs,
                                                  std::uint32_t worldSeed) {
    Rng r(worldSeed ^ std::uint32_t(s.id) ^ std::uint32_t(gs.worldTime.day));
    std::vector<Quest> out;
    int n = 1 + int(r.next_u32() % 3u);
    for (int i = 0; i < n; ++i) {
        Quest q;
        q.id = make_quest_id(s, gs.worldTime.day, i);
        q.giverSettlementId = s.id;
        q.category = QuestCategory::Procedural;
        q.difficulty = 1 + int(r.next_u32() % 5u);
        int kind = int(r.next_u32() % 3u);
        if (kind == 0) {
            q.title = "Hunt Bandits";
            q.description = "Clear bandits nearby.";
            Objective o{}; o.kind = ObjectiveKind::DestroyNpc;
            o.npcType = 4; // Bandit
            o.count = 3 + int(r.next_u32() % 3u);
            o.zoneRadius = 80.0f;
            q.objectives.push_back(o);
            Reward rw{}; rw.kind = RewardKind::Gold; rw.amount = 80 + q.difficulty * 30;
            q.rewards.push_back(rw);
        } else if (kind == 1) {
            q.title = "Deliver Wheat";
            q.description = "Bring wheat to a neighbouring settlement.";
            Objective o{}; o.kind = ObjectiveKind::DeliverItems;
            o.itemId = "wheat"; o.quantity = 5 + int(r.next_u32() % 6u);
            o.targetSettlementId = (s.id + 1 + int(r.next_u32() % 4u)) % std::max(1, int(gs.settlements.size()));
            q.objectives.push_back(o);
            Reward rw{}; rw.kind = RewardKind::Gold; rw.amount = 50 + q.difficulty * 20;
            q.rewards.push_back(rw);
        } else {
            q.title = "Scout the Frontier";
            q.description = "Visit a wild frontier cell.";
            Objective o{}; o.kind = ObjectiveKind::VisitCell;
            o.ix = (s.x + 40 + int(r.next_u32() % 60u)) % gs.mapW;
            o.iy = (s.y + 30 + int(r.next_u32() % 50u)) % gs.mapH;
            o.radius = 4.0f;
            q.objectives.push_back(o);
            Reward rw{}; rw.kind = RewardKind::Xp; rw.amount = 60 + q.difficulty * 25;
            q.rewards.push_back(rw);
        }
        out.push_back(std::move(q));
    }
    return out;
}

} // namespace sm
